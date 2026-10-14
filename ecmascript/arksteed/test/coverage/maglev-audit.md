# Аудит V8 Maglev → карта тестирования и применимости аналогов

Задача: `ISSUE-0006` (эпик `ISSUE-0001`, дизайн-решение D5 страницы
`arksteed-jit-coverage-matrix`). Цель: по закреплённому SHA официального V8
зафиксировать область тестирования Maglev, способ выбора и подтверждения tier,
поля каждого класса тестов и оценку применимости аналогов к контрактам ArkSteed.

Отчёт состоит из трёх различимых частей:

1. **статическое изучение** дерева и файлов V8 на закреплённом SHA (§1, §3–§8);
2. **фактически выполненные примеры** на доступной локальной сборке V8 (§9);
3. **неизмеренное** — динамическое покрытие Maglev (не измерялось, не утверждается, §11).

---

## 1. Закреплённая ревизия V8 (SHA)

| Параметр | Значение |
|---|---|
| Репозиторий | `https://chromium.googlesource.com/v8/v8` (зеркало: `https://github.com/v8/v8`) |
| Ветка | `refs/heads/main` |
| **SHA** | **`5cbd82de90a05c7433c0e1c7b0ec71009f7965ad`** |
| Позиция в ветке | `refs/heads/main@{#110107}` |
| Тема коммита | `[maps] Avoid self-prototype transition in Map::GetDerivedMap` |
| Дата коммита (author / committer) | Fri Sep 25 15:59:07 2026 / Fri Sep 25 17:38:55 2026 (UTC) |
| Дата и время фиксации SHA | 2026-09-25T19:27Z (начало аудита) |
| Метод верификации головы | `GET .../v8/+/log/refs/heads/main?format=JSON&n=3` — первый элемент `log` содержит `commit` = закреплённый SHA; родитель `c68946f7099c1a46dd382907e841d77eae7b2659` (позиция 110106) |
| Обоснование выбора | D5: голова основной ветки официального репозитория. Голова **верифицирована** (SHA, дата, позиция в логе), поэтому fallback «ближайший release-тег» не применялся. Перевыбор SHA в течение аудита не допускается; все ссылки ниже указывают на этот SHA и никогда на `main` |

Базовые ссылки отчёта (`<path>` — путь в дереве V8):

- gitiles: `https://chromium.googlesource.com/v8/v8/+/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/<path>`
- зеркало GitHub: `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/<path>`

Ключевые ссылки (все — на закреплённую ревизию; ни одна ссылка отчёта не использует `main`):

- лог ветки с SHA: `https://chromium.googlesource.com/v8/v8/+log/refs/heads/main?format=JSON&n=3` (первый элемент = закреплённый SHA)
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/src/maglev/maglev-compiler.cc`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/src/maglev/maglev-graph-builder.cc`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/src/flags/flag-definitions.h`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/src/runtime/runtime-test.cc`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/tools/testrunner/local/variants.py`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/mjsunit/testcfg.py`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/mjsunit/mjsunit.js`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/mjsunit/mjsunit.status`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/unittests/unittests.status`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/cctest/cctest.status`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/test262/test262.status`
- `https://github.com/v8/v8/tree/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/mjsunit/maglev` (полная энумерация набора)
- `https://github.com/v8/v8/tree/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/mjsunit/maglev/regress` (76 файлов)
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/mjsunit/maglev/00.js`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/mjsunit/maglev/osr-only-interrupt.js`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/mjsunit/maglev/tier-to-ml-to-tf.js`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/mjsunit/maglev/generator-next-lazy-deopt.js`
- `https://github.com/v8/v8/blob/5cbd82de90a05c7433c0e1c7b0ec71009f7965ad/test/mjsunit/maglev/async-function-await-inline.js`

---

## 2. Метод и разделение свидетельств

| Свидетельство | Что сделано | Чего не сделано |
|---|---|---|
| Статическое изучение | Прочитаны листинги каталогов и содержимое файлов на закреплённом SHA (gitiles `?format=JSON`, `raw.githubusercontent.com/<sha>`): `src/maglev/`, `test/mjsunit/maglev/`, `test/unittests/maglev/`, `test/cctest/`, статус-файлы, `tools/testrunner/local/variants.py`, `test/mjsunit/testcfg.py`, `src/runtime/runtime-test.cc`, `src/flags/flag-definitions.h`, `src/maglev/maglev-compiler.cc`, `src/maglev/maglev.cc`, `src/maglev/maglev-graph-builder.cc` | Не читались все ~700 файлов `test/mjsunit/maglev/` построчно; классы тестов выведены из полного инвентаря имён, содержимое сверено на представительной выборке (§4, §5) |
| Выполненные примеры | Локальная сборка V8 через Node.js: `v24.21.0`, `process.versions.v8 = 13.6.233.17-node.53`; 7 сценариев, 4 конфигурации флагов (§9) | Примеры выполнены **не** на закреплённой ревизии: собственной сборки `d8`/V8 на `5cbd82de…` в окружении нет, сеть из сборочного окружения недоступна. Результаты §9 — свидетельство техники наблюдения и семантических инвариантов, но не свидетельство о поведении закреплённой ревизии |
| Измерение покрытия Maglev | Отсутствует | Динамический процент покрытия Maglev не измерялся и в отчёте не утверждается (требование D5) |

Перепроверены (повторно верифицированы на закреплённом SHA) якоря дизайн-страницы
`arksteed-async-verification-and-maglev`, зафиксированные ранее по `main`:

- `is_resumable_function_(IsResumableFunction(compilation_unit_->shared_function_info().kind()))` — инициализация в конструкторе `MaglevGraphBuilder` и её использование при отложенных (`dead_scope_infos_`) состояниях слияния;
- `MaglevGraphBuilder::VisitSwitchOnGeneratorState()`, `VisitSuspendGenerator()`, `VisitResumeGenerator()` — диспетчер входа генератора, сохранение live-регистров и восстановление `context`/регистрового файла;
- **не подтверждены** на SHA: `BuildSuspendGenerator`, `BuildResumeGenerator` (таких символов в `maglev-graph-builder.cc` нет — на закреплённой ревизии suspend/resume реализованы визитёрами выше).

Файл: `src/maglev/maglev-graph-builder.cc` на SHA.

---

## 3. Область аудита: инвентарь

### 3.1 Компилятор

`src/maglev/` на SHA — 74 файла верхнего уровня (плюс `DEPS`, `DIR_METADATA`, `OWNERS` и 7 подкаталогов архитектур), включая:
`maglev.cc/.h`, `maglev-compiler.cc/.h`, `maglev-graph-builder.cc/.h`,
`maglev-inlining.cc/.h`, `maglev-graph-optimizer.cc/.h`,
`maglev-phi-representation-selector.cc/.h`, `maglev-truncation.cc/.h`,
`maglev-loop-peeler.cc/.h`, `maglev-regalloc.cc/.h`, `maglev-code-generator.cc/.h`,
`maglev-concurrent-dispatcher.cc/.h`, `maglev-graph-verifier.h`,
`maglev-deopt-frame-visitor.h`, `maglev-node-type.cc/.h`, `maglev-ir.cc/.h`,
`maglev-known-node-aspects.cc/.h`, `maglev-range*.h`, `maglev-cse.h`,
`maglev-graph-serializer.cc/.h`, `maglev-pipeline-statistics.cc/.h`,
`turbolev-escape-analysis.cc/.h` (Maglev как frontend Turboshaft, «Turbolev»);
подкаталоги архитектур `arm/`, `arm64/`, `loong64/`, `ppc/`, `riscv/`, `s390/`, `x64/`.
Полный листинг: `src/maglev/?format=JSON` на SHA.

### 3.2 Тесты

| Каталог/файл | Инвентарь на SHA | Роль в аудите |
|---|---|---|
| `test/mjsunit/maglev/` | Набор `*.js`/`.mjs` верхнего уровня (`00.js`…`19.js`, `add-number.js`, `osr-*.js`, `typed-array-length-*.js`, `generator-next-*.js`, `regress-*.js`, `maglev-<bugid>.js`, …) + подкаталог `regress/` (**76** файлов: `regress-1359382.js` … `regress-v8-13289.js`) | Основной семантический/regression-набор Maglev |
| `test/unittests/maglev/` | 7 файлов: `hamt-unittest.cc`, `maglev-assembler-unittest.cc`, `maglev-graph-builder-unittest.cc`, `maglev-graph-processor-unittest.cc`, `maglev-test.cc/.h`, `node-type-unittest.cc` | C++ unit-уровень |
| `test/cctest/` | `test/cctest/compiler/` не содержит файлов с `maglev` в имени; Maglev отражается через skips в `cctest.status` | C++ cctest-уровень |
| `test/unittests/unittests.status` | Maglev-зависимые skips в ALWAYS-блоке | Отключения |
| `test/mjsunit/mjsunit.status` | Skips `maglev/*` и большой блок `compiler/*`, `es6/*`, `regress/*` с обоснованием различий Maglev/Turbofan | Отключения |
| `test/test262/test262.status` | **Ни одной** записи со словом `maglev`/`Maglev` (проверено полнотекстовым поиском по файлу) | Семантика test262 через Maglev покрывается вариантом `maglev`, специальных ожиданий нет |
| `tools/testrunner/local/variants.py` | Определения всех вариантов, в т.ч. 11 Maglev-вариантов | Выбор tier |
| `tools/run-tests.py` → `testrunner.standard_runner` | Точка входа раннера; параметризация вариантами | Запуск |
| `test/mjsunit/testcfg.py` | Тест-лоадер mjsunit (`// Flags:` в каждом файле, `mjsunit.js` harness, объединение тестов с одинаковыми флагами, `MISBEHAVING_COMBINED_TESTS_FLAGS`) | Запуск |

Тесты **вне** каталогов Maglev учитываются, когда их через tier исполняют флаги/варианты:
`turbolev/*` (Maglev как frontend Turboshaft; префиксы видны в `mjsunit.status`),
`mjsunit/compiler/*` и часть `mjsunit/regress/*`, `es6/*`, `optimized-*` — в вариантах
`maglev`/`stress_maglev*`/`turbolev*` (см. §5, класс M13).

---

## 4. Выбор Maglev и подтверждение tier (инфраструктура на SHA)

### 4.1 Флаги (`src/flags/flag-definitions.h` на SHA)

- `DEFINE_BOOL(maglev, true, "enable the maglev optimizing compiler")` — под `#ifdef V8_ENABLE_MAGLEV`; при `!ENABLE_MAGLEV` флаг только для чтения и включается `DEFINE_WEAK_IMPLICATION(future, maglev)`.
- `maglev_future` (experimental) → `DEFINE_IMPLICATION(maglev_future, maglev)`.
- `stress_maglev` (по умолчанию `false`): «trigger maglev compilation earlier»; `DEFINE_IMPLICATION(stress_maglev, maglev)`, `DEFINE_WEAK_VALUE_IMPLICATION(stress_maglev, invocation_count_for_maglev, 4)`.
- `optimize_on_next_call_optimizes_to_maglev` (по умолчанию `false`) — переключает цель `%OptimizeFunctionOnNextCall`.
- `maglev_as_top_tier` (по умолчанию `false`) — отключает Turbofan/Turbolev (`DEFINE_NEG_IMPLICATION(maglev_as_top_tier, turbofan/turbolev)`) и агрессивнее настраивает inlining.
- Настройки компилятора с значениями по умолчанию: `maglev_inlining=true`, `maglev_loop_peeling=true`, `maglev_untagged_phis=true`, `maglev_truncation=true`, `maglev_range_analysis=true`, `maglev_cse=true`, `maglev_inline_api_calls=false`, `concurrent_maglev_max_threads=2`, `max_maglev_inlined_bytecode_size=100`, `max_maglev_inline_depth=1`.

### 4.2 Варианты (`tools/testrunner/local/variants.py` на SHA)

Maglev-варианты (флаги приведены дословно):

| Вариант | Флаги |
|---|---|
| `maglev` | `--maglev` |
| `maglev_future` | `--maglev --maglev-future` |
| `maglev_no_turbofan` | `--maglev --no-turbofan --optimize-on-next-call-optimizes-to-maglev` |
| `maglev_no_turbofan_regexp_from_bc` | то же + `--regexp-assemble-from-bytecode` |
| `stress_maglev` | `--maglev --stress-maglev --optimize-on-next-call-optimizes-to-maglev` |
| `stress_maglev_future` | `--maglev --maglev-future --stress-maglev --optimize-on-next-call-optimizes-to-maglev` |
| `stress_maglev_no_turbofan` | `--maglev --no-turbofan --stress-maglev --optimize-on-next-call-optimizes-to-maglev` |
| `stress_maglev_non_eager_inlining` | `--maglev --stress-maglev --maglev-non-eager-inlining --max-maglev-inlined-bytecode-size-small=0 …` |
| `stress_maglev_tracing` | `--maglev --stress-maglev --optimize-on-next-call-optimizes-to-maglev` + `--trace-maglev-*`, `--print-maglev-code`, `--print-maglev-deopt-verbose`, `--print-maglev-graph(s)` |
| `stress_maglev_tests_with_turbofan` | `--turbofan --optimize-maglev-optimizes-to-turbofan` |
| `turbolev`, `turbolev_future`, `stress_turbolev_future` | Maglev как frontend Turboshaft (`--turbolev`, `--turbolev-future`, …) |
| `jit_fuzzing_maglev` | `--fuzzing --jit-fuzzing --optimize-on-next-call-optimizes-to-maglev --no-fail` |
| `assert_types` | `--assert-types --maglev-assert-types` |
| `dumpling_test` / `dumpling_reference` | `--maglev-dumping` / `--no-maglev --no-turbofan …` |

Плюс порождение несовместимостей: `INCOMPATIBLE_FLAGS_PER_VARIANT` запрещает
`--maglev*` вместе с `--jitless`; `INCOMPATIBLE_FLAGS_PER_BUILD_VARIABLE`
содержит правило `"!has_maglev": ["--maglev"]`; `dumpling_reference`/`jitless`
несовместимы с `--maglev` через `kIncompatibleFlagsForNoTurbofan`.

### 4.3 Исполнение теста и подтверждение tier

- Каждый mjsunit-тест содержит собственный заголовок `// Flags: …` (например,
  `test/mjsunit/maglev/00.js`: `// Flags: --allow-natives-syntax --maglev`;
  `osr-only-interrupt.js`: `--allow-natives-syntax --maglev --no-stress-opt --no-baseline-batch-compilation --use-osr --turbofan --concurrent-osr --concurrent-recompilation --osr-from-maglev=4`).
  `test/mjsunit/testcfg.py` парсит флаги из источника и запускает тест отдельным
  процессом `d8` с harness `mjsunit.js`; тесты с одинаковыми флагами могут
  объединяться в один процесс (`TestCombiner`), при этом тесты с `async`,
  `--throws`, `--expose-trigger-failure` и с `Files:` не объединяются, а флаги
  `--trace*`, `--harmony*`, `--experimental*` и т.п. из объединённого прогона
  исключаются (`MISBEHAVING_COMBINED_TESTS_FLAGS`).
- Явный выбор tier в тесте: `%PrepareFunctionForOptimization(f)` (обязательная
  регистрация в `ManualOptimizationTable`, иначе `%Optimize*OnNextCall` завершается
  диагностикой) → прогрев → `%OptimizeMaglevOnNextCall(f)`.
- Подтверждение tier: `%ActiveTierIsMaglev(f)`, `%ActiveTierIsSparkplug(f)`,
  `%ActiveTierIsTurbofan(f)`, `%IsMaglevEnabled()`, `%IsTurbofanEnabled()`,
  `%CurrentFrameIsTurbofan()`; битовая маска `%GetOptimizationStatus` с таблицей
  `V8OptimizationStatus` в `test/mjsunit/mjsunit.js` (`kIsFunction 1<<0`,
  `kNeverOptimize 1<<1`, `kMaybeDeopted 1<<2`, `kOptimized 1<<3`,
  `kMaglevved 1<<4`, `kTurboFanned 1<<5`, `kInterpreted 1<<6`,
  `kTopmostFrameIsMaglev 1<<18`, `kOptimizeOnNextCallOptimizesToMaglev 1<<19`,
  `kOptimizeMaglevOptimizesToTurbofan 1<<20`, `kMarkedForMaglevOptimization 1<<21`)
  и хелперы `assertOptimized` / `assertUnoptimized` / `assertMaglevved` /
  `assertNotMaglevved`.
- Определения интринсиков: `src/runtime/runtime-test.cc` на SHA —
  `Runtime_ActiveTierIsMaglev`, `Runtime_IsMaglevEnabled`,
  `Runtime_OptimizeMaglevOnNextCall` (цель `MAGLEV`, либо `TURBOFAN_JS` при
  `optimize_maglev_optimizes_to_turbofan`), `Runtime_OptimizeFunctionOnNextCall`
  (цель `MAGLEV` при `optimize_on_next_call_optimizes_to_maglev`),
  `Runtime_PrepareFunctionForOptimization`, `Runtime_DeoptimizeFunction`,
  `Runtime_DeoptimizeNow`, `Runtime_OptimizeOsr`, `Runtime_BenchMaglev`,
  `Runtime_ExhaustInterruptBudget`, `Runtime_CurrentFrameIsTurbofan`.
- Негативный контроль встроен в сам механизм: `CanOptimizeFunction` отказывает,
  если `target_kind == CodeKind::MAGLEV && !maglev::IsMaglevEnabled()`, то есть
  под `--no-maglev` (или в сборке без `V8_ENABLE_MAGLEV`) принудительная
  оптимизация в Maglev не происходит; `assertOptimized` явно предупреждает, что
  «Such tests must add --turbofan / --maglev to flags comment», а
  `assertUnoptimized`/`assertNotMaglevved` учитывают `kMaybeDeopted`
  (флаги `--deopt-every-n-times`, GC-stress), `--lite-mode`,
  `--optimize-on-next-call-optimizes-to-maglev` и
  `--optimize-maglev-optimizes-to-turbofan`.
- Порядок фаз компилятора для привязки тестов к механизмам —
  `src/maglev/maglev-compiler.cc` на SHA: graph building → (non-eager) inlining →
  truncation propagation/truncation → LICM → phi untagging → dead-code marking /
  any-use marking → dead node sweeping / value location constraints / max call
  depth / live ranges → register allocation → code assembly (`MaglevCodeGenerator`),
  затем `GenerateCode`: `Generate` → commit dependencies (`dependencies()->Commit`)
  либо bailout `kBailedOutOfDueToDependencyChange`, и отказ
  `kMaglevCodeGenerationFailed`. Точка входа синхронной компиляции —
  `Maglev::Compile` (`src/maglev/maglev.cc`), OSR-offset передаётся параметром.

---

## 5. Классы тестов Maglev

Ниже — каталог классов. Поля заполнены для каждого класса: проверяемая
возможность/инвариант/исторический дефект; выбор Maglev и подтверждение tier;
проверки guards/deopt/GC/исключений/смены типов после прогрева; тип теста;
отключённые тесты и причины; применимость к ArkSteed. Общие механизмы выбора tier
и флагов из §4 в полях не повторяются (ссылка «§4.3»).

### M1 — C++ unit-тесты Maglev

- **Возможность/инвариант:** локальные инварианты внутренних структур: HAMT
  (структура известных узловых аспектов), ассемблер Maglev, построитель графа
  (`maglev-graph-builder-unittest.cc`), процессор графа
  (`maglev-graph-processor-unittest.cc`), типы узлов (`node-type-unittest.cc`),
  общие фикстуры (`maglev-test.cc/.h`).
- **Выбор Maglev/подтверждение tier:** tier не выбирается — тесты работают на
  C++ API компилятора; подтверждение tier не требуется (unit-уровень).
- **Guards/deopt/GC/исключения/смена типов:** не проверяются (домен — внутренние
  инварианты, а не исполнение сгенерированного кода).
- **Тип теста:** unit.
- **Отключённые тесты:** в `test/unittests/unittests.status` нет записей по
  `maglev/*`; связанные записи — `SloppyEqualityTest.SloppyEqualityTest`,
  `RunDeoptTest.DeoptSimple`, `RunDeoptTest.DeoptSimpleInExpr` с причиной
  «Behavioural differences between Maglev and Turbofan when the former is used for
  OptimizeFunctionOnNextCall / Maglev doesn't support compiler::NodeObserver»,
  и «RunDeoptTest checks frame->is_turbofan()».
- **Применимость к ArkSteed:** **применим** как класс (прецедент:
  `ecmascript/arksteed/unittests/arksteed_host_unittest` с
  `ArkSteedRegisterAllocatorTest`, `ArkSteedPipelineStatisticsTest`). Аналог
  обязателен: обязательства групп 4–9, 12 матрицы ArkSteed не закрываются JS-тестами.
- **SHA:** `test/unittests/maglev/?format=JSON`, `test/unittests/unittests.status`,
  `test/unittests/maglev/maglev-graph-builder-unittest.cc`.

### M2 — cctest-уровень и его Maglev-отключения

- **Возможность/инвариант:** C++-тесты VM/компиляторов, часть которых зависит от
  того, каким компилятором исполняется `%OptimizeFunctionOnNextCall` (например,
  `test-js-to-wasm/*`, `test-calls-with-arraylike-or-spread/*`,
  `test-verify-type/*`, `test-api/FastApiCalls`).
- **Выбор Maglev/подтверждение tier:** tier задаётся вариантом прогона
  (`--optimize-next-call-optimizes-to-maglev`-конфигурации), не отдельными флагами теста.
- **Guards/deopt/GC:** проверяются косвенно (fast-API/JS-to-Wasm пути требуют
  Turbofan-специфичных lowering'ов).
- **Тип теста:** unit/cctest.
- **Отключённые тесты и причины (verbatim-обоснования в `cctest.status` на SHA):**
  `test-js-to-wasm/*`, `test-calls-with-arraylike-or-spread/ReduceJSCreateBoundFunction`
  и др. — «Maglev doesn't support compiler::NodeObserver machinery»;
  `test-api/FastApiCalls*` — «Maglev doesn't support FastApi calls»;
  `test-run-unwinding-info/RunUnwindingInfo`, `test-cpu-profiler/FastApiCPUProfiler` —
  неполнота оптимизаций Maglev относительно Turbofan.
- **Применимость к ArkSteed:** **неприменим как аналог целиком** (в ArkSteed нет
  fast-API/JS-to-Wasm tier-взаимодействия и нет `%`-интринсиков, переключающих
  целевой компилятор), но **обязательство ArkSteed остаётся** (T5): C++-таргет
  `arksteed_host_unittest` обязателен по `arksteed-jit-test-infrastructure`, I1.
- **SHA:** `test/cctest/?format=JSON`, `test/cctest/cctest.status`.

### M3 — Числовая семантика и представления

- **Возможность/инвариант:** точность арифметики, преобразований и сравнений при
  тегированных/нетэгированных представлениях: `add-number.js`, `add-smi.js`,
  `negate.js`, `int32-*`, `int32_constants_in_phi.js`, `uint32-conversion-range.js`,
  `constant-folding-float64.js`, `optimize-float64-ieee754.js`,
  `optimize-float64-unary.js`, `float64-conversions.js`,
  `equals-number-boolean.js`, `strict-equals-number-boolean.js`,
  `maglev-truncation.js` (специальные случаи `NaN`, `±Infinity`, `−0`,
  переполнение, деление, побитовые операции; файл содержит явные пары
  «вход → ожидаемое значение», включая «This should not truncate!»).
- **Выбор Maglev/подтверждение tier:** §4.3 (`--allow-natives-syntax --maglev` +
  `%PrepareFunctionForOptimization` + `%OptimizeMaglevOnNextCall`); `maglev-truncation.js`
  дополнительно включает `--maglev-truncation`.
- **Guards/deopt:** набор значений включает случаи, требующие деоптимизации при
  неверной специализации (`NaN`/`Infinity`/границы Int32).
- **GC/исключения:** не проверяются.
- **Тип теста:** семантический (табличный oracle) + regression (см. M14).
- **Отключённые:** нет специфических пропусков именно для этих файлов в
  `mjsunit.status`; вариантно отключаются части `mjsunit/compiler/*`
  (`compiler/number-abs`, `number-floor`, `number-ceil`, `number-min`,
  `number-max`, `number-round`, `number-trunc`, `number-modulus`,
  `number-toboolean`, `number-comparison-truncations`, `shift-for-integer-div`) с
  причинами «Maglev deopts on non-int division / on negative zero for int32, even
  if truncated afterward».
- **Применимость к ArkSteed:** **применим** (обязательство группы 10 матрицы
  ArkSteed). Адаптация: ArkSteed не имеет `%OptimizeMaglevOnNextCall`; используется
  `//! PARAMS` кейса + `ArkTools.arkSteedCompileSync`/`arkSteedIsCompiled`
  (`ecmascript/builtins/builtins_ark_tools.cpp:1582,1601`) и счётчики
  `EXEC_COUNT`/`COMPILE_COUNT` (доступны в `run_arksteed_tests.py`; на закреплённой
  ревизии их не было — инфраструктура свидетельств добавлена поверх неё, см. §8).
- **SHA:** `test/mjsunit/maglev/add-number.js`, `…/maglev-truncation.js`,
  `…/constant-folding-float64.js`, `…/uint32-conversion-range.js`.

### M4 — Guards, карты/формы и смена предположений после прогрева

- **Возможность/инвариант:** корректность HClass/map-guard'ов и IC-подобных
  решений при миграции карт, полиморфизме и смене форм после прогрева:
  `check-maps-migration-invalidation.js`,
  `checkmaps-with-migration-and-deopt-mono{,2}.js`,
  `checkmaps-with-migration-and-deopt-poly{,2,3}.js`, `checkmaps-nearjumps.js`,
  `polymorphic-load.js`, `polymorphic-load-number.js`,
  `polymorphic-load-migration.js`, `polymorphic-store.js`, `poly-store-transition.js`,
  `poly-calls-1.js`, `poly-calls-2.js`, `unstable-map-transition.js`,
  `no-deopt-deprecated-map.js`, `store-oddball-to-double-elements.js`,
  `dict-load.js`, `keyed-access-thin-constant-key.js`,
  `script-context-specialization.js`, `load_mutable_heap_slot_*`, `super-ic.js`,
  `regress-peeled-loop-type-refinement.js`.
- **Выбор Maglev/подтверждение tier:** §4.3; часть тестов подтверждает тихую
  деоптимизацию (`assertUnoptimized`, `%DeoptimizeFunction`).
- **Deopt/смена типов:** имя и структура файлов (`…-and-deopt-*`,
  `no-deopt-deprecated-map.js`) показывают, что класс проверяет и ожидаемый deopt
  при смене карты, и отсутствие избыточного deopt при стабильной карте.
- **GC/исключения:** не проверяются (отдельные классы M8/M9).
- **Тип теста:** семантический + regression.
- **Отключённые:** `regress/regress-map-invalidation-2` (mjsunit) — «This test
  appears to be relying on not adding a map stability check … Wiring this through
  in Maglev would be too messy to be worth it»; `field-type-tracking` — «Maglev
  doesn't specialize much…» (см. M13).
- **Применимость к ArkSteed:** **применим** (обязательства групп 5 и 11 матрицы
  ArkSteed: HClass-проверки, IC, смена предположений после прогрева). Адаптация:
  привязка к `ecmascript/arksteed/arksteed_access_info_factory.cpp`,
  `arksteed_feedback_reader.cpp`, `arksteed_deopt_helper.cpp`; ожидание deopt
  выражается аннотацией `DEOPT_COUNT(_GE)` (доступна в раннере, §8).
- **SHA:** `test/mjsunit/maglev/checkmaps-with-migration-and-deopt-poly3.js`,
  `…/polymorphic-load.js`, `…/unstable-map-transition.js`.

### M5 — Deopt: eager, lazy, deopt-циклы

- **Возможность/инвариант:** корректность деоптимизации и восстановления
  состояния: `lazy-deopt-with-onstack-activation.js`,
  `lazy-deopt-without-onstack-activation.js`, `eager-deopt-in-inline.js`,
  `inline-fresh-parent-deopt-frame.js`, `holey-double-unsilenced-exit.js`,
  `constant-typed-array-load-deopt-detach.js`,
  `constant-typed-array-load-deopt-oob.js`,
  `constant-typed-array-store-deopt-detach.js`,
  `constant-typed-array-store-deopt-oob.js`,
  `typed-array-length-deopt.js`, `typed-array-length-deopt-large.js`,
  `generator-next-eager-deopt-*.js`, `generator-next-lazy-deopt*.js`,
  `regress-int32mod-cse.js`, `regress-yield-star-lazy-deopt.js`.
  Характерный приём (по `generator-next-lazy-deopt.js`): прогрев →
  `%OptimizeMaglevOnNextCall` → вызов, внутри которого `%DeoptimizeFunction(test)`
  → `assertUnoptimized(test)` → проверка результата.
  `typed-array-length-deopt.js` использует `%DeoptimizeNow()` внутри
  оптимизируемой функции.
- **Выбор Maglev/подтверждение tier:** §4.3.
- **Guards/deopt:** основной предмет класса; `%DeoptimizeNow` проверяет именно
  ленивый deopt из исполняющегося кода; `assertUnoptimized` учитывает
  `kMaybeDeopted`, `--deopt-every-n-times`, GC-stress и
  `--optimize-*→maglev/turbofan` (см. §4.3) — то есть V8 различает «deopt не
  произошёл» и «deopt не проверяем в данной конфигурации».
- **GC/исключения:** ленивый deopt из исключения — отдельный класс M8.
- **Тип теста:** regression/семантический + differential (результат после deopt
  сверяется с тем же ожиданием).
- **Отключённые:** `generator-next-*` не отключены; `maglev/18` отключён по другой
  причине (см. M13).
- **Применимость к ArkSteed:** **применим** (обязательство группы 5/9 матрицы
  ArkSteed). Адаптация обязательна: ArkSteed не имеет
  `%DeoptimizeFunction`/`%DeoptimizeNow`/`assertUnoptimized`; нужны отрицательный
  контроль и счётчики `DEOPT_COUNT`/`DEOPT_COUNT_GE` (доступны в раннере, §8).
  Ключевая проектная заметка для T5: семантика V8 «тихий deopt не
  выдаётся за JIT-выполнение» должна быть перенесена на ArkSteed в виде
  `DEOPT_COUNT <pattern> 0` для нормальных путей и явного `DEOPT_COUNT_GE` для
  ожидаемых переходов.
- **SHA:** `test/mjsunit/maglev/generator-next-lazy-deopt.js`,
  `…/typed-array-length-deopt.js`, `…/lazy-deopt-without-onstack-activation.js`.

### M6 — Границы, holes, detached/RA(B|S)AB, типизированные массивы

- **Возможность/инвариант:** отсутствие выхода за границы, корректные проверки
  длины/детекции: `bounds-check-range-refinement.js`,
  `bounds-check-smi-size-fold.js`, `array-at-large-oob.js`,
  `large-typedarray-oob.js`, `typedarray-out-of-bounds.js`,
  `typedarray-bounds-check-elimination.js`, `dataview-almost-oob.js`,
  `typed-array-length-all-kinds.js`, `typed-array-length-rab-gsab.js`,
  `typed-array-length-detached-1|2.js`, `typedarray-resizablearraybuffer.js`,
  `dont-forget-holes.js`, `dont-forget-oob.js`, `holey-double-store-canonicalization.js`,
  `store-constant-field-nan.js`, `typed-array-length-round.js`,
  `typed-array-length-store-as-uint8clamped.js`, `uint8-clamped-store-undefined.js`.
- **Выбор Maglev/подтверждение tier:** §4.3; часть тестов опирается на
  `--typed-array-length-loading` (`typed-array-length-deopt.js`).
- **Guards/deopt:** проверяются отказ и деоптимизация при несоответствии
  длины/детекции (`…-deopt-detach.js`, `…-deopt-oob.js`, `…-deopt-large.js`).
- **GC:** отдельные тесты (`constant-typed-array-store-invalidation.js`).
- **Тип теста:** семантический + regression.
- **Отключённые (mjsunit.status):** `maglev/regress-525732296`,
  `maglev/typed-array-length-index-into-typed-array(-large)` — только при
  `system != linux` и при включённом hardware sandboxing: «Multi-mapped mock
  allocator is only available on Linux. These tests allocate >2 GiB ArrayBuffer
  backing stores».
- **Применимость к ArkSteed:** **применим** (обязательства групп 7, 8, 11
  матрицы ArkSteed). Адаптация: ArkSteed-аналоги уже есть в
  `ecmascript/arksteed/test/typed_arrays/`; проверка границ/детекции должна
  сочетаться с safepoint/write-barrier-свидетельствами
  (`arksteed_safepoint_table.cpp`, `arksteed_write_barrier*.cpp`).
- **SHA:** `test/mjsunit/maglev/bounds-check-range-refinement.js`,
  `…/typedarray-resizablearraybuffer.js`, `…/dont-forget-holes.js`.

### M7 — Исключения и trampoline из оптимизированного кода

- **Возможность/инвариант:** корректный проброс исключения через
  сгенерированный код, включая trampoline из exception-блока:
  `exceptions.js` (проверяет int32/float64 в trampoline; содержит и
  закомментированный случай переполнения с TODO), `holey-float64-exception-phi.js`,
  `typed-array-length-exception-phi.js`, `throw-in-cstr.js`,
  `set-pending-message.js`, `regexp-test-reduction.js`, `regress-osr-try-catch.js`,
  `deep recursion / stack overflow`-случаи (`maglev/regress-421228426`).
- **Выбор Maglev/подтверждение tier:** §4.3; `exceptions.js` использует
  `%ActiveTierIsMaglev` в закомментированном варианте и обычные asserts в активных
  случаях.
- **Guards/deopt/исключения:** класс целиком про исключения; ожидаемая деоптимизация
  при выходе из оптимизированного кода в обработчик.
- **Тип теста:** семантический (проверка значения, полученного из catch).
- **Отключённые:** `maglev/regress-421228426` — `[PASS, SLOW, ['mode == debug', SKIP]]`
  («Requires throwing exception due to stack overflow»).
- **Применимость к ArkSteed:** **применим** (обязательство группы 12 матрицы
  ArkSteed: исключения для каждого порождаемого stub; прецедент —
  `ecmascript/arksteed/test/deopt/lazy/exception/`). Адаптация: ArkSteed
  не поддерживает часть opcode'ов (см. §8), поэтому исключения, порождаемые
  неподдержанными путями, проверяются как fallback, а не как JIT-исполнение.
- **SHA:** `test/mjsunit/maglev/exceptions.js`, `…/holey-float64-exception-phi.js`.

### M8 — GC, escape analysis, tenuring, отслеживание объектов

- **Возможность/инвариант:** корректность GC-корней и объектов, наблюдаемых из
  оптимизированного кода: `new-closure-tenured.js`,
  `escape-analysis-context.js`, `escape-loop-inline-call.js`,
  `alias-materialized-objects.js`, `tagged-element-load-dedup.js`,
  `inline-phi-leak.js`, `context-extension-mutability.js`,
  `extend-properties-backing-store-{1,2}.js`, `string-replace-gc`-класс,
  `regress-1407606.js` (консервативное сканирование стека),
  `test-*` GC-варианты (`--gc-interval`, `gc_stress`).
- **Выбор Maglev/подтверждение tier:** §4.3; GC-стресс достигается вариантами и
  флагами раннера (`gc_stress`, `--stress-*`), а не только `// Flags:` теста.
- **Guards/deopt/GC:** GC-корректность проверяется по сохранению значений и
  идентичности объектов после сборки; в V8 это сочетается с write barrier и
  safepoint-инфраструктурой компилятора.
- **Тип теста:** stress/семантический.
- **Отключённые:** `maglev/regress-1407606` — skip при
  `variant == conservative_stack_scanning`; `maglev/regress-494492520` — skip при
  `gc_fuzzer` (crbug.com/496520250); `es6/big-weakmap`, `wasm/*` под gc-стрессом —
  пропуски по конфигурации.
- **Применимость к ArkSteed:** **применим** (обязательства групп 8 и 11 матрицы
  ArkSteed; GC-свидетельство — `GC_COUNT_GE` + сохранение идентичности объектов).
  Адаптация: ArkSteed не имеет `gc_stress`-варианта; в кейсах используется
  `//! PARAMS --enable-force-gc=true` (в README раннера явно указано, что
  `--enable-force-gc` работает только в debug-сборке, а release-кейс обязан
  создавать естественное давление аллокаций).
- **SHA:** `test/mjsunit/maglev/escape-analysis-context.js`,
  `…/new-closure-tenured.js`, `test/mjsunit/mjsunit.status`.

### M9 — Регистры, spill/reload, большие функции, max call depth

- **Возможность/инвариант:** корректность распределения регистров и работы с
  большим числом значений: `lots-of-registers.js`, `lots-of-args.js`,
  `spill-double.js`, `truncate-int32.js`, `truncate-int32-many-uses.js`,
  `optimize-large-function.js`, `arguments-forwarding.js`,
  `argument-over-under-application.js`, `oop-*`/`extend-properties-backing-store-*`,
  а также проверка корректности live ranges/N-uses (в C++ — `maglev-regalloc.cc`,
  `maglev-regalloc-node-info.h`, `maglev-regalloc-data.h`).
- **Выбор Maglev/подтверждение tier:** §4.3.
- **Guards/deopt/GC/исключения:** проверяются косвенно (корректный результат при
  переполнении регистрового файла и при спилле double-значений).
- **Тип теста:** семантический + unit (C++).
- **Отключённые:** нет специфических.
- **Применимость к ArkSteed:** **применим** (обязательства группы 6 матрицы
  ArkSteed: spill/reload, переиспользование stack slots, параллельные перемещения,
  live ranges). Прямой аналог: `ecmascript/arksteed/unittests/` +
  `ArkSteedRegisterAllocatorTest`; JS-аналоги — кейсы с большим числом регистров
  в `ecmascript/arksteed/test/functions/`, `test/arguments/`,
  `test/spread/`.
- **SHA:** `test/mjsunit/maglev/lots-of-registers.js`, `…/spill-double.js`,
  `src/maglev/maglev-regalloc.cc`.

### M10 — Generators, resumable-функции, async/promise

- **Возможность/инвариант:** поддержка resumable-функций как целевой функции
  компилятора Maglev: `generator-next-*.js` (15+ файлов: eager/lazy deopt, for-of,
  исключения, finally, resume-value, leak-the-hole, cross-realm),
  `resumable.js`, `resumable-loop-context.js`, `nested-continuations.js`,
  `continuation-after-inlined.js`, `context-inverted-generator{,2,3}.js`,
  `async-function-await-inline.js` (Fast/Slow path: Promise, thenable, subclass,
  pending, rejected, await в цикле), `async-function-await-inline-species-protector.js`,
  `async-function-await-inline-then-protector.js`, `promise-then-reduction.js`,
  `promise-catch-reduction.js`, `promise-resolve-inline.js`,
  `resolve-promise-elide-then.js`, `inline-fulfill-promise.js`.
  В компиляторе: `is_resumable_function_` (по `SharedFunctionInfo::kind()`),
  `VisitSwitchOnGeneratorState`, `VisitSuspendGenerator`, `VisitResumeGenerator`
  в `src/maglev/maglev-graph-builder.cc` (проверено на SHA, §2);
  `MaglevCompilationUnit` и `MaglevGraphBuilder` различают OSR- и обычные графы.
- **Выбор Maglev/подтверждение tier:** §4.3; `async-function-await-inline.js`
  использует `%OptimizeFunctionOnNextCall` + `assertOptimized` и проверяет
  микротаск-порядок (`pre`/`post` журнал) — то есть подтверждает и tier, и
  наблюдаемый порядок выполнения.
- **Guards/deopt/исключения:** rejected-resume, thenable, pending-promise,
  генераторные исключения/finally; смена состояния после прогрева (диспетчер
  входа по `SuspendFlags`).
- **GC:** контексты генераторов живут в heap (`resumable-loop-context.js`).
- **Тип теста:** семантический (с асинхронным журналом) + regression.
- **Отключённые:** записей по перечисленным файлам в `mjsunit.status` нет.
- **Применимость к ArkSteed:** **неприменим как аналог обязательства JIT-исполнения**
  на закреплённой ревизии ArkSteed: async/generator-опкоды входят в список
  неподдержанных (`IsArkSteedSupportedOpcode` в `ecmascript/compiler/bytecodes.h`:
  `ASYNCFUNCTIONENTER`, `RESUMEGENERATOR`, `GETRESUMEMODE`, `SUSPENDGENERATOR_V8`,
  `ASYNCFUNCTION{AWAITUNCAUGHT,RESOLVE,REJECT}`, `GETASYNCITERATOR_IMM8`,
  `SETGENERATORSTATE_IMM8`, `CREATEGENERATOROBJ_V8`, `CREATEASYNCGENERATOROBJ_V8`,
  `ASYNCGENERATORRESOLVE_V8_V8_V8`, `ASYNCGENERATORREJECT_V8`). При этом
  **обязательство ArkSteed остаётся** (T5): поддержка await/resume — предмет
  `ISSUE-0002`; здесь проверяется только безопасный fallback для
  неподдержанных opcode (ограничение 9 эпика). Полезны как аналоги *приёма*:
  подтверждение tier на resume (`EXEC_COUNT ≥ 2` на один await), сверка журнала
  эффектов до/после ожидания, микротаски без таймеров.
- **SHA:** `test/mjsunit/maglev/generator-next-lazy-deopt.js`,
  `…/async-function-await-inline.js`, `…/resumable.js`,
  `src/maglev/maglev-graph-builder.cc`.

### M11 — OSR

- **Возможность/инвариант:** вход в исполнение по OSR и переходы tier из OSR:
  `osr-from-ml-to-tf.js`, `osr-from-ml-to-tf-mode-1.js`, `osr-to-tf.js`,
  `osr-multiple-loops.js`, `osr-only-interrupt.js`, `regress-osr-try-catch.js`.
  На закреплённой ревизии ArkSteed путь входа в установленный OSR-код отсутствует
  (`ecmascript/arksteed/test/coverage/osr-chain-audit.md`), поэтому прямые аналоги
  не заменяют обязательство T5 и не могут быть «проверкой JIT-исполнения» на
  стороне ArkSteed.
- **Выбор Maglev/подтверждение tier:** тесты используют явный `%OptimizeOsr()` из
  цикла (`osr-only-interrupt.js` дополнительно `--osr-from-maglev=4`,
  `--concurrent-osr`, `--concurrent-recompilation`, `--use-osr`, `--turbofan` и
  `%CurrentFrameIsTurbofan()` для проверки факта входа в Turbofan-кадр);
  `osr-from-ml-to-tf.js` документирует полный путь Ignition→Sparkplug→Maglev→Turbofan.
- **Guards/deopt/исключения:** `regress-osr-try-catch.js` и
  `osr-multiple-loops.js` проверяют OSR-вход в try/catch и в несколько циклов;
  `maglev/osr-to-tf` отключён в `gc_fuzzer`/`deopt_fuzzer` (см. ниже).
- **Тип теста:** семантический + stress (тайминги tier).
- **Отключённые:** `maglev/osr-to-tf` — skip при `gc_fuzzer or deopt_fuzzer`.
- **Применимость к ArkSteed:** **неприменим как исполнение OSR** (нет пути входа;
  решение D8, отчёт аудита цепочки OSR). Аналог **применим как приём проверки
  fallback**: `--compiler-enable-osr=true` + корректный результат +
  интерпретаторный twin (уже реализовано в
  `ecmascript/arksteed/test/osr/fallback_loop` и `…/fallback_loop_interp_twin`).
- **SHA:** `test/mjsunit/maglev/osr-only-interrupt.js`, `…/osr-multiple-loops.js`.

### M12 — Оптимизации/снижения builtins, IC, массивов, строк

- **Возможность/инвариант:** корректность встроенных снижений и inline-кэшей:
  `array-prototype-at.js`, `array-prototype-at-holey.js`,
  `array-prototype-map-elements-kinds{,-dict}.js`, `array-prototype.slice.js`,
  `array-foreach-with-transition.js`, `array-push-*`,
  `collection-iterator-next{,-inlined}.js`, `map-iterator-creation.js`,
  `function-apply.js`, `reflect-apply.js`, `reflect-get.js`, `reflect-has.js`,
  `object-is.js`, `object-isprototypeof.js`, `get-prototype.js`,
  `get-template-object.js`, `string-*` (at/compare/constructor/indexof/length/
  slice/substring/startsWith/wrapper-add), `super-ic.js`, `simple-inlining.js`,
  `omit-default-ctors*.js`, `inner-function.js`, `load-named.js`, `lda-global*.js`,
  `literals.js`, `number-to-string.js`, `test-undetectable-protector.js`,
  `tier-to-ml-to-tf.js`.
- **Выбор Maglev/подтверждение tier:** §4.3.
- **Guards/deopt/GC/исключения:** `throw-in-cstr.js`, `set-pending-message.js`;
  деоптимизация при изменении элементов/строк/защитников.
- **Тип теста:** семантический + regression.
- **Отключённые (причины verbatim в `mjsunit.status`):** «Maglev doesn't lower
  CallWithSpread» (`compiler/regress-385386138`), «Maglev doesn't unroll constant
  arrays in apply» (`compiler/function-apply-constant-array*`), «Maglev doesn't
  inline String.prototype.includes» (`optimized-string-includes`), «Maglev
  doesn't optimize string wrapper addition» (`string-wrapper-add-1|2`), «Maglev
  doesn't specialize much when it comes to BigInt operations» (`compiler/bigint-*`),
  «Maglev doesn't inline some Dataview accessors» (`compiler/dataview-*`),
  «Maglev doesn't silent holey NaNs if it hasn't seen any» (`getters-on-elements`),
  «Maglev deopts more aggressively on insufficient feedback»
  (`array-literal-transitions`), `const-tracking-let-*`, `optimized-filter`,
  `optimized-map`, `field-type-tracking`, `compiler/fast-api-*`,
  `regress/regress-map-invalidation-2`.
- **Применимость к ArkSteed:** **применим частично**: обязательство группы 12
  матрицы ArkSteed (матрицы stub: каждый `CallCommonStub`/`CallRuntime`-stub,
  порождаемый компилятором, обязан иметь покрытие нормального пути, guard/deopt и
  исключений) остаётся полностью. Аналог V8 «inline-редукция builtin» применим
  только там, где ArkSteed реально имеет соответствующее снижение в
  `ecmascript/arksteed/arksteed_graph_builder.cpp` и `arksteed_codegen.cpp`;
  перечень поддерживаемых снижений на закреплённой ревизии не документирован и
  должен быть получен аудитом ArkSteed (T3).
- **SHA:** `test/mjsunit/maglev/array-prototype-at.js`, `…/collection-iterator-next.js`,
  `…/tier-to-ml-to-tf.js`.

### M13 — Конкурентная компиляция, стресс/фаззинг-варианты, Turbolev

- **Возможность/инвариант:** корректность при раннем и конкурентном tiering, при
  включённых экспериментальных проходах и при использовании Maglev как frontend
  Turboshaft: `maglev-assert-types.js`, `maglev-inlining-dead.js`,
  `maglev-loop-3-preds.js`, `unsused-loop-phi.js` (`unused-loop-phi.js`),
  `loop-phi-shrinking.js`, `loop-preheader.js`,
  `maglev-343069827.js`, `maglev-508811474.js`, `351177445.js`; варианты
  `stress_maglev*`, `maglev_future`, `jit_fuzzing_maglev`, `assert_types`,
  `turbolev*`, `dumpling_test/reference`; C++ — `maglev-concurrent-dispatcher.cc`,
  `maglev-graph-serializer.cc/.h`, `turbolev-escape-analysis.cc`.
- **Выбор/подтверждение:** вариант задаёт флаги (§4.2); тесты с
  `--optimize-on-next-call-optimizes-to-maglev` проверяют tier по
  `kOptimizeOnNextCallOptimizesToMaglev` (см. `assertUnoptimized` в §4.3).
- **Guards/deopt/GC/исключения:** проверяются косвенно — корректность при раннем
  tiering и при конкурентной компиляции (гонка установки кода и деоптимизации),
  корректность сериализации/верификации графа (unit-уровень); отдельные
  GC/исключения-сценарии этого класса в V8 не выделены.
- **Тип теста:** stress/generated (фаззинг варианта), unit (serializer/verifier).
- **Отключённые тесты и причины:**
  - `maglev/18` — `[SKIP]`, «Needs deterministic test helpers for concurrent maglev
    tiering. TODO(jgruber,v8:7700): Implement ASAP» — **прямая параллель к
    инфраструктуре ArkSteed**: без детерминированных хелперов наблюдения тест
    конкурентного tiering нестабилен;
  - `maglev/regress-431665299` — `[SKIP]` в `mode == debug`; в ряде
    arch/simulator-наборов помечен `[PASS, SLOW]`;
  - `maglev/regress-464612781`, `maglev/regress-500774816` — `[PASS, SLOW]`;
  - `maglev/regress-432847895` — skip в «очень медленных» simulator-наборах;
  - `maglev/regress-498816446-3`, `maglev/regress-521604719` — конфигурационные
    skip-записи (lower-limits/pointer-compression/wasm-условия);
  - `sandbox/regress/regress-502997649`, `turbolev/*` и большой блок
    `compiler/*`/`es6/*` — см. M12 и §6.
- **Применимость к ArkSteed:** **частично применим**. Аналог «конкурентная
  компиляция» применим (ArkSteed компилирует код асинхронно/фоново и имеет
  `arksteed_task.cpp`, `arksteed_compiler.cpp:214-215`), но требует
  детерминированных хелперов — ровно тот пробел, который V8 фиксирует в
  `maglev/18`. Аналоги `turbolev*`/`maglev_future`/`jit_fuzzing_maglev`
  **неприменимы** (в ArkSteed нет ни Turboshaft-frontend, ни fuzzing-варианта);
  обязательства ArkSteed остаются и покрываются через T5 (стресс-сценарии с
  фиксированным seed/бюджетом — по матрице ArkSteed).
- **SHA:** `tools/testrunner/local/variants.py`,
  `test/mjsunit/maglev/maglev-assert-types.js`, `test/mjsunit/mjsunit.status`,
  `src/maglev/maglev-concurrent-dispatcher.cc`.

### M14 — Regression-набор Maglev

- **Возможность/инвариант/исторический дефект:** каждый файл — минимизированный
  воспроизводитель конкретного дефекта. Примеры (содержимое сверено на SHA):
  `regress-1403324.js` — взаимодействие оптимизированного доступа к свойству,
  `arguments[42]`/inline-closure и добавленного позже getter'а на
  `Object.prototype`; ожидание `NaN`; старт с `%PrepareFunctionForOptimization`/
  `%OptimizeMaglevOnNextCall`. Прочие файлы именуются по id бага
  (`regress-<crbug>`, `regress-crbug-*`, `regress-v8-*`, `maglev-<bugid>`), плюс
  подкаталог `test/mjsunit/maglev/regress/` из 76 файлов.
- **Выбор Maglev/подтверждение tier:** §4.3; часто используется сочетание
  «warmup → форсированная оптимизация → действие, инвалидирующее предположение».
- **Guards/deopt/GC/исключения:** по природе класса — смена карт/защитников,
  деоптимизация, реже GC/исключения.
- **Тип теста:** regression.
- **Отключённые:** см. §6 (`maglev/regress-*` записи в `mjsunit.status`).
- **Применимость к ArkSteed:** **применим как организационный аналог**: каждый
  найденный дефект ArkSteed оформляется отдельной задачей с воспроизводителем
  (критерий 8 эпика, решение `known-defect` матрицы ArkSteed). Значения id багов V8
  неприменимы; сохраняется только структура «минимизированный воспроизводитель +
  фиксированный expected_output + привязка к задаче исправления».
- **SHA:** `test/mjsunit/maglev/regress-1403324.js`,
  `test/mjsunit/maglev/regress/?format=JSON`, `test/mjsunit/maglev/regress-421228426.js`.

---

## 6. Отключённые/условно-отключённые тесты и причины (сводно)

Все записи ниже прочитаны в `test/mjsunit/mjsunit.status`, `cctest.status`,
`unittests.status` на закреплённом SHA.

**Безусловные/ALWAYS-записи (внутри первого блока `[ALWAYS, {…}]`):**

| Запись | Значение | Причина в файле |
|---|---|---|
| `maglev/18` | `[SKIP]` | «Needs deterministic test helpers for concurrent maglev tiering. TODO(jgruber,v8:7700): Implement ASAP.» |
| `maglev/regress-421228426` | `[PASS, SLOW, ['mode == debug', SKIP]]` | «Requires throwing exception due to stack overflow» |
| `maglev/regress-464612781`, `maglev/regress-500774816` | `[PASS, SLOW]` | «Slow tests» |
| блок `const-tracking-let-*`, `getters-on-elements`, `array-literal-transitions`, `optimized-string-includes`, `string-wrapper-add-1|2`, `compiler/bigint-*`, `compiler/dataview-*`, `compiler/fast-api-*`, `compiler/number-*`, `optimized-filter`, `optimized-map`, `field-type-tracking`, `additive-safe-int-feedback`, `regress/regress-map-invalidation-2`, `es6/iterator-eager-deopt`, `es6/array-iterator-turbo`, … | `[SKIP]` | Различия lowering'ов/frontend'а Maglev и Turbofan: «Because of frontend lowerings being different in Maglev and Turbofan …», «Maglev doesn't inline …», «Maglev deopts more aggressively on insufficient feedback», «Maglev doesn't support additive safe ints», «Maglev is still missing some optimizations compared to Turbofan» и т.п. |
| `SloppyEqualityTest.SloppyEqualityTest`, `RunDeoptTest.DeoptSimple`, `RunDeoptTest.DeoptSimpleInExpr` (`unittests.status`) | `[SKIP]` | «Behavioural differences between Maglev and Turbofan when the former is used for OptimizeFunctionOnNextCall»; «Maglev doesn't support compiler::NodeObserver machinery»; «RunDeoptTest checks frame->is_turbofan()» |
| `test-js-to-wasm/*`, `test-calls-with-arraylike-or-spread/*`, `test-verify-type/*`, `test-api/FastApiCalls*`, `test-run-unwinding-info/RunUnwindingInfo` (`cctest.status`) | `[SKIP]` | «Maglev doesn't support compiler::NodeObserver machinery» / «Maglev doesn't support FastApi calls» |

**Условные записи (примеры, условия дословно):**

| Запись | Условие |
|---|---|
| `maglev/regress-431665299` | `['mode == debug', SKIP]`; также `[PASS, SLOW]` в части arch/simulator-наборов |
| `maglev/regress-355256380` | «not has_webassembly or (variant == jitless and not has_wasm_interpreter)» + условия snapshot-варианта |
| `maglev/regress-525732296`, `maglev/typed-array-length-index-into-typed-array(-large)` | `system != linux` и hardware sandbox support (мок-аллокатор >2 GiB) |
| `maglev/osr-to-tf` | `gc_fuzzer or deopt_fuzzer` |
| `maglev/regress-494492520` | `gc_fuzzer` (https://crbug.com/496520250) |
| `maglev/regress-1407606` | `variant == conservative_stack_scanning` |
| `maglev/regress-432847895` | «очень медленные» simulator-наборы (arm/arm64/loong64/mips64el и `nooptimization`-like условия) |
| `maglev/regress-498816446-3`, `maglev/regress-521604719` | конфигурационные условия соответствующего блока (Wasm-фичи/pointer compression/lower limits) |

**Отдельно:** в `test/test262/test262.status` на SHA нет ни одной записи про
Maglev — семантика test262 покрывается вариантом `maglev` без специальных
ожиданий/исключений.

**Наблюдение для T5.** Часть «пропусков» выше — не дефекты, а зафиксированные
различия возможностей tiers (например, Maglev не поддерживает fast API calls).
Формулировка «аналог неприменим» для таких пунктов не отменяет обязательство
ArkSteed: у ArkSteed собственная граница поддержки задана
`IsArkSteedSupportedOpcode` (§8), и каждое обязательство должно закрываться
ArkSteed-специфичной проверкой.

---

## 7. Карта «механизм → исходник V8@SHA → тест → соответствие ArkSteed»

Группы обязательств — по странице `arksteed-jit-coverage-matrix`. Столбец
«применимость» оценивает применимость **аналога V8**; обязательство ArkSteed
остаётся в любом случае (фиксирует T5/ISSUE-0007).

| # | Обязательство ArkSteed | Механизм V8 Maglev (исходник на SHA) | Аналог-тесты Maglev | Применимость аналога к ArkSteed |
|---|---|---|---|---|
| 1 | Вход и решение о компиляции; OSR-гейт; fallback неподдерживаемых opcode | `src/maglev/maglev.cc` (`Maglev::Compile`), `src/maglev/maglev-compiler.cc` (`Compile` / `GenerateCode`, `BailoutReason::kMaglevCodeGenerationFailed`, `kBailedOutOfDueToDependencyChange`), `src/maglev/maglev-compilation-info.*`, флаги `maglev`/`maglev_future`/`stress_maglev` (`src/flags/flag-definitions.h`), гейты в `src/runtime/runtime-test.cc` (`CanOptimizeFunction`, `maglev::IsMaglevEnabled`) | варианты `maglev*`/`stress_maglev*` (§4.2), `tier-to-ml-to-tf.js`, `%IsMaglevEnabled()`; fallback-сценарии — `osr-only-interrupt.js`, `regress-osr-try-catch.js` | **Требует адаптации.** ArkSteed-гейт иной: `IsArkSteedSupportedOpcode` (bytecodes.h) + `CompileDecision`; отказ компиляции проверяется аннотацией `COMPILE_COUNT <метод> 0` вместе с `ArkTools.arkSteedIsCompiled`. OSR-часть — `not-applicable` (D8, `osr-chain-audit.md`), fallback — `covered-new` (`test/osr/fallback_loop*`) |
| 2 | Bytecode preprocessing и feedback | `maglev-graph-builder.cc` (обработка `ProcessedFeedback`, `MaglevCompilationUnit`), `maglev-known-node-aspects.*`, `src/compiler/processed-feedback.*` | M4/M12-наборы (`array-*`, `polymorphic-*`, `keyed-access-thin-constant-key.js`) | **Применим.** Прямые аналоги ArkSteed: `arksteed_bytecode_preprocessor.cpp`, `arksteed_bytecode_analysis.cpp`, `arksteed_feedback_reader.cpp`, `arksteed_access_info_factory.cpp`, `arksteed_processed_feedback.h`; тестовые входы переносятся, oracle — спецификация |
| 3 | Построение CFG/SSA, Phi, циклы, недостижимый код | `maglev-graph-builder.cc`, `maglev-interpreter-frame-state.*`, `maglev-graph.h`, `maglev-graph-verifier.h` | `loop-phi-shrinking.js`, `unused-loop-phi.js`, `loop-preheader.js`, `maglev-loop-3-preds.js`, `phi-untagging-*.js`, C++ `maglev-graph-builder-unittest.cc` | **Применим.** ArkSteed-аналоги: `arksteed_graph_builder.cpp`, `arksteed_graph_verifier.h`, `arksteed_register_merge_state.h`; прецедент проверки — `//! HAS/HAS_NOT/COUNT*`-аннотации раннера |
| 4 | Проходы и упрощения | `maglev-compiler.cc` (truncation, LICM, phi untagging, dead-code, any-use), `maglev-truncation.*`, `maglev-graph-optimizer.*`, `maglev-cse.h`, `maglev-range*.h` | `maglev-truncation.js`, `phi-untagging-*`, `constant-fold-uint-compared-to-zero.js`, `constant-folding-float64.js`, `regress-int32mod-cse.js` | **Применим.** ArkSteed: `arksteed_constant_folding.cpp`, `arksteed_loop_effects.cpp`, `arksteed_side_effect_classifier.h`; unit-проверки инвариантов — в `arksteed_host_unittest` (T5) |
| 5 | Guards и deoptimization (eager/lazy, insufficient-profile) | `maglev-graph-builder.cc` (deopt-фреймы, `lazy_deopt_result_location_scope_`, speculation), `maglev-deopt-frame-visitor.h`, `src/deoptimizer/*` | весь M5 + `checkmaps-with-migration-and-deopt-*.js`, `typed-array-length-deopt*.js`, `constant-typed-array-load-deopt-*.js`, `generator-next-*-deopt*.js` | **Требует адаптации.** ArkSteed: `arksteed_deopt_helper.cpp`, `arksteed_deopt_abi.h`, `arksteed_pgo_dependency_recorder.h`; нужны `DEOPT_COUNT(_GE)` и отрицательный контроль (счётчики доступны в раннере). Урок V8: различать «deopt не произошёл» и «deopt не проверяем в конфигурации» (`kMaybeDeopted`, GC-stress) |
| 6 | Liveness и regalloc | `maglev-regalloc.cc`, `maglev-regalloc-node-info.h`, `maglev-regalloc-data.h`, `maglev-register-frame-array.h`, `maglev-pre-regalloc-codegen-processors.h` | `lots-of-registers.js`, `spill-double.js`, `truncate-int32-many-uses.js`, C++ regalloc-unittest'ы | **Применим.** ArkSteed: `arksteed_regalloc.cpp`, `arksteed_regalloc_vertex_info.h`, `arksteed_register_merge_state.h`, `ArkSteedRegisterAllocatorTest` |
| 7 | Codegen, calling convention, runtime/stub calls | `maglev-code-generator.cc/.h`, `maglev-assembler*`, `arch/x64/*`, `maglev-code-gen-state*` | `lots-of-args.js`, `argument-over-under-application.js`, `call-js-runtime.js`, `call-runtime-for-pair.js`, `call-with-spread-*.js`, `throw-in-cstr.js` | **Применим.** ArkSteed: `arksteed_codegen.cpp`, `arch/x64/arksteed_assembler_x64.cpp`, `arksteed_deferred_code.h`, `arksteed_assembler*` |
| 8 | Safepoints и GC-метаданные, write barriers | `maglev-code-generator.cc` (safepoint/deopt tables), `maglev-deopt-frame-visitor.h`, `maglev-pretenure-store-values`, tests `escape-analysis-context.js`, `new-closure-tenured.js` | M8 (GC), M6 (detach/RAB), `regress-1407606.js` | **Применим.** ArkSteed: `arksteed_safepoint_table.cpp`, `arksteed_write_barrier.cpp`, `arksteed_write_barrier_value_kind_pass.cpp`; GC-свидетельство — `GC_COUNT_GE` |
| 9 | Переходы runtime: deopt, инвалидация, повторная компиляция, интерпретатор↔JIT | `maglev-concurrent-dispatcher.cc/.h`, `maglev.cc`, tiering-интринсики `src/runtime/runtime-test.cc` (`%DeoptimizeFunction`, `%OptimizeOsr`, `%ExhaustInterruptBudget`, `%ActiveTierIs*`) | M5, M11, M13 (+ `tier-to-ml-to-tf.js`) | **Требует адаптации.** Аннотации `COMPILE_COUNT(_GE)`, `DEOPT_COUNT(_GE)` (доступны в раннере) — прямой перенос смысла V8-свидетельств; интерпретатор→JIT через OSR — `not-applicable` (D8) |
| 10 | Семантика чисел | `maglev-ir.cc` (арифметика/сравнения), `maglev-truncation.*`, `maglev-range*.h` | M3 целиком (`maglev-truncation.js` как эталон табличного oracle) | **Применим.** ArkSteed-аналоги: `test/arithmetic/`, `test/conversions/`, `test/typeof/`; oracle — ECMA-262 и вручную вычисленные константы (D4) |
| 11 | Объекты, IC, массивы, строки, замыкания | `maglev-graph-builder.cc` (property load/store, `TryBuildPropertyLoad`, IC-lowering), `maglev-map-inference.h`, `maglev-ic-inlining.*` (внутри builder), `maglev-node-type.*` | M4, M12 (`array-*`, `polymorphic-*`, `super-ic.js`, `string-*`, `object-is*.js`) | **Применим.** ArkSteed: `test/objects/`, `test/ic/`, `test/arrays/`, `test/typed_arrays/`, `test/strings/`, `test/inline/`, `test/prototype_chain`-кейсы существующего набора |
| 12 | Матрицы stub (`CallCommonStub`/`CallRuntime`) | `maglev-code-generator.cc` (вызовы runtime/builtins), `maglev-ir.h/cc` (Call/`CallBuiltin`-узлы) | M7/M12 + `call-js-runtime.js`, `call-runtime-for-pair.js`, `collection-iterator-next*.js`, `promise-*-reduction.js` | **Применим.** Обязательство ArkSteed остаётся полностью; прецедент покрытия — `ecmascript/arksteed/test/deopt/lazy/call_common_stub/*` и `call_runtime/*` |
| Доп. | Turbolev (Maglev как frontend Turboshaft) | `src/maglev/turbolev-escape-analysis.cc`, `maglev-graph-serializer.*`, флаги `turbolev*`, `maglev_as_top_tier` | `turbolev/*`-записи в `mjsunit.status`, варианты `turbolev*` | **Неприменим** (в ArkSteed нет Turboshaft-пайплайна); обязательство ArkSteed не возникает |
| Доп. | Конкурентная компиляция и tiering-стабильность | `maglev-concurrent-dispatcher.*`, `maglev-pipeline-statistics.*`, `%ExhaustInterruptBudget` | `maglev/18` (SKIP), `tier-to-ml-to-tf.js`, `stress_maglev*`-варианты | **Требует адаптации** (нужны детерминированные хелперы; V8 сам фиксирует этот пробел) |

---

## 8. Границы ArkSteed, существенные для применимости

Зафиксированы по закреплённой ревизии ArkSteed
(`e31cc9a31d421761f1f05df49d39e5065a22e194`), локальные файлы рабочей копии:

- **Допуск байткода.** `IsArkSteedSupportedOpcode` (`ecmascript/compiler/bytecodes.h`)
  возвращает `false` для: jump-опкодов сравнения с null/undefined; jump-опкодов
  сравнения двух значений; async/generator-семейства; `DEBUGGER`; sendable
  class/module runtime-опкодов. (В дизайн-документе эпика для этой проверки
  использовалось имя `IsArkSteedBytecodeSupported` — на закреплённой ревизии
  символ называется `IsArkSteedSupportedOpcode`; для T5 брать фактическое имя.)
- **Наблюдение JIT-исполнения.** `--compiler-arksteed-print-method-name`
  (опция существует: `ecmascript/js_runtime_options.cpp:68,267-268`; чтение —
  `ecmascript/arksteed/arksteed_compiler.cpp:107`), runtime-stub `PrintMethodName`
  (`ecmascript/stubs/runtime_stubs.cpp:5351`), `ArkTools.arkSteedCompileSync` /
  `ArkTools.arkSteedIsCompiled` (`ecmascript/builtins/builtins_ark_tools.cpp:1582,1601`).
  В дереве доставки stub печатает `>>> Running method: <record>.<method>` в
  compiler-компонент и вызывается из входного блока обычных (не-OSR) компиляций
  (`ecmascript/arksteed/arksteed_graph_builder.cpp`, `CallRuntime PrintMethodName`).
- **Аннотации раннера.** На закреплённой ревизии `e31cc9a31d…` набор ограничен
  `PARAMS`, `COMPILE_MODE`, `BYTECODE_COUNT`, `CODEGEN_COUNT`, `HAS`, `HAS_NOT`,
  `COUNT`, `COUNT_GE`, `COUNT_LE` (`ecmascript/arksteed/test/README.md`); аннотаций
  свидетельств `EXEC_COUNT(_GE)`/`COMPILE_COUNT(_GE)`/`DEOPT_COUNT(_GE)`/`GC_COUNT_GE`
  в `run_arksteed_tests.py` там нет (проверено поиском). Инфраструктура свидетельств
  добавлена поверх закреплённой ревизии (коммит ветки доставки `c7c136bf12`, «Add
  ArkSteed JIT execution marker and runner evidence annotations») и присутствует в
  дереве доставки: семейство `EvidenceConstraint` в `run_arksteed_tests.py`, раздел
  «Evidence annotations» в `ecmascript/arksteed/test/README.md`, маркер выполнения в
  `ecmascript/arksteed/arksteed_graph_builder.cpp` и логирование stub'а в
  `ecmascript/stubs/runtime_stubs.cpp`. Эти аннотации обязательны для свидетельств
  по `arksteed-jit-test-evidence`; в §5/§7 они упоминаются как доступные, а не
  запланированные.
- **C++ unit-таргет:** `ecmascript/arksteed/unittests/BUILD.gn` группа
  `arksteed_host_unittest` (файлы `input_result_alias_test.cpp`,
  `pipeline_statistics_test.cpp`).
- **OSR:** путь входа в установленный OSR-код отсутствует
  (`ecmascript/arksteed/test/coverage/osr-chain-audit.md`); безопасный fallback
  уже покрыт (`ecmascript/arksteed/test/osr/fallback_loop`,
  `…/fallback_loop_interp_twin`).

---

## 9. Фактически выполненные примеры (динамические, вне закреплённой ревизии)

**Окружение:** `node v24.21.0`, `process.versions.v8 = 13.6.233.17-node.53`,
Linux x86_64. **Это не закреплённая ревизия V8**: собственной сборки `d8`
на `5cbd82de…` в окружении нет. Результаты ниже фиксируются как отдельный класс
свидетельств — они демонстрируют технику (выбор tier, подтверждение tier,
отрицательный контроль, differential) и не переносятся автоматически на
закреплённую ревизию.

**Скрипт** `/tmp/maglev-probe.js` (написан по мотивам
`test/mjsunit/maglev/00.js`, `tier-to-ml-to-tf.js`,
`generator-next-lazy-deopt.js`, `osr-only-interrupt.js`; печатает строки
`SEMANTIC:` — результаты уровня спецификации и `TIER:` — факты о tier).
Проверки: явный выбор tier и подтверждение; deopt после прогрева; смена
формы/карты после прогрева; GC между оптимизированными вызовами; исключение в
оптимизированном коде; нормальный tiering без форсирования; OSR в цикл.

**Команды и результаты** (4 конфигурации; `SEMANTIC` идентичны во всех):

```bash
# A: Maglev (по умолчанию включён), --use-osr
node --allow-natives-syntax --expose-gc --use-osr /tmp/maglev-probe.js
# B: негативный контроль tier-свидетельства
node --allow-natives-syntax --expose-gc --use-osr --no-maglev /tmp/maglev-probe.js
# C: интерпретаторный twin
node --allow-natives-syntax --expose-gc --use-osr --no-maglev --no-turbofan --no-sparkplug /tmp/maglev-probe.js
# D: стресс-конфигурация
node --allow-natives-syntax --expose-gc --use-osr --stress-maglev /tmp/maglev-probe.js
```

| Конфигурация | Факты о tier (`TIER:`) |
|---|---|
| A (Maglev) | `maglev.enabled=true`; `add.active_maglev=true`; `mul.active_maglev.before_deopt=true`; `readV.active_maglev.after_gc=true`; `loopFn.reached_maglev_by_tiering=true`; `mul.active_maglev.after_deopt=false`; `loadX.active_maglev.after_new_shape=false`; `osrLoop.opt_status=129` |
| B (`--no-maglev`) | `maglev.enabled=false`; **все** `*.active_maglev=false`; `loopFn.reached_maglev_by_tiering=false`; `osrLoop.opt_status=641` |
| C (интерпретатор) | `maglev.enabled=false`, `turbofan.enabled=false`, `sparkplug.enabled=false`; все `*.active_maglev=false`; `osrLoop.opt_status=131` |
| D (`--stress-maglev`) | как A (`add.active_maglev=true`, `loopFn.reached_maglev_by_tiering=true`) |

Наблюдения:

1. **Differential.** Строка `SEMANTIC:` побайтово совпадает во всех четырёх
   конфигурациях, например:
   `add(5,6)=11; loadX(o2) new-shape=2; mayThrow(-1) opt-throw=caught:neg; mul(8,9) deopted=72; osrLoop(1000000)=499999500000; osrLoop.expected=499999500000; readV(holder) after-gc=42`.
   Совпадение режимов не является достаточным oracle (D4), но подтверждает, что
   перенос тестового приёма не ломает семантику и что интерпретаторный twin
   пригоден как дифференциальная проверка.
2. **Подтверждение tier.** `%ActiveTierIsMaglev` истинно ровно в Maglev-конфигурациях
   (A, D) на трёх разных функциях (функция после форсированной оптимизации,
   функция после GC, функция, дошедшая до Maglev нормальным tiering'ом).
3. **Отрицательный контроль.** Под `--no-maglev` (B) все свидетельства о Maglev
   ломаются (`maglev.enabled=false`, все `active_maglev=false`,
   `reached_maglev_by_tiering=false`), а результаты уровня спецификации не
   меняются — то есть счётчик/маркер tier чувствителен к отключению компилятора,
   что и требуется от негативного контроля свидетельства.
4. **Deopt и смена формы.** После `%DeoptimizeFunction` tier возвращается к
   не-Maglev, результат остаётся корректным; после появления новой формы объекта
   (`{x,y}`) функция перестаёт быть Maglev-оптимизированной, значения остаются
   корректными.
5. **OSR.** Отдельный прогон `node --allow-natives-syntax --use-osr --trace-osr t9.js`
   (вход через `%OptimizeOsr()` из первой итерации цикла) даёт в трассе
   `[OSR - entry. function: osrLoop, osr offset: 36]` (5 раз),
   `[OSR - compilation started. … mode: ConcurrencyMode::kConcurrent]` и
   `[OSR - compilation finished. function: osrLoop, osr offset: 36]`; результат
   `osrLoop(1000000)=499999500000` совпадает с вручную вычисленным
   `(999999*1000000)/2`. Это подтверждает, что исходный приём V8-теста
   (`%OptimizeOsr` из цикла + сверка результата) воспроизводим.
6. **Ограничение.** В конфигурации A `%GetOptimizationStatus` вернул 49 для
   Maglev-функции и 129 после deopt, в B — 129/641, в C — 131; **битовые позиции
   в локальной сборке не сопоставлялись** с таблицей `V8OptimizationStatus` из
   `test/mjsunit/mjsunit.js` закреплённой ревизии, поэтому в примерах tier
   подтверждается исключительно `%ActiveTierIsMaglev`, а не битовой маской.
7. **Ошибка, зафиксированная как находка при выполнении:** вызов
   `%OptimizeOsr(functionObject)` аварийно завершает процесс
   (`Check failed: v8_flags.fuzzing`), поскольку первый аргумент
   `Runtime_OptimizeOsr` — глубина стека (`IsSmi`), а не функция. Корректная форма
   — `%OptimizeOsr()` изнутри оптимизируемой функции. Это согласуется с
   `src/runtime/runtime-test.cc` на SHA; в отчёте фиксируется как воспроизводимая
   деталь использования интринсика.

---

## 10. Что из результатов применимо к ArkSteed (сводка для T5)

1. **Переносимые приёмы:** явная подготовка и форсированная оптимизация
   (`%PrepareFunctionForOptimization` + `%OptimizeMaglevOnNextCall` →
   `ArkTools.arkSteedCompileSync` + per-case `PARAMS`); подтверждение tier
   (`%ActiveTierIsMaglev`/`assertMaglevved` → `ArkTools.arkSteedIsCompiled` +
   `COMPILE_COUNT`); негативный контроль отключением компилятора
   (`--no-maglev` → `--compiler-enable-jit=false`); differential c
   интерпретаторным twin; табличный oracle из спецификации; проверка деоптимизации
   как отдельного исхода (`%DeoptimizeFunction`/`assertUnoptimized` →
   `DEOPT_COUNT(_GE)`); обработка «deopt не проверяем в этой конфигурации»
   (аналог `kMaybeDeopted`/GC-stress); OSR-приём «вход из цикла + сверка
   результата» — только для fallback-строки. Пример уже реализованного переноса
   приёма в дереве доставки: `ecmascript/arksteed/test/jittest/exec_marker/exec_marker.ts`
   (`COMPILE_COUNT target 1`, `EXEC_COUNT *target 3`,
   `DEOPT_COUNT *Deoptimize* 0` при `ArkTools.arkSteedCompileSync`).
2. **Непрямые аналоги:** Turbolev/`maglev_future`/`jit_fuzzing_maglev`/`assert_types`
   (нет соответствующей инфраструктуры в ArkSteed); V8-версии тестов
   `-deopt-detach`, `-deopt-oob`, `typed-array-length-*` применимы как входы, но
   oracle и ожидаемое свидетельство должны быть переопределены под ArkSteed.
3. **Обязательства, которые не снимаются неприменимостью аналога:** все строки
   T5/ISSUE-0007, особенно async/generator-домен (в JIT ArkSteed неподдержан —
   проверяется только fallback; поддержка — ISSUE-0002) и OSR-строки
   (`not-applicable` с обоснованием, `osr-chain-audit.md`).
4. **Явно не переносится:** флаги `--maglev*`, `--stress-maglev`,
   `--optimize-on-next-call-optimizes-to-maglev`, `--maglev-*` (проходы),
   `%`-интринсики и ожидания конкретных значений `%GetOptimizationStatus`; запрет
   прямого переноса flags/intrinsics/ожиданий — ограничение 6 эпика.

---

## 11. Ограничения отчёта

- **Динамическое покрытие Maglev не измерялось**; проценты покрытия не
  утверждаются (требование D5). В отчёте нет ни одного числового значения
  покрытия.
- **Собственная сборка V8 на закреплённом SHA отсутствует** в окружении: сеть из
  сборочного окружения недоступна, `d8` для `5cbd82de…` не собирался. Все
  динамические результаты (§9) получены на `V8 13.6.233.17-node.53` и помечены как
  таковые.
- **Полнота инвентаря, а не построчный разбор:** перечень классов тестов выведен
  из полного листинга `test/mjsunit/maglev/` и подкаталога `regress/` (**76**
  файлов), содержимое проверено на представительной выборке (§5, §6). Для
  отдельных regression-файлов формулировка «исторический дефект» основана на
  соглашении об именовании (`regress-<id>`, `maglev-<id>`) и подтверждена
  разбором двух файлов; никакой список «все дефекты Maglev» не заявляется.
- **Статус-файл как источник:** записи `[SKIP]`/`[PASS, SLOW]` приведены дословно
  из `test/mjsunit/mjsunit.status`, `test/cctest/cctest.status`,
  `test/unittests/unittests.status`; часть блоков skips размещена в ALWAYS-блоке
  (проверено чтением соответствующих фрагментов файла), часть — под
  конфигурационными условиями, перечисленными в §6.
- **Запрет на перенос:** флаги, intrinsics и ожидаемые значения V8 не переносятся
  в ArkSteed без адаптации (ограничение 6 эпика); карта §7 фиксирует только
  применимость аналогов.

---

## 12. Источники и лицензии

- Все перечисленные файлы V8 — ревизия
  `5cbd82de90a05c7433c0e1c7b0ec71009f7965ad` официального репозитория
  `chromium.googlesource.com/v8/v8` (зеркало `github.com/v8/v8`); цитируемые
  фрагменты приведены как короткие выдержки из файлов под лицензией BSD-3-Clause
  V8 («Copyright the V8 project authors», `LICENSE` V8); копирование тестовых
  файлов в репозиторий ArkSteed в этой задаче не выполнялось, поэтому добавление
  лицензионных заголовков не требуется. При переносе любого файла/кода V8 в
  ArkSteed (например, в задачах кластеров T5+) обязательны сохранение
  copyright-заголовка и ссылки на ревизию (ограничение 6 эпика).
- Локальные источники ArkSteed: закреплённая ревизия
  `e31cc9a31d421761f1f05df49d39e5065a22e194`
  (`ecmascript/compiler/bytecodes.h`, `ecmascript/arksteed/**`,
  `ecmascript/arksteed/test/README.md`, `ecmascript/arksteed/test/coverage/osr-chain-audit.md`).
