# Compiler type identifiers

> Agent-assisted changes to module structure, capabilities or build wiring must update this guide and affected knowledge pages.

## 1. Scope

TypeScript type references and builtin identifiers used by runtime/compiler code, plus AOT builtin declarations shipped by SDK packaging.

## 2. Code map

| Path | Responsibility | Risk |
|---|---|---|
| [builtin_type_id.h](builtin_type_id.h) | BuiltinTypeId compared with the frontend BuiltinType enum | 🔴 Toolchain agreement |
| [global_ts_type_ref.h](global_ts_type_ref.h) | GlobalTSTypeRef encoding and type kinds | 🔴 Encoded type compatibility |
| [global_type_info.h](global_type_info.h) | GlobalTypeID and TypeLocation | 🟡 Compiler consumers |
| [lib_ark_builtins.d.ts](lib_ark_builtins.d.ts) | AOT builtin declarations bundled by the SDK | 🟡 SDK compatibility |

## 3. Routing

Read first. [AOT guide](../../docs/aot-guide_zh.md), [Bytecode guide](../jspandafile/AGENTS.md), [Diagnostic heap metadata entry points](../../docs/knowledge/README.md#3-补充入口).

## 4. Constraints

Keep shared type IDs and encoding in sync with their consumers and the frontend. Trace enum order, bit fields and default values before changing them. Check the SDK package when declarations change.

## 5. Verification

Build the affected AOT compiler and runtime. Run an AOT regression with the affected type, and check older ABC inputs if type IDs or encoding change.
