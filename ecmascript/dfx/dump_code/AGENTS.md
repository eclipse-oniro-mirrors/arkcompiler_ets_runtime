# JIT ELF dumps

> Agent-assisted changes to module structure, capabilities or build wiring must update this guide and affected knowledge pages.

## 1. Scope

Diagnostic ELF construction for runtime-generated machine code.

## 2. Code map

| Path | Responsibility | Risk |
|---|---|---|
| [jit_dump_elf.h](jit_dump_elf.h) | JsJitDumpElf and section layout | 🟡 ELF format compatibility |
| [jit_dump_elf.cpp](jit_dump_elf.cpp) | Section layout, symbol data and file output | 🟡 Offsets and alignment |

## 3. Routing

Read first. [DFX](../../../docs/knowledge/DFX-Subsystem.md).

## 4. Constraints

Keep section offsets, alignment, sizes and string/symbol references consistent. Verify file ownership and failed writes along the existing output path.

## 5. Verification

Build the runtime and inspect a representative generated dump with an ELF reader. Check section ranges and symbol resolution for the affected architecture.
