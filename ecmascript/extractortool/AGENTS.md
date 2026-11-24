# AGENTS

This file provides guidance for AI agents when working with code in the extractortool component.

Read the [SourceMap and HAP integration notes](../../docs/knowledge/ExtractorTool-Subsystem.md) for code and test entry points.

## Overview

ExtractorTool is a C++ component that provides source map processing for JavaScript debugging in the ArkCompiler ETS Runtime. The core extraction functionality (Extractor, ZipFile, FileMapper, etc.) is provided by the `foundation/ability/ability_base:extractortool` shared library (`OHOS::AbilityBase` namespace). This directory only retains arkcompiler-specific components.

## Architecture

### Local Components

- **`source_map.h/cpp`**: Processes JavaScript source maps for debugging support. Translates generated positions back to original source locations. Handles VLQ (Variable-Length Quantity) encoded mappings. Manages source map data from multiple packages. Uses the foundation Extractor internally for HAP file access.

The adapter layer in [adapter/ohos/extractortool/](../../adapter/ohos/extractortool/) bridges `OHOS::AbilityBase` types into the `panda::ecmascript` namespace. The adapter host test target is declared in [adapter/ohos/BUILD.gn](../../adapter/ohos/BUILD.gn).

### External Dependencies (from foundation/ability/ability_base)

- **`extractor.h`**: Main interface for file extraction from HAP/ZIP files (namespace `OHOS::AbilityBase`)
- **`zip_file.h`**: Low-level ZIP archive parser
- **`file_mapper.h`**: Memory-mapped file access abstraction
- Build target: `ability_base:extractortool`

### Key Data Structures

- **`SourceMapInfo`**: Stores source map position information (before/after row/column, sources/names indices)
- **`MappingInfo`**: Represents line/column mapping information
- **`SourceMapData`**: Complete source map data including sources, package name, position mappings
- **`InitStatus`**: Enum for source map initialization state (`NOT_EXECUTED`, `IN_EXECUTED`, `EXECUTED_SUCCESSFULLY`)

## Building

This project uses the **GN (Generate Ninja)** build system, integrated with the larger OpenHarmony/ArkCompiler build infrastructure.

### ENABLE_ABILITY_EXTRACTOR Macro

The `ENABLE_ABILITY_EXTRACTOR` define gates all code that depends on `ability_base:extractortool`. It is controlled at the GN level — no `#if` guards inside `adapter/ohos/extractortool/extractor_adapter.h` or `extractor_adapter.cpp`.

For dependency and macro conditions, see `enable_ecma_stackinfo` and the `ability_base:extractortool` dependency in [js_runtime_config.gni](../../js_runtime_config.gni) and [BUILD.gn](../../BUILD.gn).

**Source file inclusion** (`BUILD.gn`):
```gn
if (enable_ecma_stackinfo) {
  ecma_stackinfo_source = [ "adapter/ohos/extractortool/extractor_adapter.cpp" ]
}
```

`extractor_adapter.cpp` is only added to the source list when the dependency is available, so no preprocessor guard is needed inside the file.

Consumer files (`js_stackinfo.cpp/h`, `source_map.cpp/h`) still use `#if defined(ENABLE_ABILITY_EXTRACTOR)` to guard code sections that reference ability_base types, because those files compile in all OHOS configurations.

### Build Commands

Run from the OpenHarmony source root.
```bash
# Host tools (Linux x64)
./build.sh --product-name rk3568 --build-target ark_js_host_linux_tools_packages

# Device targets (libark_jsruntime shared object)
./build.sh --product-name rk3568 --build-target libark_jsruntime

# Add --gn-args is_debug=true for debug builds
```

## Testing

### Test Structure

Tests are organized in `tests/BUILD.gn`:

- **ExtractorToolTest** - Main test executable including:
  - `source_map_test.cpp` - Source map processing tests

> The `extractor_adapter_test.cpp` has moved to `adapter/ohos/tests/` together with the adapter sources. The `ExtractorAdapterTest` target is defined in `adapter/ohos/BUILD.gn`.

- **SourceMapUnitTest** - Additional source map unit tests:
  - `source_map_unit_test.cpp`

### Adapter Test Design

`extractor_adapter_test.cpp` is fully self-contained:
- Fake `OHOS::AbilityBase` types (`Extractor`, `FileMapper`, `ZipFile`, `ZipEntry`) are defined inline at the top of the file
- The adapter functions (`GetMmapDataAsShared`, `GetFilePathByOffset`) are implemented inline with the same logic as `extractor_adapter.cpp`
- No stub header files, no `fake/` directory, no `-include` flags, no `ENABLE_ABILITY_EXTRACTOR` define needed
- Trade-off: the test validates a copy of the adapter logic, not the compiled production `.cpp`. If `extractor_adapter.cpp` changes, the test must be updated manually.

### Running Tests

See the [SourceMap test commands](../../docs/knowledge/ExtractorTool-Subsystem.md#2-测试与验证).
