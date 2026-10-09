# ArkSteed 帧结构知识文档

> 对应源码目录：`ecmascript/frames.h`（帧结构定义）、`ecmascript/arksteed/`（代码生成与 prologue）、`ecmascript/compiler/trampoline/{x64,aarch64}/arksteed_call.cpp`（跳板）、`ecmascript/deoptimizer/`（去优化器）。
>
> 适用范围：ArkSteed JIT 编译器（`ets_runtime_enable_ark_steed`，`ECMASCRIPT_ENABLE_ARK_STEED` 宏）在 x64 / aarch64 上产生的所有栈帧。

---

## 1. 概述：帧分类总表

ArkSteed 执行路径上只有 **一种真代码帧**（`STEED_FUNCTION_FRAME`），其余全部是过渡性质的跳板帧或纯 ABI 帧。跳板帧不承载 JS 语义（没有 function / bytecode offset），`FrameIterator` 遍历时会被跳过或仅用于链表回溯。

| 帧类型 | 分类 | 建帧者 | 生存期 | 是否含 JS 语义 |
|--------|------|--------|--------|----------------|
| `STEED_FUNCTION_FRAME` | 真代码帧 | ArkSteed 生成代码自身（prologue） | 函数执行期间 | 是（jsFunc / lexicalEnv） |
| `OPTIMIZED_ENTRY_FRAME` | 跳板帧 | `ArkSteedCallEntry` stub | C++/解释器首次进入 Steed 代码 | 否（仅记录 prevFp 链接） |
| `OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME` | 跳板帧 | `SteedCallAndPushArgv` / `SteedCallWithArgVAndPushArgv` stub | 调用桥执行期间 | 否（标记出栈参数区归属 + 保存 C++ callee-save） |
| `ASM_INTERPRETER_BRIDGE_FRAME`（可带 lazy 标志位） | 过渡帧 | `DeoptPushAsmInterpBridgeFrame` | 去优化重入解释器瞬间 | 否（重建解释器帧的桥） |

帧机制速查（详见各帧小节）：

| 帧 | callee-save 保存区 | 回栈取上一帧 | 退帧恢复 PC |
|----|--------------------|--------------|-------------|
| `STEED_FUNCTION_FRAME` | 无（跨调用值一律 spill 进帧内槽） | `[fp]` prevFp 槽 | epilogue 后 `ret`，读 `[fp+8]` returnAddr |
| `OPTIMIZED_ENTRY_FRAME` | 有（C ABI） | `[fp-16]` prevFp，直接跳 C++ 侧 leave frame | stub `ret`，读 C ABI call 压入的 returnAddr |
| `OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME` | 有（C ABI） | `[fp]` 旧 rbp/fp | `ret`（arm64 `RestoreFpAndLr`），读 `[fp+8]` returnAddr |
| `ASM_INTERPRETER_BRIDGE_FRAME` | 有（`FromInterpreterHandler` 时除外） | `[fp-16]` prevFp（被去优化 Steed 帧 fp） | `PopAsmInterpBridgeFrame` 后 `ret`，读 `[fp]` returnAddr |

`FrameType::STEED_FUNCTION_FRAME` 枚举与帧结构 `SteedFunctionFrame` 均定义于 `ecmascript/frames.h`。

---

## 2. STEED_FUNCTION_FRAME（唯一真代码帧）

### 2.1 作用说明

ArkSteed JIT 编译产物中每个 JS 函数体的执行帧。持有 GC 可见的两个 tagged 根（jsFunc、lexicalEnv），划定 tagged/untagged 栈槽区，并作为 `FrameIterator` / GC / deopt / 栈回溯识别"这是正在执行的 Steed 机器码帧"的锚点。

### 2.2 槽位说明

权威布局注释见 `SteedFunctionFrame`（`ecmascript/frames.h`）。fp（rbp/x29）指向 prevFp 槽，x64 / arm64 布局完全同构（`STATIC_ASSERT_EQ_ARCH`）：

| 槽位（fp 相对） | 内容 | 说明 |
|-----------------|------|------|
| `[fp+16]` 起（帧外） | argc、call-target、new-target、this、arg[0..N-1] | caller 物化的出栈参数区，低→高内存序 `[argc][call-target][new-target][this][args...]`；argc 含 3 个必传槽 |
| `[fp+8]` | returnAddr | caller 的 `call` / `blr` 压入 |
| `[fp]` | prevFp | 上一帧 fp（fp 即指向本槽） |
| `[fp-8]` | frameType = `STEED_FUNCTION_FRAME` | 固定头开始 |
| `[fp-16]` | jsFunc（call-target） | tagged 根 |
| `[fp-24]` | lexicalEnv | tagged 根 |
| `[fp-32]` 起 | tagged_slot[0..N-1] | FP 相对寻址的 tagged 栈槽，prologue 预填 undefined |
| 其下 | untagged_slot[0..M-1] | 非标记栈槽 |
| 其下 | pushed_reg / pushed_double | 调用前临时 spill 区 |
| 帧底（可选） | 对齐 padding 槽 | tagged+untagged 槽总数为偶数时补 1 槽，保持 callsite 16B 对齐 |

与 FastJIT 最大差异：**全部参数走栈，无 fast-call 寄存器传参变体**

### 2.3 回栈：如何得到上一帧

读 `[fp]` prevFp 槽。`FrameIterator::Advance` 的 STEED 分支（`frames.cpp`）取 `GetPrevFrameFp()` 回溯，并在 GC/DEOPT 模式下记录 `optimizedReturnAddr_`（用于查 safepoint 表）。`IsJSFrame()` 已包含 Steed（`frames.h`）。

### 2.4 退帧：PC 如何恢复

epilogue 不逐槽清理：x64 `mov rsp, rbp; pop rbp`，arm64 `mov sp, fp` 后从 `[sp+8]` 恢复 lr；随后 `ret` 从 `[fp+8]` 的 returnAddr 槽取回 PC。

### 2.5 构造方式与代码位置

prologue 由编译器在每个生成函数头部发出：`ArkSteedCodeGenerator` 调 `ArkSteedAssembler::Prologue`（`ecmascript/arksteed/arksteed_codegen.cpp`；实现于 `ecmascript/arksteed/arch/x64/arksteed_assembler_x64.cpp` 与 `arch/arm64/arksteed_assembler_arm64.cpp`）。步骤：压 fp 并切换 → 压 frameType/jsFunc/lexicalEnv 固定头 → tagged 槽预填 undefined → 预留 untagged 槽与对齐 padding。epilogue 对应 `ArkSteedAssembler::Epilogue`。

寄存器传入约定（caller 在 `call` / `blr` 前设置，双架构跳板与 Steed 直调均遵守）：

| 架构 | jsFunc | lexicalEnv | 设置方 |
|------|--------|-----------|--------|
| x64 | r12 | rbx | 跳板 `InvokeSteedCode`（`trampoline/x64/arksteed_call.cpp`）；Steed 直调 `ArkSteedAssembler::PrepareSteedCalleeContext` |
| arm64 | x20 | x19 | 跳板 `ArkSteedCall::EmitSteedCall` + `PushMandatoryJSArgs`（`trampoline/aarch64/arksteed_call.cpp`）；直调同上 |

### 2.6 遍历、GC 与元数据

- **GC**：`SteedFunctionFrame::GCIterate`（`frames.cpp`）依次扫固定头两根、caller 出栈参数区、MachineCode 引用，再走 safepoint 表；`IterateSafePointTable` 从 `MachineCode::GetStackMapOrOffsetTableAddress()` 取 **ArkSteed 自研 safepoint 表** 。
- **deopt 元数据**：`GetDeoptBundleInfo`（`frames.cpp`）用同一张表解码出统一的 `kungfu::ARKDeopt`；去优化上下文收集见 `Deoptimizier::CollectSteedDeoptContextFromRuntime`（`ecmascript/deoptimizer/deoptimizer.cpp`）。

---

## 3. 入口跳板帧

### 3.1 OPTIMIZED_ENTRY_FRAME（由 ArkSteedCallEntry 建立）

#### 3.1.1 作用说明

C++ / 解释器首次进入 Steed 代码的边界帧。保存 C++ callee-save 寄存器与 glue，记录 prevFp 把 C++ 栈与 JS 托管栈链接起来，使 `FrameIterator` 能从 JS 帧直接跳回 C++ 侧 leave frame。本身不含 JS 语义，遍历时跳过（`Advance` 仅回溯）。

入口状态（C ABI / CCallConv）：

| 架构 | glue | argc（仅用户参数） | argv | prevFp |
|------|------|------|------|--------|
| x64 | rdi | rsi | rdx（`[call-target, new-target, this, arg0...]`） | rcx |
| arm64 | x0 | x1 | x2（同上） | x3 |

#### 3.1.2 槽位说明

x64（fp 指向旧 rbp 槽；**returnAddr 不在 `[fp+8]`**，这是与标准帧链的关键差异）：

| 槽位（fp 相对） | 内容 |
|-----------------|------|
| `[fp+56]` | returnAddr（C ABI call 压入，弹帧后 `ret` 回 C++） |
| `[fp+16]`..`[fp+48]` | C++ callee-save 5 槽：r12、r13、r14、r15、rbx |
| `[fp+8]` | glue（入口 rdi，压栈保存） |
| `[fp]` | rbp(old)（C++ caller 的 fp，弹帧恢复用） |
| `[fp-8]` | frameType = `OPTIMIZED_ENTRY_FRAME` |
| `[fp-16]` | prevFp（C++ 侧 leave frame 指针，入口 rcx） |

arm64（`[fp+8]` 即 lr，标准帧链布局；callee-save 在帧头之下）：

| 槽位（fp 相对） | 内容 |
|-----------------|------|
| `[fp+8]` | returnAddr（lr，`PushFpAndLr` 压入） |
| `[fp]` | fp(old)（C++ caller 的 fp） |
| `[fp-8]` | frameType |
| `[fp-16]` | prevFp（C++ 侧 leave frame 指针，入口 x3） |
| `[fp-16]` 以下 | CalleeSave：x19–x28 + d8–d15（9 对 144B） |

注意：**glue 的跨调用保存，两架构策略不同**。入口 glue（x64 rdi / arm64 x0）是 caller-saved 寄存器，内层转调 `SteedCallWithArgVAndPushArgv` 后即失效，而弹帧时仍需 glue 写 `glue->leaveFrame`：

- **x64 压栈**：glue 存入帧内 `[fp+8]` 栈槽，`PopJSFunctionEntryFrame` 直接 `Popq` 该槽恢复。原因是 SysV 仅 5 个 callee-save GPR 且各有用途——共享此帧的 AOT `JSFunctionEntry` 已把 rbx/r12 挪作 argv/needPushArgv 停泊位，两个调用方无统一的 glue 停靠寄存器约定，栈槽才是共享的恢复源（Steed 入口虽也在 r12 放了一份，弹帧实际消费的是栈槽）。
- **arm64 停泊 x20**：无 glue 栈槽，stub 在内层 call 前 `Mov(x20, glue)`，x20 为 AAPCS64 callee-save，内层桥自身的 `CalleeSave`/`CalleeRestore`（桥内甚至会把 x20 重用作 jsFunc 传参，返回前又恢复）保证其跨调用保值；弹帧前 `Mov(x2, x20)` 取回。AAPCS64 有 x19–x28 共 10 个 callee-save GPR，有余量。x20 并非全局 glue 专用寄存器——仅是这族入口 stub 的停靠约定（全局固定 glue 寄存器只出现在 `ENABLE_CMC_IR_FIX_REGISTER` 的 x28，及 eager deopt 入口的 x17）。

#### 3.1.3 callee-save 寄存器

**有**。x64：r12、r13、r14、r15、rbx（`ExtendedAssembler::PushCppCalleeSaveRegisters`）；arm64：x19–x28 + d8–d15（`CalleeSave`）。目的是让跳板自身满足 C ABI——调用方 C++ 期望这些寄存器跨调用保持，弹帧时由对应 Pop 原语恢复。

#### 3.1.4 回栈：如何得到上一帧

读 `[fp-16]` 的 prevFp 槽（`OptimizedEntryFrame::GetPrevFrameFp`）。该值由 C++ 发起者传入（x64 rcx / arm64 x3），指向 C++ 侧 leave frame；`FrameIterator::Advance` 的 OPTIMIZED_ENTRY_FRAME 分支据此**直接跳回 C++ 侧 leave frame**，不经过中间 C++ 帧。

#### 3.1.5 退帧：PC 如何恢复

`OptimizedCall::PopJSFunctionEntryFrame`：弹 prevFp → 跳过 frameType → 恢复 rbp → 恢复 glue → 弹出并恢复 C++ callee-save → 把 prevFp 写回 glue 的 leaveFrame 字段；随后 stub `ret`，PC 靠 C ABI call 压入的 returnAddr 恢复（x64 在帧顶 callee-save 之上；arm64 经 `RestoreFpAndLr` 从 `[fp+8]` 恢复 lr）。

#### 3.1.6 构造方式与代码位置

建帧原语 `OptimizedCall::PushJSFunctionEntryFrame`（`trampoline/{x64,aarch64}/optimized_call.cpp`），由跳板 `ArkSteedCall::ArkSteedCallEntry`（`trampoline/{x64,aarch64}/arksteed_call.cpp`）调用；C++ 发起者 `EcmaVM::ExecuteArkSteed`（`ecmascript/ecma_vm.cpp`），经 `thread_->GetRTInterface(ID_ArkSteedCallEntry)` 间接进入。上游 C++ 入口共三个：`JSFunction::InvokeOptimizedEntrypoint`（`js_function.cpp`，两个重载）与 `EcmaInterpreter::GeneratorReEnterAot`（`interpreter-inl.cpp`）。

流程：压 ENTRY_FRAME → 从 argv 抽出 func/newTarget/this 并偏移出用户 argv → CCallConv 转 `SteedCallWithArgVAndPushArgv` → 返回后弹 ENTRY_FRAME、更新 glue 的 leave frame、`ret` 回 C++。

### 3.2 OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME（由 SteedCall* 跳板建立）

#### 3.2.1 作用说明

调用桥执行期的参数配置帧。职责有三：① 保存 C++ callee-save 使跳板自身满足 C ABI；② 以帧类型标记出栈参数区归属（`ComputeArgsConfigFrameSp` 供 callsite sp 计算）；③ 承载桥局部槽（x64 的 newTarget 暂存槽、arm64 的 reservedSlotCount 槽）。

两个入口变体（共享 `ArkSteedCall::EmitSteedCall` 骨架）：

| stub | 入口状态（x64） | 用途 |
|------|----------------|------|
| `SteedCallAndPushArgv` | glue/totalArgc/argV(可 0)/jsfunc/newTarget/this + 用户参数在 entry sp | 变参直推路径 |
| `SteedCallWithArgVAndPushArgv` | glue/userArgc/jsFunc/newTarget/this/argv 指针 | ENTRY_FRAME 与 bound-call 路径转参后复用 |

#### 3.2.2 槽位说明

x64（与 `OptimizedJSFunctionFrame` 同构的帧头，fp 指向旧 rbp 槽）：

| 槽位（fp 相对） | 内容 |
|-----------------|------|
| `[fp+8]` | returnAddr（`call SteedCall*` 压入） |
| `[fp]` | rbp(old)（上一帧 fp，回溯链） |
| `[fp-8]` | frameType = `OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME` |
| `[fp-16]`..`[fp-48]` | C++ callee-save 5 槽：r12、r13、r14、r15、rbx |
| `[fp-56]` | newTarget 暂存槽（桥局部槽） |
| 更低地址 | 对齐 padding（可选）→ 补齐的 undefined → 物化中的出栈参数区 `[argc][call-target][new-target][this][args...]`（`call codeAddr` 后其上为 STEED_FUNCTION_FRAME） |

arm64（帧头同构；`[fp-16]` 为头部对齐槽，重复保存 x19）：

| 槽位（fp 相对） | 内容 |
|-----------------|------|
| `[fp+8]` | returnAddr（lr） |
| `[fp]` | fp(old)（上一帧 fp，回溯链） |
| `[fp-8]` | frameType |
| `[fp-16]` | x19 头部对齐槽（`PushOptimizedArgsConfigFrame` 以 stp 对建帧） |
| `[fp-24]`..`[fp-160]` | CalleeSave 9 对：x19–x28 + d8–d15（144B） |
| `[fp-176]` | reservedSlotCount 桥局部槽（16B 区） |
| 更低地址 | 对齐 padding（可选）→ undefined 补齐 → 出栈参数区（前两个用户参数可经 x6/x7 拷入） |

#### 3.2.3 callee-save 寄存器

**有**，同 ENTRY_FRAME：x64 rbx、r12–r15；arm64 x19–x28 + d8–d15（另有 `[fp-16]` 的 x19 头部对齐槽）。跳板被 C ABI 调用，须保持调用方寄存器。

#### 3.2.4 回栈：如何得到上一帧

读 `[fp]` 旧 rbp/fp 槽。`FrameIterator::Advance` 的 OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME 分支按 `OptimizedJSFunctionFrame`（同构布局）解释，`GetPrevFrameFp()` 回溯；遍历时视作非 JS 帧跳过。

#### 3.2.5 退帧：PC 如何恢复

`ArkSteedCall::RestoreSteedCallFrame`：x64 跳过局部槽 → `PopCppCalleeSaveRegisters` → 跳过 frameType → `pop rbp` → `ret`；arm64 `CalleeRestore` → `PopOptimizedArgsConfigFrame`（内含 `RestoreFpAndLr`）→ `ret`。PC 靠 `[fp+8]` 的 returnAddr / lr 槽恢复。

#### 3.2.6 构造方式与代码位置

建帧在 `ArkSteedCall::PrepareSteedCallFrame`（`trampoline/{x64,aarch64}/arksteed_call.cpp`；arm64 底层原语 `OptimizedCall::PushOptimizedArgsConfigFrame`），由 `ArkSteedCall::EmitSteedCall` 骨架统一发出

---

## 4. 去优化相关帧

### 4.1 ASM_INTERPRETER_BRIDGE_FRAME（可带 lazy 标志位）

#### 4.1.1 作用说明

去优化重入解释器 / 基线代码瞬间的过渡桥帧：把运行时重建的解释器帧与原调用方衔接起来——保存原 Steed 帧的返回地址与被去优化帧的 fp。lazy deopt 场景在 FrameType 上或 `1ULL << 30` 标志位（`FrameIterator::LAZY_DEOPT_FLAG_BIT`，`ecmascript/frames.h`）；`FrameIterator::GetFrameType()` 读取时会掩掉该位。

#### 4.1.2 槽位说明

固定头 4 槽，双架构同构，与 `AsmInterpretedBridgeFrame`（`ecmascript/frames.h`）一一对应：

| 槽位（fp 相对） | 内容 |
|-----------------|------|
| `[fp]` | returnAddr（原 Steed 帧返回地址，entry stub 从 `AsmStackContext.returnAddress` 写入 `[callFrameTop-8]`） |
| `[fp-8]` | frameType = `ASM_INTERPRETER_BRIDGE_FRAME`（lazy deopt 时或上 bit30） |
| `[fp-16]` | prevFp（被去优化 Steed 帧的 fp） |
| `[fp-24]` | pc（固定 0） |
| 帧头之下 | x64：对齐槽 + C++ callee-save（5 槽）+ `call enterDeoptimizedFrame` 压入的返回地址（指向 `PopAsmInterpBridgeFrame`）；arm64：CalleeSave 9 对（无单独对齐槽，`Bl` 不压栈） |
| 更下方 | 运行时重建的解释器 / 基线帧（`DeoptEnterAsmInterpOrBaseline` 继续下探） |

#### 4.1.3 callee-save 寄存器

**有**（`FromInterpreterHandler` 场景除外，此时解释器已持有这些寄存器）：x64 rbx、r12–r15；arm64 x19–x28 + d8–d15。

#### 4.1.4 回栈：如何得到上一帧

读 `[fp-16]` prevFp 槽（`AsmInterpretedBridgeFrame::GetPrevFrameFp`，即被去优化 Steed 帧的 fp）。`FrameIterator::Advance` 的 ASM_INTERPRETER_BRIDGE_FRAME 分支据此回溯，并以 `[fp]` 的 returnAddr 参与调用点信息计算。

#### 4.1.5 退帧：PC 如何恢复

解释器执行完毕后，经 `call / bl enterDeoptimizedFrame` 压入的返回地址落入 `PopAsmInterpBridgeFrame`（`trampoline/{x64,aarch64}/common_call.cpp`）：弹 callee-save 与对齐 → 跳过 pc → 恢复 rbp/fp → 跳过 frameType 后 `ret`。PC 靠 `[fp]` 的 returnAddr 槽恢复，回到原 Steed 帧的调用方。

#### 4.1.6 构造方式与代码位置

`OptimizedCall::DeoptPushAsmInterpBridgeFrame`（`trampoline/{x64,aarch64}/optimized_call.cpp`；读 `AsmStackContext::isFrameLazyDeopt` 决定是否写 bit30），由 `ArkSteedDeoptimizationEntry`（eager 路径）与 `DeoptHandlerAsm`（lazy 路径）调用；配合 `CommonCall::PopAsmInterpBridgeFrame` 弹帧、`OptimizedCall::DeoptEnterAsmInterpOrBaseline` 进入重建帧。


---

## 5. 帧速查索引

| 帧 | 结构定义 | 建帧函数 | 拆帧函数 |
|----|----------|----------|----------|
| STEED_FUNCTION_FRAME | `SteedFunctionFrame`（`frames.h`） | `ArkSteedAssembler::Prologue`（`arksteed_codegen.cpp` 发出） | `ArkSteedAssembler::Epilogue` |
| OPTIMIZED_ENTRY_FRAME | `OptimizedEntryFrame`（`frames.h`） | `OptimizedCall::PushJSFunctionEntryFrame`（`ArkSteedCall::ArkSteedCallEntry` 调用） | `OptimizedCall::PopJSFunctionEntryFrame` |
| OPTIMIZED_JS_FUNCTION_ARGS_CONFIG_FRAME | `OptimizedJSFunctionFrame` 同构（`frames.h`） | `ArkSteedCall::PrepareSteedCallFrame` / `OptimizedCall::PushOptimizedArgsConfigFrame` | `ArkSteedCall::RestoreSteedCallFrame` / `OptimizedCall::PopOptimizedArgsConfigFrame` |
| ASM_INTERPRETER_BRIDGE_FRAME（±lazy bit30） | `AsmInterpretedBridgeFrame`（`frames.h`） | `OptimizedCall::DeoptPushAsmInterpBridgeFrame` | `CommonCall::PopAsmInterpBridgeFrame` |
