# ArkSteed CompileInfoFacts（编译期节点事实）知识文档

> 对应源码：`ecmascript/arksteed/arksteed_compile_info_facts.h/.cpp`（事实容器与生命周期）、`ecmascript/arksteed/arksteed_graph_builder.cpp`（事实的生产与消费）、`ecmascript/arksteed/arksteed_side_effect_classifier.h`（副作用分类与失效）。

---

## 1. CompileInfoFacts是什么

### 1.1 概念综述

它是一份**随字节码遍历（GraphBuilder）逐块流动、逐点更新的"节点事实缓存"**，回答三类问题：

1. **这个值是什么类型？**（类型格 `NodeType`）
2. **这个对象的 Map（hidden class）可能是哪些？**（候选 Map 集合，区分 stable / unstable）
3. **哪些加载/计算的结果可以复用？**（属性加载缓存、常量属性加载缓存、可用表达式缓存）

它的价值在于：解释器 IC 反馈只给出"过去观测到什么"，而 CompileInfoFacts 在编译期把这些观测**在控制流上传播、在检查点强化、在副作用点失效**，从而让同一函数内后续代码点能免费复用先前建立的事实（跳过冗余检查、消除重复加载、复用公共子表达式）。

### 1.2 关键类型，属性说明

每个值节点（`ValueVertex`）的事实条目是 **`NodeInfo`**：

| class | 说明 |
|---|---|
| `CompileInfoFacts` | 事实容器；ArkSteed 中按 RPO 基本块持有快照（`GraphBuilder::compileInfoFacts_`），GraphBuilder 遍历到哪个块就激活哪份 |
| `NodeInfo` | （`NodeType` 位集类型格 + `possibleHClasses_`）, 每个候选 HClass 带 `isStable` 标志 |
| `NodeType` | 16 位位集类型格（INT/DOUBLE/STRING/JS_ARRAY/JS_FUNCTION/JS_TYPED_ARRAY/…），支持交/并/移除/包含判断（`IntersectNodeType` / `UnionNodeType` / `NodeTypeIs` / `NodeTypeCanBe`），组合出 NUMBER、ODDBALL、JS_RECEIVER、ANY_HEAP_OBJECT 等复合类型 |
| `AlternativeNodes` | 同名同义：同一逻辑值的不同机器表示，避免重复 box/unbox |


| CompileInfoFacts关键属性 | 说明 |
| --- | --- |
| `loadedProperties_` / `loadedConstantProperties_`（键 `LoadedPropertyKey` = receiver + `PropertyKey`）| 已加载属性表 / 已加载常量属性表, 常量表跨副作用存活（`isConst` 属性），可变表随写失效 |
| `loadedEnvSlots_` / `loadedEnvConstants_`（键 `EnvSlotKey`）+ `EnvSlotAliasMode` | 词法环境槽位（LdLexVar/StLexVar）的加载缓存与别名分析 |
| `availableExpressions_`（hash 索引）+ `effectEpoch_` | CSE 表；计数器驱动惰性失效 |

---

## 2. 新增/修改字节码处理时与 CompileInfoFacts 的交互

新增或修改一种字节码的翻译（GraphBuilder 中的建点 / Lowering / 特化路径）时，必须回答一个核心问题：**这条指令对已有事实是"证明"还是"破坏"**——是建立/收窄事实，还是可能使已缓存的事实不再为真。两者可以同时发生（如 StLexVar：写入既是 `ENV_SLOT_WRITE` 失效，又用 `RecordEnvSlot` 更新槽位缓存）。

### 2.1 交互入口：统一走带 facts 参数的 NewVertex

建 IR 节点一律使用 `GraphBuilder::NewVertex(CompileInfoFacts*, ...)` 重载（`arksteed_graph_builder.cpp`），它根据节点的 `PROPERTIES` 声明自动完成两类交互：

| 节点性质 | NewVertex 自动执行的动作 |
|---|---|
| `CanWrite`（写属性 / 写槽位 / 调用 / Map 迁移…） | `ArkSteedSideEffectClassifier::Classify` 归类 → `MarkPossibleSideEffect(effect, CurrentLoopEffects())`（失效路径） |
| 可 CSE 且不写 | `FindExpression` 查缓存 → miss 时建点并 `AddExpression` 登记（读堆属性的节点按 `effectEpoch_` 版本化，纯计算节点登记为永不过期） |

因此新增 Vertex 时**第一件事是正确声明 `PROPERTIES`（CanRead / CanWrite）**，交互的大半由该声明自动驱动；一般不要绕过该入口手搓节点。仅当像 `NewElementsKindTransition` 那样手动构造节点（不经 `Classify` 静态映射）时，才需要手动调用 `MarkPossibleSideEffect`。

### 2.2 判断准则：更新还是失效

| 这条字节码… | 动作 |
|---|---|
| 纯读 / 纯计算，或发射了守卫、加载完成、转换指令本身就是证明 | **登记 / 更新**事实（Record/Ensure 系列，见 2.3） |
| 可能修改堆属性、环境槽、HClass，或执行未知调用 | **失效**（按 `SideEffectKind` 精确失效或保守全失效，见 2.4） |

### 2.3 场景一：登记 / 更新事实（Record/Ensure 系列）

| 场景 | 要调用的函数 | 说明 |
|---|---|---|
| 静态可知的类型（常量加载、运算结果、Phi 传播等） | `EnsureType` | 与已有类型取交集，只收窄不放大 |
| 索引访问 / 数组操作证明值非空洞 | `RecordNonHole` | 消除后续 hole 检查 |
| HClass 守卫发射后（统一入口 `BuildHClassCheck`） | 候选为 1：`RecordHClass`；多候选：`RecordPossibleHClasses`；已有候选集先 `NarrowPossibleHClasses` 收窄 | 守卫本身即是证明，后续访问免费复用 |
| 特化路径不经守卫直接登记（建空数组、命名属性存储触发 Map 迁移） | `RecordHClass`（stable 判定用 `IsHClassStableForFacts`） | 转换指令本身就是证明 |
| 证明某候选 HClass 已稳定（lazy deopt 场景 `DependOnStableHClass`） | `MarkPossibleHClassStable` | |
| stale HClass 重验证守卫通过 | `MarkPossibleHClassesFresh` | 事实"失而复得" |
| 同一值建立替代机器表示 | `SetAlternative` | 避免 box/unbox 重复 |
| 命名属性 / Elements / Length 加载完成 | `RecordLoadedProperty` | 键 = receiver + PropertyKey |
| 常量属性加载完成（`isConst` 属性、TypedArrayLength） | `RecordLoadedConstantProperty` | 存常量表，跨副作用存活 |
| 词法环境槽位读、新建环境的 parent 槽 | `RecordEnvSlot` / `RecordEnvConstant` | StLexVar 写入同样用 `RecordEnvSlot`：存储即更新 |
| 表达式登记（CSE） | `AddExpression` | 由 `NewVertex` 在建点自动完成，一般不手调 |

### 2.4 场景二：副作用失效

**主路径**：为新 Vertex 在 `ArkSteedSideEffectClassifier`（`arksteed_side_effect_classifier.h`）添加 `Classify` 重载，返回**能证明的最精确**的 `SideEffectKind`；`NewVertex` 检测到 `CanWrite` 后自动调用 `MarkPossibleSideEffect`。失效粒度由 kind 决定：

| SideEffectKind | 代表节点 | 失效动作 |
|---|---|---|
| NO_SIDE_EFFECT | — | 无 |
| FIELD_WRITE | `StoreTaggedFieldVertex` 系列 | NAMED 键精确清同名缓存（`ClearLoadedPropertiesForKey`）；UNKNOWN 键全清（`ClearLoadedProperties`）；计数器(effectEpoch_) +1 |
| ELEMENTS_WRITE | `StoreTaggedElementVertex` 系列 | 仅计数器(effectEpoch_) +1：backing-store 槽写不替换 elements、不改 length，Length/Elements 缓存仍有效 |
| TYPED_ARRAY_WRITE / SAFE_CALL | typed array 写、`EnsurePropertiesCapacityVertex` 等 | 仅计数器(effectEpoch_) +1 |
| ENV_SLOT_WRITE | `StoreEnvSlotVertex` | `ClearAliasedEnvSlotsFor`：MAY_ALIAS 模式下只清"同槽、异 env、值不同"的别名条目；计数器(effectEpoch_) +1 |
| MAP_TRANSITION | `TransitionHClassWithBarrierVertex` | **标 stale 而非删除**（`MarkHClassesStaleForTransition`）+ 清全部属性缓存 + 计数器(effectEpoch_) +1 |
| ELEMENTS_KIND_TRANSITION | `NewElementsKindTransition`（手动构造，不经 `Classify`） | `MarkHClassesStaleForElementsKindTransition`：清迁移源 HClass 的 Elements 缓存并标 stale；计数器(effectEpoch_) +1 |
| UNKNOWN_CALL | `CallVertex` / `CallKnownJSFunctionVertex` 等 | `OnSideEffect()` 最保守全失效：新鲜不稳定 HClass 全标 stale、清属性缓存、清槽位缓存、别名模式归零、计数器(effectEpoch_) +1 |

无法精确归类时会落到模板兜底重载（WARN 日志 + `UNKNOWN_CALL`）——新节点不应长期依赖兜底，必须提供显式 `Classify`。


- **新增SideEffectKind**：如要新增或修改SideEffectKind枚举含义，请务必修改MarkPossibleSideEffect中的switch块并同步此文档
- **手动失效**（少数特判路径）：不经 `NewVertex` 手动构造节点时，直接调 `MarkPossibleSideEffect(effect, CurrentLoopEffects())`；极端场景用 `OnSideEffect()`（等价 UNKNOWN_CALL 全失效）、`ClearAll()`（整表作废）、`ClearEnvSlotsFor`（`LowerPopLexicalEnv` 弹出环境时清该 env 全部槽位缓存）。

### 2.5 注意事项

1. **`PROPERTIES` 声明是交互的开关**：CanRead 影响 CSE（AvailableExpression）的计数器属性effectEpoch如何登记 ，CanWrite 触发副作用失效。声明错误 = 缓存污染或漏失效，属于正确性 bug。
2. **HClass 事实失效是降级 stale 而非删除**（`possibleHClassesAreStale_`）：后续 `HClassInference`（ALLOW_STALE 模式）可读取 stale 集合发射重验证守卫，通过后 `MarkPossibleHClassesFresh` 恢复，多态 IC 的事实可"失而复得"。
3. **常量表跨副作用存活**：`loadedConstantProperties_` / `loadedEnvConstants_` 不随写失效，只有可变表随写失效。把可变属性误登记进常量表会导致错误复用。
4. **CSE 条目靠 `effectEpoch_` 惰性淘汰**：`FindExpression` 发现条目effectEpoch_早于当前effectEpoch_即删除并判 miss；纯指令effectEpoch_为 `EFFECT_EPOCH_FOR_PURE_INSTRUCTIONS`（uint32 max）永不过期；计数器溢出封顶于 `EFFECT_EPOCH_OVERFLOW`，封顶后 `AddExpression` 停止登记。读堆属性的缓存复用还有"须创建于当前块"的约束。
5. **块间流转无需字节码 handler 关心**：进入每个 RPO 块时由 `InitCompileInfoFacts` 自动 `Clone` / `Merge` / `CloneForLoopHeader` / `CloneForOptimisticLoopHeader`；catch 块由框架在可抛指令处通过 `AddCompileInfoFacts` 累积合并——新增可抛指令时确认其建点已走该框架，异常路径不得使用"指令完成后才成立"的事实。
6. **失效与登记可同时发生**：写类指令在失效之后若能确立新事实（如 StLexVar），还要补一次登记调用。


