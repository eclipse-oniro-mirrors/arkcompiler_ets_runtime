# VM statistics

> Agent-assisted changes to module structure, capabilities or build wiring must update this guide and affected knowledge pages.

## 1. Scope

VM call timing, operation counts and JIT warmup observations.

## 2. Code map

| Path | Responsibility | Risk |
|---|---|---|
| [runtime_stat.h](runtime_stat.h) | EcmaRuntimeStat and RuntimeTimerScope | 🟡 Counters and instrumentation overhead |
| [function_call_timer.h](function_call_timer.h) | FunctionCallTimer and call timing | 🟡 Timer pairing |
| [opt_code_profiler.h](opt_code_profiler.h) | TypedOpProfiler and opcode statistics | 🟡 Counter attribution |
| [jit_warmup_profiler.h](jit_warmup_profiler.h) | JIT warmup observations | 🟡 Profiler lifetime |

## 3. Routing

Read first. [DFX](../../../docs/knowledge/DFX-Subsystem.md).

## 4. Constraints

Preserve timer scope pairing and counter semantics. Trace caller-side enable checks before changing instrumentation on execution paths.

## 5. Verification

Build the relevant enabled and disabled configurations. Check a representative counter or timing report and compare the disabled path for hot-path changes.
