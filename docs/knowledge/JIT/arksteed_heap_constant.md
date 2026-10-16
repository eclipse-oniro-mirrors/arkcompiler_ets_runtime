# ArkSteed HeapConstant 知识文档

> 对应源码目录：`ecmascript/arksteed/`（IR 节点与代码生成）、`ecmascript/compiler/jit_compilation_env.h/.cpp`（编译期常量表）、`ecmascript/arksteed/arksteed_task.cpp` 与 `ecmascript/mem/embedded_code_ref_set.cpp`（安装与 GC 维护）、`ecmascript/mem/machine_code.h/.cpp`（MachineCode 对象布局）。

---

## 1. HeapConstant 是什么

`HeapConstantVertex`（定义于 `ecmascript/arksteed/arksteed_opcode.h`）是 ArkSteed IR 中的一种常量节点（零输入、TAGGED 表示），语义是：

> **"这个值在编译期被观测到总是同一个堆对象，把它当作常量嵌入机器码。"**

它的作用是 **把"按名字动态查找"变成"直接引用 + 守卫"**。JS 语义上"取全局变量 / 调方法 / 访问继承属性"都要查表、走原型链；运行时统计表明绝大多数情况命中同一个对象，于是 JIT 打赌把它钉进机器码，用守卫（guard）校验假设，失败则 deopt 回解释器。

`HeapConstantVertex` 存 `handleIndex`（编译期常量表索引）+ `staticNodeType`（编译期可确定的静态类型）。`staticNodeType` 由 `GraphBuilder` 在建点时根据对象实际类型记录（如 STRING / HEAP_OBJECT 等），供中端类型事实使用（见 `CompileInfoFacts::GetStaticNodeType`）。它不能存地址：GC 是移动式的，编译到运行之间对象可能搬家，写死的地址会变成悬垂指针。

机器码中最终落到两处：

- **文本区嵌入引用**（embedded reference）：x64 为 MOVABS 立即数，ARM64 为字面量池 8 字节槽，由 `EmbeddedCodeRefSet` 登记、GC 移动后重补丁；
- **heap constant 表**（MachineCode payload 内的 `uint64_t` 数组）：供 deopt 物化读取，随 MachineCode 一起被 GC 扫描。

---

## 2. 什么时候使用 HeapConstant

### 2.1 什么时候需要登记（拿handleIndex）
登记 = 把对象放进`JitCompilationEnv` 的 heapConstantTable，本质是给机器码换一种“GC 安全地引用堆对象”的方式（embedded reference + GC 重补丁 / deopt 字面量表）

**判断标准：编译出的机器码需要“在编译期就知道是哪个堆对象”，且该对象无法用 raw bits 表示。**

两个具体形态：
1. 运行期要把对象加载到寄存器参与运算/比较（作为数据或期望值）；
2. 对象身份作为守卫/分发的期望值嵌进检查类指令。

### 2.2 登记之后，什么时候建HeapConstantVertex
常量是数据流中的一个 tagged 值
- 作为普通 vertex 的输入：`LoadTaggedField` 的 base（box.value、原型 holder 字段）、`CallKnownJSFunction` 的 target、`TransitionHClassWithBarrier` 的输入、直接 SetAcc（字符串常量）；
- 作为 通用 比较 vertex 的操作数：如`DeoptIfReferenceNotEqual(actual, expected)` ——这类 vertex 没有 handleIndex 定制 codegen，期望值必须是输入操作数（call target、canonical method 都属此类）；
- 会出现在 deopt frame state 里：`AppendDeoptSource` 只认 operand 形式的`HeapConstantVertex` （HEAP_LITERAL 物化）， 只要该常量可能进 lazy deopt 帧，就必须建节点 。

--- 

## 3. 怎么登记HeapConstant：什么时候调用哪个API

四个入口（`arksteed_graph_builder.cpp`）共享同一条底层路径 `GraphBuilder::TryRecordHeapConstant` → `ArkSteedHeapBroker::TryRecordHeapConstant` → `JitCompilationEnv::RecordHeapConstant`。差异在于**登记之后做什么**：

| 入口 | 产物 | 语义 |
|------|------|------|
| `GetHeapConstant(ref)` | `ValueVertex*`（IR 值节点） | 对象要作为**数据流中的值**参与运算 |
| `GetHeapConstantHandleIndex(ref)` | `optional<uint32_t>` | 索引作为**守卫/分发的元数据**嵌入检查类 vertex |
| `GetHeapConstantNameHandleIndex(nameRef)` | `optional<uint32_t>` | 同上，限定 **String/Symbol** 属性键 |
| `GetHeapConstantHandleIndices(refs, &out)` | `bool`（写入 vector） | 批量登记，**全有或全无** |

四者命中同一张环境表（`JitCompilationEnv` 的 `heapConstantTable`，按 `GetTaggedType()` 去重），因此同一对象无论走哪条路径都拿到**同一个 handleIndex**。

### 3.1 `GetHeapConstant`：登记并创建值节点

**判断标准**：这个对象需要在 IR 数据流中**作为一个 tagged 值被后续节点消费**——作为其他 vertex 的输入、参与寄存器分配、可能成为 phi 输入、或需要出现在 deopt 帧状态里。

### 3.2 `GetHeapConstantHandleIndex`：仅登记，索引作元数据

**判断标准**：对象不进入数据流，只作为**比较的期望值**被一个控制/检查类 vertex 持有；codegen 时该 vertex 自己 `MoveEmbeddedTagged` 加载期望值并比较。

### 3.3 `GetHeapConstantNameHandleIndex`：String/Symbol 属性键

**判断标准**：需要把**属性键**（名字）而非普通对象登记为常量。入口内部要求 ref 是 String 或 Symbol，否则失败。现用于 `TryBuildLoadPropertyByValue` 的 `propertyKeyHandleIndex`（key 由值计算但被观测为常量字符串时，把"按值访问"降级为"命名访问"）。

### 3.4 `GetHeapConstantHandleIndices`：批量、全有或全无

**判断标准**：一组 HClass 必须**同时**登记成功才有意义（原型链形状、多态组），任何一个失败则整组放弃（返回 false，不产生部分状态）。

### 3.5 误用后果
用 GetHeapConstantHandleIndex 代替 GetHeapConstant 会导致数据流缺值节点（需要手工在 codegen 补加载，容易漏）；用 GetHeapConstant 代替 GetHeapConstantHandleIndex 会额外创建 IR 节点、把守卫期望值卷入寄存器分配，得不偿失。

---

## 4. 修改 HeapConstant 生命周期逻辑时的约束与不变式

生命周期：**编译期登记 → 生成占位与重定位信息 → 主线程安装 → GC 更新引用 → deopt 读取**。修改时重点保证以下约束。

### 4.1 编译期用句柄，不固定对象地址

- 登记必须在 broker 的 `SerializingScope` 内进行，且只接受由 `JSHandle` 保护的强引用堆对象。GC 可能移动对象，不能用裸地址代替句柄。
- 传给安装任务的句柄必须保持有效直到安装完成；复制 `JSHandle` 不会延长其底层句柄槽的寿命。修改任务生命周期时，必须检查句柄区何时回收，也不能跨编译任务缓存这些句柄。
- 同一对象复用同一 `handleIndex`，值节点统一通过 `Graph::GetHeapConstant` 创建，避免重复登记和重复建点。

### 4.2 安装必须完整成功后才能发布

- 编译期用 `VALUE_HOLE` 占位并记录重定位信息，不直接写入对象地址。ARM64 还须保证字面量池的对齐和加载距离合法；新增大块指令发射时不能绕过 `CheckCodePools(true)` 的检查。
- 安装期在 `JitGCLockHolder` 保护的对象不可移动区间内读取句柄，并校验目标、索引、补丁范围及平台格式。
- 顺序保持为：**填 deopt 常量表及记忆集 → 全量校验并完成文本区补丁 → 登记嵌入引用 → 刷新指令缓存、恢复代码写保护 → 发布入口**。任何失败都必须完整回滚，带占位值或只完成部分补丁的代码不得执行。

### 4.3 GC 必须同时维护两处引用

- **文本区嵌入引用**由 `EmbeddedCodeRefSet` 更新并重新打补丁，补丁后刷新指令缓存。年轻代、本地老年代、共享堆的分区必须与各自 GC 路径匹配；老年代 GC 只扫描已标记的 MachineCode，不能让死代码继续保活目标。
- **deopt 常量表**由 `MachineCode::VisitRangeSlot` 扫描。安装写入表槽时，指向年轻代对象的引用须登记 old-to-new 记忆集，指向共享堆对象的引用须登记 local-to-share 记忆集，否则对象移动后表槽可能留下旧地址。
- 嵌入引用表的安装、更新和移除必须在已有 JIT/GC 锁或共享 GC 的 `SuspendAll` 边界内，不能新增无同步的访问。

### 4.4 Deopt 的索引、计数和布局必须一致

- `handleIndex` 是编译环境常量表的索引，`literalIndex` 是当前代码对象 deopt 常量表的索引，**两者不可混用**；通过 `DeoptLiteralTableBuilder` 建立映射。
- safepoint 表中的 `literalCount` 必须等于 deopt 句柄列表长度；运行期读取须同时检查索引未越界、常量表空间足够。
- 修改 MachineCode 中常量表的位置或大小时，必须同步核对 `VisitRangeSlot`（GC 扫描）、`ReadDeoptLiteral`（deopt 读取）和 `SetArkSteedTranslationData`（后续数据布局），避免漏扫或读错位置。

### 4.5 不绕过 GC 门控，兼顾共享管线

- **CMC GC 当前禁用这条链路**。启用前必须补齐常量表和嵌入引用的写屏障支持，不能只移除 `g_isEnableCMCGC` 检查。
- `JitCompilationEnv::RecordHeapConstant` 和句柄表由 ArkSteed 与 kungfu 优化 JIT 共用。修改去重、分配或索引语义时，必须核对并回归两条管线。
