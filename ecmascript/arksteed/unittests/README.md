# ArkSteed 原生单元测试

本目录通过 GTest 和 GN 构建原生 C++ 单元测试；JS/TS 功能用例及其运行器保留在相邻的 `../test/`。

`input_result_alias_test.cpp` 验证当前普通结果分配保护所有已分配到寄存器的输入：包括 `UseRegister`、`UseAndClobberRegister` 和寄存器形式的 `UseAny`。输入值没有后续使用或已溢出，不代表当前节点可以提前覆盖其物理寄存器。显式固定/共用结果、原地算术和调用清理仍按各自既有策略处理。

测试构造确定的寄存器压力，通过真实 `AssignInputs` 消费约束、替换操作数并推进使用链，再调用真实结果分配。不依赖图构建器恰好产生某种寄存器排列。测试使用现有 GTest 和 `host_unittest_action`，不修改生产逻辑或导出符号。

## 执行

在独立 OpenHarmony 源码根目录（本仓库的 `../..`）只运行 ArkSteed 原生单元测试。分组门禁必须在
x64 Debug 和 x64 Release 两个模式下都通过（Release 同时验证 `NDEBUG` 下的编译与 LTO 链接）：

```bash
python3 ark.py x64.debug arksteed_host_unittest -j4 \
  --gn-args=ets_runtime_enable_ark_steed=true
python3 ark.py x64.release arksteed_host_unittest -j4 \
  --gn-args=ets_runtime_enable_ark_steed=true
```

`arksteed_host_unittest` 是本目录的 GN 测试分组，不包含其他模块或 JS/TS 用例。完整目标列表见 `BUILD.gn`；将命令中的分组名替换为 `ArkSteedRegisterAllocatorTestAction`，单独运行该测试目标。
本项目的 `gtest_main` 把报告输出默认设为 `xml:./`，所以直接执行任一 `<Name>` 二进制会把
`<Name>.xml`（同名已存在时追加 `_1`、`_2`）写进当前目录；这是测试框架的默认行为，额外的
`--gtest_repeat` 等参数不会覆盖它。定向执行（含重复执行）请在 build 上下文 `builds/<WD>`
内启动，或显式传入绝对路径的 `--gtest_output=json:<文件>`，并且不要从源码 checkout 顶层启动：
引擎的 worktree 清洁门禁要求 checkout 在评审前后保持 `git status --porcelain --untracked-files=all`
为空，任何写进 checkout 的报告都是未跟踪文件。

- `ArkSteedPipelineStatisticsTest`（`pipeline_statistics_test.cpp`，pipeline statistics 单元域：
  选项、`Chunk` 计数、per-pass collector/summary 与名称匹配，矩阵行 G6.5，见
  `../test/coverage/matrix.md` 的「Записи закрытия строк кластера unit」章节）；
- `ArkSteedFeedbackReaderTest`（`feedback_reader_test.cpp`，反馈读取器单元域：named/value/
  element/global/call/operation feedback 的已处理字段、IC 槽定位与拒绝路径，矩阵行 G2.3u，见
  `../test/coverage/matrix.md` 的「Записи закрытия строк кластера unit」章节和本文件末节）。该目标
  使用完整优化器 source set，并以 `arksteed_bytecode_fixture` 为目标提供夹具数据与 abc。
- `ArkSteedDeferredCodeTest`（`deferred_code_test.cpp`，deferred 代码发射与布局单元域：
  `Deferred block` 注释的计数、顺序与尾部位置，矩阵行 G7.4u，见
  `../test/coverage/matrix.md` 的「Записи закрытия строк кластера unit」章节和本文件末节）。该目标
  使用完整优化器 source set；图由真实 IR 顶点手工构造（夹具 A），汇编器开启注释（夹具 C），
  不新增 abc 资产。
- `ArkSteedGraphVerifierTest`（`graph_verifier_test.cpp`，通过真实 IR 驱动 production verifier 的 Debug 断言与有效图正向控制，详见本文件末节）。

选择单个目标不替代分组门禁：只有整个 `arksteed_host_unittest` 在两个模式下的完整运行才算 gate。

组织规则（H1）：每个域名恰好一个 `host_unittest_action`；大型域可按行为拆分为多个源文件，
共享同一个 fixture 类型和目标，避免复制夹具或建立平行测试入口。新增用例应追加到已有域及目标。
每个新目标只注册在 `arksteed_host_unittest` 分组一次，不加入本目录的 `unittest` 分组
（后者进入 arm64/amd64 套件，H2）。夹具初始化失败、断言失败或链接失败都会使分组变红：
测试不会被跳过、禁用或从分组中移除。

该目标自动构建当前测试和依赖，然后通过项目统一执行器运行。无需预先构建运行时、读取链接响应文件或使用专用 Python 脚本。启用 ArkSteed 时，目标也接入 `ark_unittest` 的编译器宿主测试分组；沿用该分组的 ASAN 和独立 OHOS 宿主测试限制。

`ArkSteedRegisterAllocatorTest` suite 当前共有 23 项，覆盖通用/浮点结果选择、既有策略、快慢分配寄存器约束、字面量及 unmapped arguments 初始化约束和溢出槽管理。完整过滤器为 `ArkSteedRegisterAllocatorTest.*`，不使用文件名 `InputResultAlias` 过滤。测试程序位于 `out/x64.debug/tests/unittest/ets_runtime/ets_runtime/ArkSteedRegisterAllocatorTest`，支持标准 `--gtest_filter` 和 `--gtest_output` 参数。构建目标依赖完整优化器 source set，而非导出符号有限的动态库；Ninja 会根据源码依赖重新编译，不再手动混用旧目标文件。

## Debug 与 Release

两个模式都是必需的门禁：Debug 保留 production `ASSERT`，Release 验证 `NDEBUG` 下的编译与 LTO 链接
以及文档化行为。部分域名使用负向检查（death 测试或损坏输入）：依赖 production `ASSERT` 的负向观察
只在 Debug 有效，并以 `#ifndef NDEBUG` 限定；Release 下同一夹具改由正向控制和文档化行为覆盖（H5）。
每个域名的具体限制记录在 `../test/coverage/matrix.md` 中对应行的关闭记录里；任何模式下测试都不被
跳过或禁用。

## 覆盖

通用与浮点寄存器均覆盖：

- 普通 `UseRegister` 输入的最后使用、节点内额外使用、后续活跃，分别检查有无结果提示。
- 操作数被替换、值被释放后，普通结果仍避开末尾使用输入；结果压力溢出不能搬走当前输入。
- `UseAndClobberRegister` 输入仍受保护，后续活跃原值保留在溢出槽。
- 重复输入、临时寄存器排除及下一个节点不残留输入保护。
- 空闲表快捷路径通过真实分配器溢出并释放非输入寄存器构造，断言存在未保护的空闲寄存器、输入仍受保护；结果提示不能覆盖输入。
- 寄存器形式的 `UseAny` 输入同样受保护；显式固定/共用结果、调用清理序列，以及整数/浮点原地运算声明保持既有行为。

常量和动态大小统一使用 `AllocateInYoung(size, hclass)`，生产慢路径传入 tagged size 和 HClass；保留 unmapped arguments 初始化约束测试。分配大小采用 INT64，寄存器测试包括高于 32 位的大小，验证活跃值保护及慢路径 tagged 参数约束。

快分配节点检查普通输入/结果寄存器约束及活跃寄存器保留；慢分配使用通用 `CallRuntime AllocateInYoung`，检查 tagged size 及可选的 tagged HClass 参数、固定输入/返回寄存器、既有调用深度统计、tagged/Float64 跨调用保存和编译事实分类。通用节点保留 GC 分配及其他保守调用属性；单参数和双参数形式均返回未初始化完成的对象，禁止 lazy frame、catch 和异常恢复元数据；四类初始化检查输入、共用结果和临时寄存器约束。初始化结果共用 allocation 输入，四类字面量初始化器均使用显式 tagged HClass 输入和一个临时寄存器。区间验证覆盖 `CanAllocateInYoung → BranchIfTrue`、快慢块、合并 Phi 和初始化：检查谓词的只读/不可复用属性与寄存器约束、参数与分支方向、Phi 输入与前驱对应关系、原始内存唯一用途，以及检查到分配和分配到初始化之间的安全操作。谓词必须在分支所在块内计算；拒绝其他布尔条件、未调度或旧块中的谓词、重复调度，以及谓词与分支之间的调用、写入和普通计算，寄存器分配后的搬移除外。快慢块和初始化块之间允许跳转，寄存器分配后允许搬移；慢调用大小须为与检查值相等的 tagged 整数常量，或由同一 INT64 大小与整数标记编码得到的动态值；带 HClass 的重载进入同一初始化区间，其他 runtime 调用不能代替分配。新增检查覆盖 0/3 参数、非 tagged size/HClass、两种形式的恢复元数据、分配后调用或反优化，以及未初始化对象进入帧状态。双参数正向夹具验证四类字面量及 arguments 的合流和初始化，寄存器测试覆盖常量/动态大小及跨调用存活的 HClass 根。HClass 来源由构图保证，不新增输入身份比较或运行时类型检查。

arguments 专项校验覆盖实际参数数量、完整 64 位元素区大小公式、快慢大小一致性、对象固定布局大小，以及非法操作和裸地址逃逸。正向用例覆盖零参数、动态参数、大于普通对象阈值和高于 32 位的大小；负向用例覆盖大小截断、参数数量错配和慢路径参数错配。arguments 对象初始化仅允许已有的非 GC 屏障调用。元素区初始化节点只写入数组头；原生夹具验证分配和数组头契约，未建模后续元素复制循环。实际循环在第二次分配前写完所有元素且没有 safepoint，由 JS 压力及显式 GC 用例补充验证。这些原生测试不等同于大于 4 GiB 的真实内存分配或机器码执行测试。

`initialization_interval_verifier_test.cpp` 的手工图夹具同时满足公共 pre-regalloc 图契约：已安装的普通顶点、Phi 及控制顶点显式设置其基础 `Vertex` owner，所有基本块具有终结控制顶点，跳转的 predecessor index 与目标块的前驱顺序一致。分配语义负向用例保留这些结构条件，仅破坏被测的分配/初始化约束；专门破坏公共图结构的用例只要求断言终止，不依赖先触发哪条诊断。

同步重入专项测试及构建注册随旧完整创建调用契约一并删除；这不表示同步重入已经安全。GC、同步回调、异常及依赖失效恢复由其他 PR 处理，当前聚焦测试不证明这些边界。

本测试不替代 JavaScript 对象/数组分配、真实调用、GC、反优化及跨架构运行回归。本分支实际验证及环境限制见 `.agents/unify-young-allocation-plan.md`。

## 反馈读取器（`feedback_reader_test.cpp`）

`ArkSteedFeedbackReaderTest` 是 G2.3u 的 unit 域名（契约 C3），遵循“一个文件一个域名”和
“一个域名一个 `host_unittest_action`”。域覆盖 `ArkSteedFeedbackReader` 的
`ReadNamedAccessFeedback`/`ReadValueAccessFeedback`/`ReadElementAccessFeedback`/
`ReadGlobalAccessFeedback`/`ReadCallFeedback`/`ReadOperationFeedback` 以及两种
`TryGetFeedbackSlotId`（主槽与允许立即数的形式）：命名的 mono 对、位于 IC 上限的 poly 数组、
超出上限的 poly 数组、megamorphic 槽、未执行槽、元素 mono/poly、全局 `PropertyBox`、调用
`JSFunction` 与负 builtin id，以及全部 `ArkSteedOperationHint` 取值。

夹具是 `ArkSteedBytecodeFixture`（`fixtures/bytecode_fixture.h`，域
`arksteed-host-unit-bytecode-fixture`）：每个 profile 状态一个夹具方法，方法元数据、解码后的
`kungfu::BytecodeInfo`、`ProfileTypeInfo` 与 `ArkSteedHeapBroker` 都由它提供；本目标不新增 abc
资产、不改生产代码、不加 friend 声明或导出符号。

该域是 P0，读取路径必须与生产一致：`ArkSteedHeapBroker::SerializingScope` 内部取
`Jit::JitLockHolder`，后者管理其编译环境线程（JIT 线程）的 managed 状态并在 Debug 断言该线程
就是当前线程，因此每次读取都在夹具绑定的 JIT 线程池工作线程上执行，与编译任务相同；用例线程
只投递任务并等待，等待期间离开 managed 状态（`ThreadNativeScope`），否则需要挂起全部线程的 GC
会一直等待该线程到达安全点。投递到 JIT 任务池的适配任务派生自 `JitTask::AsyncTask`（VM 析构时
`Jit::ClearTaskWithVm` 会把池中仍被引用的每个任务 downcast 到该类型），并基于夹具绑定的真实
`JitTask`（`ArkSteedBytecodeFixture::PoolTask`），因此该 downcast 得到合法对象。用例等待的是池对
适配任务的释放（适配任务析构，发生在 `Run` 返回且 worker 清空 running 槽之后），而不只是 body
完成：这样夹具 teardown 不会与仍被池引用的投递重叠。回归用例
`PoolDispatchReleasesTaskOwnershipBeforeTeardown` 连续投递 64 次，反复进入该完成→teardown 窗口。

独立 oracle（D2）不是被测表达式本身：夹具声明的槽状态（弱引用 + handler、四个 case pair 的
poly 数组、超出上限的数组、hole、未执行槽、`PropertyBox`、`JSFunction`、负 builtin id、
采样整数）、站点解码出的 IC 槽，以及 `pgo::PGOSampleType` 的文档化位布局（低 10 位为 primitive
type、第 10 位起为 false weight、第 21 位起为 true weight）。负向对照
`MutationControlsRejectShiftedExpectationsAndSubstitutedNameInput` 证明同一检测器对偏移的
case 数、偏移的槽号以及被替换成 `ICSlotId` 的名字输入敏感，并且被替换输入上的生产调用返回
false 且清空输出结构。

单独运行该目标：

```bash
python3 ark.py x64.debug ArkSteedFeedbackReaderTestAction \
  --gn-args=ets_runtime_enable_ark_steed=true
```

二进制位于 `out/x64.debug/tests/unittest/ets_runtime/ets_runtime/ArkSteedFeedbackReaderTest`，
支持 `--gtest_filter`/`--gtest_output`；门禁仍为 `arksteed_host_unittest` 在 x64 Debug 和 Release
两个模式下的整体运行。

`feedback_reader_test.cpp` 还包含夹具生命周期用例 `ArkSteedBytecodeFixtureLifecycleTest`（域
`arksteed-host-unit-bytecode-fixture`，不是 G2.3u 的用例，
`--gtest_filter=ArkSteedFeedbackReaderTest.*` 不会选中它）：它以零 readiness 预算确定性地到达
夹具 readiness 握手的失败路径（`SetReadinessBudgetForTesting`），并证明失败后的 teardown 不崩溃、
不挂起。夹具在失败返回前停止 JIT 任务池：`Taskpool::Destroy` 把池的初始化计数降为零，使
`Jit::ClearTaskWithVm` 驱动的 `ForEachTask` 不再触及任何任务；`Runner::TerminateThread` 汇合工作
线程，其 pop 循环先排空队列再响应终止标志，因此握手探针任务已被回收，不会以不兼容类型进入宿主
VM 的清理路径。

## Deferred 代码（`deferred_code_test.cpp`）

`ArkSteedDeferredCodeTest` 是矩阵行 G7.4u 的 unit 域名（契约 C11），遵循“一个文件一个域名”和
“一个域名一个 `host_unittest_action`”。域覆盖 `ArkSteedCodeGenerator::Generate()` 的 deferred
发射：`EmitDeferredCode()` 为每条 deferred 记录写入一个 `Deferred block` 注释，并把它放在
`EmitDeferredCode()` 之后的主流程之外；`DeferredCopyCOWElements` 的实体以
`CopyCOWElements deferred call` 注释开头。

夹具 A（`NativeAreaAllocator` + `Chunk` + `Graph` + 真实 IR 顶点）手工构造单基本块图，夹具 C
是真汇编器（`EnableComments(true)`）并带真实 `JSThread`（`entryThread`）；图经过生产寄存器分配
处理器、生产寄存器分配器和生产代码生成器。观测值只有注释文本、注释的 `pcOffset` 顺序和代码
长度，不比较机器码字节或绝对偏移（D4）。

独立 oracle（D2）不是被测表达式本身：deferred 记录数由图本身推导——
`EnsureWritableElementsVertex` 带 `IS_DEFERRED_CALL`，每条顶点恰好排队一条
`DeferredCopyCOWElements`，因此 `N` 条这样的顶点产生 `N` 条记录，而没有这类顶点的图产生 0 条。
单基本块图以 `ReturnVertex` 结束，所以主流程的最后一条注释是 `Epilogue`，deferred 区域必须位于
它之后。负向对照 `MutationControlRejectsLostBlockAndMidFlowBlock` 证明检测器对丢失的 deferred
块（计数少于图排队的数量）、多余的块以及位于主流程中间的块敏感；这些检查只比较测试自身的注释
列表，不依赖生产 `ASSERT`，因此两个模式下都不跳过、不禁用（H5）。

单独运行该目标：

```bash
python3 ark.py x64.debug ArkSteedDeferredCodeTestAction \
  --gn-args=ets_runtime_enable_ark_steed=true
```

二进制位于 `out/x64.debug/tests/unittest/ets_runtime/ets_runtime/ArkSteedDeferredCodeTest`，
支持 `--gtest_filter`/`--gtest_output`；门禁仍为 `arksteed_host_unittest` 在 x64 Debug 和 Release
两个模式下的整体运行。

本测试不替代 JavaScript 对象/数组分配、真实调用、GC、反优化及跨架构运行回归。

## DCE 契约域

`ArkSteedDceTest` 包含 34 个 GTest 用例，Debug 和 Release 运行相同的有效 IR 语义测试，覆盖 `arksteed_dce.{h,cpp}` 的删除策略、依赖遍历、图过滤和完整 DCE pass。

The single target uses `dce_policy_test.cpp`, `dce_editor_test.cpp` and `dce_closure_test.cpp` for the
behavioral cases. `dce_test_fixture.{h,cpp}` supplies one shared fixture type and graph helpers; declarations
and short templates stay in the header, while graph construction and assertions are defined out of line.
The former opcode/property-table duplication tests are removed; behavior tests retain conservative
required-operation handling, unknown opcodes, hidden dependencies, exact survivors and rootless cycles.

Allocation retention fixtures use the current capacity-check branch, fast/slow allocation paths, merge Phi
and matching initializer. The initialized array result is unused; both filtering and full DCE must preserve
the complete allocation interval. These fixtures pass the production verifier before and after DCE.

- `DceRemovalPolicy::Classify` 使用 opcode 的正向 `CAN_BE_REMOVED_IF_DEAD` 属性，仅允许已证明可删除的纯值操作和 Phi；调用、写入、分配、可抛出或反优化操作及 GC 相关节点必须保留。
- 公共 `DceDependencies::Visit` 返回 `void`，按稳定顺序遍历普通输入、eager/lazy 帧状态及 catch Phi 依赖。私有活跃性遍历不让 throwable 保留 dead catch Phi 的独占输入；live Phi 通过自己的普通输入保留 producer。
- `DceGraphEditor::Filter` 删除未标记的可删除值和 dead Phi，保留控制节点、参数及必要操作，返回 `Changed` 或 `Unchanged`。调用者必须提供当前图的有效 live set，且所有保留节点的依赖必须保留；Debug 断言检查这些选择义务。
- `ArkSteedDce::Run` 的用例覆盖 dead chain、共享 producer、普通及 catch Phi cycle、隐藏依赖、dead catch Phi 删除、遍历顺序无关性及幂等性；有效图通过前后图校验及 pre-regalloc 消费者。
- `ArkSteedDce::Run` 使用一个 invocation-local `DcePreparation`：保留 registry 指针集合、计算一次单调依赖闭包并稳定过滤；不保留独立 probe、重复准备、Release 图校验或错误诊断。瞬时标记先记录清理所有权，再设置 `DceMarked`，退出时清除，包括已从图中移除的节点。
- finalized pre-allocation 图的公共完整性由正常的 `ArkSteedGraphVerifier` Debug 断言负责，编译器在 DCE 前后调用它。Release 信任内部 IR；DCE 不再提供 `Failed` 或畸形图恢复契约。外部输入及生产边界的既有拒绝路径不受此策略影响。

Both DCE entry points require a non-null graph: Debug asserts this precondition, and Release provides no
null-input recovery. Property predicates and getters share X-macro lists with local `G.PRE.02-CPP`
annotations because these macros generate declarations rather than replace ordinary function calls.
Keep each macro within the `G.PRE.06` ten-line limit, including the `#define` line.
The compiler retains both DCE-boundary graph checks without separate pipeline-statistics entries;
`CoreDce` remains timed. Debug verification work still contributes to the total compilation time.

仅构建并运行 DCE 域（从 OpenHarmony 源码根目录执行）：

```bash
python3 ark.py x64.debug ArkSteedDceTestAction \
  --gn-args=ets_runtime_enable_ark_steed=true
```

目标通过现有 `host_unittest_action` 构建测试和依赖，再由 `script/run_ark_executable.py` 设置共享库搜索路径并运行 GTest；无需手工链接或直接执行缺少环境的测试程序。

For an already configured x64 Debug tree, build and run just the DCE and initialization-interval domains
from the OpenHarmony source root:

```bash
prebuilts/build-tools/linux-x86/bin/ninja -C out/x64.debug -j4 \
  ArkSteedDceTestAction ArkSteedInitializationIntervalVerifierTestAction
```

Use `out/x64.release` for the corresponding Release targets. The initialization-interval test includes
`js_array.h` and `js_arguments.h` directly; it must not depend on verifier implementation includes.

JS/TS 运行时回归的六个 DCE 用例及运行命令见 `../test/README.md` 的 DCE 小节；原生图契约测试不替代真实运行时的副作用、异常和 GC 检查。

## 图校验域（`graph_verifier_test.cpp`）

`ArkSteedGraphVerifierTest` 在 Debug 中有 9 个用例：一个有效图正向控制，以及重复 block id、空 Phi、Phi 表示不匹配、空 Phi 输入、调用布局不匹配、转换输入表示不匹配、缺少 glue 的 common-stub 调用、分支输入表示不匹配这八类断言控制。每类畸形图都有修复后的有效图对照，检查完整校验不修改图。Release 仅运行一个有效图用例，不对畸形内部 IR 承诺结果。

校验器通过真实 `GraphProcessor<ArkSteedGraphVerifier>` 运行，使用无 VM 的原生图 fixture。公共检查涵盖 concrete 输入布局及选项、成员及 owner、常量池及参数、普通/帧/catch 引用、CFG/Phi/异常边及 loop 约定，并要求尚未安装寄存器分配元数据。普通输入沿用本分支的精确表示匹配（`FLOAT64` 和 `HOLEY_FLOAT64` 不互换）；帧值仅保留实际 argc 描述符的既有配对例外。所有校验 scratch、索引及循环均处于 `#ifndef NDEBUG` 内；Release 的显式校验调用不执行完整性检查。

`ecmascript/frames.h` owns the ArkSteed caller-argument FP-slot constants used by the graph builder and verifier: raw argc is slot +2 and the tagged call target is slot +3. Moving these declarations does not change the frame ABI or broaden the argc-only deopt representation exception.

`arksteed_graph_verifier.h` keeps the verifier interface; its Debug implementation and typed graph-processor
instantiation live in `arksteed_graph_verifier.cpp`. The optimizer source set supplies that implementation to
the compiler and native tests. Release keeps the existing no-op verifier interface and performs no IR preflight.

编译器还在寄存器分配之后以 `afterRegalloc=true` 调用同一个校验器。该阶段不再要求 pre-regalloc 图域及空分配元数据；保留基础分支的 young-allocation 初始化区间校验，并仅在该阶段允许区间内的 `GapMove` 和 `ConstantGapMove`。普通 DCE 前后调用仍执行完整的 pre-regalloc 公共契约。

单独运行图校验域：

```bash
python3 ark.py x64.debug ArkSteedGraphVerifierTestAction \
  --gn-args=ets_runtime_enable_ark_steed=true
```

将 `x64.debug` 替换为 `x64.release` 可运行对应 Release 目标。
