# Thread checkpoints

> Agent-assisted changes to module structure, capabilities or build wiring must update this guide and affected knowledge pages.

## 1. Scope

RAII scopes for managed/native thread transitions and coordinated suspension.

## 2. Code map

| Path | Responsibility | Risk |
|---|---|---|
| [thread_state_transition.h](thread_state_transition.h) | State transitions and suspension scopes | 🔴 Safepoint and heap-access ordering |
| [../js_thread.h](../js_thread.h) | Thread state and glue | 🔴 Mutator state |
| [../runtime.cpp](../runtime.cpp) | SuspendAll and ResumeAll | 🔴 Runtime-wide coordination |

## 3. Routing

Read first. [Concurrency](../../docs/knowledge/ArkTS-Concurrency.md), [GC](../../docs/knowledge/ArkTS-GC.md).

## 4. Constraints

Pair suspension and resumption on every exit path. Trace how a native thread reenters managed execution before changing a barrier or safepoint. Review lock ordering with GC and runtime shutdown.

## 5. Verification

Run affected GC and thread-state tests from [the runtime test targets](../tests/BUILD.gn). Exercise managed/native transitions during suspension and after resumption.
