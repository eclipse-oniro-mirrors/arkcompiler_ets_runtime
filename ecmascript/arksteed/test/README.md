## Introduction

This folder contains test cases for ArkSteed JIT compiler.

Each leaf sub-directory contains a test case with one entry JS/TS file and one `expected_output.txt`. Additional sibling modules are allowed; with several sources, the entry file must be named after the directory. Siblings are emitted as separate ABC files by default. If the directory contains `fileInfo.txt`, all sources are instead compiled into one merged ABC with `es2abc --module --merge-abc @fileInfo.txt`, using record names supplied by that file.

**Annotations** are comments starting with `//!` in JS/TS files specify runtime options, CFG constraints, bytecode counts, and generated-code comment counts. The runner parses CFGs and code comments from compiler logs, and bytecode instructions from `.abc` disassembly. See section ["Test Case Annotations"](#test-case-annotations) for details.

Besides the annotation constraints, the runner verifies the live range correctness of every dumped CFG: no value's live range may end before one of its uses starts, and every value used as an allocated Phi input must have a live range.

## Test Case Runner

Execute the command `python3 ./run_arksteed_tests.py [<args> ...]` to run all the test cases.

Optional arguments:
* `-p <ARCH>` or `--platform <ARCH>`: Switches between platforms. `<ARCH>` can one of `x64` or `arm64`. The default value of `<ARCH>` is the same as host platform. QEMU is required for cross-platform testing.
* `-m <MODE>` or `--mode <MODE>`: Switches between build modes. `<MODE>` can be one of `debug` (by default), `release` or `fastverify`.
* `-f` or `--skip-stub`: Skips building `stub.an`.
* `-F` or `--skip-build`: Skips building all the targets.
* `-I`, `--include` or `--filter <DIR>`: Filters the test cases to be executed with given *directory pattern*. Shell-style wildcards are supported: `--include control_flow/if/else*` keeps both `else` and `elseif`.
    * `--include` can appear multiple times to include more than one directories.
* `-X` or `--exclude <DIR>`: Excludes the test cases with given *directory pattern* (wildcards supported).
    * Example: `--exclude functions/constructor` excludes all the cases in `./functions/constructor`.
    * `--exclude` can appear multiple times to exclude more than one directories.
* `-S` or `--skip-ir-check`: Disables IR constraint checking. Only the correctness of VM's behavior is tested.
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

* `COMPILE_MODE <mode>`: Compilation mode of the current file: `module` (by default), `script` or `commonjs`.
* `PARAMS <params>`: Runtime options passed to `ark_js_vm` for this test case. Shall appear at most once per case; a parameter replaces the runner default of the same option.
    * Example: `//! PARAMS --enable-force-gc=true --compiler-jit-hotness-threshold=100`.
    * The runner disables inline by default to preserve the IR and behavior expected by existing tests. Inline-specific cases explicitly enable it with `--compiler-arksteed-enable-inline=true`.
    * `--enable-force-gc=true` only forces collection in a debug VM; a release test must create enough natural allocation pressure instead.
    * The options driving the checks (`--log-level`, `--log-components`, all `--compiler-arksteed-print-*` options, `--compiler-arksteed-enable-code-comment`, `--compiler-trace-deopt`, `--compiler-enable-lazy-deopt-trace`, `--stub-file`, `--icu-data-path`) are core to the runner and cannot be overridden.
* `METHOD <function>`: Scopes the following IR, bytecode and codegen constraints to `function`, until the next `METHOD`.
* `BYTECODE_COUNT <mnemonic> [<operand-pattern>] <n>`: Requires exactly `n` matching instructions in the current method. The optional shell-style operand pattern is matched against the complete, whitespace-normalized operand string.
    * Example: `//! BYTECODE_COUNT createemptyarray 0x11a 1`.
* `CODEGEN_COUNT <comment-pattern> <n>`: Requires exactly `n` matching generated-code comments (shell-style wildcards) in the current method; enables code printing and comments for the case. These counts verify that code-generation comments were emitted, not that the corresponding path executed or that its ABI and GC behavior are correct.
* `HAS <opcode>` / `HAS_NOT <opcode>`: Requires the IR opcode to appear at least once / not at all.
* `COUNT <opcode> <n>` / `COUNT_GE <opcode> <n>` / `COUNT_LE <opcode> <n>`: Requires the IR opcode to appear exactly / at least / at most `n` times.

Constraints on `CallCommonStub` and `CallRuntime` **must** be followed by a stub-name pattern (shell-style wildcards), matched against both the dumped `COStub_*` / `RTStub_*` spelling and its prefix-stripped form. Example: `//! COUNT CallRuntime Throw* 1`.

IR constraints must express the behavior under test, not an incidental frontend lowering.

### Evidence annotations (case-scoped, matched against captured stdout)

These count events in the case's captured stdout. `<pattern>` is a shell-style wildcard matched against the text after the `[<component>] ` prefix of a log line; `<n>=0` asserts the absence of matching events, and a violation reports the actual value. The options needed for these counts are enabled by the runner and cannot be disabled through `PARAMS`.

* `COMPILE_COUNT` / `COMPILE_COUNT_GE <pattern> <n>`: `ArkSteedCompilerTask: Starts compiling` markers of methods whose name matches `<pattern>`, aggregated over recompilations.
* `EXEC_COUNT` / `EXEC_COUNT_GE <pattern> <n>`: `>>> Running method:` markers whose `<record>.<method>` matches `<pattern>`.
* `DEOPT_COUNT` / `DEOPT_COUNT_GE <pattern> <n>`: Deopt events matching `<pattern>` (e.g. `Deoptimize*`, `Lazy Deoptimize*`, `*Deoptimize*`). Only deopt events are deopt evidence; compilation markers, execution markers, CFG dumps and codegen comments are never counted. Normal paths are expected to carry `DEOPT_COUNT <pattern> 0`, so a silent deopt into the interpreter is never reported as JIT execution.
* `GC_COUNT_GE <pattern> <n>`: At least `n` `gc`-component lines matching `<pattern>`.

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
