# ets_runtime 知识与目录指南索引

按任务、路径和术语检索请从 [仓库根指南](../../AGENTS.md) 开始。

## 1. 子系统知识

| 主题 | 文档 |
|---|---|
| 内存管理和 GC | [ArkTS GC](ArkTS-GC.md) |
| 并发和共享堆屏障 | [并发](ArkTS-Concurrency.md) |
| 共享对象与序列化 | [Sendable](ArkTS-Sendable.md) |
| ESM、CommonJS 和模块快照 | [模块](ArkTS-Module.md) |
| CPU 采样、堆分析、追踪和 VM 统计 | [DFX](DFX-Subsystem.md) |
| 断点、回调和 Drop Frame | [Debugger](Debugger-Subsystem.md) |
| SourceMap、HAP 依赖与测试 | [SourceMap 与 HAP 集成](ExtractorTool-Subsystem.md) |
| Quick Fix、热重载与冷补丁 | [Patch](Patch-Subsystem.md) |
| ArkSteed 编译知识 | [栈帧](JIT/arksteed_frames.md) · [回栈](JIT/arksteed_unwind.md) · [节点事实](JIT/arksteed_compile_info_facts.md) · [堆常量](JIT/arksteed_heap_constant.md) |

## 2. 嵌套指南

| 指南 | 适用范围 |
|---|---|
| [compiler_service/AGENTS.md](../../compiler_service/AGENTS.md) | AOT system ability and IPC |
| [ecmascript/arksteed/AGENTS.md](../../ecmascript/arksteed/AGENTS.md) | ArkSteed pipeline, IR and machine code |
| [ecmascript/base/AGENTS.md](../../ecmascript/base/AGENTS.md) | Runtime helpers and primitives |
| [ecmascript/builtins/AGENTS.md](../../ecmascript/builtins/AGENTS.md) | ECMAScript builtins and ArkTS extensions |
| [ecmascript/checkpoint/AGENTS.md](../../ecmascript/checkpoint/AGENTS.md) | Thread transitions and suspension |
| [ecmascript/compiler/AGENTS.md](../../ecmascript/compiler/AGENTS.md) | AOT/JIT compiler infrastructure |
| [ecmascript/compiler/aot_file/AGENTS.md](../../ecmascript/compiler/aot_file/AGENTS.md) | AOT files and their lifecycle |
| [ecmascript/compiler/aot_snapshot/AGENTS.md](../../ecmascript/compiler/aot_snapshot/AGENTS.md) | AOT snapshot data |
| [ecmascript/compiler/assembler/AGENTS.md](../../ecmascript/compiler/assembler/AGENTS.md) | Architecture-specific assemblers |
| [ecmascript/compiler/baseline/AGENTS.md](../../ecmascript/compiler/baseline/AGENTS.md) | Baseline JIT |
| [ecmascript/compiler/codegen/AGENTS.md](../../ecmascript/compiler/codegen/AGENTS.md) | Compiler backends and code generation |
| [ecmascript/compiler/mock/AGENTS.md](../../ecmascript/compiler/mock/AGENTS.md) | Compiler mocks |
| [ecmascript/compiler/pgo_type/AGENTS.md](../../ecmascript/compiler/pgo_type/AGENTS.md) | PGO type analysis |
| [ecmascript/compiler/trampoline/AGENTS.md](../../ecmascript/compiler/trampoline/AGENTS.md) | Compiler trampolines |
| [ecmascript/cross_vm/AGENTS.md](../../ecmascript/cross_vm/AGENTS.md) | Hybrid VM interop and unified GC |
| [ecmascript/daemon/AGENTS.md](../../ecmascript/daemon/AGENTS.md) | Shared heap GC daemon |
| [ecmascript/debugger/AGENTS.md](../../ecmascript/debugger/AGENTS.md) | Breakpoints, stepping and frame operations |
| [ecmascript/deoptimizer/AGENTS.md](../../ecmascript/deoptimizer/AGENTS.md) | Frame and state recovery |
| [ecmascript/dfx/cpu_profiler/AGENTS.md](../../ecmascript/dfx/cpu_profiler/AGENTS.md) | CPU sampling and profile output |
| [ecmascript/dfx/dump_code/AGENTS.md](../../ecmascript/dfx/dump_code/AGENTS.md) | Diagnostic JIT ELF output |
| [ecmascript/dfx/hprof/AGENTS.md](../../ecmascript/dfx/hprof/AGENTS.md) | Heap snapshots, tracking and sampling |
| [ecmascript/dfx/stackinfo/AGENTS.md](../../ecmascript/dfx/stackinfo/AGENTS.md) | Synchronous and asynchronous stacks |
| [ecmascript/dfx/tracing/AGENTS.md](../../ecmascript/dfx/tracing/AGENTS.md) | Runtime event tracing |
| [ecmascript/dfx/vmstat/AGENTS.md](../../ecmascript/dfx/vmstat/AGENTS.md) | VM counters and timing |
| [ecmascript/extractortool/AGENTS.md](../../ecmascript/extractortool/AGENTS.md) | Source map and HAP integration |
| [ecmascript/ic/AGENTS.md](../../ecmascript/ic/AGENTS.md) | Property inline caches |
| [ecmascript/interpreter/AGENTS.md](../../ecmascript/interpreter/AGENTS.md) | C++ and assembly interpreter paths |
| [ecmascript/jit/AGENTS.md](../../ecmascript/jit/AGENTS.md) | JIT compilation lifecycle |
| [ecmascript/jobs/AGENTS.md](../../ecmascript/jobs/AGENTS.md) | Microtasks and pending jobs |
| [ecmascript/js_vm/AGENTS.md](../../ecmascript/js_vm/AGENTS.md) | Host VM command-line interface |
| [ecmascript/jspandafile/AGENTS.md](../../ecmascript/jspandafile/AGENTS.md) | Ark bytecode loading |
| [ecmascript/mem/AGENTS.md](../../ecmascript/mem/AGENTS.md) | Allocation, barriers and garbage collection |
| [ecmascript/module/AGENTS.md](../../ecmascript/module/AGENTS.md) | ECMAScript modules and resolution |
| [ecmascript/napi/AGENTS.md](../../ecmascript/napi/AGENTS.md) | C++ native interop |
| [ecmascript/patch/AGENTS.md](../../ecmascript/patch/AGENTS.md) | Patch loading and method replacement |
| [ecmascript/pgo_profiler/AGENTS.md](../../ecmascript/pgo_profiler/AGENTS.md) | Profile recording and data |
| [ecmascript/quick_fix/AGENTS.md](../../ecmascript/quick_fix/AGENTS.md) | Quick Fix command-line tool |
| [ecmascript/regexp/AGENTS.md](../../ecmascript/regexp/AGENTS.md) | RegExp parsing and execution |
| [ecmascript/require/AGENTS.md](../../ecmascript/require/AGENTS.md) | CommonJS loading and cache |
| [ecmascript/serializer/AGENTS.md](../../ecmascript/serializer/AGENTS.md) | Serialization, transfer and interop |
| [ecmascript/snapshot/AGENTS.md](../../ecmascript/snapshot/AGENTS.md) | VM and module snapshots |
| [ecmascript/stackmap/AGENTS.md](../../ecmascript/stackmap/AGENTS.md) | Stack maps and frame metadata |
| [ecmascript/string/AGENTS.md](../../ecmascript/string/AGENTS.md) | String representations and interning |
| [ecmascript/stubs/AGENTS.md](../../ecmascript/stubs/AGENTS.md) | Runtime and compiler stubs |
| [ecmascript/trampoline/AGENTS.md](../../ecmascript/trampoline/AGENTS.md) | Architecture-specific trampolines |
| [ecmascript/ts_types/AGENTS.md](../../ecmascript/ts_types/AGENTS.md) | Compiler type encoding and builtin IDs |

## 3. 补充入口

下列目录可结合根指南和对应的源码或测试入口查阅。

| 目录 | 源码入口 | 关联指引 |
|---|---|---|
| `ecmascript/intl/` | [locale_helper.h](../../ecmascript/intl/locale_helper.h) | [Builtins 指南](../../ecmascript/builtins/AGENTS.md)、[运行时测试](../../ecmascript/tests/BUILD.gn) |
| `ecmascript/js_type_metadata/` | [元数据生成脚本](../../script/metadata_generate.py)、[根 BUILD.gn](../../BUILD.gn) 的 `gen_js_type_metadata` | [DFX](DFX-Subsystem.md) |
| `ecmascript/ohos/` | [ohos_pkg_args.h](../../ecmascript/ohos/ohos_pkg_args.h) | [OHOS 测试目标](../../ecmascript/ohos/tests/BUILD.gn)、[AOT](../aot-guide_zh.md) |
| `ecmascript/platform/` | [map.h](../../ecmascript/platform/map.h)、[signal_manager.h](../../ecmascript/platform/signal_manager.h) | [Unix 测试](../../ecmascript/platform/unix/tests/BUILD.gn)、[OHOS 测试](../../ecmascript/platform/unix/ohos/tests/BUILD.gn) |
| `ecmascript/sdk/` | [SDK 构建配置](../../ecmascript/sdk/BUILD.gn)、[AOT 声明](../../ecmascript/ts_types/lib_ark_builtins.d.ts) | [编译环境](../environment-setup-and-compilation.md) |
| `ecmascript/tests/` | [单测目标](../../ecmascript/tests/BUILD.gn)、[测试模板](../../test/test_helper.gni) | [根指南的验证章节](../../AGENTS.md#5-构建与验证) |
| `ecmascript/utils/` | [collections_atomic_helper.h](../../ecmascript/utils/collections_atomic_helper.h) | [并发](ArkTS-Concurrency.md)、[Builtins 测试](../../ecmascript/builtins/tests/BUILD.gn) |
