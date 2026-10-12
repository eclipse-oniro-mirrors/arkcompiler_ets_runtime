# ArkSteed 原生单元测试

本目录通过 GTest 和 GN 构建原生 C++ 单元测试；JS/TS 功能用例及其运行器保留在相邻的 `../test/`。

当前的 `input_result_alias_test.cpp` 只验证普通结果分配遵守 `UseRegister` 已有的 `USED_AT_END`：输入值没有后续使用，不代表当前节点可以提前覆盖其物理寄存器。不增加独立的输入策略或隔离标记，也不扩展 `UseAny`、调用及显式固定/共用结果的约束。

测试构造确定的寄存器压力，通过真实 `AssignInputs` 消费约束、替换操作数并推进使用链，再调用真实结果分配。不依赖图构建器恰好产生某种寄存器排列。测试使用现有 GTest 和 `host_unittest_action`，不修改生产逻辑或导出符号。

## 执行

在独立 OpenHarmony 源码根目录（本仓库的 `../..`）只运行 ArkSteed 原生单元测试：

```bash
python3 ark.py x64.debug arksteed_host_unittest \
  --gn-args=ets_runtime_enable_ark_steed=true
```

`arksteed_host_unittest` 是本目录的 GN 测试分组，不包含其他模块或 JS/TS 用例。目前只有寄存器分配测试；也可将命令中的分组名替换为 `ArkSteedRegisterAllocatorTestAction`，单独运行该测试目标。

该目标自动构建当前测试和依赖，然后通过项目统一执行器运行。无需预先构建运行时、读取链接响应文件或使用专用 Python 脚本。启用 ArkSteed 时，目标也接入 `ark_unittest` 的编译器宿主测试分组；沿用该分组的 ASAN 和独立 OHOS 宿主测试限制。

GTest 将检查分成八个用例：通用/浮点结果选择、通用/浮点既有策略、空对象约束、空数组约束、young-space 快速分配约束和 unmapped arguments 初始化约束。测试程序位于 `out/x64.debug/tests/unittest/ets_runtime/ets_runtime/ArkSteedRegisterAllocatorTest`，支持标准 `--gtest_filter` 和 `--gtest_output` 参数。构建目标依赖完整优化器 source set，而非导出符号有限的动态库；Ninja 会根据源码依赖重新编译，不再手动混用旧目标文件。

## 覆盖

通用与浮点寄存器均覆盖：

- 普通 `UseRegister` 输入的最后使用、节点内额外使用、后续活跃，分别检查有无结果提示。
- 操作数被替换、值被释放后，普通结果仍避开末尾使用输入；结果压力溢出不能搬走当前输入。
- `UseAndClobberRegister` 仍可复用，后续活跃原值仍可从溢出槽恢复。
- 重复输入、空闲表快捷路径、临时寄存器排除及下一个节点清空结果排除集合。
- `UseAny`、显式固定/共用结果、调用清理序列，以及整数/浮点原地运算声明保持既有行为。

另检查空对象、空数组的既有约束，以及新拆分出的 young-space 快速分配和 unmapped arguments 初始化约束。快速分配需要一个普通临时寄存器；初始化节点接收 glue/argc/GlobalEnv/raw address 并需要两个临时寄存器。初始化节点不分配且不是调用节点，可触发 GC 的分配慢路径由 GraphBuilder 显式构造成 runtime call。

本测试不替代 JavaScript 对象/数组分配、真实调用、GC、反优化及跨架构运行回归。本分支实际验证及环境限制见 `.agents/arksteed-createemptyobject-implementation-report.md`。
