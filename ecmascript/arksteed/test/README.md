## Introduction

This folder contains test cases for ArkSteed JIT compiler.

Each leaf sub-directory contains a test case consisting of the following files:
* **Exactly one** JS/TS file which is the test case;
* **Exactly one** `expected_output.txt` file which holds the expected contents to stdout.

`./run_arksteed_tests.py` is the test case runner which collects all the test cases by traversing the directory tree and then run them.

Comments starting with `//!` in JS/TS files specify runtime options, CFG constraints, bytecode counts, and generated-code comment counts. The runner parses CFGs and code comments from compiler logs, and bytecode instructions from `.abc` disassembly.

Besides the annotation constraints, the runner verifies the live range correctness of every dumped CFG: no value's live range may end before one of its uses starts, and every value used as an allocated Phi input must have a live range.
An `ark_disasm` failure is diagnostic-only unless a bytecode constraint is present, in which case the test fails.

## Test Case Runner

Execute the command `python3 ./run_arksteed_tests.py [<args> ...]` to run all the test cases.

Optional arguments:
* `-p <ARCH>` or `--platform <ARCH>`: Switches between platforms. `<ARCH>` can one of `x64` or `arm64`. The default value of `<ARCH>` is the same as host platform. QEMU is required for cross-platform testing.
* `-m <MODE>` or `--mode <MODE>`: Switches between debug and release build. `<MODE>` can be one of `debug` (by default) or `release`.
* `-f` or `--skip-stub`: Skips building `stub.an`.
* `-F` or `--skip-build`: Skips building all the targets.
* `-I`, `--include` or `--filter <DIR>`: Filters the test cases to be executed with given *directory pattern*. Shell-style wildcards are supported: `--include control_flow/if/else*` keeps both `else` and `elseif`.
    * `--include` can appear multiple times to include more than one directories.
* `-X` or `--exclude <DIR>`: Excludes the test cases with given *directory pattern* (wildcards supported).
    * Example: `--exclude functions/constructor` excludes all the cases in `./functions/constructor`.
    * `--exclude` can appear multiple times to exclude more than one directories.
* `-d` or `--log-debug`: Sets log level to debug.
* `-l` or `--log-components`: Sets log components *besides* compiler log (comma-separated, e.g. `-l gc,ecma`).
    * Example: `-l gc` makes the runner pass `--log-components=compiler:gc` to `ark_js_vm` (note: the VM expects a colon-separated list, and the compiler log is always enabled).
* `--print-asm-code`: Dumps generated ASM code (along with comments) to compiler log.
* `-c` or `--colored`: Enables colored text on the terminal.
* `-C` or `--fully-colored`: Enables colored text both on the terminal and in the compiler log.
* `-s` or `--stops-on-error`: Stops the runner if any test case fails.
* `-j <N>` or `--num-workers <N>`: Enables parallelism with given number of workers.
* `-v` or `--verbose`: Shows verbose information.

## Artifacts

All generated artifacts are written to a fresh `/tmp/arksteed-<platform>-<mode>-<timestamp>/` directory whose tree mirrors this test directory:

* `<case>/<case>.abc`: The compiled bytecode file;
* `<case>/<case>.disasm.txt`: Disassembly of the `.abc` file;
* `<case>/<case>.stdout.txt` / `<case>/<case>.stderr.txt`: Captured VM output. The compiler log (including the CFG dumps checked by the `//!` IR constraints) is always enabled and goes to stdout;
* `run.meta.json`: Run metadata (arguments, git revision, tool locations).

## Test Case Annotations

Annotations start with `//!`. In-line comments to annotations (starting with `#`) are supported.

* `COMPILE_MODE <mode>`: Sets the compilation mode of current .js/.ts file. `<mode>` should be one of `module` (by default), `script` and `commonjs`.
* `PARAMS <params>`: Which runtime options shall be passed to the ArkTS runtime (i.e. the `ark_js_vm` executable) for this test case.
    * Example: `//! PARAMS --enable-force-gc=true --compiler-jit-hotness-threshold=100` passes these 2 parameters to `ark_js_vm`.
    * A parameter replaces the runner default of the same option (e.g. `--enable-force-gc=true` overrides the default `=false`).
    * `--enable-force-gc=true` only forces collection in a debug VM; the relevant runtime hooks are compiled out in release builds. A release test must create enough natural allocation pressure instead of relying on this option.
    * The following options are core to the runner and **can not** be overriden by `PARAMS` annotation. An error will occur if you attempt to override them:
        * `--log-level`
        * `--log-components`
        * All `--compiler-arksteed-print-*` options
        * `--compiler-arksteed-enable-code-comment`
        * `--compiler-trace-deopt` and `--compiler-enable-lazy-deopt-trace` (the deopt traces counted by `DEOPT_COUNT`/`DEOPT_COUNT_GE`)
        * `--stub-file`
        * `--icu-data-path`
    * `PARAM` annotation shall appear at most once in each test case.
* `METHOD <function>`: Leads the IR, bytecode, and codegen constraints to the method specified;
    * Example: All method-scoped annotations after `//! METHOD foo` (and before the next `METHOD`) apply to `function foo()`.
* `BYTECODE_COUNT <mnemonic> [<operand-pattern>] <n>`: Requires exactly `n` matching instructions in the current method.
    * The optional operand pattern is matched against the complete, whitespace-normalized operand string using shell-style wildcards.
    * Omitting the operand pattern counts every instruction with that mnemonic.
    * Example: `//! BYTECODE_COUNT createemptyarray 0x3 1` checks an IMM8-sized slot value.
    * Example: `//! BYTECODE_COUNT createemptyarray 0x11a 1` checks an IMM16-sized slot value.
* `CODEGEN_COUNT <comment-pattern> <n>`: Requires exactly `n` matching ArkSteed generated-code comments in the current method.
    * The comment pattern uses shell-style wildcards. Tests with this constraint automatically enable generated-code printing and comments.
    * Example: `//! CODEGEN_COUNT AllocateJSArray inline success 1` verifies that the inline-success body reached code generation without depending on architecture-specific instruction spelling.
    * These counts verify that code-generation comments were emitted, not that the corresponding path executed or that its ABI and GC behavior are correct.
* `HAS <opcode>`: Specifies an IR opcode which is expected to appear at least once.
    * Example: `//! HAS I32Mul` requires that the compiled CFG of current method should contain at least one IR node of type `I32Mul`.
* `HAS_NOT <opcode>`: Specifies an IR opcode which is expected *not* to appear.
    * Example: `//! HAS_NOT I32Add` requires that the compiled CFG of current method should not contain any IR node of type `I32Add`.
* `COUNT <opcode> <n>`: Specifies an IR opcode which is expected to appear *exactly* n times.
    * Example: `//! COUNT Call 3` requires that the compiled CFG of current method should not contain exactly 3 IR nodes of type `Call`.
* `COUNT_GE <opcode> <n>`: Specifies an IR opcode which is expected to appear *at least* n times.
    * Example: `//! COUNT_GE Call 3` requires that the compiled CFG of current method should not contain 3 or more IR nodes of type `Call`.
* `COUNT_LE <opcode> <n>`: Specifies an IR opcode which is expected to appear *at most* n times.
    * Example: `//! COUNT_GE Call 5` requires that the compiled CFG of current method should not contain 5 or less IR nodes of type `Call`.

Constraints on `CallCommonStub` and `CallRuntime` **must** be followed by a stub-name pattern (shell-style wildcards supported).
* The pattern is matched against the stub name as dumped (`COStub_*` / `RTStub_*`) as well as its prefix-stripped form, so `GetPropertyByName`, `Get*ByName` and `COStub_GetPropertyByName` are all valid spellings.
* Example: `//! HAS_NOT CallCommonStub GetPropertyByName` requires that no `CallCommonStub` node calling `COStub_GetPropertyByName` appears, while e.g. `COStub_Add` calls are unaffected.
* Example: `//! COUNT CallRuntime Throw* 1` requires exactly one `CallRuntime` node whose stub name starts with `Throw`.

### Evidence annotations (case-scoped, matched against captured stdout)

These annotations count events in the case's captured stdout. `<pattern>` is a shell-style wildcard matched against the useful part of the relevant log line (the text after the `[<component>] ` prefix). `<n>=0` is allowed and asserts the absence of matching events. A violation reports the actual value; an unknown keyword is a case configuration error.

* `COMPILE_COUNT <pattern> <n>`: exactly `n` `ArkSteedCompilerTask: Starts compiling` markers for methods whose name matches `<pattern>` (aggregated over recompilations).
* `COMPILE_COUNT_GE <pattern> <n>`: at least `n` such markers.
* `EXEC_COUNT <pattern> <n>`: exactly `n` `>>> Running method:` execution markers whose `<record>.<method>` matches `<pattern>`. Implies `--compiler-arksteed-print-method-name=true` for the case.
* `EXEC_COUNT_GE <pattern> <n>`: at least `n` such markers. Implies the same option.
* `DEOPT_COUNT <pattern> <n>`: exactly `n` deopt events matching `<pattern>` (e.g. `Deoptimize*`, `Lazy Deoptimize*`, `*Deoptimize*`). Implies `--compiler-trace-deopt=true` **and** `--compiler-enable-lazy-deopt-trace=true` for the case; neither option can be set through `PARAMS`, so the observation the annotation asserts cannot be disabled.
* `DEOPT_COUNT_GE <pattern> <n>`: at least `n` such events. Implies the same options.
* `GC_COUNT_GE <pattern> <n>`: at least `n` `gc`-component lines matching `<pattern>`. Implies enabling the `gc` log component for the case.

Only deopt events are deopt evidence; every other compiler-component line is not. Exactly three line forms are counted, one record per event:

* eager with a JS stack trace: `Deoptimize    at <fn> (<file>:<line>:<col>)`;
* eager on the ArkSteed path, which skips the GC-capable stack trace: `Eager deopt disables GC; skip GC-capable JS stack trace construction.`; this record also matches the normalized `Deoptimize Eager deopt disables GC*` spelling, so the `Deoptimize*` family counts every eager event;
* lazy: `Lazy Deoptimize    at <fn> (<file>:<line>:<col>)`.

Compilation markers (`Starts compiling: DeoptimizeTarget`), execution markers of such a method, CFG dumps (`v14: DeoptIfNotBoolean`), debug-level codegen comments and the resume bytecode instruction that follows an eager event carry no deopt identity and are never counted. A record is counted once even when a pattern matches more than one of its spellings.

Normal paths are expected to carry `DEOPT_COUNT <pattern> 0` for the target method, so a silent deopt into the interpreter after entering compiled code is never reported as JIT execution.

The annotation behaviour is covered by `python3 ./runner_self_test.py` (no VM needed), which checks the deopt-evidence classification, the conflicting-`PARAMS` configuration error and the option implication. The in-tree regression cases are `deopt/deopt_count_compile_name`, `deopt/deopt_count_eager` and `deopt/deopt_count_lazy`.

Example:

```js
//  PARAMS applies to all the functions in this test case. (p.s. This line is trivial JS/TS comment)
//! PARAMS  --enable-force-gc=true --compiler-jit-hotness-threshold=60000

//! METHOD      foo
//! BYTECODE_COUNT createemptyarray 0x3 1
//! HAS         CallCommonStub  GetPropertyByName
//! HAS_NOT     I32Mul
//! COUNT_GE    InitialValue    4
//! COUNT_LE    InitialValue    5
//! CODEGEN_COUNT AllocateJSArray inline success 1

//! METHOD  bar
//! BYTECODE_COUNT createemptyarray 0x11a 1
//! HAS     LoadTaggedField             # Loads GlobalEnv from LexicalEnv
//! COUNT   Call            1
```

More examples are shown in the following test cases:
* `pgo/int32/test_1`
* `pgo/int32/test_2`
