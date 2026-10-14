# Пилот численного C++ coverage ArkSteed (ISSUE-0059)

Задача: заранее доказать рабочий численный C++ coverage production-файлов
`ecmascript/arksteed/**` для парного замера ISSUE-0011 — на закреплённой
ревизии `e31cc9a31d421761f1f05df49d39e5065a22e194` и на текущей ревизии, одним
и тем же измерительным патчем и одной областью измерения.

Полный парный замер по всем наборам тестов, отчёт Debug/Release и закрытие
матрицы остаются в ISSUE-0011. Здесь измеряется только измерительный способ:
он должен реально добавлять coverage-флаги к компиляции и линковке всех
production-объектов ArkSteed, давать данные профиля именно этих файлов после
небольшого JIT-прогона и не менять обычную сборку.

Измеренная «текущая» ревизия — `63937a075b8824aff7be6e83e774261e0f4bc4fe`
(`git_head` в `run.meta.json` прогона A, см. §5). Это последний коммит ветки с
исходниками и сборочными файлами; отчёт и дополнительная проверка инструмента
добавлены следующим коммитом (`git diff --stat 63937a075b HEAD` показывает
только `ecmascript/arksteed/test/coverage/coverage-pilot.md` и
`ecmascript/arksteed/test/coverage/measure_arksteed_coverage_test.py`). Оба эти
файла не участвуют ни в сборке, ни в измерении (тест инструмента не является ни
сборочным входом, ни входом `measure_arksteed_coverage.py`), поэтому измеренная
и доставленная ревизия имеют один и тот же production-код и те же измерительные
артефакты.

## 1. Исходный дефект (воспроизведён)

Базовый снимок (`baseline.md`, `coverage-before.md`, ISSUE-0003) зафиксировал
недоступность численного замера. Причины воспроизведены на этой ревизии:

* `enable_coverage` (`js_runtime_config.gni:149`) — обычное присваивание вне
  `declare_args()`, поэтому GN-аргумент игнорируется. Воспроизведение
  (`builds/WD-0121/out/x64.covdefect`, лог `gn-gen.log`):

  ```
  WARNING at the command-line "--args":1:100: Build argument has no effect.
  The variable "enable_coverage" was set as a build argument but never
  appeared in a declare_args() block in any buildfile.
  ```

* Даже действующий флаг не покрыл бы область: блоки `if (enable_coverage)`
  (`BUILD.gn:1821,2013`, `common_components/BUILD.gn:444,519`,
  `ecmascript/pgo_profiler/tests/BUILD.gn:92`) добавляют `--coverage` только к
  `libark_jsruntime`/`libark_jsruntime_test`, `common_components` и pgo-тестам.

* Production-исходники ArkSteed компилируются в двух наборах, и ни один не
  инструментировался:
  * `ecmascript/compiler:libark_jsoptimizer_set_with_maple`
    (`ecmascript/compiler/BUILD.gn:349`,
    `sources = libark_jsoptimizer_sources + arksteed_jit_sources`) — компилятор
    ArkSteed, 22 верхнеуровневых TU плюс `arch/x64/arksteed_assembler_x64.cpp`;
  * `ecmascript/arksteed` — runtime-файлы `arksteed_deopt_helper.cpp`,
    `arksteed_safepoint_table.cpp`, `arksteed_task.cpp`, которые добавляются в
    `ecma_source` (`BUILD.gn:1349-1353`, при `ets_runtime_enable_ark_steed`) и
    компилируются в `libark_jsruntime_common_set("libark_jsruntime_set")`
    (`js_runtime_config.gni:291`).

Проверка «до» на этой же ревизии (`gn desc` для целей, несущих ArkSteed):

```
$ gn desc out/x64.covdefect //arkcompiler/ets_runtime/ecmascript/compiler:libark_jsoptimizer_set_with_maple cflags_cc | grep -c -- --coverage
0
$ gn desc out/x64.covdefect //arkcompiler/ets_runtime/ecmascript/compiler:libark_jsoptimizer ldflags | grep -c -- --coverage
0
$ gn desc out/x64.covdefect //arkcompiler/ets_runtime:libark_jsruntime_set cflags_cc | grep -c -- --coverage
0
$ gn desc out/x64.covdefect //arkcompiler/ets_runtime:libark_jsruntime ldflags | grep -c -- --coverage
0
```

Все 26 production-TU `ecmascript/arksteed/**` (22 верхнеуровневых компилятора +
`arch/x64` + 3 runtime) остаются без инструментации, а значит без `.gcno` и
`.gcda`.

## 2. Измерительный патч

Патч — `ecmascript/arksteed/test/coverage/arksteed-coverage.patch` (2 файла,
60 строк добавления). Он добавляет:

* `js_runtime_config.gni`: объявленный GN-аргумент `arksteed_coverage = false`
  (по умолчанию выключен) в существующем `declare_args()`;
* `ecmascript/compiler/BUILD.gn`: `config("arksteed_coverage_compile")`
  (`cflags_cc = [ "--coverage" ]`) и `config("arksteed_coverage_link")`
  (`ldflags = [ "--coverage" ]`);
* `ecmascript/compiler/BUILD.gn`: подключение обеих конфигураций к
  `ohos_source_set("libark_jsoptimizer_set_with_maple")` — компиляционная
  приватно, линковая через `public_configs`;
* `js_runtime_config.gni`: подключение компиляционной конфигурации к
  `libark_jsruntime_common_set` (набор, компилирующий три ArkSteed runtime-TU) и
  линковой конфигурации к `libark_jsruntime_shared_library_template`
  (`libark_jsruntime` / `libark_jsruntime_dfx`) — целе, которая линкует объекты
  этого набора.

Компиляционная конфигурация на `libark_jsruntime_set` инструментирует весь
набор (479 TU), а не только три ArkSteed-файла: GN не поддерживает
per-source-конфигурации, а именно этот набор несёт production-объекты ArkSteed.
Отчёт фильтрует область, поэтому лишние VM-TU в численный результат не входят.
Так же устроено существующее (неработающее) намерение `enable_coverage` на этот
набор.

Почему отдельный аргумент, а не исправление `enable_coverage`: это имя
присваивается как обычная переменная в трёх разных файлах
(`js_runtime_config.gni`, `arkcompiler/toolchain/toolchain_config.gni`,
`developtools/profiler/device/base/config.gni`) и нигде не объявлено через
`declare_args()`; объявление его в одном файле при импорте в одну область видимости
даёт ошибку GN «Build argument ... declared ... Previous declaration». Отдельное
имя не меняет существующие сборки и точно указывает область измерения.

Патч побайтово равен `git diff --abbrev=7 e31cc9a31d421761f1f05df49d39e5065a22e194
HEAD -- js_runtime_config.gni ecmascript/compiler/BUILD.gn` (проверено `diff` без
различий) и применён в двух независимых checkout-ах:

```
$ git -C builds/coverage-pilot/pinned apply --check --cached <patch>   # index закреплённого checkout = e31cc9a31, нетронутый
$ git -C workdirs/WD-0121 apply -R --check <patch>                     # текущая ревизия = патч применён
```

Обе команды завершаются кодом 0. `--cached` проверяет применимость к
**закреплённой ревизии**: рабочий каталог `builds/coverage-pilot/pinned` несёт
патч в применённом виде (это источник сборки B), поэтому его index — чистая
`e31cc9a31`, а незакоммиченные правки двух build-файлов и есть патч: `git -C
builds/coverage-pilot/pinned diff` совпадает с файлом патча (различия только в
сокращённых строках `index`), а `git -C builds/coverage-pilot/pinned apply -R
--check <patch>` даёт 0. Базовые blob-иды патча совпадают с закреплённой
ревизией (`f065b14` = `e31cc9a31:js_runtime_config.gni`,
`1494135` = `e31cc9a31:ecmascript/compiler/BUILD.gn`).

Отчёты `ecmascript/arksteed/test/coverage/baseline.md`, `coverage-before.md` и
`known-failures.md` не изменялись.

## 3. Критерий 1: GN действительно добавляет coverage-флаги

Инструментированная генерация (`builds/WD-0121/out/x64.debug`):

```
$ gn desc out/x64.debug //arkcompiler/ets_runtime/ecmascript/compiler:libark_jsoptimizer_set_with_maple cflags_cc | grep -c -- --coverage
1
$ gn desc out/x64.debug //arkcompiler/ets_runtime/ecmascript/compiler:libark_jsoptimizer ldflags | grep -c -- --coverage
1
$ gn desc out/x64.debug //arkcompiler/ets_runtime:libark_jsruntime_set cflags_cc | grep -c -- --coverage
1
$ gn desc out/x64.debug //arkcompiler/ets_runtime:libark_jsruntime ldflags | grep -c -- --coverage
1
```

При выключенном аргументе ровно те же четыре запроса дают 0 вхождений
`--coverage` — и для графа с применённым патчем (`builds/WD-0121/out/x64.normcheck4`),
и для графа канонической ревизии без патча
(`builds/WD-0121/out/x64.normcheck6`).

Компиляция и линковка подтверждены не только `gn desc`, но и фактическими
правилами ninja:

* `out/x64.debug/obj/.../libark_jsoptimizer_set_with_maple.ninja`:
  `cflags_cc = -fvisibility=hidden -std=c++17 -fno-rtti --coverage -DARK_GC_SUPPORT …`,
  178 правил `cxx`, из них 23 production-TU ArkSteed (22 верхнеуровневых плюс
  `obj/.../ecmascript/arksteed/arch/x64/libark_jsoptimizer_set_with_maple/arksteed_assembler_x64.o:
  cxx ../../arkcompiler/ets_runtime/ecmascript/arksteed/arch/x64/arksteed_assembler_x64.cpp`);
* `out/x64.debug/obj/.../libark_jsruntime_set.ninja`:
  `cflags_cc = … --coverage …`, 479 правил `cxx`, включая три production-TU
  ArkSteed:
  `obj/arkcompiler/ets_runtime/ecmascript/arksteed/libark_jsruntime_set/arksteed_task.o: cxx ../../arkcompiler/ets_runtime/ecmascript/arksteed/arksteed_task.cpp`
  (аналогично `arksteed_deopt_helper.o`, `arksteed_safepoint_table.o`);
* `out/x64.debug/obj/.../libark_jsoptimizer.ninja` и `libark_jsruntime.ninja`:
  `ldflags = … -fuse-ld=lld -m64 -Werror -flto=thin --coverage …` (линковка
  coverage-runtime и flush профиля при выходе процесса).

Инструментированная сборка выполнена командой (в каталоге build-контекста,
`CCACHE_DIR`/`CCACHE_BASEDIR` экспортированы на общий кэш и контекст; внутренние
кавычки значения `cc_wrapper` обязательны):

```
$ python3 ark.py x64.debug ark_js_vm arkcompiler/ets_frontend/es2abc ark_disasm \
    libark_jsoptimizer stub.an -j4 \
    --gn-args=ets_runtime_enable_ark_steed=true \
    --gn-args=arksteed_coverage=true \
    '--gn-args=cc_wrapper="bash /work/dexter-root/proj/arksteed/misc/scripts/ccache-arksteed.sh"'
```

Сборка этого каталога завершилась кодом 0 и строкой `=== ninja success! ===`.
Повторный запуск той же команды на измеряемой ревизии §5.1 после rebase дал
`ninja: no work to do` — существующие объекты побайтово соответствуют
исходникам и `args.gn` доставленной ревизии, а не прежнему дереву.

Компилятор: `clang 15.0.4` (`--coverage` = `-fprofile-arcs -ftest-coverage` +
link `libclang_rt.profile`), обработка профиля: `llvm-cov 15.0.4` (`llvm-cov
gcov -i -b -c`).

## 4. Критерий 5: обычная сборка без coverage не меняется

Сгенерированный граф сборки при выключенном аргументе побайтово совпадает с
графом той же ревизии без патча:

```
$ md5sum out/x64.normcheck6/build.ninja.pristine out/x64.normcheck4/build.ninja out/x64.normcheck5/build.ninja out/x64.normcheck7/build.ninja
c1c3d5dd694586e1d4997fd9f2732aa0  out/x64.normcheck6/build.ninja.pristine   # канонические build-файлы без патча
c1c3d5dd694586e1d4997fd9f2732aa0  out/x64.normcheck4/build.ninja            # патч применён, arksteed_coverage=false
c1c3d5dd694586e1d4997fd9f2732aa0  out/x64.normcheck5/build.ninja            # то же, без cc_wrapper
c1c3d5dd694586e1d4997fd9f2732aa0  out/x64.normcheck7/build.ninja            # заново на измеряемой ревизии §5.1
```

`out/x64.normcheck6/build.ninja.pristine` получен из канонических build-файлов
без патча: файлы временно возвращены командой
`git checkout fbe56b49c5f24a85108c068f2f21adebb0f6d775 -- js_runtime_config.gni
ecmascript/compiler/BUILD.gn`, затем `gn gen out/x64.normcheck6` и
`git checkout HEAD -- js_runtime_config.gni ecmascript/compiler/BUILD.gn`
(`git status --porcelain --untracked-files=all` пуст до и после). Граф
`out/x64.normcheck4` — тот же генератор из финального состояния патча с
выключенным `arksteed_coverage`.

Ранее сохранённые копии `out/x64.normcheck3/build.ninja` (md5 `ae6181ba…`) и
`out/x64.normcheck/build.ninja.pristine` относятся к **прежней** канонической
ревизии (до rebase, в котором MR-0036/MR-0037 изменили build-файлы, в том числе
`ecmascript/arksteed/unittests/BUILD.gn`) и поэтому имеют другой md5; критерий 5
перепроверен на текущей ревизии парой выше.

Обычная верификация `misc/scripts/verify-ets-runtime.sh` (x64 fastverify, без
`arksteed_coverage`) на этой ревизии прошла полностью — см. §8.

Новые конфиги включаются только при `arksteed_coverage = true`.

## 5. Критерий 3: профиль production-файлов и численный отчёт

Малый JIT-прогон — один и тот же кейс обеих ревизий
`ecmascript/arksteed/test/jittest/createemptyarray_inline_allocation`
(файл побайтово одинаков в обеих ревизиях). Кейс вызывает
`ArkTools.arkSteedCompileSync` для трёх функций и дожидается компиляции
(`ArkTools.waitJitCompileFinish`), то есть гарантированно компилирует их
JIT-компилятором ArkSteed; VM загружает инструментированную
`libark_jsoptimizer.so` через `dlopen` (`ecmascript/jit/jit_resources.cpp`), а
три runtime-файла приходят из инструментированной `libark_jsruntime.so`. Профиль
обеих библиотек сбрасывается при выходе процесса.

Запуск (абсолютные пути к runner и build root; рабочий каталог процесса —
каталог сборки, поэтому clang пишет `.gcda` рядом с `.gcno`, как и записано в
объектах; профиль прошлого прогона удаляется до запуска):

```
# A, измеряемая ревизия, build-контекст builds/WD-0121
cd /work/dexter-root/proj/arksteed/builds/WD-0121/out/x64.debug
find . -name '*.gcda' -delete
python3 /work/dexter-root/proj/arksteed/misc/scripts/run-arksteed-workdir-tests.py \
  /work/dexter-root/proj/arksteed/workdirs/WD-0121/ecmascript/arksteed/test/run_arksteed_tests.py \
  /work/dexter-root/proj/arksteed/builds/WD-0121 \
  --build-mode debug -p x64 -F -j 1 -I jittest/createemptyarray_inline_allocation

# B, закреплённая ревизия, build-контекст builds/coverage-pilot/context
cd /work/dexter-root/proj/arksteed/builds/coverage-pilot/context/out/x64.debug
find . -name '*.gcda' -delete
python3 /work/dexter-root/proj/arksteed/misc/scripts/run-arksteed-workdir-tests.py \
  /work/dexter-root/proj/arksteed/builds/coverage-pilot/pinned/ecmascript/arksteed/test/run_arksteed_tests.py \
  /work/dexter-root/proj/arksteed/builds/coverage-pilot/context \
  --build-mode debug -p x64 -F -j 1 -I jittest/createemptyarray_inline_allocation
```

Обе команды завершаются кодом 0 и печатают
`[1/1] [PASS] jittest/createemptyarray_inline_allocation`.

Проверка полноты прогона (обязательный шаг): кейс, кроме трёх синхронных
компиляций, запускает в конце программы асинхронную hotness-компиляцию ArkSteed
(`checkSurvivors`, порог hotness 2, два вызова) — это фоновая задача
`ArkSteedCompilerTask`. В `stdout.txt` артефактов прогона должно быть четыре
строки `ArkSteedCompilerTask: Finished compiling`:

```
$ grep -c 'Finished compiling' /tmp/arksteed-x64-debug-20260928-211659/jittest/createemptyarray_inline_allocation/createemptyarray_inline_allocation.stdout.txt
4
```

Прогон под длительной тяжёлой нагрузкой (load average ≈ 50 от параллельных
сборок других задач) обрывает фоновую компиляцию выходом процесса: те же команды
дают меньший профиль (функции 1079/3098, строки 6633/19050 вместо 1296/3098 и
8128/19050; ветви в том прогоне не приводятся — он снят до исправления счётчика
ветвей, §5.4). Поэтому при неполном прогоне он повторяется. Документированная
процедура выполнена для B дважды подряд и для A на измеряемой ревизии
(включая повторный прогон после rebase); каждый полный прогон давал четыре
компиляции и одинаковые числа (A: 1296/3098, 8128/19050, 4843/22007;
B: 1295/3079, 8107/18844, 4825/21852).

Отчёт (одинаковая команда для обоих каталогов сборки):

```
python3 /work/dexter-root/proj/arksteed/workdirs/WD-0121/ecmascript/arksteed/test/coverage/measure_arksteed_coverage.py \
  --out <build>/out/x64.debug \
  --json <build>/out/x64.debug/arksteed-coverage-pilot.json \
  --markdown <build>/out/x64.debug/arksteed-coverage-pilot.md
```

### 5.1 Ревизия A — измеряемая (`63937a075b8824aff7be6e83e774261e0f4bc4fe`)

* Build context: `builds/WD-0121` (`arkcompiler/ets_runtime` →
  `workdirs/WD-0121`); каталог сборки `builds/WD-0121/out/x64.debug`
  (аргументы §3).
* `gn desc` этой сборки: по одному вхождению `--coverage` в `cflags_cc`
  `libark_jsoptimizer_set_with_maple` и `libark_jsruntime_set` и в `ldflags`
  `libark_jsoptimizer` и `libark_jsruntime`.
* Прогон: `[1/1] [PASS] jittest/createemptyarray_inline_allocation`,
  артефакты `/tmp/arksteed-x64-debug-20260928-211659`; `run.meta.json` фиксирует
  `git_head = 63937a075b8824aff7be6e83e774261e0f4bc4fe`; в `stdout.txt` четыре
  компиляции ArkSteed (три синхронные и `checkSurvivors`).
* Профили: **26 из 26** TU области; для каждого есть непустой `.gcda`.
* Отчёты: `builds/WD-0121/out/x64.debug/arksteed-coverage-pilot.json`,
  `builds/WD-0121/out/x64.debug/arksteed-coverage-pilot.md`.
* Числа: функции **1296 / 3098** (41.83%), строки **8128 / 19050** (42.67%),
  ветви **4843 / 22007** (22.01%).

### 5.2 Ревизия B — закреплённая (`e31cc9a31d421761f1f05df49d39e5065a22e194`)

* Отдельный checkout закреплённой ревизии: `builds/coverage-pilot/pinned`
  (git worktree, detached HEAD), измерительный патч применён `git apply`
  (применимость к закреплённой ревизии проверена `git apply --check --cached`,
  §2).
* Build context: `builds/coverage-pilot/context`; каталог сборки
  `builds/coverage-pilot/context/out/x64.debug`, те же GN-аргументы и тот же
  `ccache`.
* `gn desc` этой сборки: те же четыре вхождения `--coverage` (по одному в
  `cflags_cc` набора `libark_jsoptimizer_set_with_maple` с 178 TU и набора
  `libark_jsruntime_set` с 479 TU и по одному в `ldflags` `libark_jsoptimizer`
  и `libark_jsruntime`).
* Прогон: тот же кейс через runner закреплённой ревизии,
  `[1/1] [PASS] jittest/createemptyarray_inline_allocation`,
  артефакты `/tmp/arksteed-x64-debug-20260928-194748`; в `stdout.txt` те же
  четыре компиляции ArkSteed.
* Профили: **26 из 26** TU области (те же пути, что для A).
* Отчёты: `builds/coverage-pilot/context/out/x64.debug/arksteed-coverage-pilot.json`,
  `builds/coverage-pilot/context/out/x64.debug/arksteed-coverage-pilot.md`.
* Числа: функции **1295 / 3079** (42.06%), строки **8107 / 18844** (43.02%),
  ветви **4825 / 21852** (22.08%).

### 5.3 Сверка по TU (A = измеряемая, B = закреплённая)

Значения — «покрыто / всего». Различия знаменателей объясняются тем, что между
ревизиями изменены четыре production-TU ArkSteed (`arksteed_graph_builder.cpp`,
`arksteed_bytecode_preprocessor.cpp`, `arksteed_compiler.cpp`,
`arksteed_regalloc.cpp`) и включённые ими заголовки
(`arksteed_async_opcode_inventory.h`, `arksteed_bytecode_preprocessor.h`,
`arksteed_graph_builder.h`, `arksteed_regalloc.h`). Остальные 22 TU идентичны по
исходнику и дают одинаковые значения на обеих ревизиях, что подтверждает
детерминированность замера.

| TU | A строки | B строки | A функции | B функции | A ветви | B ветви |
|---|---|---|---|---|---|---|
| `ecmascript/arksteed/arch/x64/arksteed_assembler_x64.cpp` | 325/680 | 325/680 | 79/149 | 79/149 | 73/315 | 73/315 |
| `ecmascript/arksteed/arksteed_access_info_factory.cpp` | 204/775 | 204/775 | 22/52 | 22/52 | 82/1216 | 82/1216 |
| `ecmascript/arksteed/arksteed_assembler.cpp` | 31/45 | 31/45 | 12/25 | 12/25 | 6/38 | 6/38 |
| `ecmascript/arksteed/arksteed_bytecode_analysis.cpp` | 168/220 | 168/220 | 22/37 | 22/37 | 113/294 | 113/294 |
| `ecmascript/arksteed/arksteed_bytecode_preprocessor.cpp` | 446/735 | 438/717 | 50/84 | 49/83 | 248/862 | 242/844 |
| `ecmascript/arksteed/arksteed_codegen.cpp` | 1229/3171 | 1229/3171 | 134/406 | 134/406 | 845/4971 | 845/4971 |
| `ecmascript/arksteed/arksteed_comment.cpp` | 51/69 | 51/69 | 15/19 | 15/19 | 14/40 | 14/40 |
| `ecmascript/arksteed/arksteed_compile_info_facts.cpp` | 450/836 | 450/836 | 82/138 | 82/138 | 198/583 | 198/583 |
| `ecmascript/arksteed/arksteed_compiler.cpp` | 239/284 | 238/278 | 27/41 | 27/41 | 232/650 | 230/624 |
| `ecmascript/arksteed/arksteed_constant_folding.cpp` | 91/291 | 91/291 | 25/54 | 25/54 | 43/249 | 43/249 |
| `ecmascript/arksteed/arksteed_deopt_helper.cpp` | 274/884 | 274/884 | 31/77 | 31/77 | 174/1002 | 174/1002 |
| `ecmascript/arksteed/arksteed_disassembler.cpp` | 38/46 | 38/46 | 4/4 | 4/4 | 9/24 | 9/24 |
| `ecmascript/arksteed/arksteed_feedback_reader.cpp` | 157/342 | 157/342 | 21/38 | 21/38 | 66/257 | 66/257 |
| `ecmascript/arksteed/arksteed_graph_builder.cpp` | 1975/6903 | 1963/6716 | 325/896 | 324/876 | 1118/6819 | 1110/6704 |
| `ecmascript/arksteed/arksteed_graph_printer.cpp` | 409/449 | 409/449 | 41/54 | 41/54 | 465/1006 | 465/1006 |
| `ecmascript/arksteed/arksteed_heap_broker.cpp` | 53/81 | 53/81 | 14/28 | 14/28 | 28/98 | 28/98 |
| `ecmascript/arksteed/arksteed_loop_effects.cpp` | 28/85 | 28/85 | 11/29 | 11/29 | 11/73 | 11/73 |
| `ecmascript/arksteed/arksteed_opcode.cpp` | 336/904 | 336/904 | 131/397 | 131/397 | 198/861 | 198/861 |
| `ecmascript/arksteed/arksteed_regalloc.cpp` | 994/1224 | 994/1229 | 167/414 | 168/416 | 585/1509 | 583/1513 |
| `ecmascript/arksteed/arksteed_regalloc_types.cpp` | 25/68 | 25/68 | 3/6 | 3/6 | 31/167 | 31/167 |
| `ecmascript/arksteed/arksteed_safepoint_table.cpp` | 114/226 | 114/226 | 14/18 | 14/18 | 38/214 | 38/214 |
| `ecmascript/arksteed/arksteed_task.cpp` | 120/184 | 120/184 | 18/33 | 18/33 | 140/406 | 140/406 |
| `ecmascript/arksteed/arksteed_vertex.cpp` | 51/74 | 51/74 | 15/30 | 15/30 | 26/74 | 26/74 |
| `ecmascript/arksteed/arksteed_vreg.cpp` | 1/12 | 1/12 | 1/2 | 1/2 | 0/18 | 0/18 |
| `ecmascript/arksteed/arksteed_write_barrier.cpp` | 194/253 | 194/253 | 14/31 | 14/31 | 15/76 | 15/76 |
| `ecmascript/arksteed/arksteed_write_barrier_value_kind_pass.cpp` | 125/209 | 125/209 | 18/36 | 18/36 | 85/185 | 85/185 |
| **Итого** | 8128/19050 | 8107/18844 | 1296/3098 | 1295/3079 | 4843/22007 | 4825/21852 |

Численный отчёт ненулевой на обеих ревизиях и построен по профилю
production-объектов ArkSteed, а не по числу тестов или по успешному `gn gen`:
оба запуска `measure_arksteed_coverage.py` завершились кодом 0, а инструмент
возвращает код 1, если ни один TU области не инструментирован или не исполнен.

Значения соответствуют **одному малому JIT-кейсу** — это цель пилота: доказать
измерительный способ. Парный замер по всем наборам тестов остаётся в
ISSUE-0011 и выполняется теми же аргументом, областью и инструментом.

### 5.4 Метрика ветвей (исправление счётчика)

`llvm-cov gcov -i` помечает каждый исход ветви одним из трёх состояний:
`taken` (вычислен и хотя бы раз пройден), `nottaken` (вычислен, не пройден) и
`notexec` (не вычислялся). Метрика ветвей отчёта — это gcov-метрика «taken at
least once»: знаменатель — все три состояния, числитель — только `taken`.

Исходная версия инструмента сопоставляла только `taken|nottaken`, поэтому
`notexec`-записи молча выпадали из знаменателя и покрытие ветвей было завышено
примерно в 2.5 раза (`arksteed_task.cpp` — 140/268 = 52.24% вместо
140/406 = 34.48%, где `llvm-cov gcov` печатает
`Taken at least once: 34.48% of 406`; сводно A — 4843/8745 = 55.38% вместо
4843/22007 = 22.01%). Числитель (4843 и 4825) совпадал с gcov, то есть выборка
TU не менялась — неверным был только знаменатель.

Исправление в `measure_arksteed_coverage.py`:

* `BRANCH_RE` принимает все три состояния:
  `^branch:<line>,(taken|nottaken|notexec)$`; `notexec` увеличивает `branches`,
  но не `branches_covered`;
* запись ветви неузнаваемого вида не игнорируется: инструмент печатает её на
  stderr и завершается кодом 3, поэтому метрика не может снова измениться
  молча;
* обе сборки перечитаны исправленным инструментом; JSON/markdown отчёты
  перегенерированы; функции и строки не изменились, изменились только ветви
  (§5.1–§5.3).

Регрессионная проверка счётчика — `measure_arksteed_coverage_test.py` в этом же
каталоге (VM-free, на синтетическом промежуточном тексте `llvm-cov gcov -i`, в
том числе на реальной выборке `arksteed_task.cpp` 140/128/138):

```
$ python3 ecmascript/arksteed/test/coverage/measure_arksteed_coverage_test.py
[PASS] check_branch_states
[PASS] check_header_sections_separate
[PASS] check_unknown_records_reported
[PASS] check_main_rejects_unknown_records

4/4 regression check(s) passed
```

## 6. Критерий 4: область, исключения, пути, инструменты

* Область измерения: production-файлы `ecmascript/arksteed/**` — 26 TU:
  22 компиляторных TU из `arksteed_jit_sources`, `arch/x64/arksteed_assembler_x64.cpp`
  и 3 runtime-TU (`arksteed_deopt_helper.cpp`, `arksteed_safepoint_table.cpp`,
  `arksteed_task.cpp`). Совпадает с областью `coverage-before.md` (26 `.cpp`;
  незакрытых production-TU нет).
* Исключения: `ecmascript/arksteed/unittests/**`,
  `ecmascript/arksteed/test/**`, `ecmascript/arksteed/arch/arm64/**` (не
  компилируется на x64).
* Конвенция атрибуции точек: численный итог складывается из секции
  скомпилированного `.cpp` каждого из 26 TU (26 строк таблицы §5.3).
  Instrumented-точки заголовков, включаемых в эти TU, в итог не входят: одна и
  та же inline-функция (и её ветви) иначе считалась бы в каждом включившем её TU,
  и знаменатель зависел бы от состава включений. Это не уменьшение области:
  файловый состав области `coverage-before.md` (production-файлы
  `ecmascript/arksteed/**`: 26 `.cpp` плюс заголовки) одинаков для «до» и
  «после», область и исключения по умолчанию у инструмента одни и те же для
  обеих ревизий, а заголовки, дающие instrumented-секции внутри этих 26 TU
  (46 из 50 заголовков области на измеренной ревизии; четыре заголовка не имеют
  instrumented-точек), всё равно инструментированы и видны в промежуточных
  gcov-данных — они сознательно не атрибутируются итогу, а не теряются молча.
  Парный замер ISSUE-0011 обязан использовать эту же конвенцию (тот же
  инструмент с областью и исключениями по умолчанию), иначе числа несопоставимы.
* Инструментируются два набора: `libark_jsoptimizer_set_with_maple` (178 TU,
  включая 23 production-TU ArkSteed) и `libark_jsruntime_set` (479 TU, включая
  3 production-TU ArkSteed). Отчёт фильтрует область. Это осознанный выбор:
  GN не поддерживает per-source-конфигурации, а именно эти наборы реально несут
  production-объекты ArkSteed; при выключенном аргументе граф сборки не
  меняется.
* Профили: `.gcno`/`.gcda` лежат в каталоге объектов сборки:
  * 22 компиляторных TU — `<build-context>/out/x64.debug/obj/arkcompiler/ets_runtime/ecmascript/arksteed/libark_jsoptimizer_set_with_maple/*.gcno|.gcda`;
  * `arksteed_assembler_x64.cpp` — `<build-context>/out/x64.debug/obj/arkcompiler/ets_runtime/ecmascript/arksteed/arch/x64/libark_jsoptimizer_set_with_maple/*.gcno|.gcda`;
  * три runtime-TU — `<build-context>/out/x64.debug/obj/arkcompiler/ets_runtime/ecmascript/arksteed/libark_jsruntime_set/*.gcno|.gcda`.
  Эквивалентный рекурсивный шаблон:
  `<build-context>/out/x64.debug/obj/arkcompiler/ets_runtime/ecmascript/arksteed/**/{libark_jsoptimizer_set_with_maple,libark_jsruntime_set}/*.gcno|.gcda`.
* Неизмеренное обозначено явно: JIT-код VM, `ecmascript/compiler/**` вне
  ArkSteed, arm64 и другие backend в это измерение не входят. Инструментированные
  VM-TU вне области отбрасываются фильтром `--scope ecmascript/arksteed/`. Три
  VM-TU вне области (`ecmascript/base/config.cpp`,
  `ecmascript/base/string_helper.cpp`,
  `common_components/objects/base_string_table.cpp`) имеют пустой `.gcno`
  (20 байт, нет инструментированных точек), поэтому `llvm-cov` не выдаёт для
  них секцию; `measure_arksteed_coverage.py` сообщает об этом предупреждением и
  на область измерения это не влияет.

Инструменты: `gn 2021 (1cdd270b)`, `ninja 1.11.0`, `clang 15.0.4` (OHOS dev,
llvm-project `ef68e8e38d8a101c561d2775f6273ed0e1a50a6b`), `llvm-cov 15.0.4`,
`ccache 4.9.1`, `Python 3.12.3`, Linux x86_64.

## 7. Сводка по критериям задачи

| Критерий | Свидетельство |
|---|---|
| (1) выключенный по умолчанию способ действительно добавляет coverage-флаги при компиляции и линковке ArkSteed, подтверждено `gn desc` | §2, §3: `arksteed_coverage = false` по умолчанию; при `true` — по одному `--coverage` в `cflags_cc` обоих несущих наборов и в `ldflags` `libark_jsoptimizer` и `libark_jsruntime` (подтверждено `gn desc` и правилами ninja обеих ревизий) |
| (2) один и тот же измерительный патч в отдельных checkout закреплённой и текущей ревизии, baseline-снимок не изменён | §2: `git apply --check --cached` на нетронутом index `builds/coverage-pilot/pinned` (e31cc9a31…) и `git apply -R --check` на текущей; применённый патч совпадает с файлом патча; патч побайтово равен `git diff --abbrev=7 e31cc9a31 HEAD -- <2 build-файла>`; отчёты `coverage/baseline.md`, `coverage-before.md`, `known-failures.md` не изменены |
| (3) после малого JIT-прогона получен профиль production-файлов `ecmascript/arksteed` и ненулевой численный отчёт на обеих ревизиях | §5: 26/26 TU с профилем на каждой ревизии; A (`63937a075b`) — строки 8128/19050, функции 1296/3098, ветви 4843/22007 (22.01%); B (`e31cc9a31`) — строки 8107/18844, функции 1295/3079, ветви 4825/21852 (22.08%); `git_head` прогона A — измеряемая ревизия; метрика ветвей — gcov «taken at least once» (все три состояния счётчика, §5.4) |
| (4) сохранены SHA, команды, версии инструментов, флаги, область, исключения, пути к профилям и отчётам | §3, §4, §5, §6: SHA измеряемой и закреплённой ревизий, `args.gn` и `ark.py`-команда сборки, исполняемые команды прогона (абсолютные пути, удаление `.gcda`, проверка полноты), версии gn/ninja/clang/llvm-cov/ccache/Python, область `ecmascript/arksteed/**` (26 TU) и исключения, каталоги `.gcno`/`.gcda` (включая `arch/x64`), пути JSON/markdown |
| (5) обычная сборка без coverage не меняется | §4: `build.ninja` побайтово совпадает с той же ревизией без патча (md5 `c1c3d5dd…` для канонических build-файлов и для патча с выключенным аргументом); полная верификация `verify-ets-runtime.sh` зелёная (§8) |

## 8. Обязательная верификация

Полная верификация `misc/scripts/verify-ets-runtime.sh`
(`bash /work/dexter-root/proj/arksteed/misc/scripts/verify-ets-runtime.sh` из
рабочей копии; x64 fastverify, без `arksteed_coverage`) выполнена на измеряемой
ревизии: прогон
`builds/WD-0121/verification-20260928T211804Z-F5RyO4`,
`run.meta.json.source_revision` = `63937a075b8824aff7be6e83e774261e0f4bc4fe`
(та же ревизия, что измерена в §5), exit 0; в `summary.tsv`:
`fastverify-build` 0, `fastverify-integrated` 0 (1109/1109),
`fastverify-external` 0 (172/172); `fastverify-host` и `release-*` пропущены по
настройкам верификатора (`ARK_RUN_HOST_TESTS=0`, `ARK_BUILD_RELEASE=0`) и
записаны как skipped (код `-`), а не как passed. Коммит с этим отчётом не меняет
ни одного файла, участвующего в сборке или в измерении, поэтому результат
относится и к итоговому состоянию ветки.

## 9. Ссылки на артефакты

* A (измеряемая `63937a075b`): прогон `/tmp/arksteed-x64-debug-20260928-211659`
  (`run.meta.json.git_head` = `63937a075b8824aff7be6e83e774261e0f4bc4fe`),
  отчёты `builds/WD-0121/out/x64.debug/arksteed-coverage-pilot.{json,md}`.
* B (закреплённая `e31cc9a31`): прогон `/tmp/arksteed-x64-debug-20260928-194748`,
  отчёты `builds/coverage-pilot/context/out/x64.debug/arksteed-coverage-pilot.{json,md}`.
* Определение дефекта: `builds/WD-0121/out/x64.covdefect/gn-gen.log`.
* Графы без coverage: `builds/WD-0121/out/x64.normcheck6` (канонические
  build-файлы, `build.ninja.pristine`), `out/x64.normcheck4` и
  `out/x64.normcheck5` (патч применён, `arksteed_coverage=false`).
