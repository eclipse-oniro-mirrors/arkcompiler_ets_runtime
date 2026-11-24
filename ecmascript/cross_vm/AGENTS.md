# Hybrid VM interoperability

> Agent-assisted changes to module structure, capabilities or build wiring must update this guide and affected knowledge pages.

## 1. Scope

Dynamic runtime adapters for hybrid VM references, object access, conversion, and unified GC.

## 2. Code map

| Path | Responsibility | Risk |
|---|---|---|
| [cross_vm_operator.h](cross_vm_operator.h) | CrossVMOperator handoff and VM interface | 🔴 Cross-runtime lifetime |
| [dynamic_object_accessor.h](dynamic_object_accessor.h) | Dynamic object access contract | 🔴 Ownership and rooting |
| [dynamic_type_converter.h](dynamic_type_converter.h) | Dynamic type conversion | 🔴 Representation compatibility |
| [unified_gc/unified_gc.cpp](unified_gc/unified_gc.cpp) | Unified collection | 🔴 Cross-VM reachability |

## 3. Routing

Read first. [Concurrency](../../docs/knowledge/ArkTS-Concurrency.md), [GC](../../docs/knowledge/ArkTS-GC.md).

## 4. Constraints

Check the `ark_js_hybrid` source and test gates. Trace references through both heaps, including ownership, write barriers, handles and conversions at the interop boundary.

## 5. Verification

Run `Cross_VM_Operator_TestAction`, `Unified_GC_TestAction`, and, where enabled, `Dynamic_Object_Accessor_TestAction` from [the runtime test targets](../tests/BUILD.gn). Exercise the hybrid configuration for hybrid-only behavior.
