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
- `ArkSteedGraphVerifierTest`（`graph_verifier_test.cpp`，graph verifier 单元域：真实 IR 顶点通过
  `GraphProcessor<ArkSteedGraphVerifier>` 驱动 production verifier，八个 Debug 损坏类别 X1–X8 的
  条件化 death 观察、repaired 双生子、两模式正向控制与 Release 定义控制，矩阵行 G3.5，见本文件末节）。

选择单个目标不替代分组门禁：只有整个 `arksteed_host_unittest` 在两个模式下的完整运行才算 gate。

组织规则（H1）：每个域名一个文件和恰好一个 `host_unittest_action`（“一个文件一个域名、
一个域名一个目标”）；其他工作（例如 async 史诗）向同一组件追加的用例进入该域名已有的文件-所有者，
而不是新建平行文件。每个新目标只注册在 `arksteed_host_unittest` 分组一次，不加入本目录的
`unittest` 分组（后者进入 arm64/amd64 套件，H2）。夹具初始化失败、断言失败或链接失败都会使分组变红：
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

## Graph verifier（`graph_verifier_test.cpp`）

`ArkSteedGraphVerifierTest` 是矩阵行 G3.5 的 unit 域名（契约 C5），遵循“一个文件一个域名”和
“一个域名一个 `host_unittest_action`”。域覆盖 production `ArkSteedGraphVerifier`：每个用例都像
真实编译一样通过 `GraphProcessor<ArkSteedGraphVerifier>` 运行 verifier（`RunVerifier` 为每次运行
构造新的处理器实例，`seen_` 不会在两次运行之间复用）。夹具 A（`NativeAreaAllocator` + `Chunk` +
真实 `Graph`/`BB`/`Vertex::New`）不创建 VM、`JSThread`、`EcmaHandleScope`、汇编器和字节码资产，
也不修改生产逻辑或导出符号。

有效图覆盖处理器的全部位置：五个常量池、带控制顶点的三个基本块、一个 Phi 与非控制序列；零输入的
`DeoptVertex` 和零输入的 `CallRuntimeVertex` 都是合法形状。两模式共有的正向控制
`ValidGraphPassesEveryProcessorStageWithoutMutation` 运行 verifier 两次，要求完成且前后结构快照
（块顺序/身份与 id、常量池成员、控制顶点、Phi 与非控制组成、每个顶点的 opcode/表示/输入数与输入
身份）相等，从而确认 verifier 不修改图；快照从不调用 `Vertex::GetId()`。

独立 oracle（D2）是文档化契约而不是被测谓词的返回值：调用布局是
`GetInputCount() == CallVertex::FIRST_ARG_INDEX + GetActualArgc()`（`FIRST_ARG_INDEX == 3`，
即 target、new.target、this 加实际参数），`DeoptVertex` 固定零输入，runtime call 可以有零输入。

用例清单与两个模式的实际计数（名称与 `--gtest_output=json` 报告一致）：

x64 Debug — 九个用例：

- `ValidGraphPassesEveryProcessorStageWithoutMutation`（两模式共有的正向控制）；
- `DuplicateBlockIdIsDetectedInDebug`（X1：两个块复用同一个范围内 id，条件文本
  `seen_[block->GetId()] == 0`）；
- `EmptyPhiIsDetectedInDebug`（X2：Phi 没有任何输入，`GetInputCount() > 0`）；
- `PhiRepresentationMismatchIsDetectedInDebug`（X3：Phi 输入表示不一致，
  `GetInput(i)->GetValueRepresentation() == repr`）；
- `NullPhiInputIsDetectedInDebug`（X4：Phi 的第二个输入槽为 null，`input != nullptr`）；
- `CallLayoutMismatchIsDetectedInDebug`（X5：实际参数数为 1 的调用只有三个输入槽，
  `GetInputCount() == FIRST_ARG_INDEX + actualArgc_`）；
- `ConversionRepresentationMismatchIsDetectedInDebug`（X6：`U32ToTaggedNumberVertex` 取 TAGGED
  而不是必需的 INT32，`input->GetValueRepresentation() == expectedRepr`）；
- `EmptyCommonStubCallIsDetectedInDebug`（X7：common-stub 调用没有任何输入，
  `GetInputCount() >= 1`）；
- `BranchRepresentationMismatchIsDetectedInDebug`（X8：`BranchIfInt32CompareVertex` 取 TAGGED
  而不是必需的 INT32，`input->GetValueRepresentation() == expectedRepr`）。

x64 Release — 两个用例：

- `ValidGraphPassesEveryProcessorStageWithoutMutation`；
- `CorruptionFixturesWithoutActiveAssertionsDoNotAbortAndDoNotMutateTheGraph`。

检测语义：只有 verifier 通过 `RunVerifier` 真实运行时的死亡才算检测，而且每个 death 用例的
`ASSERT_DEATH` 都匹配该违反自身的条件文本（例如 `seen_[block->GetId()] == 0`），因此通用
`ASSERTION FAILED`、其它断言或无关崩溃都不能当作检测。夹具构造失败或 `Vertex::New` 返回空同样
不是检测：每个 death 用例先在同一父进程里运行该类的 repaired 双生子（只去掉唯一违反的同一张图），
要求 verifier 完成且前后快照相等——去掉违反后死亡变成完成，这就是敏感性；随后才在满足全部先决条件
的损坏图上观察死亡。损坏与 repaired 的区分、以及 X1 的全部块都先经 `Graph::Add` 取得合法 id
（范围内），都属于夹具事实，不依赖生产谓词。

Debug/Release 模式划分：death 用例只在 `#ifndef NDEBUG` 下声明，因为 production `ASSERT` 在
`NDEBUG` 下是 no-op。Release 用例在 `#ifdef NDEBUG` 下，只运行六种已定义的损坏形状 X1、X3、X5、
X6、X7、X8 以及全部八类 X1–X8 的 repaired 双生子，要求完成且前后快照相等，不断言任何 Release
检测。损坏的 X2（Phi 无输入）和 X4（Phi 的 null 输入）在 Release 下不被任何路径调用：关闭断言后
它们的 verifier 路径会读取无效的输入槽（X4 还会解引用 null 输入），因此对它们不承诺任何 Release
行为（包括“不崩溃”）；只有它们的 repaired 半边参与 Release 控制。用例不跳过、不禁用，也不使用
`GTEST_SKIP`。

头文件文档残留（单独工作，不在本单元域范围）：`ecmascript/arksteed/arksteed_graph_verifier.h:62-69`
的注释曾经不准确地描述调用最小值和 Deopt/runtime-call 最小值，不能当作强制契约；真实契约是上面的
调用布局、`DeoptVertex` 固定零输入和 runtime call 可以零输入。该残留已记录为单独工作
（ISSUE-0079）；本单元域不修改 production，也不发明新的任务标识。

单独运行该目标：

```bash
python3 ark.py x64.debug ArkSteedGraphVerifierTestAction \
  --gn-args=ets_runtime_enable_ark_steed=true
```

二进制位于 `out/x64.<mode>/tests/unittest/ets_runtime/ets_runtime/ArkSteedGraphVerifierTest`
（`<mode>` 为 `debug` 或 `release`），支持 `--gtest_filter`/`--gtest_output`；门禁仍为
`arksteed_host_unittest` 在 x64 Debug 和 Release 两个模式下的整体运行：

```bash
python3 ark.py x64.debug arksteed_host_unittest -j4 --gn-args=ets_runtime_enable_ark_steed=true
python3 ark.py x64.release arksteed_host_unittest -j4 --gn-args=ets_runtime_enable_ark_steed=true
```
