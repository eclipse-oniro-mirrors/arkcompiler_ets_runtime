# libark_jsruntime_dfx.so 裁剪与单独验证指南

## 概述

为降低设备侧 ROM 占用，`libark_jsruntime_dfx.so`（dfx 版运行时）对运行时不依赖的
**AOT（生成侧）/ JIT / PGO** 相关代码进行了裁剪，裁剪后 so 体积显著下降
（以 rk3568 为例，strip 后约 8.6MB，对比基线约 13.2MB）。

> 说明：裁剪不影响应用运行的 **AOT 加载**路径（加载 .an 快照、ELF 等），仅裁剪
> 运行时内部不再使用的编译生成与 profile 采集逻辑。

## 裁剪机制

裁剪通过两处构建配置组合实现，均定义于 `js_runtime_config.gni` / `BUILD.gn`：

1. **源文件排除**（`dfx_trim_exclude_sources`，共 25 个文件）：
   - `ecmascript/pgo_profiler/` 全部实现文件（pgo_profiler、pgo_profiler_decoder、
     pgo_profiler_encoder、pgo_profiler_manager、pgo_state、pgo_utils 等）
   - `ecmascript/compiler/pgo_type/`（pgo_type_manager、pgo_type_parser、
     pgo_type_recorder、pgo_hclass_generator）
   - `ecmascript/compiler/pgo_bc_info.cpp`
   - `ecmascript/jit/` 全部实现文件（jit、jit_task、jit_thread、jit_profiler、
     jit_resources、jit_dfx、compile_decision）
   - `ecmascript/dfx/dump_code/jit_dump_elf.cpp`

2. **编译宏**（`dfx_extra_config`）：
   ```gn
   config("dfx_extra_config") {
     defines = [
       "ENABLE_HITRACE_LOCAL_HANDLE_DETECT",
       "EXCLUDE_JIT",
       "EXCLUDE_PGO",
     ]
   }
   ```
   源码中通过 `#ifndef EXCLUDE_JIT` / `#ifndef EXCLUDE_PGO` 门控对上述被裁剪
   符号的调用。

**门控原则**：
- 对**被裁剪源文件中定义、但声明在头文件**中的非内联符号，任何仍在编译的
  `.cpp` 引用点都必须用 `#ifndef EXCLUDE_*` 包裹（必要时提供空实现/默认返回值）。
- 头文件中的声明与 `.cpp` 中的定义要保持门控一致，避免声明存在而定义被裁剪
  导致链接期 undefined symbol。
- 仅 `using` 别名、类型前向声明、指针成员、内联访问器不产生链接引用，无需门控。

## 构建参数

| 参数 | 默认值 | 说明 |
|:--|:--|:--|
| `ets_runtime_feature_enable_dfx` | `false` | 是否编译 `libark_jsruntime_dfx.so`（设备侧，仅 phone/PC 类产品启用） |
| `ets_runtime_test_enable_dfx` | `false` | host/设备测试是否**链接 dfx 裁剪版运行时**并排除 JIT/PGO/AOT 相关测试用例 |

两者独立：前者控制设备产物，后者控制测试构建与运行。

## 单独验证步骤

所有命令在 OpenHarmony 源码根目录执行，产品以 rk3568 为例。

### 1. 构建设备 dfx 版运行时

```bash
./build.sh --product-name rk3568 --build-target libark_jsruntime_dfx \
    --gn-args ets_runtime_feature_enable_dfx=true
```

### 2. 构建并运行 host 测试（dfx 模式）

```bash
./build.sh --product-name rk3568 --build-target ark_unittest \
    --gn-args ets_runtime_test_enable_dfx=true
```

`ark_unittest` 聚合全部 host 单元测试（C++ 单测链接 `libark_jsruntime_test.so`，
字节码/ES2021 测试走 `ark_js_vm` 动态链接 dfx 版运行时）。

### 3. 构建并运行设备/fuzz 测试（dfx 模式）

```bash
./build.sh --product-name rk3568 --build-target ark_js_unittest \
    --gn-args ets_runtime_test_enable_dfx=true
```

### 4. 常规回归（不带 dfx 参数）

确保非 dfx 构建不受影响：

```bash
./build.sh --product-name rk3568 --build-target ark_unittest
./build.sh --product-name rk3568 --build-target ark_js_unittest
```

## 测试排除范围（`ets_runtime_test_enable_dfx=true` 时）

dfx 裁剪了 JIT/PGO/AOT 生成侧代码，以下测试在 dfx 模式下自动排除/门控：

- **host（ark_unittest）**：
  - 整组排除：`compiler/aot_file/tests`、`compiler/codegen/maple/test`、
    `compiler/tests`、`jit/tests`、`pgo_profiler/tests`、`ohos/tests`
    （后者的 CodeDecryptTest、CrashTest、JSPandaFileSnapshotInterfacesTest、
    ModuleSnapshotInterfacesTest、OhosTest 均链接编译器库 `libark_jsoptimizer_set`）
  - 用例级排除：`ecmascript/tests` 的 `GC_MachineCode_Test`/`GC_MemMapAllocator_Test`
    （调用 `Jit::GetInstance`）、`JsStackInfoTest` 的 `TestArkWriteJitCode`
    （JIT ELF dump）、`JSNApiTests_AotTrigger`（`PGOProfilerManager::RequestAot`）
- **设备/fuzz（ark_js_unittest）**：
  - 整组排除：`jit/tests`、`pgo_profiler/tests`、`compiler/aot_file/tests`
  - fuzzer 排除：`binarytotext`、`loadapbinaryfile`、`mergeapfiles`、
    `mergeapfilesformerger`、`mergewithexistprofile`（均依赖 PGO 文件解析/合并/编码）

## 注意事项

- **设备 dfx so** 使用 version script（`libark_jsruntime.map`）仅导出白名单符号并
  `local:*`，配合 `--gc-sections` 可丢弃未被引用的死代码，因此少量未门控引用在
  设备构建中不会暴露。
- **host 测试 so（`libark_jsruntime_test.so`）** 无 version script，且以
  `-fno-lto` 链接，所有符号均为导出 root，`--gc-sections` 无法丢弃死函数。
  **任何对已裁剪符号的未门控引用都会在 host 测试链接期报
  `ld.lld: error: undefined symbol`**，因此在新增代码时务必遵循"门控原则"。
- 修改 `dfx_trim_exclude_sources` 或新增对裁剪符号的调用后，须同时跑
  "单独验证步骤 1~3" 与 "常规回归"（步骤 4）。
