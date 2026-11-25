# ecmascript/interpreter

Bytecode interpreter that executes ArkCompiler ABC instructions through a register-based virtual machine.
Primary Language: C++

## Design

The interpreter provides execution of ark bytecode with C++ implementation:

```
  ABC bytecode (.abc file)
       │
       ▼
  EcmaInterpreter::Execute()
       │
       └──▶ C++ interpreter (interpreter-inl.cpp)
             ├── switch/case dispatch
             ├── Bytecode handler functions
             ├── Type checks and guards
             └── Runtime calls (fast_runtime_stub/slow_runtime_stub)
```

**Key abstractions:**
- **EcmaInterpreter** — Main interpreter entry point and execution control
- **FrameHandler** — Stack frame management, argument passing, and return value handling
- **EcmaRuntimeCallInfo** — Execution context containing VM, arguments, and return slot
- **FastRuntimeStub** — Fast-path runtime call declarations
- **SlowRuntimeStub** — Slow-path runtime call implementations

**Assembly interpreter integration:**
- `interpreter_assembly.cpp` provides assembly interpreter call entry points and debugging utilities
- Actual assembly interpreter implementation is located in `ecmascript/compiler/interpreter_stub.cpp`
- `templates/` contains assembly template files for bytecode handler generation

## Reference Source
- [interpreter.h](interpreter.h), `EcmaInterpreter::Execute` - Interpreter interface
- `arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.h` - C++ interpreter implementation

## Key Files

| File | Role |
|------|------|
| `interpreter.h/.cpp` | Main interpreter entry point |
| `interpreter-inl.h/.cpp` | C++ interpreter implementation with switch/case dispatch |
| `interpreter_assembly.h/.cpp` | Assembly interpreter call entry and debugging utilities |
| `frame_handler.h/.cpp` | Stack frame and call management |
| `fast_runtime_stub.h` | Fast runtime call declarations |
| `slow_runtime_stub.h/.cpp` | Slow runtime call implementations |
| `templates/` | Assembly template files for bytecode handler generation |

## Building

Standalone repo (from repo root `../../`):
```bash
python ark.py x64.debug
```

Full OHOS tree:
```bash
./build.sh --product-name <product-name> --build-target ark_js_host_linux_tools_packages
```

## Tests

Standalone repo (from repo root `../../`):
```bash
python ark.py x64.debug unittest   # includes interpreter tests
```

Full OHOS tree:
```bash
./build.sh --product-name <product-name> --build-target arkcompiler/ets_runtime/ecmascript/tests:host_unittest
```

## Boundaries

- Assembly interpreter code is in `ecmascript/compiler/interpreter_stub.cpp`; this directory only provides call entry points.
- C++ interpreter and assembly interpreter must maintain consistent semantics.
- Hotness counter updates trigger JIT compilation; ensure correct threshold management.
- Keep bytecode semantics aligned between the C++ and IR/assembly interpreters. Check the equivalent handler in [interpreter_stub.cpp](../compiler/interpreter_stub.cpp) when changing the C++ path, and the C++ handler when changing the IR path.
- Check GC-visible stack arguments on CallRuntime and SlowRuntimeStub paths, which may trigger GC. See the [stubs guide](../stubs/AGENTS.md).
