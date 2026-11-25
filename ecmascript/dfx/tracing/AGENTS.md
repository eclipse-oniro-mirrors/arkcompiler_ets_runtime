# Runtime event tracing

> Agent-assisted changes to module structure, capabilities or build wiring must update this guide and affected knowledge pages.

## 1. Scope

RuntimeListener-based trace collection and event output.

## 2. Code map

| Path | Responsibility | Risk |
|---|---|---|
| [tracing.cpp](tracing.cpp) | Tracing start/stop, event collection and output | 🟡 Listener lifetime and runtime cost |
| [../../debugger/notification_manager.h](../../debugger/notification_manager.h) | Runtime listener dispatch | 🟡 Registration and hot-path cost |

## 3. Routing

Read first. [DFX](../../../docs/knowledge/DFX-Subsystem.md), [Debugger](../../../docs/knowledge/Debugger-Subsystem.md).

## 4. Constraints

Pair listener registration and removal. Preserve the event schema, synchronization, buffer limits, and disabled-path cost. Include the debugger when checking listener coexistence.

## 5. Verification

Build a runtime configuration that includes `tracing.cpp` and verify trace start, event collection and stop, including coexistence with a debugger listener.
