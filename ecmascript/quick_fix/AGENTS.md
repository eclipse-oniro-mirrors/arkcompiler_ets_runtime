# Quick Fix command-line tool

> Agent-assisted changes to module structure, capabilities or build wiring must update this guide and affected knowledge pages.

## 1. Scope

CLI for exercising runtime patch load and unload with supplied ABC files.

## 2. Code map

| Path | Responsibility | Risk |
|---|---|---|
| [main.cpp](main.cpp) | Command parsing and JSNApi load/unload calls | 🟡 User-visible exit and error behavior |
| [BUILD.gn](BUILD.gn) | quick_fix executable | 🟡 Runtime linkage |

## 3. Routing

Read first. [Patch](../../docs/knowledge/Patch-Subsystem.md), [Runtime patch guide](../patch/AGENTS.md).

## 4. Constraints

Keep patch implementation and method replacement in the runtime patch layer. Preserve CLI argument handling, failures and load/unload sequencing.

## 5. Verification

Build `//arkcompiler/ets_runtime/ecmascript/quick_fix:quick_fix` (GN label). Run applicable cases in [ark_quickfix_test](../../test/quickfix/BUILD.gn) for runtime behavior changes.
