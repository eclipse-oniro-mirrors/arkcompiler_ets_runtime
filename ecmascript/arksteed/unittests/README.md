# ArkSteed 原生单元测试

本目录通过 GTest 和 GN 构建原生 C++ 单元测试；JS/TS 功能用例及其运行器保留在相邻的 `../test/`。

`input_result_alias_test.cpp` 验证当前普通结果分配保护所有已分配到寄存器的输入：包括 `UseRegister`、`UseAndClobberRegister` 和寄存器形式的 `UseAny`。输入值没有后续使用或已溢出，不代表当前节点可以提前覆盖其物理寄存器。显式固定/共用结果、原地算术和调用清理仍按各自既有策略处理。

测试构造确定的寄存器压力，通过真实 `AssignInputs` 消费约束、替换操作数并推进使用链，再调用真实结果分配。不依赖图构建器恰好产生某种寄存器排列。测试使用现有 GTest 和 `host_unittest_action`，不修改生产逻辑或导出符号。

## 执行

在独立 OpenHarmony 源码根目录（本仓库的 `../..`）只运行 ArkSteed 原生单元测试：

```bash
python3 ark.py x64.debug arksteed_host_unittest \
  --gn-args=ets_runtime_enable_ark_steed=true
```

`arksteed_host_unittest` 是本目录的 GN 测试分组，不包含其他模块或 JS/TS 用例。完整目标列表见 `BUILD.gn`；将命令中的分组名替换为 `ArkSteedRegisterAllocatorTestAction`，单独运行该测试目标。

该目标自动构建当前测试和依赖，然后通过项目统一执行器运行。无需预先构建运行时、读取链接响应文件或使用专用 Python 脚本。启用 ArkSteed 时，目标也接入 `ark_unittest` 的编译器宿主测试分组；沿用该分组的 ASAN 和独立 OHOS 宿主测试限制。

`ArkSteedRegisterAllocatorTest` suite 当前共有 23 项，覆盖通用/浮点结果选择、既有策略、快慢分配寄存器约束、字面量及 unmapped arguments 初始化约束和溢出槽管理。完整过滤器为 `ArkSteedRegisterAllocatorTest.*`，不使用文件名 `InputResultAlias` 过滤。测试程序位于 `out/x64.debug/tests/unittest/ets_runtime/ets_runtime/ArkSteedRegisterAllocatorTest`，支持标准 `--gtest_filter` 和 `--gtest_output` 参数。构建目标依赖完整优化器 source set，而非导出符号有限的动态库；Ninja 会根据源码依赖重新编译，不再手动混用旧目标文件。

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
