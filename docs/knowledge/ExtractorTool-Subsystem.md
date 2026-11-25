# SourceMap 与 HAP 提取集成

`ecmascript/extractortool/` 保留 SourceMap 解析和位置转换。HAP/ZIP 提取及文件映射的 `Extractor`、`ZipFile` 和 `FileMapper` 由 OpenHarmony 的 `ability_base:extractortool` 提供。本仓的适配代码位于 [adapter/ohos/extractortool/](../../adapter/ohos/extractortool/)。[子目录指南](../../ecmascript/extractortool/AGENTS.md)

## 1. 代码入口

| 场景 | 入口 | 核对项 |
|---|---|---|
| 从 HAP 读取 SourceMap | [source_map.cpp](../../ecmascript/extractortool/src/source_map.cpp) 中的 `SourceMap::Init` 和 `ReadSourceMapData` | `ENABLE_ABILITY_EXTRACTOR` 与 `enable_ecma_stackinfo` 的实际条件见 [js_runtime_config.gni](../../js_runtime_config.gni) 和 [BUILD.gn](../../BUILD.gn) |
| 延迟解析 | `SplitSourceMap()`、`ParseSourceMapData` | `sourceMaps_` 和 `sourceMapDatas_` 中的 `string_view` 依赖 `dataPtr_` 的生命周期 |
| 接收现成映射 | `SplitSourceMap(const std::string&)` | 解析结果存入 `eagerSourceMaps_`，位置查询时优先检查该表 |
| 行列与栈位置转换 | `VlqRevCode`、`Find`、`TranslateUrlPositionBySourceMap`、`TranslateStackTraceWithStatus` | 保持原有行列换算和映射失败处理，状态定义见 [source_map_init_status.h](../../ecmascript/extractortool/src/source_map_init_status.h) |
| 诊断调用方 | [js_stackinfo.cpp](../../ecmascript/dfx/stackinfo/js_stackinfo.cpp) | 检查提取能力开关与文件映射的使用场景，栈追踪见 [DFX](DFX-Subsystem.md) |

## 2. 测试与验证

从 OpenHarmony 源码根目录运行对应的 host action。构建入口见 [根指南](../../AGENTS.md)，测试定义见 [tests/BUILD.gn](../../ecmascript/extractortool/tests/BUILD.gn)。

```bash
./build.sh --product-name rk3568 --build-target ExtractorToolTestAction
./build.sh --product-name rk3568 --build-target SourceMapUnitTestAction
./build.sh --product-name rk3568 --build-target ExtractorAdapterTestAction
```

`ExtractorToolTestAction` 覆盖常见映射与转换，`SourceMapUnitTestAction` 包含大输入用例。`ExtractorAdapterTestAction` 使用模拟的 AbilityBase 类型和测试内的适配逻辑。涉及真实 HAP 提取时，还需在启用提取能力的产品配置中验证包内映射读取。 [适配层测试定义](../../adapter/ohos/BUILD.gn)
