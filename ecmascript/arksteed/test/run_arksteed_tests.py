#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright (c) 2026 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Test case runner for the ArkSteed JIT compiler.

Collects test cases by traversing the directory tree (each leaf directory
holds exactly one JS/TS source plus one expected_output.txt), compiles the
sources with es2abc, runs them under ark_js_vm with the compiler log
unconditionally enabled, and verifies:

  * stdout (with log lines filtered out) matches expected_output.txt;
  * the `//!` annotations (COMPILE_MODE, PARAMS, the IR constraints HAS,
    HAS_NOT, COUNT, COUNT_GE, COUNT_LE, etc., BYTECODE_COUNT, and
    CODEGEN_COUNT) hold on the CFG dumps, abc disassembly, or generated-code
    comments.

All generated artifacts (abc, disassembly, stdout/stderr captures) are
written to a fresh /tmp/arksteed-<mode>-<timestamp>/ directory whose tree
mirrors this test directory; nothing is written inside the source tree.

Exit status is 0 iff every selected case passes.
"""

import argparse
from builtins import str
import collections
import difflib
from fnmatch import fnmatchcase
import hashlib
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import time
from abc import ABC, abstractmethod
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import ClassVar, Dict, List, NamedTuple, Optional, Tuple

SCRIPT_DIR = Path(__file__).resolve().parent
TEST_DIR = SCRIPT_DIR
ARK_ROOT = SCRIPT_DIR.parents[4]
OPCODE_LIST_HEADER = SCRIPT_DIR.parent / "arksteed_opcode_list.h"

IS_MACOS = platform.system().lower() == "darwin"
LIB_PATH_ENV_VAR = "DYLD_LIBRARY_PATH" if IS_MACOS else "LD_LIBRARY_PATH"

BASE_ARGS = [
    "--compiler-enable-jit=true",
    "--compiler-enable-litecg=true",
    "--enable-force-gc=false",
    "--open-ark-tools=true",
]

ARKSTEED_GN_ARG = "ets_runtime_enable_ark_steed=true"
COMPILE_TIMEOUT = 30  # es2abc is always a host binary
DISASM_TIMEOUT = 30
DISASM_TIMEOUT_ARM64 = 30
EXECUTION_TIMEOUT = 300
QEMU_TIMEOUT_FACTOR = 10

SOURCE_SUFFIXES = (".ts", ".js", ".cjs")
EXPECTED_OUTPUT_NAME = "expected_output.txt"

# COMPILE_MODE values and the es2abc flag each mode maps to. "module" is the
# default; "script" compiles the source as a global script (no module wrapper),
# which is what makes top-level `var`/function bindings emit LDGLOBALVAR /
# STGLOBALVAR instead of module-local ldlexvar / stlexvar.
COMPILE_MODES = ("module", "script", "commonjs")
COMPILE_MODE_FLAGS = {
    "module": ["--module"],
    "script": [],
    "commonjs": ["--commonjs"],
}

CRASH_SIGNALS = {6: "SIGABRT", 11: "SIGSEGV"}
CRASH_RETURN_CODES = set(range(128, 128 + 32))

# A method may be JIT-compiled more than once per run (recompilation after
# deopt, OSR, ...); every compilation emits its own section starting with
# `======== ArkSteedCompilerTask: Starts compiling: <name> ========`
# (see ArkSteedCompilerTask::DebugLogOnCompilationStart). The name may be
# empty for anonymous functions. Counts for a method are aggregated over all
# of its compilations.
COMPILE_START_RE = re.compile(
    r"^\[compiler\] ======== ArkSteedCompilerTask: Starts compiling: ?(\S*) ========",
    re.MULTILINE,
)
# The execution marker emitted by the E1 infrastructure: the stub logs
# `>>> Running method: <record>.<method>` into the compiler component on every
# activation of the generated code (see RuntimeStubs::PrintMethodName).
EXEC_MARKER_RE = re.compile(
    r"^\[compiler\] >>> Running method: (.*?)\s*$",
    re.MULTILINE,
)

# Deopt events are traced into the compiler component by
# `--compiler-trace-deopt` (eager) and `--compiler-enable-lazy-deopt-trace`
# (lazy). Each event emits exactly one identifying line:
#   * the shared eager path, with a JS stack trace
#     (`Deoptimizier::Dump`, ecmascript/deoptimizer/deoptimizer.cpp:644-658):
#       `Deoptimize    at <fn> (<file>:<line>:<col>)`;
#   * the ArkSteed eager path, which deliberately skips the GC-capable stack
#     trace (ecmascript/arksteed/arksteed_deopt_helper.cpp:1268 ->
#     `UpdateAndDumpDeoptInfo(..., false)`):
#       `Eager deopt disables GC; skip GC-capable JS stack trace construction.`;
#   * the lazy path (ecmascript/dependent_infos.cpp:56-83):
#       `Lazy Deoptimize    at <fn> (<file>:<line>:<col>)`.
# The resume bytecode instruction that an eager event logs afterwards
# (deoptimizer.cpp:656-657) carries no deopt identity and is not a record of
# its own, so every event is counted exactly once. Compilation markers, CFG
# dumps (`v14: DeoptIfNotBoolean`), debug-level codegen comments
# (`DeoptIfHClassNotInVertex`) and method/marker names containing `Deoptimize`
# are not deopt events.
DEOPT_LAZY_PREFIX = "Lazy Deoptimize"
DEOPT_EAGER_PREFIX = "Deoptimize"
DEOPT_EAGER_NO_STACK_TRACE_PREFIX = "Eager deopt disables GC"
DEOPT_EVENT_PREFIXES = (
    DEOPT_LAZY_PREFIX,
    DEOPT_EAGER_PREFIX,
    DEOPT_EAGER_NO_STACK_TRACE_PREFIX,
)

# A traced deopt event with a JS stack trace is one multi-line log message:
# the event line, one `    at <fn> (<file>:<line>:<col>)` line per frame, and
# the newline that closes the message (the logger adds another one, which is
# why the message leaves a blank line in stdout). The runner enables those
# traces itself (DEOPT_COUNT*), so the whole message - not just its first line -
# has to be filtered out of the expected-output comparison. This filter must run
# before the plain per-component line filter.
DEOPT_TRACE_LOG_RE = re.compile(
    r"(?m)^\[compiler\] (?:Lazy )?Deoptimize\s+at [^\n]*\n(?:    at [^\n]*\n)*\n"
)


def deopt_record_spellings(candidate: str) -> List[str]:
    """Equivalent spellings of one deopt event record.

    The ArkSteed eager path suppresses the GC-capable JS stack trace, so its
    record text does not carry the `Deoptimize` marker of the other eager
    path; it also matches the normalized `Deoptimize <note>` spelling. That
    keeps the documented `Deoptimize*` / `*Deoptimize*` pattern family able
    to count every eager event, while the raw log text stays matchable.
    """
    if candidate.startswith(DEOPT_EAGER_NO_STACK_TRACE_PREFIX):
        return [candidate, f"{DEOPT_EAGER_PREFIX} {candidate}"]
    return [candidate]


GRAPH_DUMP_START = "[compiler] ===== Starts Graph Dump ====="
GRAPH_DUMP_END = "[compiler] ===== Finishes Graph Dump ====="

# ark_disasm renders function bodies as `.function ... {`, followed by
# indented instruction lines and a closing `}`. Restrict bytecode matching to
# those bodies so records and annotations cannot be mistaken for instructions.
DISASM_FUNCTION_START_RE = re.compile(r"^\.function\b.*\{\s*$")
DISASM_FUNCTION_NAME_RE = re.compile(r"^\.function\s+\S+\s+([^\s(]*)\(")
DISASM_INSTRUCTION_RE = re.compile(
    r"^\s+([A-Za-z_][A-Za-z0-9_.]*)(?:\s+(.*?\S))?\s*$"
)
CODEGEN_ASM_START = "[compiler] ========== ArkSteed After Codegen ASM =========="
CODEGEN_ASM_END = "[compiler] ================================================"
ARKSTEED_COMMENT_RE = re.compile(r"^\[compiler\]\s+\[ArkSteed\]\s*(.*?)\s*$")


def iter_graph_dumps(stdout: str):
    """Yield (method_name, dump_text) for every CFG dump in the log.

    The dump delimiters (not the `Starts compiling` markers) delimit one
    graph: the attribution line can be missing for anonymous functions, and
    the per-graph vN numbering restarts with every dump - merging two dumps
    makes equal labels overwrite each other.
    """
    position = 0
    while True:
        start = stdout.find(GRAPH_DUMP_START, position)
        if start == -1:
            return
        end = stdout.find(GRAPH_DUMP_END, start)
        end = len(stdout) if end == -1 else end + len(GRAPH_DUMP_END)
        method = "(unknown)"
        for match in COMPILE_START_RE.finditer(stdout, 0, start):
            method = match.group(1) or "(anonymous)"
        yield method, stdout[start:end]
        position = end


def iter_compilation_sections(stdout: str):
    """Yield (method_name, section) for each ArkSteed compilation task."""

    starts = list(COMPILE_START_RE.finditer(stdout))
    for index, start in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(stdout)
        yield start.group(1) or "(anonymous)", stdout[start.end():end]


# Vertex lines look like `<label>:  <Opcode> [...] (inputs) ; extras` where the
# label is `v<id>`, `n<label>` or `v?` (see Vertex::Dump). Vertices inside
# nested blocks are prefixed with box-drawing tree characters (`││  v43:`),
# so the label may be preceded by any run of non-word decoration characters.
VERTEX_LINE_RE = re.compile(r"^[\W_]*(v\?|v\d+|n\S+):\s+([A-Za-z_]\w*)")
ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-9;]*m")

ANSI_GREEN = "\033[32m"
ANSI_RED = "\033[31m"
ANSI_RESET = "\033[0m"

# `//!` annotations. IR constraints are only valid inside a METHOD scope;
# see the Constraint class hierarchy below for their grammar and semantics.
# Constraints on these opcodes must name the target stub, e.g.
# `//! HAS_NOT CallCommonStub GetPropertyByName` (wildcards allowed); other
# stubs of the same opcode are irrelevant to such a constraint.
STUB_CALL_OPCODES = ("CallCommonStub", "CallRuntime")

# Options owned by the runner (logging / dump infrastructure and environment
# paths). Setting them via `//! PARAMS` is a configuration error: overriding
# any of these could silently disable the compiler log, the CFG dumps, the
# deopt traces and every check built on top of them.
RUNNER_MANAGED_OPTIONS = (
    "--log-level",
    "--log-components",
    "--compiler-arksteed-print-graph",
    "--compiler-arksteed-print-method-name",
    "--compiler-arksteed-print-code",
    "--compiler-arksteed-enable-code-comment",
    "--compiler-arksteed-print-with-colors",
    "--stub-file",
    "--icu-data-path",
    # Deopt tracing is what `DEOPT_COUNT`/`DEOPT_COUNT_GE` counts; `PARAMS`
    # must not be able to disable the observation these annotations assert.
    "--compiler-trace-deopt",
    "--compiler-enable-lazy-deopt-trace",
)


# ---------------------------------------------------------------------------
# Build configuration and toolchain locations
# ---------------------------------------------------------------------------


HOST_MACHINE_TO_PLATFORM = {
    "x86_64": "x64",
    "amd64": "x64",
    "aarch64": "arm64",
    "arm64": "arm64",
}

HOST_PLATFORM = HOST_MACHINE_TO_PLATFORM.get(platform.machine().lower())

# Where prebuilts_download lands gn/ninja per host (see
# build/prebuilts_download/prebuilts_download_config.json); mirrors ark.py.
HOST_BUILD_TOOLS_DIR = {
    "x64": "linux-x86",
    "arm64": "linux-aarch64",
}


def detect_host_platform() -> str:
    if HOST_PLATFORM is None:
        print(f"Error: unsupported host machine '{platform.machine()}' "
              f"(supported: {', '.join(sorted(HOST_MACHINE_TO_PLATFORM))}). "
              "Pass -p/--platform explicitly.", file=sys.stderr)
        sys.exit(2)
    return HOST_PLATFORM


@dataclass(frozen=True)
class BuildConfig:
    mode: str
    platform: str
    host: Optional[str] = HOST_PLATFORM

    @property
    def _out_prefix(self) -> str:
        return f"out/{self.platform}.{self.mode}"

    def _out_path(self, *parts: str) -> Path:
        return ARK_ROOT.joinpath(self._out_prefix, *parts)

    @property
    def is_cross_build(self) -> bool:
        """True when the target runs on a different machine than the host."""
        return self.platform != self.host

    @property
    def host_toolchain_prefix(self) -> str:
        """GN host-toolchain output label of the es2abc build directory."""
        return "clang_arm64" if self.host == "arm64" else "clang_x64"

    @property
    def ninja(self) -> Path:
        if IS_MACOS:
            return ARK_ROOT / "prebuilts/build-tools/darwin-arm64/bin/ninja"
        dirname = HOST_BUILD_TOOLS_DIR.get(self.host or "x64")
        return ARK_ROOT / "prebuilts/build-tools" / dirname / "bin/ninja"

    @property
    def ark_js_vm(self) -> Path:
        return self._out_path("arkcompiler", "ets_runtime", "ark_js_vm")

    @property
    def ark_disasm(self) -> Path:
        return self._out_path("arkcompiler", "runtime_core", "ark_disasm")

    @property
    def es2abc(self) -> Path:
        if not self.is_cross_build:
            # The host-toolchain es2abc lands in the plain output directory.
            return self._out_path("arkcompiler", "ets_frontend", "es2abc")
        return self._out_path(self.host_toolchain_prefix,
                              "arkcompiler", "ets_frontend", "es2abc")

    @property
    def stub_file(self) -> Optional[Path]:
        # On Linux the path compiled into STUB_AN_FILE resolves inside the
        # build tree, so the flag is only needed on macOS.
        if not IS_MACOS:
            return None
        return self._out_path("gen", "arkcompiler", "ets_runtime", "stub.an")

    @property
    def icu_data_path(self) -> Optional[Path]:
        # localeCompare/Intl need the ICU data; without it the VM throws
        # "RangeError: invalid collation" on locale-sensitive builtins.
        path = ARK_ROOT / "third_party/icu/ohos_icu4j/data"
        return path if path.is_dir() else None

    @property
    def qemu_ld_prefix(self) -> Optional[Path]:
        if not self.is_cross_build or self.platform != "arm64":
            return None
        return self._out_path("common", "common", "libc")

    @property
    def command_prefix(self) -> List[str]:
        # Native execution when the target matches the host machine.
        if self.qemu_ld_prefix is None:
            return []
        qemu = shutil.which("qemu-aarch64-static") or shutil.which("qemu-aarch64")
        return [qemu, "-L", str(self.qemu_ld_prefix)]

    @property
    def lib_paths(self) -> List[Path]:
        paths = [
            self._out_path("arkcompiler", "ets_runtime"),
            self._out_path("thirdparty", "icu"),
            self._out_path("thirdparty", "zlib"),
            self._out_path("thirdparty", "bounds_checking_function"),
        ]
        if self.platform == "arm64":
            paths.extend(
                [
                    self._out_path("arkcompiler", "runtime_core"),
                    self._out_path("arkcompiler", "toolchain"),
                    self._out_path("thirdparty", "libuv"),
                ]
            )
        elif IS_MACOS:
            paths.extend(
                [
                    self._out_path("thirdparty", "libuv"),
                    ARK_ROOT / "prebuilts/clang/ohos/darwin-arm64/llvm/lib",
                ]
            )
        else:
            paths.extend(
                [
                    self._out_path("resourceschedule", "frame_aware_sched"),
                    self._out_path("hiviewdfx", "hilog"),
                    ARK_ROOT / "prebuilts/clang/ohos/linux-x86_64/llvm/lib",
                    self._out_path("hmosbundlemanager", "zlib_override"),
                ]
            )
        return paths


def build_ark(cfg: BuildConfig, skip_stub: bool) -> bool:
    """Run `python3 ark.py <platform>.<mode>` with the ArkSteed GN args."""

    target = f"{cfg.platform}.{cfg.mode}"
    print(f"Building ArkSteed ({target})...")

    cmd = ["python3", "ark.py", target]
    gn_args = [ARKSTEED_GN_ARG]
    if cfg.platform == "arm64" and cfg.is_cross_build:
        gn_args.append("run_with_qemu=true")
    if skip_stub:
        gn_args.append("skip_gen_stub=true")
    cmd.append(f"--gn-args={' '.join(gn_args)}")

    try:
        result = subprocess.run(cmd, cwd=ARK_ROOT)
        if result.returncode != 0:
            print(f"Build failed with return code {result.returncode}", file=sys.stderr)
            return False
        if cfg.is_cross_build:
            # ark.py only builds the target-toolchain es2abc; the host one
            # (used to compile the test sources) needs an explicit target.
            result = subprocess.run(
                [str(cfg.ninja), "-C", f"out/{target}",
                 f"{cfg.host_toolchain_prefix}/arkcompiler/ets_frontend/es2abc"],
                cwd=ARK_ROOT,
            )
            if result.returncode != 0:
                print("Host es2abc build failed", file=sys.stderr)
                return False
        print("Build successful")
        return True
    except Exception as e:  # noqa: BLE001 - report any build-system failure
        print(f"Build exception: {e}", file=sys.stderr)
        return False


def check_arksteed_gn_args(cfg: BuildConfig) -> bool:
    """Ensure the selected build directory was generated with ArkSteed enabled."""

    args_gn = ARK_ROOT / cfg._out_prefix / "args.gn"
    if not args_gn.exists():
        print(f"Error: args.gn does not exist: {args_gn}", file=sys.stderr)
        return False
    content = args_gn.read_text(encoding="utf-8")
    if not re.search(r"^\s*ets_runtime_enable_ark_steed\s*=\s*false\s*(?:#.*)?$", content, re.MULTILINE):
        return True
    print(
        f"Error: ArkSteed GN option is not enabled in {args_gn}. "
        f"Please rebuild, or run:\n"
        f"  python3 ark.py {cfg.platform}.{cfg.mode} --gn-args={ARKSTEED_GN_ARG}",
        file=sys.stderr,
    )
    return False


# ---------------------------------------------------------------------------
# Test case discovery and `//!` annotation parsing
# ---------------------------------------------------------------------------


# Call vertices carry their target stub as a bare token among the extras
# (e.g. `COStub_GetPropertyByName`, `RTStub_NumberToString`); the COStub_ /
# RTStub_ prefixes are distinctive enough to anchor the match regardless of
# the extras separator style (semicolon- or space-separated).
STUB_ANNOTATION_RE = re.compile(r"(?:^|[\s;])((?:COStub_|RTStub_)[A-Za-z_0-9]+)")


class MethodCounts(NamedTuple):
    opcodes: collections.Counter               # opcode -> count
    stubs: collections.Counter                 # (opcode, stub name) -> count


class ConstraintError(Exception):
    """Raised when a `//!` constraint annotation cannot be parsed."""


@dataclass(frozen=True)
class BytecodeCountConstraint:
    """Exact count of a mnemonic and optional operand pattern in ark_disasm."""

    mnemonic: str
    operands: Optional[str]
    count: int

    @classmethod
    def from_tokens(cls, tokens: List[str]) -> "BytecodeCountConstraint":
        if len(tokens) < 3 or not tokens[-1].isdigit():
            raise ConstraintError(
                "'BYTECODE_COUNT' expects <mnemonic> [<operand-pattern>] <n>"
            )
        mnemonic = tokens[1]
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.]*", mnemonic) is None:
            raise ConstraintError(f"invalid bytecode mnemonic '{mnemonic}'")
        # Annotation tokenization has already normalized whitespace. Match the
        # same normalized representation produced by collect_bytecode_instructions.
        operands = " ".join(tokens[2:-1]) or None
        return cls(mnemonic, operands, int(tokens[-1]))

    def label(self) -> str:
        operands = f" {self.operands}" if self.operands is not None else ""
        return f"BYTECODE_COUNT {self.mnemonic}{operands} == {self.count}"

    def annotation_text(self) -> str:
        """The annotation body that re-parses into an equal constraint."""
        operands = f" {self.operands}" if self.operands is not None else ""
        return f"BYTECODE_COUNT {self.mnemonic}{operands} {self.count}"

    def actual(self, instructions: List[Tuple[str, str]]) -> int:
        return sum(
            1 for mnemonic, operands in instructions
            if mnemonic == self.mnemonic
            and (self.operands is None or fnmatchcase(operands, self.operands))
        )

    def violated(self, instructions: List[Tuple[str, str]]) -> Optional[str]:
        actual = self.actual(instructions)
        if actual == self.count:
            return None
        return f'"{self.label()}" violated, found {actual}'


@dataclass(frozen=True)
class CodegenCountConstraint:
    """Exact count of a comment emitted with generated ArkSteed code."""

    pattern: str
    count: int

    @classmethod
    def from_tokens(cls, tokens: List[str]) -> "CodegenCountConstraint":
        if len(tokens) < 3 or not tokens[-1].isdigit():
            raise ConstraintError(
                "'CODEGEN_COUNT' expects <comment-pattern> <n>"
            )
        return cls(" ".join(tokens[1:-1]), int(tokens[-1]))

    def label(self) -> str:
        return f"CODEGEN_COUNT {self.pattern} == {self.count}"

    def annotation_text(self) -> str:
        """The annotation body that re-parses into an equal constraint."""
        return f"CODEGEN_COUNT {self.pattern} {self.count}"

    def actual(self, comments: List[str]) -> int:
        return sum(1 for comment in comments if fnmatchcase(comment, self.pattern))

    def violated(self, comments: List[str]) -> Optional[str]:
        actual = self.actual(comments)
        if actual == self.count:
            return None
        return f'"{self.label()}" violated, found {actual}'


@dataclass(frozen=True)
class Constraint(ABC):
    """Base class of the `//!` IR constraints.

    Each subclass owns the constraint's whole lifecycle - annotation grammar
    (from_tokens), matching against the CFG dumps (actual) and the verdict
    (violated, label) - so adding a constraint kind is one new class instead
    of edits scattered over the runner.
    """

    opcode: str
    # Stub-name pattern (wildcards allowed) for CallCommonStub / CallRuntime
    # constraints; None means "any node of the opcode".
    stub: Optional[str]
    KEYWORD: ClassVar[str]

    @staticmethod
    def _stub_matches(name: str, pattern: str) -> bool:
        """A stub pattern matches the dumped full name (`COStub_*` /
        `RTStub_*`) or its prefix-stripped form, so `GetPropertyByName`,
        `Get*ByName` and `COStub_GetPropertyByName` all work."""
        stripped = re.sub(r"^(?:COStub_|RTStub_)", "", name)
        return fnmatchcase(name, pattern) or fnmatchcase(stripped, pattern)

    @classmethod
    def takes_count(cls) -> bool:
        return False

    @classmethod
    def _grammar(cls) -> str:
        tail = "<opcode> [<stub-name>]"
        return f"{tail} <n>" if cls.takes_count() else tail

    @classmethod
    def from_tokens(cls, tokens: List[str], valid_opcodes: Optional[set]) -> "Constraint":
        """Parse `<opcode> [<stub-name>] [<n>]` tail tokens into a constraint,
        raising ConstraintError with a specific message on any mismatch."""
        keyword, counting = tokens[0], cls.takes_count()
        rest = tokens[1:]
        number: Optional[int] = None
        if counting:
            if not rest or not rest[-1].isdigit():
                raise ConstraintError(f"'{keyword}' expects {cls._grammar()}")
            number = int(rest[-1])
            rest = rest[:-1]
        if not rest:
            raise ConstraintError(f"'{keyword}' expects {cls._grammar()}")
        opcode, extra = rest[0], rest[1:]
        if valid_opcodes is not None and opcode not in valid_opcodes:
            raise ConstraintError(
                f"unknown IR opcode '{opcode}' (not in {OPCODE_LIST_HEADER.name})")
        stub: Optional[str] = None
        if opcode in STUB_CALL_OPCODES:
            if len(extra) != 1 or extra[0].isdigit():
                raise ConstraintError(
                    f"a constraint on {opcode} requires a stub name pattern "
                    "(e.g. 'GetPropertyByName', wildcards allowed)")
            stub = extra[0]
        elif extra:
            raise ConstraintError(
                f"'{opcode}' does not take a stub name; unexpected token '{extra[0]}'")
        fields: Dict[str, object] = {"opcode": opcode, "stub": stub}
        if counting:
            fields["count"] = number
        return cls(**fields)  # type: ignore[arg-type]

    def actual(self, counts: MethodCounts) -> int:
        """Number of matching nodes across the CFG dumps of one method."""
        if self.stub is None:
            return counts.opcodes.get(self.opcode, 0)
        return sum(number for (opcode, name), number in counts.stubs.items()
                   if opcode == self.opcode and self._stub_matches(name, self.stub))

    def label(self) -> str:
        """Human-readable form used in verdicts and verbose dumps."""
        tail = f" {self.SYMBOL} {self.count}" if self.takes_count() else ""
        return f"{self.KEYWORD} {self.opcode}" \
               + (f" {self.stub}" if self.stub else "") + tail

    def __str__(self) -> str:
        return self.label()

    def annotation_text(self) -> str:
        """The annotation body that re-parses into an equal constraint."""
        tail = f" {self.count}" if self.takes_count() else ""
        return f"{self.KEYWORD} {self.opcode}" \
               + (f" {self.stub}" if self.stub else "") + tail

    @abstractmethod
    def violated(self, counts: MethodCounts) -> Optional[str]:
        """Error text when the dumps violate this constraint, else None."""


@dataclass(frozen=True)
class HasConstraint(Constraint):
    KEYWORD = "HAS"

    def violated(self, counts: MethodCounts) -> Optional[str]:
        actual = self.actual(counts)
        if actual >= 1:
            return None
        return f"\"{self.label()}\" violated, found {actual}"


@dataclass(frozen=True)
class HasNotConstraint(Constraint):
    KEYWORD = "HAS_NOT"

    def violated(self, counts: MethodCounts) -> Optional[str]:
        actual = self.actual(counts)
        if actual == 0:
            return None
        return f"\"{self.label()}\" violated, found {actual}"


@dataclass(frozen=True)
class CountingConstraint(Constraint):
    count: int
    SYMBOL: ClassVar[str]

    @classmethod
    def takes_count(cls) -> bool:
        return True

    @abstractmethod
    def _satisfied(self, actual: int) -> bool:
        ...

    def violated(self, counts: MethodCounts) -> Optional[str]:
        actual = self.actual(counts)
        if self._satisfied(actual):
            return None
        return f"\"{self.label()}\" violated, found {actual}"


@dataclass(frozen=True)
class CountConstraint(CountingConstraint):
    KEYWORD = "COUNT"
    SYMBOL = "=="

    def _satisfied(self, actual: int) -> bool:
        return actual == self.count


@dataclass(frozen=True)
class CountGeConstraint(CountingConstraint):
    KEYWORD = "COUNT_GE"
    SYMBOL = ">="

    def _satisfied(self, actual: int) -> bool:
        return actual >= self.count


@dataclass(frozen=True)
class CountLeConstraint(CountingConstraint):
    KEYWORD = "COUNT_LE"
    SYMBOL = "<="

    def _satisfied(self, actual: int) -> bool:
        return actual <= self.count


CONSTRAINT_BY_KEYWORD = {cls.KEYWORD: cls for cls in
                         (HasConstraint, HasNotConstraint, CountConstraint,
                          CountGeConstraint, CountLeConstraint)}


# ---------------------------------------------------------------------------
# Evidence annotations (case-scoped counts over captured stdout)
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class EvidenceConstraint(ABC):
    """Base class of the case-scoped evidence annotations.

    Each annotation counts occurrences in the already-captured stdout whose
    payload matches a shell-style wildcard pattern. The payload is the useful
    part of a log line after the `[<component>] ` prefix (for `COMPILE_COUNT`
    and `EXEC_COUNT` the payload is the method name / `<record>.<method>`
    extracted from the marker line). `n=0` is valid and asserts the absence of
    matching events.
    """

    pattern: str
    count: int
    KEYWORD: ClassVar[str]
    SYMBOL: ClassVar[str]
    EVIDENCE_KEY: ClassVar[str]

    @classmethod
    def from_tokens(cls, tokens: List[str]) -> "EvidenceConstraint":
        if len(tokens) < 3 or not tokens[-1].isdigit():
            raise ConstraintError(f"'{cls.KEYWORD}' expects <pattern> <n>")
        pattern = " ".join(tokens[1:-1])
        if not pattern:
            raise ConstraintError(f"'{cls.KEYWORD}' expects a non-empty <pattern>")
        return cls(pattern, int(tokens[-1]))

    def label(self) -> str:
        return f"{self.KEYWORD} {self.pattern} {self.SYMBOL} {self.count}"

    def annotation_text(self) -> str:
        """The annotation body that re-parses into an equal constraint."""
        return f"{self.KEYWORD} {self.pattern} {self.count}"

    def spellings(self, candidate: str) -> List[str]:
        """Equivalent spellings of one evidence record.

        A record is counted once when the pattern matches any of its
        spellings, so an equivalent spelling never inflates a count.
        """
        return [candidate]

    def actual(self, evidence: Dict[str, List[str]]) -> int:
        return sum(
            1 for candidate in evidence.get(self.EVIDENCE_KEY, [])
            if any(fnmatchcase(spelling, self.pattern)
                   for spelling in self.spellings(candidate))
        )

    @abstractmethod
    def _satisfied(self, actual: int) -> bool:
        ...

    def violated(self, evidence: Dict[str, List[str]]) -> Optional[str]:
        actual = self.actual(evidence)
        if self._satisfied(actual):
            return None
        return f"\"{self.label()}\" violated, found {actual}"


@dataclass(frozen=True)
class CompileCountConstraint(EvidenceConstraint):
    KEYWORD = "COMPILE_COUNT"
    SYMBOL = "=="
    EVIDENCE_KEY = "compile"

    def _satisfied(self, actual: int) -> bool:
        return actual == self.count


@dataclass(frozen=True)
class CompileCountGeConstraint(EvidenceConstraint):
    KEYWORD = "COMPILE_COUNT_GE"
    SYMBOL = ">="
    EVIDENCE_KEY = "compile"

    def _satisfied(self, actual: int) -> bool:
        return actual >= self.count


@dataclass(frozen=True)
class ExecCountConstraint(EvidenceConstraint):
    KEYWORD = "EXEC_COUNT"
    SYMBOL = "=="
    EVIDENCE_KEY = "exec"

    def _satisfied(self, actual: int) -> bool:
        return actual == self.count


@dataclass(frozen=True)
class ExecCountGeConstraint(EvidenceConstraint):
    KEYWORD = "EXEC_COUNT_GE"
    SYMBOL = ">="
    EVIDENCE_KEY = "exec"

    def _satisfied(self, actual: int) -> bool:
        return actual >= self.count


class DeoptEvidenceConstraint(EvidenceConstraint):
    """Base of the deopt annotations (records carry equivalent spellings)."""

    def spellings(self, candidate: str) -> List[str]:
        return deopt_record_spellings(candidate)


@dataclass(frozen=True)
class DeoptCountConstraint(DeoptEvidenceConstraint):
    KEYWORD = "DEOPT_COUNT"
    SYMBOL = "=="
    EVIDENCE_KEY = "deopt"

    def _satisfied(self, actual: int) -> bool:
        return actual == self.count


@dataclass(frozen=True)
class DeoptCountGeConstraint(DeoptEvidenceConstraint):
    KEYWORD = "DEOPT_COUNT_GE"
    SYMBOL = ">="
    EVIDENCE_KEY = "deopt"

    def _satisfied(self, actual: int) -> bool:
        return actual >= self.count


@dataclass(frozen=True)
class GcCountGeConstraint(EvidenceConstraint):
    KEYWORD = "GC_COUNT_GE"
    SYMBOL = ">="
    EVIDENCE_KEY = "gc"

    def _satisfied(self, actual: int) -> bool:
        return actual >= self.count


EVIDENCE_CONSTRAINT_BY_KEYWORD = {cls.KEYWORD: cls for cls in
                                  (CompileCountConstraint, CompileCountGeConstraint,
                                   ExecCountConstraint, ExecCountGeConstraint,
                                   DeoptCountConstraint, DeoptCountGeConstraint,
                                   GcCountGeConstraint)}


@dataclass(frozen=True)
class AnnotationRecord:
    """One parsed `//!` annotation together with its source location.

    The sensitivity-control model works at the level of individual source
    lines: it must be able to name the mutated line, re-parse the mutated
    annotation with the runner's own grammar and report
    `line <N>: <original> -> <mutated>`. `parse_annotations` stores one record
    per successfully parsed assertion, in source order.
    """

    keyword: str
    line: int                      # 1-based line number in the case source
    text: str                      # original line, stripped
    indent: str                    # leading whitespace of the original line
    target: object                 # parsed constraint (IR / bytecode / codegen / evidence)
    method: Optional[str] = None   # METHOD scope for method-scoped assertions


# Case classes the runner can select. The embedded class is the leaf-directory
# layout of this test tree (one JS/TS source plus one `expected_output.txt`)
# and is the only class `discover_cases` selects on this revision; the corpus
# class (`--corpus`, its own oracle and namespace, ISSUE-0047) is a separate
# selection. The sensitivity-control mode is defined for the embedded class
# only (design S1).
CASE_CLASS_EMBEDDED = "embedded"
CASE_CLASS_CORPUS = "corpus"


@dataclass
class TestCase:
    rel: str  # path relative to TEST_DIR, e.g. "example/test_1"
    source: Path
    expected: Path
    # Extra module sources compiled as sibling .abc files next to the entry
    # abc, so the runtime can resolve `import ... from './<stem>'` records
    # across files (e.g. shared/sendable module tests). Empty for the common
    # single-file (self-import via --merge-abc) cases.
    siblings: List[Path] = field(default_factory=list)
    compile_mode: str = "module"
    params: List[str] = field(default_factory=list)
    constraints: Dict[str, List[Constraint]] = field(default_factory=dict)
    bytecode_constraints: Dict[str, List[BytecodeCountConstraint]] = field(
        default_factory=dict
    )
    codegen_constraints: Dict[str, List[CodegenCountConstraint]] = field(default_factory=dict)
    evidence_constraints: List[EvidenceConstraint] = field(default_factory=list)
    config_errors: List[str] = field(default_factory=list)
    # Source-ordered records of every parsed assertion (sensitivity controls).
    annotations: List[AnnotationRecord] = field(default_factory=list)
    # Selected case class; only the embedded class is controlled (S1).
    case_class: str = CASE_CLASS_EMBEDDED

    @property
    def entry(self) -> str:
        return self.source.stem


@dataclass
class CaseResult:
    rel: str
    reasons: List[str] = field(default_factory=list)
    duration: float = 0.0
    repro_cmd: Optional[str] = None
    # Per-case runner messages (verbose cmd line, disasm warnings); printed
    # together with the verdict so parallel runs stay grouped per case.
    log: List[str] = field(default_factory=list)
    # Raw captures of the case run; the control stage derives its base
    # observation from them (S2). The disassembly is kept only when the
    # control mode is on (it is read from the artifact file).
    stdout: str = ""
    disassembly: Optional[str] = None

    @property
    def passed(self) -> bool:
        return not self.reasons


def _pattern_matches(rel: str, patterns: List[str]) -> bool:
    """Whether any pattern selects `rel`.

    A pattern matches when it matches the case path itself or any of its
    ancestor directory paths, with shell-style wildcards. For wildcard-free
    patterns this is exactly the previous directory-prefix semantics
    (`deopt` selects `deopt/...` but not a sibling `deopt_lazy_x`).
    """
    ancestors = rel.split("/")
    paths = ["/".join(ancestors[:i]) for i in range(len(ancestors), 0, -1)]
    for pattern in patterns:
        pattern = pattern.rstrip("/")
        if pattern and any(fnmatchcase(path, pattern) for path in paths):
            return True
    return False


def discover_cases(
    includes: List[str], excludes: List[str], verbose: bool = False
) -> List[TestCase]:
    """Walk the test tree and return one TestCase per conforming leaf directory."""

    cases: List[TestCase] = []
    if not TEST_DIR.exists():
        print(f"Test directory does not exist: {TEST_DIR}", file=sys.stderr)
        return cases

    for dirpath, dirnames, filenames in os.walk(TEST_DIR):
        dirnames[:] = sorted(d for d in dirnames if d not in ("__pycache__", "excluded"))
        sources = sorted(f for f in filenames if f.endswith(SOURCE_SUFFIXES))
        if not sources and EXPECTED_OUTPUT_NAME not in filenames:
            continue
        case_dir = Path(dirpath)
        rel = case_dir.relative_to(TEST_DIR).as_posix()
        if rel == ".":
            continue
        if not sources:
            if verbose:
                print(f"warning: skipping {rel}: no source file", file=sys.stderr)
            continue
        # Single-source cases keep the historical behaviour (any file name,
        # self-imports resolved by --merge-abc). With several sources, exactly
        # one must follow the <dirname>.<ext> entry convention; the rest are
        # compiled as sibling modules.
        entry_source: Optional[Path] = None
        siblings: List[Path] = []
        if len(sources) == 1:
            entry_source = case_dir / sources[0]
        else:
            matches = [case_dir / s for s in sources if Path(s).stem == case_dir.name]
            if len(matches) != 1:
                if verbose:
                    print(f"warning: skipping {rel}: {len(sources)} source files "
                          f"but {len(matches)} match the '<dirname>.ts' entry convention",
                          file=sys.stderr)
                continue
            entry_source = matches[0]
            siblings = [case_dir / s for s in sources if case_dir / s != entry_source]
        if EXPECTED_OUTPUT_NAME not in filenames:
            if verbose:
                print(f"warning: skipping {rel}: missing {EXPECTED_OUTPUT_NAME}", file=sys.stderr)
            continue
        if includes and not _pattern_matches(rel, includes):
            continue
        if excludes and _pattern_matches(rel, excludes):
            continue
        cases.append(TestCase(rel, entry_source, case_dir / EXPECTED_OUTPUT_NAME,
                              siblings=siblings))
    return cases


def load_opcode_order() -> Optional[List[str]]:
    """IR opcode names in declaration order from arksteed_opcode_list.h.

    The order is the candidate order of the `address-substitution` control
    class, so it must be the header's own order and not a sorted copy.
    """
    try:
        text = OPCODE_LIST_HEADER.read_text(encoding="utf-8")
    except OSError as e:
        print(f"warning: cannot read {OPCODE_LIST_HEADER} ({e}); "
              "IR opcodes in annotations will not be validated", file=sys.stderr)
        return None
    return re.findall(r"\bV\((\w+)\)", text)


def load_opcode_names() -> Optional[set]:
    """Collect IR opcode names from arksteed_opcode_list.h (the authoritative list)."""
    order = load_opcode_order()
    return None if order is None else set(order)


def parse_annotations(case: TestCase, valid_opcodes: Optional[set]) -> None:
    """Fill case options and constraints; record problems in config_errors."""

    compile_mode: Optional[str] = None
    params: Optional[List[str]] = None
    current_method: Optional[str] = None
    try:
        lines = case.source.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as e:
        case.config_errors.append(f"cannot read source file: {e}")
        return

    for lineno, raw in enumerate(lines, 1):
        stripped = raw.strip()
        if not stripped.startswith("//!"):
            continue
        body = stripped[3:]
        comment_at = body.find("#")
        if comment_at != -1:
            body = body[:comment_at]
        tokens = body.split()
        if not tokens:
            continue

        def err(message: str) -> None:
            case.config_errors.append(f"line {lineno}: {message}")

        keyword = tokens[0]
        if keyword == "COMPILE_MODE":
            if compile_mode is not None:
                err("COMPILE_MODE may appear at most once")
            elif len(tokens) != 2:
                err("COMPILE_MODE expects exactly one mode")
            elif tokens[1] not in COMPILE_MODES:
                err(f"unknown COMPILE_MODE '{tokens[1]}' "
                    f"(expected one of {', '.join(COMPILE_MODES)})")
            else:
                compile_mode = tokens[1]
        elif keyword == "PARAMS":
            if params is not None:
                err("PARAMS may appear at most once")
            else:
                params = tokens[1:]
                # Log and dump infrastructure is runner-managed: overriding
                # any of it via PARAMS could silently disable the compiler
                # log, the CFG dumps and every check built on top of them.
                for name in _iter_option_names(params):
                    if name in RUNNER_MANAGED_OPTIONS:
                        err(f"'{name}' is runner-managed and "
                            "cannot be set via PARAMS")
        elif keyword == "METHOD":
            if len(tokens) != 2:
                err("METHOD expects exactly one function name")
            else:
                current_method = tokens[1]
                case.constraints.setdefault(current_method, [])
        elif keyword == "BYTECODE_COUNT":
            if current_method is None:
                err("'BYTECODE_COUNT' constraint appears before any METHOD annotation")
                continue
            try:
                constraint = BytecodeCountConstraint.from_tokens(tokens)
                case.bytecode_constraints.setdefault(current_method, []).append(constraint)
                case.annotations.append(AnnotationRecord(
                    keyword, lineno, raw.strip(), raw[:len(raw) - len(raw.lstrip())],
                    constraint, current_method))
            except ConstraintError as error:
                err(str(error))
        elif keyword == "CODEGEN_COUNT":
            if current_method is None:
                err("'CODEGEN_COUNT' constraint appears before any METHOD annotation")
                continue
            try:
                constraint = CodegenCountConstraint.from_tokens(tokens)
                case.codegen_constraints.setdefault(current_method, []).append(constraint)
                case.annotations.append(AnnotationRecord(
                    keyword, lineno, raw.strip(), raw[:len(raw) - len(raw.lstrip())],
                    constraint, current_method))
            except ConstraintError as error:
                err(str(error))
        elif keyword in CONSTRAINT_BY_KEYWORD:
            if current_method is None:
                err(f"'{keyword}' constraint appears before any METHOD annotation")
                continue
            try:
                constraint = CONSTRAINT_BY_KEYWORD[keyword].from_tokens(tokens, valid_opcodes)
                case.constraints[current_method].append(constraint)
                case.annotations.append(AnnotationRecord(
                    keyword, lineno, raw.strip(), raw[:len(raw) - len(raw.lstrip())],
                    constraint, current_method))
            except ConstraintError as error:
                err(str(error))
        elif keyword in EVIDENCE_CONSTRAINT_BY_KEYWORD:
            # Evidence annotations are case-scoped (not method-scoped) and do
            # not require a preceding METHOD annotation.
            try:
                constraint = EVIDENCE_CONSTRAINT_BY_KEYWORD[keyword].from_tokens(tokens)
                case.evidence_constraints.append(constraint)
                case.annotations.append(AnnotationRecord(
                    keyword, lineno, raw.strip(), raw[:len(raw) - len(raw.lstrip())],
                    constraint, None))
            except ConstraintError as error:
                err(str(error))
        else:
            err(f"unknown annotation keyword '{keyword}'")
    case.compile_mode = compile_mode if compile_mode is not None else "module"
    case.params = params if params is not None else []


# ---------------------------------------------------------------------------
# Compilation, disassembly and execution
# ---------------------------------------------------------------------------


def compile_case(case: TestCase, abc_path: Path, cfg: BuildConfig) -> Tuple[bool, str]:
    """Compile the source(s) to abc with es2abc in the case's COMPILE_MODE."""

    # Single-file cases use --merge-abc (self-imports resolve into the one
    # merged record). Multi-file cases compile every source as its own module
    # abc placed next to the entry abc, so the runtime resolves sibling
    # records ('./<stem>') from the artifact directory by file name.
    merge_abc = not case.siblings
    targets = [(case.source, abc_path, COMPILE_MODE_FLAGS[case.compile_mode])]
    for sib in case.siblings:
        # Sibling modules compile as ES modules; a `.cjs` sibling compiles as
        # CommonJS (its exports become by-name bindings, not index bindings).
        mode = "commonjs" if sib.suffix == ".cjs" else "module"
        targets.append((sib, abc_path.parent / f"{sib.stem}.abc",
                        COMPILE_MODE_FLAGS[mode]))
    for source, output, mode_flags in targets:
        cmd = [
            str(cfg.es2abc),
            str(source),
        ]
        if merge_abc:
            cmd.append("--merge-abc")
        cmd += ["--output", str(output)]
        cmd.extend(mode_flags)
        try:
            result = subprocess.run(
                cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                timeout=COMPILE_TIMEOUT,
            )
        except subprocess.TimeoutExpired:
            return False, "es2abc timed out"
        except OSError as e:
            return False, f"failed to run es2abc: {e}"
        if result.returncode != 0:
            tail = "\n".join((result.stderr or result.stdout or "").splitlines()[-15:])
            return False, f"es2abc exited with {result.returncode}:\n{tail}"
    return True, ""


def disassemble_case(case: TestCase, abc_path: Path, disasm_path: Path,
                     cfg: BuildConfig, log: List[str]) -> Optional[str]:
    """Dump abc disassembly, returning an error also recorded as a warning."""

    if not cfg.ark_disasm.exists():
        error = f"ark_disasm does not exist: {cfg.ark_disasm}"
        log.append(f"warning: {error}")
        return error
    cmd = cfg.command_prefix + [str(cfg.ark_disasm), str(abc_path), str(disasm_path)]
    timeout = DISASM_TIMEOUT_ARM64 if cfg.is_cross_build else DISASM_TIMEOUT
    try:
        result = subprocess.run(cmd, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        error = f"ark_disasm timed out for {case.rel}"
        log.append(f"warning: {error}")
        return error
    except OSError as e:
        error = f"ark_disasm failed to run for {case.rel}: {e}"
        log.append(f"warning: {error}")
        return error
    if result.returncode != 0:
        error = f"ark_disasm failed for {case.rel} (return code {result.returncode})"
        log.append(f"warning: {error}")
        return error
    return None


def build_repro_command(cmd: List[str], cfg: BuildConfig) -> str:
    """One copy-pastable shell line re-running the command under GDB."""

    parts = ["env", f"{LIB_PATH_ENV_VAR}={':'.join(str(p) for p in cfg.lib_paths)}"]
    if cfg.qemu_ld_prefix is not None:
        parts.append(f"QEMU_LD_PREFIX={cfg.qemu_ld_prefix}")
    parts.append("gdb")
    parts.append("--args")
    parts.extend(shlex.quote(str(token)) for token in cmd)
    return " ".join(parts)


def _option_name(token: str) -> Optional[str]:
    """Option name of a long-option token (`--opt=value` -> `--opt`), else None."""

    if token.startswith("--"):
        return token.split("=", 1)[0]
    return None


def _iter_option_names(params: List[str]):
    """Yield option names from `//! PARAMS` tokens, covering both `--opt=value`
    and the space-separated `--opt value` form."""
    index = 0
    while index < len(params):
        name = _option_name(params[index])
        if name is None:
            index += 1
            continue
        yield name
        index += 1
        if "=" not in params[index - 1] and index < len(params) \
                and not params[index].startswith("--"):
            index += 1  # skip the value of a space-separated option


def _collect_param_overrides(params: List[str]) -> set:
    """Option names set by `//! PARAMS`."""
    return set(_iter_option_names(params))


def _case_has_evidence(case: TestCase, *kinds) -> bool:
    """Whether the case carries any evidence annotation of the given kinds."""
    return any(isinstance(constraint, kinds) for constraint in case.evidence_constraints)


def _implied_log_components(case: TestCase) -> List[str]:
    """Log components implied by evidence annotations (E7).

    `GC_COUNT_GE` implies the `gc` component for this case only.
    """
    if _case_has_evidence(case, GcCountGeConstraint):
        return ["gc"]
    return []


def _needs_deopt_trace(case: TestCase) -> bool:
    """Whether the case asserts deopt evidence (`DEOPT_COUNT*`).

    Both deopt traces are then enabled for the case: the annotations count
    deopt events including lazy ones, and `PARAMS` cannot disable either
    option (both are runner-managed).
    """
    return _case_has_evidence(case, DeoptCountConstraint, DeoptCountGeConstraint)


def build_vm_command(
    case: TestCase, abc_path: Path, cfg: BuildConfig, args: argparse.Namespace,
) -> List[str]:
    """Assemble the ark_js_vm command line for one case.

    Evidence annotations imply options for this case only (E7): EXEC_COUNT
    implies the execution marker, DEOPT_COUNT implies both deopt traces, and
    GC_COUNT_GE implies the gc log component.
    """
    needs_exec_marker = _case_has_evidence(case, ExecCountConstraint, ExecCountGeConstraint)
    needs_deopt_trace = _needs_deopt_trace(case)
    components = args.log_components_value.split(":")
    for component in _implied_log_components(case):
        if component not in components:
            components.append(component)

    defaults = list(BASE_ARGS)
    if cfg.stub_file is not None:
        defaults.append(f"--stub-file={cfg.stub_file}")
    if cfg.icu_data_path is not None:
        defaults.append(f"--icu-data-path={cfg.icu_data_path}")
    defaults.append(f"--log-level={'debug' if args.log_debug else 'info'}")
    defaults.append(f"--log-components={':'.join(components)}")
    defaults.append("--compiler-arksteed-print-graph=true")
    defaults.append(
        f"--compiler-arksteed-print-method-name={'true' if needs_exec_marker else 'false'}")
    if needs_deopt_trace:
        defaults.append("--compiler-trace-deopt=true")
        defaults.append("--compiler-enable-lazy-deopt-trace=true")
    if args.print_asm_code or any(case.codegen_constraints.values()):
        defaults.append("--compiler-arksteed-print-code=true")
        defaults.append("--compiler-arksteed-enable-code-comment=true")
    if args.fully_colored:
        # ANSI escapes in the log are stripped before CFG parsing and log-line
        # filtering, so the verdicts are unaffected.
        defaults.append("--compiler-arksteed-print-with-colors=true")
    # A case parameter replaces (instead of merely overriding) the default it
    # names, so the command line never carries contradicting duplicates. The
    # options the evidence annotations imply are runner-managed and cannot be
    # named by `//! PARAMS` at all.
    overridden = _collect_param_overrides(case.params)
    defaults = [d for d in defaults if _option_name(d) not in overridden]

    cmd = cfg.command_prefix + [str(cfg.ark_js_vm)] + defaults + case.params
    cmd.extend([f"--entry-point={case.entry}", str(abc_path)])
    return cmd


def execute_abc(
    case: TestCase, abc_path: Path, cfg: BuildConfig, args: argparse.Namespace,
    log: List[str],
) -> Tuple[List[str], Optional[int], str, str, bool]:
    """Run ark_js_vm on the abc; returns (cmd, returncode, stdout, stderr, timed_out)."""

    cmd = build_vm_command(case, abc_path, cfg, args)

    env = os.environ.copy()
    env[LIB_PATH_ENV_VAR] = ":".join(str(p) for p in cfg.lib_paths)
    if cfg.qemu_ld_prefix is not None:
        env["QEMU_LD_PREFIX"] = str(cfg.qemu_ld_prefix)
    if args.verbose:
        log.append(f"cmd: {' '.join(cmd)}")

    timeout = EXECUTION_TIMEOUT
    if cfg.is_cross_build:
        timeout *= QEMU_TIMEOUT_FACTOR
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
            timeout=timeout, env=env,
        )
        return cmd, result.returncode, result.stdout or "", result.stderr or "", False
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode("utf-8", errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        return cmd, None, out, "", True
    except OSError as e:
        log.append(f"error: failed to run ark_js_vm: {e}")
        return cmd, None, "", "", False


# ---------------------------------------------------------------------------
# Verdicts: output comparison and IR constraint checking
# ---------------------------------------------------------------------------


def is_crash(returncode: Optional[int]) -> bool:
    if returncode is None:
        return False
    if returncode < 0:
        return abs(returncode) in CRASH_SIGNALS
    return returncode in CRASH_RETURN_CODES and (returncode - 128) in CRASH_SIGNALS


def crash_signal_name(returncode: Optional[int]) -> Optional[str]:
    if returncode is None:
        return None
    signal_number = abs(returncode) if returncode < 0 else returncode - 128
    return CRASH_SIGNALS.get(signal_number)


def compare_output(
    stdout: str, expected_file: Path, log_fragment_res: List[re.Pattern]
) -> Optional[str]:
    """Return None on match (or skipped comparison), otherwise a unified diff."""
    try:
        expected = expected_file.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        return f"cannot read {expected_file}: {e}"
    if not expected.strip():
        # Empty expectation means "nothing to check" (mirrors corpus convention).
        return None

    filtered = stdout
    for fragment_re in log_fragment_res:
        filtered = fragment_re.sub("", filtered)
    actual_lines = [line.rstrip() for line in filtered.splitlines()]
    expected_lines = [line.rstrip() for line in expected.splitlines()]
    if actual_lines == expected_lines:
        return None
    return "\n".join(
        difflib.unified_diff(
            expected_lines, actual_lines,
            fromfile="expected", tofile="actual", lineterm="",
        )
    )


def collect_opcode_counts(
    stdout: str, valid_opcodes: Optional[set]
) -> Dict[str, MethodCounts]:
    """Aggregate per-method IR opcode counts from the CFG dumps in the log."""
    counts: Dict[str, MethodCounts] = {}
    for method, dump in iter_graph_dumps(stdout):
        entry = counts.setdefault(
            method, MethodCounts(collections.Counter(), collections.Counter()))
        for line in dump.splitlines():
            line = ANSI_ESCAPE_RE.sub("", line)
            if line.startswith("[compiler] "):
                line = line[len("[compiler] "):]
            vertex = VERTEX_LINE_RE.match(line.strip())
            if vertex is None:
                continue
            opcode = vertex.group(2)
            if valid_opcodes is not None and opcode not in valid_opcodes:
                continue
            entry.opcodes[opcode] += 1
            stub = STUB_ANNOTATION_RE.search(line)
            if stub is not None:
                entry.stubs[(opcode, stub.group(1))] += 1
    return counts


def check_constraints(
    constraints: Dict[str, List[Constraint]],
    counts: Dict[str, MethodCounts],
) -> List[str]:
    """Verify all IR constraints against the aggregated opcode counts."""
    errors: List[str] = []
    for method, method_constraints in constraints.items():
        method_counts = counts.get(method)
        if method_counts is None:
            errors.append(
                f"method '{method}' was never JIT-compiled (no CFG dump in compiler log)"
            )
            continue
        for constraint in method_constraints:
            message = constraint.violated(method_counts)
            if message is not None:
                errors.append(f"{method}: {message}")
    return errors


def disasm_method_name(function_header: str) -> Optional[str]:
    """Extract the source-level method name from an ark_disasm function header."""

    match = DISASM_FUNCTION_NAME_RE.match(function_header)
    if match is None:
        return None
    qualified_name = match.group(1)
    if "#" in qualified_name:
        return qualified_name.rsplit("#", 1)[1]
    return qualified_name.rsplit(".", 1)[-1]


def collect_bytecode_instructions(
    disassembly: str,
) -> Dict[str, List[Tuple[str, str]]]:
    """Collect normalized bytecode instructions grouped by method name."""

    instructions: Dict[str, List[Tuple[str, str]]] = {}
    current_method: Optional[str] = None
    for line in disassembly.splitlines():
        stripped = line.strip()
        if DISASM_FUNCTION_START_RE.match(stripped):
            current_method = disasm_method_name(stripped)
            if current_method is not None:
                instructions.setdefault(current_method, [])
            continue
        if current_method is None:
            continue
        if stripped == "}":
            current_method = None
            continue
        instruction = DISASM_INSTRUCTION_RE.match(line)
        if instruction is None:
            continue
        mnemonic = instruction.group(1)
        operands = " ".join((instruction.group(2) or "").split())
        instructions[current_method].append((mnemonic, operands))
    return instructions


def check_bytecode_constraints(
    disasm_path: Path, constraints: Dict[str, List[BytecodeCountConstraint]]
) -> List[str]:
    """Verify method-scoped BYTECODE_COUNT constraints against abc disassembly."""

    if not constraints:
        return []
    try:
        disassembly = disasm_path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        return [f"cannot read abc disassembly: {error}"]
    if not any(DISASM_FUNCTION_START_RE.match(line.strip())
               for line in disassembly.splitlines()):
        return ["abc disassembly contains no function bodies "
                "(empty output or unsupported ark_disasm format)"]
    instructions = collect_bytecode_instructions(disassembly)
    errors: List[str] = []
    for method, method_constraints in constraints.items():
        method_instructions = instructions.get(method)
        if method_instructions is None:
            errors.append(f"method '{method}' does not appear in abc disassembly")
            continue
        for constraint in method_constraints:
            message = constraint.violated(method_instructions)
            if message is not None:
                errors.append(f"{method}: {message}")
    return errors


def collect_codegen_comments(stdout: str) -> Dict[str, List[str]]:
    """Collect normalized ArkSteed generated-code comments by method."""

    comments: Dict[str, List[str]] = {}
    for method, section in iter_compilation_sections(stdout):
        method_comments = comments.setdefault(method, [])
        in_asm_dump = False
        for raw_line in section.splitlines():
            line = ANSI_ESCAPE_RE.sub("", raw_line)
            if line == CODEGEN_ASM_START:
                in_asm_dump = True
                continue
            if line == CODEGEN_ASM_END:
                in_asm_dump = False
                continue
            if not in_asm_dump:
                continue
            match = ARKSTEED_COMMENT_RE.match(line)
            if match is not None:
                method_comments.append(" ".join(match.group(1).split()))
    return comments


def check_codegen_constraints(
    constraints: Dict[str, List[CodegenCountConstraint]],
    comments: Dict[str, List[str]],
) -> List[str]:
    """Verify method-scoped CODEGEN_COUNT constraints."""

    errors: List[str] = []
    for method, method_constraints in constraints.items():
        method_comments = comments.get(method)
        if method_comments is None:
            errors.append(
                f"method '{method}' was never JIT-compiled "
                "(no generated-code section in compiler log)"
            )
            continue
        for constraint in method_constraints:
            message = constraint.violated(method_comments)
            if message is not None:
                errors.append(f"{method}: {message}")
    return errors


def collect_evidence(stdout: str) -> Dict[str, List[str]]:
    """Collect case-scoped evidence payloads from captured stdout.

    Returns a dict keyed by evidence kind with the candidate strings the
    evidence patterns match against (fnmatchcase):
      * `compile`  -> method names from `Starts compiling` markers;
      * `exec`     -> `<record>.<method>` from `>>> Running method:` markers;
      * `deopt`    -> useful content of deopt-event lines only (`Deoptimize`,
                      `Lazy Deoptimize`, and the ArkSteed eager record that
                      skips the GC-capable stack trace); other compiler lines
                      (compilation markers, CFG/debug dumps, names containing
                      `Deoptimize`) are never deopt evidence;
      * `gc`       -> useful content of `[gc]` lines.
    """
    stripped = ANSI_ESCAPE_RE.sub("", stdout)
    evidence: Dict[str, List[str]] = {
        "compile": [m.group(1) or "(anonymous)" for m in COMPILE_START_RE.finditer(stripped)],
        "exec": [m.group(1).strip() for m in EXEC_MARKER_RE.finditer(stripped)],
        "deopt": [],
        "gc": [],
    }
    for line in stripped.splitlines():
        if line.startswith("[compiler] "):
            payload = line[len("[compiler] "):]
            if payload.startswith(DEOPT_EVENT_PREFIXES):
                evidence["deopt"].append(payload)
        elif line.startswith("[gc] "):
            evidence["gc"].append(line[len("[gc] "):])
    return evidence


def check_evidence_constraints(
    constraints: List[EvidenceConstraint], evidence: Dict[str, List[str]]
) -> List[str]:
    """Verify case-scoped evidence annotations against captured stdout."""
    errors: List[str] = []
    for constraint in constraints:
        message = constraint.violated(evidence)
        if message is not None:
            errors.append(message)
    return errors


# Live range checking (ported from the retired batch runner; the patterns are
# adapted to the current vertex dump format: `v<id>:` labels and the
# `→ live range: [start-end]` suffix).
LIVE_RANGE_RE = re.compile(r"(v\d+):\s+\S+.*?→\s*live range: \[(\d+)-(\d+)\]")
USE_LINE_RE = re.compile(r"(v\d+):.*?\(([^)]+)\)")
PHI_LINE_RE = re.compile(r"(v\d+):\s*Phi[^\(]*\(([^)]+)\)")
PHI_WORD_RE = re.compile(r"\bphi\b", re.IGNORECASE)
INPUT_REF_RE = re.compile(r"\bv\d+\b")


def _check_live_range_section(section: str) -> List[str]:
    """Check live range correctness within one compiler graph dump.

    1. Every value used as a Phi input must have a live range;
    2. For every use, the used value's live range must extend to (at least)
       the start of the using vertex - a value dying before its use is a bug.
    """
    # The dump may carry ANSI colors (-C); strip them before any matching,
    # otherwise the escape sequences break up the patterns mid-token.
    section = ANSI_ESCAPE_RE.sub("", section)
    live_ranges: Dict[str, Tuple[int, int]] = {}
    uses: Dict[str, List[str]] = {}
    phi_inputs: set = set()

    for match in LIVE_RANGE_RE.finditer(section):
        live_ranges[match.group(1)] = (int(match.group(2)), int(match.group(3)))

    for match in USE_LINE_RE.finditer(section):
        if PHI_WORD_RE.search(match.group(0)):
            continue  # Phi inputs are handled separately below.
        uses.setdefault(match.group(1), []).extend(INPUT_REF_RE.findall(match.group(2)))

    for match in PHI_LINE_RE.finditer(section):
        # Only an allocated Phi imposes requirements on its inputs; the
        # compiler deliberately leaves some Phis unallocated ("Skips
        # unallocated Phi" in the log), and so may their inputs be.
        if match.group(1) in live_ranges:
            phi_inputs.update(INPUT_REF_RE.findall(match.group(2)))

    errors: List[str] = []
    if not live_ranges and "live range:" in section:
        errors.append("live range info present but unparsable (log format drift?)")
        return errors

    for var in sorted(phi_inputs):
        if var not in live_ranges:
            errors.append(f"{var} is used as a Phi input but has no live range")

    for use_var, args in uses.items():
        if use_var not in live_ranges:
            continue
        use_start, _ = live_ranges[use_var]
        for arg in args:
            if arg not in live_ranges:
                continue
            arg_start, arg_end = live_ranges[arg]
            if arg_end < use_start:
                errors.append(
                    f"{arg} [{arg_start}-{arg_end}] dies before its use "
                    f"{use_var} [starting at {use_start}]"
                )
    return errors


def check_live_ranges(stdout: str) -> List[str]:
    """Check live ranges of every CFG dump; vN numbering restarts per graph,
    so each dump is checked independently."""
    errors: List[str] = []
    for method, dump in iter_graph_dumps(stdout):
        errors.extend(f"{method}: {e}" for e in _check_live_range_section(dump))
    return errors


# ---------------------------------------------------------------------------
# Sensitivity controls (VM-free core)
# ---------------------------------------------------------------------------
#
# The `--sensitivity-controls` mode turns every selected case into a mandatory
# set of derived, provably wrong variants (design
# `arksteed-sensitivity-control-mode`). This section holds the VM-free half of
# that mode:
#
#   * the case-level class model and the applicability rule (S2);
#   * the enumeration of the count-shaped and address assertions and the
#     per-assertion coverage rule (S2);
#   * the construction of derived assertions (count-perturbation S3,
#     address-substitution S4);
#   * the failure-class classification and the control verdict (S6);
#   * variant materialisation with the byte-preservation digest (S5);
#   * the report rendering (S7).
#
# Nothing here runs the VM, a build, `ark_js_vm`, `ark_disasm` or `es2abc`:
# every function works on a parsed `TestCase`, on an observation made of
# captured stdout plus the abc disassembly, and on a supplied artifact root.
# The flag surface, the run pipeline and the exit codes are a separate stage.

# Canonical control-class tokens (R3). A closure record cites exactly these
# strings in its `Sensitivity Control` field.
CONTROL_CLASS_JIT_OFF = "JIT-off"
CONTROL_CLASS_EXPECTATION = "expectation substitution"
CONTROL_CLASS_COUNT = "count-perturbation"
CONTROL_CLASS_ADDRESS = "address-substitution"
CONTROL_CLASSES: Tuple[str, ...] = (
    CONTROL_CLASS_JIT_OFF,
    CONTROL_CLASS_EXPECTATION,
    CONTROL_CLASS_COUNT,
    CONTROL_CLASS_ADDRESS,
)

# The class-2 variant appends exactly this line to a temporary copy of
# `expected_output.txt`. The copy makes `expected.strip()` truthy, which
# activates the stdout comparison even when the original expectation file is
# empty; the sentinel line is the verdict anchor of the class.
CONTROL_SENTINEL = "ARKSTEED-CONTROL-SENTINEL"
CONTROL_JIT_OPTION = "--compiler-enable-jit"
CONTROL_VARIANT_BASE = "base"
CONTROL_VARIANT_JIT_OFF = "jit-off"
CONTROL_VARIANT_EXPECTATION = "expectation"
CONTROL_DIR_NAME = "controls"

# Control verdict tokens (S7).
CONTROL_VERDICT_VALID = "valid"
CONTROL_VERDICT_INVALID = "invalid"
CONTROL_VERDICT_INAPPLICABLE = "inapplicable"
CONTROL_VERDICT_NOT_RUN = "not-run"
CONTROL_VERDICTS: Tuple[str, ...] = (
    CONTROL_VERDICT_VALID,
    CONTROL_VERDICT_INVALID,
    CONTROL_VERDICT_INAPPLICABLE,
    CONTROL_VERDICT_NOT_RUN,
)

# Failure-class tokens of a control variant (S6). The valid set is what a
# control may legitimately observe; every class of the invalid set means the
# case is insensitive or the control is defective, and never a pass.
FAILURE_VIOLATION = "violation"
FAILURE_MISSING_CFG_DUMP = "missing-cfg-dump"
FAILURE_MISMATCH = "mismatch"
FAILURE_LIVE_RANGE = "live-range"
FAILURE_CRASH = "crash"
FAILURE_TIMEOUT = "timeout"
FAILURE_COMPILE_ERROR = "compile-error"
FAILURE_NONZERO_EXIT = "nonzero-exit"
FAILURE_CONFIG_ERROR = "config-error"
FAILURE_UNCLASSIFIED = "unclassified"
FAILURE_CLASSES: Tuple[str, ...] = (
    FAILURE_VIOLATION,
    FAILURE_MISSING_CFG_DUMP,
    FAILURE_MISMATCH,
    FAILURE_LIVE_RANGE,
    FAILURE_CRASH,
    FAILURE_TIMEOUT,
    FAILURE_COMPILE_ERROR,
    FAILURE_NONZERO_EXIT,
    FAILURE_CONFIG_ERROR,
    FAILURE_UNCLASSIFIED,
)
VALID_FAILURE_CLASSES = frozenset({
    FAILURE_VIOLATION,
    FAILURE_MISSING_CFG_DUMP,
    FAILURE_MISMATCH,
    FAILURE_LIVE_RANGE,
})
INVALID_FAILURE_CLASSES = frozenset({
    FAILURE_CRASH,
    FAILURE_TIMEOUT,
    FAILURE_COMPILE_ERROR,
    FAILURE_NONZERO_EXIT,
    FAILURE_CONFIG_ERROR,
    FAILURE_UNCLASSIFIED,
})

# Expected failure class per control class (S6).
CONTROL_EXPECTED_CLASS: Dict[str, str] = {
    CONTROL_CLASS_JIT_OFF: FAILURE_VIOLATION,
    CONTROL_CLASS_EXPECTATION: FAILURE_MISMATCH,
    CONTROL_CLASS_COUNT: FAILURE_VIOLATION,
    CONTROL_CLASS_ADDRESS: FAILURE_VIOLATION,
}

# Applicability preconditions, named in the `inapplicable` reason (S2).
CONTROL_PRECONDITION_JIT_OFF = (
    "no positive JIT evidence (EXEC_COUNT*/COMPILE_COUNT* with n >= 1)")
CONTROL_PRECONDITION_EXPECTATION = "no expectation file"
CONTROL_PRECONDITION_COUNT = "no count-shaped assertion"
CONTROL_PRECONDITION_ADDRESS = "no address assertion"

# Count-perturbation value families (S3).
EQUALITY_COUNT_KEYWORDS = frozenset({
    "COUNT", "BYTECODE_COUNT", "CODEGEN_COUNT",
    "COMPILE_COUNT", "EXEC_COUNT", "DEOPT_COUNT",
})
LOWER_BOUND_COUNT_KEYWORDS = frozenset({
    "COUNT_GE", "COMPILE_COUNT_GE", "EXEC_COUNT_GE",
    "DEOPT_COUNT_GE", "GC_COUNT_GE",
})
UPPER_BOUND_COUNT_KEYWORDS = frozenset({"COUNT_LE"})

# Failure-class anchors: the runner's recorded reason vocabulary (S6).
FAILURE_VIOLATION_RE = re.compile(r'"[^"\n]*" violated, found \d+')
FAILURE_MISSING_CFG_DUMP_RE = re.compile(
    r"was never JIT-compiled \(no (?:CFG dump|generated-code section) in compiler log\)")
FAILURE_LIVE_RANGE_RE = re.compile(
    r"dies before its use|is used as a Phi input but has no live range|"
    r"live range info present but unparsable")
FAILURE_CRASH_RE = re.compile(r"ark_js_vm crashed with SIG(?:ABRT|SEGV)")
FAILURE_NONZERO_EXIT_RE = re.compile(r"ark_js_vm exited with return code -?\d+")
FAILURE_CONFIG_ERROR_RE = re.compile(r"^line \d+: |^cannot read source file: ")
FAILURE_MISMATCH_PREFIX = "stdout mismatch:"
FAILURE_TIMEOUT_TEXT = "execution timed out"
FAILURE_COMPILE_ERROR_PREFIX = "compilation failed:"


def annotation_kind(target: object) -> str:
    """Assertion family of a parsed annotation target."""
    if isinstance(target, EvidenceConstraint):
        return "evidence"
    if isinstance(target, BytecodeCountConstraint):
        return "bytecode"
    if isinstance(target, CodegenCountConstraint):
        return "codegen"
    if isinstance(target, Constraint):
        return "ir"
    raise TypeError(f"not a parsed assertion: {target!r}")


@dataclass(frozen=True)
class ControlAssertion:
    """One assertion of a parsed case that a control class can mutate."""

    kind: str
    record: AnnotationRecord
    constraint: object

    @property
    def method(self) -> Optional[str]:
        return self.record.method

    @property
    def line(self) -> int:
        return self.record.line

    def label(self) -> str:
        return self.constraint.label()

    def key(self) -> Tuple[str, Optional[str], int]:
        """Stable identity used by the per-assertion coverage rule."""
        return (self.kind, self.record.method, self.record.line)


@dataclass(frozen=True)
class CaseAssertions:
    """Count-shaped and address assertions of one case (S2).

    Count-shaped assertions are the method-scoped IR constraints (`HAS`,
    `HAS_NOT`, `COUNT`, `COUNT_GE`, `COUNT_LE`), `BYTECODE_COUNT`,
    `CODEGEN_COUNT` and the seven case-scoped evidence annotations. Address
    assertions are the IR constraints that name an opcode, stub-call
    constraints included.
    """

    count_shaped: Tuple[ControlAssertion, ...]
    address: Tuple[ControlAssertion, ...]


def enumerate_case_assertions(case: TestCase) -> CaseAssertions:
    """Enumerate the control assertions of a parsed case, in source order."""
    count_shaped: List[ControlAssertion] = []
    address: List[ControlAssertion] = []
    for record in case.annotations:
        assertion = ControlAssertion(annotation_kind(record.target), record, record.target)
        count_shaped.append(assertion)
        if assertion.kind == "ir":
            address.append(assertion)
    return CaseAssertions(tuple(count_shaped), tuple(address))


def positive_jit_evidence_assertions(
    assertions: CaseAssertions,
) -> Tuple[ControlAssertion, ...]:
    """Assertions that carry positive JIT evidence (class-1 precondition).

    Positive JIT evidence is a `COMPILE_COUNT*` or `EXEC_COUNT*` annotation
    with `n >= 1`; the interpreter twin of a JIT case carries none, so the
    class-1 control is inapplicable for it.
    """
    positive = []
    for assertion in assertions.count_shaped:
        target = assertion.constraint
        if isinstance(target, (CompileCountConstraint, CompileCountGeConstraint,
                               ExecCountConstraint, ExecCountGeConstraint)) \
                and target.count >= 1:
            positive.append(assertion)
    return tuple(positive)


@dataclass(frozen=True)
class ClassApplicability:
    """Whether a control class applies to a case, and why not (S2)."""

    class_token: str
    applicable: bool
    reason: Optional[str] = None


def control_applicability(case: TestCase) -> Dict[str, ClassApplicability]:
    """Per-class applicability of a parsed case, a pure function (S2).

    Class 1 requires positive JIT evidence, class 2 the presence of the
    `expected_output.txt` file (its content may be empty - the class-2 variant
    appends the sentinel to a temporary copy and thereby activates the
    comparison), class 3 at least one count-shaped assertion and class 4 at
    least one address assertion. A class whose precondition is absent yields
    an `inapplicable` verdict naming the missing precondition in its reason
    and is never counted as a pass.
    """
    assertions = enumerate_case_assertions(case)
    positive = positive_jit_evidence_assertions(assertions)
    has_expectation = case.expected.exists()
    return {
        CONTROL_CLASS_JIT_OFF: ClassApplicability(
            CONTROL_CLASS_JIT_OFF, bool(positive),
            None if positive else CONTROL_PRECONDITION_JIT_OFF),
        CONTROL_CLASS_EXPECTATION: ClassApplicability(
            CONTROL_CLASS_EXPECTATION, has_expectation,
            None if has_expectation else CONTROL_PRECONDITION_EXPECTATION),
        CONTROL_CLASS_COUNT: ClassApplicability(
            CONTROL_CLASS_COUNT, bool(assertions.count_shaped),
            None if assertions.count_shaped else CONTROL_PRECONDITION_COUNT),
        CONTROL_CLASS_ADDRESS: ClassApplicability(
            CONTROL_CLASS_ADDRESS, bool(assertions.address),
            None if assertions.address else CONTROL_PRECONDITION_ADDRESS),
    }


def parse_control_annotation(
    text: str, valid_opcodes: Optional[set] = None,
) -> object:
    """Parse one `//!` annotation (body or full line) with the runner grammar."""
    body = text.strip()
    if body.startswith("//!"):
        body = body[3:]
    comment_at = body.find("#")
    if comment_at != -1:
        body = body[:comment_at]
    tokens = body.split()
    if not tokens:
        raise ConstraintError("empty annotation")
    keyword = tokens[0]
    if keyword in CONSTRAINT_BY_KEYWORD:
        return CONSTRAINT_BY_KEYWORD[keyword].from_tokens(tokens, valid_opcodes)
    if keyword in EVIDENCE_CONSTRAINT_BY_KEYWORD:
        return EVIDENCE_CONSTRAINT_BY_KEYWORD[keyword].from_tokens(tokens)
    if keyword == "BYTECODE_COUNT":
        return BytecodeCountConstraint.from_tokens(tokens)
    if keyword == "CODEGEN_COUNT":
        return CodegenCountConstraint.from_tokens(tokens)
    raise ConstraintError(f"unknown annotation keyword '{keyword}'")


def render_ir_annotation(keyword: str, opcode: str, stub: Optional[str],
                         count: Optional[int]) -> str:
    """Annotation body of an IR constraint (used by the mutation builders)."""
    body = f"{keyword} {opcode}"
    if stub is not None:
        body += f" {stub}"
    if count is not None:
        body += f" {count}"
    return body


def _try_parse_control_annotation(text: str) -> Optional[object]:
    """Parse a mutated annotation, or None when the grammar rejects it.

    A derived value that renders into an unparsable annotation is a
    construction defect and becomes an invalid instance; it must never crash
    the control stage.
    """
    try:
        return parse_control_annotation(text)
    except ConstraintError:
        return None


def _construction_error_instance(
    assertion: ControlAssertion, class_token: str, variant_id: str, reason: str,
) -> "ControlInstance":
    return ControlInstance(
        class_token=class_token,
        variant_id=variant_id,
        target=f"line {assertion.line}: {assertion.record.text}",
        expected_class=CONTROL_EXPECTED_CLASS[class_token],
        anchor=assertion.label(),
        assertion=assertion,
        line=assertion.line,
        original_text=assertion.record.text,
        construction_error=reason,
    )


@dataclass
class ControlObservation:
    """Base-run observation the mutation builders are evaluated against.

    Built from captured stdout plus the optional abc disassembly, so the whole
    construction is reproducible without a VM.
    """

    counts: Dict[str, MethodCounts] = field(default_factory=dict)
    instructions: Dict[str, List[Tuple[str, str]]] = field(default_factory=dict)
    comments: Dict[str, List[str]] = field(default_factory=dict)
    evidence: Dict[str, List[str]] = field(default_factory=dict)
    stdout: str = ""


def observe_case(
    case: TestCase, stdout: str, valid_opcodes: Optional[set],
    disassembly: Optional[str] = None,
) -> ControlObservation:
    """Build the base observation of a case from captured run output."""
    del case  # kept in the signature to mirror the per-case pipeline
    return ControlObservation(
        counts=collect_opcode_counts(stdout, valid_opcodes),
        instructions=collect_bytecode_instructions(disassembly or ""),
        comments=collect_codegen_comments(stdout),
        evidence=collect_evidence(stdout),
        stdout=stdout,
    )


def assertion_violated(assertion: ControlAssertion,
                      observation: ControlObservation) -> Optional[str]:
    """Verdict of the *original* assertion on the observation, else None."""
    target = assertion.constraint
    if isinstance(target, Constraint):
        counts = observation.counts.get(assertion.method)
        if counts is None:
            return f"no CFG dump for method '{assertion.method}'"
        return target.violated(counts)
    if isinstance(target, BytecodeCountConstraint):
        instructions = observation.instructions.get(assertion.method)
        if instructions is None:
            return f"no disassembly for method '{assertion.method}'"
        return target.violated(instructions)
    if isinstance(target, CodegenCountConstraint):
        comments = observation.comments.get(assertion.method)
        if comments is None:
            return f"no generated-code section for method '{assertion.method}'"
        return target.violated(comments)
    if isinstance(target, EvidenceConstraint):
        return target.violated(observation.evidence)
    return "unsupported assertion"


def assertion_observation(assertion: ControlAssertion,
                          observation: ControlObservation) -> Optional[int]:
    """Observed count of the assertion's measurement, or None if unavailable."""
    target = assertion.constraint
    if isinstance(target, Constraint):
        counts = observation.counts.get(assertion.method)
        return None if counts is None else target.actual(counts)
    if isinstance(target, BytecodeCountConstraint):
        instructions = observation.instructions.get(assertion.method)
        return None if instructions is None else target.actual(instructions)
    if isinstance(target, CodegenCountConstraint):
        comments = observation.comments.get(assertion.method)
        return None if comments is None else target.actual(comments)
    if isinstance(target, EvidenceConstraint):
        return target.actual(observation.evidence)
    return None


def assertion_count(target: object) -> Optional[int]:
    """Declared bound of a count-shaped assertion (None for `HAS`/`HAS_NOT`)."""
    if isinstance(target, (HasConstraint, HasNotConstraint)):
        return None
    return getattr(target, "count", None)


def assertion_keyword(target: object) -> str:
    """Annotation keyword of a count-shaped assertion."""
    if isinstance(target, BytecodeCountConstraint):
        return "BYTECODE_COUNT"
    if isinstance(target, CodegenCountConstraint):
        return "CODEGEN_COUNT"
    return getattr(target, "KEYWORD", "")


def _assertion_target_violated(
    target: object, kind: str, method: Optional[str], observation: ControlObservation,
) -> Tuple[Optional[str], Optional[int], bool]:
    """`(violation, observed, available)` of one assertion target.

    `violation` is the runner's violation message (None when the assertion
    holds) and `available` is False when the observation does not cover the
    target's measurement at all.
    """
    if isinstance(target, Constraint):
        counts = observation.counts.get(method)
        if counts is None:
            return None, None, False
        return target.violated(counts), target.actual(counts), True
    if isinstance(target, BytecodeCountConstraint):
        instructions = observation.instructions.get(method)
        if instructions is None:
            return None, None, False
        return target.violated(instructions), target.actual(instructions), True
    if isinstance(target, CodegenCountConstraint):
        comments = observation.comments.get(method)
        if comments is None:
            return None, None, False
        return target.violated(comments), target.actual(comments), True
    if isinstance(target, EvidenceConstraint):
        return (target.violated(observation.evidence),
                target.actual(observation.evidence), True)
    return None, None, False


def perturbed_count_value(keyword: str, declared: int,
                          observed: Optional[int]) -> Tuple[Optional[int], Optional[str]]:
    """Derived count of a class-3 instance, knowingly wrong by construction.

    Returns `(derived, error)`: exactly one of the two is set. Equality forms
    take `n + 1` (the base assertion holds, so the observation equals `n`),
    lower bounds take `observed + 1` and the upper bound `COUNT_LE` takes
    `observed - 1`. `COUNT_LE` with observation 0 has no smaller non-negative
    bound: the instance is inapplicable and the assertion must be covered by
    class 4 on the same assertion.
    """
    if keyword in EQUALITY_COUNT_KEYWORDS:
        return declared + 1, None
    if keyword in LOWER_BOUND_COUNT_KEYWORDS:
        if observed is None:
            return None, "no observation for the count-shaped assertion"
        return observed + 1, None
    if keyword in UPPER_BOUND_COUNT_KEYWORDS:
        if observed is None:
            return None, "no observation for the count-shaped assertion"
        if observed < 1:
            return None, ("COUNT_LE with observed 0 has no smaller "
                          "non-negative bound; cover the assertion by class 4")
        return observed - 1, None
    return None, f"unsupported count-shaped keyword '{keyword}'"


def count_perturbation_text(assertion: ControlAssertion,
                            derived: int) -> Optional[str]:
    """Annotation body of the class-3 mutation, or None if unrenderable."""
    target = assertion.constraint
    if isinstance(target, Constraint):
        return render_ir_annotation(target.KEYWORD, target.opcode, target.stub, derived)
    if isinstance(target, BytecodeCountConstraint):
        return target.annotation_text().rsplit(" ", 1)[0] + f" {derived}"
    if isinstance(target, CodegenCountConstraint):
        return f"CODEGEN_COUNT {target.pattern} {derived}"
    if isinstance(target, EvidenceConstraint):
        return f"{target.KEYWORD} {target.pattern} {derived}"
    return None


def address_substitution_candidates(
    constraint: Constraint, opcode_order: List[str],
) -> List[Tuple[str, Optional[str], str]]:
    """Candidate `(opcode, stub, rule)` substitutions, in the S4 order.

    Rule `a`: for a stub-call constraint, the other stub-call opcode with the
    same stub pattern. Rule `b`: every other opcode in the declaration order
    of `arksteed_opcode_list.h`, keeping the stub pattern only for stub-call
    candidates. A stub-call candidate for a constraint without a stub pattern
    is skipped: the grammar requires a stub name on those opcodes.
    """
    candidates: List[Tuple[str, Optional[str], str]] = []
    if constraint.opcode in STUB_CALL_OPCODES and constraint.stub is not None:
        for opcode in STUB_CALL_OPCODES:
            if opcode != constraint.opcode:
                candidates.append((opcode, constraint.stub, "a"))
    for opcode in opcode_order:
        if opcode == constraint.opcode:
            continue
        if opcode in STUB_CALL_OPCODES:
            if constraint.stub is None:
                continue
            candidates.append((opcode, constraint.stub, "b"))
        else:
            candidates.append((opcode, None, "b"))
    return candidates


@dataclass(frozen=True)
class ControlInstance:
    """One constructed derived variant (not yet executed).

    `line`, `original_text` and `mutant_line` describe the single source-line
    variance of classes 3 and 4; the class-2 variance is the appended sentinel
    line in a temporary copy of the expectation file; class 1 changes only the
    VM command line. `construction_error` marks an instance that could not be
    built (for example `no substitution candidate`) and `inapplicable_reason`
    an instance that is inapplicable by construction (for example `COUNT_LE`
    with observation 0); neither is ever a pass.
    """

    class_token: str
    variant_id: str
    target: str
    expected_class: str
    anchor: str
    assertion: Optional[ControlAssertion] = None
    line: Optional[int] = None
    original_text: Optional[str] = None
    mutant_line: Optional[str] = None
    observation: Optional[int] = None
    derived: Optional[int] = None
    substitute: Optional[str] = None
    candidate_rule: Optional[str] = None
    mutation: Optional[str] = None
    construction_error: Optional[str] = None
    inapplicable_reason: Optional[str] = None

    @property
    def mutation_record(self) -> Optional[str]:
        """`line <N>: <original> -> <mutated>` for classes 3 and 4."""
        if self.line is None or self.mutant_line is None:
            return None
        return (f"line {self.line}: "
                f"{(self.original_text or '').strip()} -> "
                f"{self.mutant_line.strip()}")


def _annotation_instance(
    assertion: ControlAssertion, class_token: str, variant_id: str,
    mutant_line: Optional[str], mutated_constraint: object,
    observation: Optional[int], derived: Optional[int],
    construction_error: Optional[str] = None,
) -> ControlInstance:
    return ControlInstance(
        class_token=class_token,
        variant_id=variant_id,
        target=f"line {assertion.line}: {assertion.record.text}",
        expected_class=CONTROL_EXPECTED_CLASS[class_token],
        anchor=mutated_constraint.label(),
        assertion=assertion,
        line=assertion.line,
        original_text=assertion.record.text,
        mutant_line=mutant_line,
        observation=observation,
        derived=derived,
        mutation=(f"line {assertion.line}: {assertion.record.text} -> "
                  f"{(mutant_line or '').strip()}"),
        construction_error=construction_error,
    )


def build_count_perturbation_instance(
    assertion: ControlAssertion, observation: ControlObservation,
) -> ControlInstance:
    """One class-3 instance for one count-shaped assertion (S3)."""
    target = assertion.constraint
    observed = assertion_observation(assertion, observation)
    variant_id = f"count-{assertion.line}"
    if isinstance(target, (HasConstraint, HasNotConstraint)):
        flipped = "HAS_NOT" if isinstance(target, HasConstraint) else "HAS"
        body = render_ir_annotation(flipped, target.opcode, target.stub, None)
        mutated = _try_parse_control_annotation(body)
        if mutated is None:
            return _construction_error_instance(
                assertion, CONTROL_CLASS_COUNT, variant_id,
                f"unparsable derived annotation: {body}")
        instance = _annotation_instance(
            assertion, CONTROL_CLASS_COUNT, variant_id,
            assertion.record.indent + "//! " + body, mutated,
            observed, None)
        return instance
    derived, error = perturbed_count_value(
        assertion_keyword(target),
        0 if assertion_count(target) is None else assertion_count(target),
        observed)
    if error is not None:
        return ControlInstance(
            class_token=CONTROL_CLASS_COUNT,
            variant_id=variant_id,
            target=f"line {assertion.line}: {assertion.record.text}",
            expected_class=CONTROL_EXPECTED_CLASS[CONTROL_CLASS_COUNT],
            anchor=assertion.label(),
            assertion=assertion,
            line=assertion.line,
            original_text=assertion.record.text,
            observation=observed,
            inapplicable_reason=error,
        )
    body = count_perturbation_text(assertion, derived)
    if body is None:
        return ControlInstance(
            class_token=CONTROL_CLASS_COUNT,
            variant_id=variant_id,
            target=f"line {assertion.line}: {assertion.record.text}",
            expected_class=CONTROL_EXPECTED_CLASS[CONTROL_CLASS_COUNT],
            anchor=assertion.label(),
            assertion=assertion,
            line=assertion.line,
            original_text=assertion.record.text,
            observation=observed,
            construction_error="unsupported count-shaped assertion",
        )
    mutated = _try_parse_control_annotation(body)
    if mutated is None:
        return _construction_error_instance(
            assertion, CONTROL_CLASS_COUNT, variant_id,
            f"unparsable derived annotation: {body}")
    violation, _, available = _assertion_target_violated(
        mutated, assertion.kind, assertion.method, observation)
    mutant_line = assertion.record.indent + "//! " + body
    if not available:
        return _annotation_instance(
            assertion, CONTROL_CLASS_COUNT, variant_id, mutant_line, mutated,
            observed, derived,
            construction_error="no observation for the mutated assertion")
    if violation is None:
        return _annotation_instance(
            assertion, CONTROL_CLASS_COUNT, variant_id, mutant_line, mutated,
            observed, derived,
            construction_error=f"derived assertion still holds: {mutated.label()}")
    return _annotation_instance(
        assertion, CONTROL_CLASS_COUNT, variant_id, mutant_line, mutated,
        observed, derived)

def build_address_substitution_instance(
    assertion: ControlAssertion, observation: ControlObservation,
    opcode_order: List[str],
) -> ControlInstance:
    """One class-4 instance for one address assertion (S4)."""
    target = assertion.constraint
    variant_id = f"address-{assertion.line}"
    counts = observation.counts.get(assertion.method)
    if counts is None:
        return _construction_error_instance(
            assertion, CONTROL_CLASS_ADDRESS, variant_id, "no substitution candidate")
    declared = assertion_count(target)
    for opcode, stub, rule in address_substitution_candidates(target, opcode_order):
        body = render_ir_annotation(target.KEYWORD, opcode, stub, declared)
        mutated = _try_parse_control_annotation(body)
        if mutated is None or mutated.violated(counts) is None:
            continue
        return ControlInstance(
            class_token=CONTROL_CLASS_ADDRESS,
            variant_id=variant_id,
            target=f"line {assertion.line}: {assertion.record.text}",
            expected_class=CONTROL_EXPECTED_CLASS[CONTROL_CLASS_ADDRESS],
            anchor=mutated.label(),
            assertion=assertion,
            line=assertion.line,
            original_text=assertion.record.text,
            mutant_line=assertion.record.indent + "//! " + body,
            observation=mutated.actual(counts),
            substitute=opcode,
            candidate_rule=rule,
            mutation=(f"line {assertion.line}: {assertion.record.text} -> "
                      f"//! {body}"),
        )
    return _construction_error_instance(
        assertion, CONTROL_CLASS_ADDRESS, variant_id, "no substitution candidate")


def build_control_instances(
    case: TestCase, observation: ControlObservation,
    opcode_order: Optional[List[str]],
) -> List[ControlInstance]:
    """Construct every derived variant of a case from its base observation.

    One instance per class-1 and class-2 obligation and one per count-shaped
    (class 3) and address (class 4) assertion of the case. Gaps in the
    observation, an impossible `COUNT_LE` bound and a missing address
    substitution candidate become explicit `inapplicable` or invalid
    instances, never a silent pass.
    """
    assertions = enumerate_case_assertions(case)
    applicability = control_applicability(case)
    instances: List[ControlInstance] = []

    if applicability[CONTROL_CLASS_JIT_OFF].applicable:
        witness = positive_jit_evidence_assertions(assertions)[0]
        instances.append(ControlInstance(
            class_token=CONTROL_CLASS_JIT_OFF,
            variant_id=CONTROL_VARIANT_JIT_OFF,
            target=f"{CONTROL_JIT_OPTION}=false (VM command line)",
            expected_class=CONTROL_EXPECTED_CLASS[CONTROL_CLASS_JIT_OFF],
            anchor=witness.label(),
            assertion=witness,
            mutation=f"{CONTROL_JIT_OPTION}=false for the derived variant",
        ))

    if applicability[CONTROL_CLASS_EXPECTATION].applicable:
        instances.append(ControlInstance(
            class_token=CONTROL_CLASS_EXPECTATION,
            variant_id=CONTROL_VARIANT_EXPECTATION,
            target=EXPECTED_OUTPUT_NAME,
            expected_class=CONTROL_EXPECTED_CLASS[CONTROL_CLASS_EXPECTATION],
            anchor=CONTROL_SENTINEL,
            mutation=f"append {CONTROL_SENTINEL} to a copy of {EXPECTED_OUTPUT_NAME}",
        ))

    for assertion in assertions.count_shaped:
        instances.append(build_count_perturbation_instance(assertion, observation))

    for assertion in assertions.address:
        if opcode_order is None:
            instances.append(ControlInstance(
                class_token=CONTROL_CLASS_ADDRESS,
                variant_id=f"address-{assertion.line}",
                target=f"line {assertion.line}: {assertion.record.text}",
                expected_class=CONTROL_EXPECTED_CLASS[CONTROL_CLASS_ADDRESS],
                anchor=assertion.label(),
                assertion=assertion,
                line=assertion.line,
                original_text=assertion.record.text,
                construction_error="no substitution candidate",
            ))
        else:
            instances.append(build_address_substitution_instance(
                assertion, observation, opcode_order))
    return instances


# ---------------------------------------------------------------------------
# Control verdicts (S6)
# ---------------------------------------------------------------------------


def classify_failure_classes(reasons: List[str],
                            config_errors: Tuple[str, ...] = ()) -> frozenset:
    """Classify the recorded reasons of a variant run into failure classes.

    Returns a set of canonical tokens (S6). An empty set means the variant
    passed. The valid set is {violation, missing-cfg-dump, mismatch,
    live-range}; every other token means the run failed for a reason that does
    not prove sensitivity. The classification is per reason: every reason that
    matches no pattern contributes `unclassified`, so an unrecognized or
    infrastructure reason next to a recognized one still makes the instance
    invalid (S6: environment failures are not passes).
    """
    classes = set()
    if config_errors:
        classes.add(FAILURE_CONFIG_ERROR)
    for reason in reasons:
        matched = False
        if FAILURE_MISMATCH_PREFIX in reason:
            classes.add(FAILURE_MISMATCH)
            matched = True
        if FAILURE_VIOLATION_RE.search(reason):
            classes.add(FAILURE_VIOLATION)
            matched = True
        if FAILURE_MISSING_CFG_DUMP_RE.search(reason):
            classes.add(FAILURE_MISSING_CFG_DUMP)
            matched = True
        if FAILURE_LIVE_RANGE_RE.search(reason):
            classes.add(FAILURE_LIVE_RANGE)
            matched = True
        if FAILURE_CRASH_RE.search(reason):
            classes.add(FAILURE_CRASH)
            matched = True
        if FAILURE_TIMEOUT_TEXT in reason:
            classes.add(FAILURE_TIMEOUT)
            matched = True
        if FAILURE_COMPILE_ERROR_PREFIX in reason:
            classes.add(FAILURE_COMPILE_ERROR)
            matched = True
        if FAILURE_NONZERO_EXIT_RE.search(reason):
            classes.add(FAILURE_NONZERO_EXIT)
            matched = True
        if FAILURE_CONFIG_ERROR_RE.search(reason):
            classes.add(FAILURE_CONFIG_ERROR)
            matched = True
        if not matched:
            classes.add(FAILURE_UNCLASSIFIED)
    return frozenset(classes)


def control_anchor_present(instance: ControlInstance, reasons: List[str]) -> bool:
    """Whether the instance's anchor appears in the recorded reasons (S6)."""
    text = "\n".join(reasons)
    if instance.anchor is None:
        return False
    return instance.anchor in text


@dataclass(frozen=True)
class ControlInstanceResult:
    """Verdict of one executed control instance."""

    instance: ControlInstance
    verdict: str
    actual_classes: frozenset = frozenset()
    reason: Optional[str] = None
    # Recorded failure evidence of the executed instance: the anchor-bearing
    # line of the variant's recorded reasons (the class-2 mismatch diff line
    # with the sentinel, or the violation line of the mutated assertion). It is
    # the audit record of what the control actually observed (S7).
    evidence: Optional[str] = None

    @property
    def class_token(self) -> str:
        return self.instance.class_token


def evaluate_control_instance(
    instance: ControlInstance, base_passed: bool, reasons: List[str],
    stdout: str = "", config_errors: Tuple[str, ...] = (),
) -> ControlInstanceResult:
    """Compute the verdict of one variant (S6).

    An instance is valid iff the base case passed, the variant ran and failed
    with the expected class, the anchor is present and no invalid class was
    observed. Extra valid classes are recorded and not penalised. A passed
    variant is invalid (`case passed`); a class-1 variant that still compiled
    is invalid (`jit-off ineffective`); an instance that could not be built is
    invalid with its construction error; an instance inapplicable by
    construction keeps its `inapplicable` verdict and is never a pass.
    """
    if instance.inapplicable_reason is not None:
        return ControlInstanceResult(
            instance, CONTROL_VERDICT_INAPPLICABLE,
            reason=instance.inapplicable_reason)
    if instance.construction_error is not None:
        return ControlInstanceResult(
            instance, CONTROL_VERDICT_INVALID,
            reason=instance.construction_error)
    if not base_passed:
        return ControlInstanceResult(
            instance, CONTROL_VERDICT_INVALID, reason="base case did not pass")
    if instance.class_token == CONTROL_CLASS_JIT_OFF and stdout \
            and collect_evidence(stdout)["compile"]:
        return ControlInstanceResult(
            instance, CONTROL_VERDICT_INVALID, reason="jit-off ineffective")
    classes = classify_failure_classes(list(reasons), config_errors)
    if not classes:
        return ControlInstanceResult(
            instance, CONTROL_VERDICT_INVALID, reason="case passed")
    if instance.expected_class not in classes:
        return ControlInstanceResult(
            instance, CONTROL_VERDICT_INVALID, classes,
            f"expected failure class missing ({instance.expected_class})")
    if not control_anchor_present(instance, list(reasons)):
        return ControlInstanceResult(
            instance, CONTROL_VERDICT_INVALID, classes,
            f"anchor missing ({instance.anchor})")
    invalid = sorted(classes & INVALID_FAILURE_CLASSES)
    if invalid:
        return ControlInstanceResult(
            instance, CONTROL_VERDICT_INVALID, classes,
            f"failure class not valid ({invalid[0]})")
    return ControlInstanceResult(instance, CONTROL_VERDICT_VALID, classes)


@dataclass(frozen=True)
class ClassControlResult:
    """Per-class verdict of one controlled case (S7)."""

    class_token: str
    applicable: bool
    verdict: str
    reason: Optional[str] = None
    instance_results: Tuple[ControlInstanceResult, ...] = ()


def base_run_state(case: "CaseControlResult") -> str:
    """Render the base-run state of a case (S7).

    A case left uncontrolled by `--stops-on-error` was never executed: its
    base state is unknown, and rendering it as `failed` would let an auditor
    mistake a bounded stop for a case defect. The not-run verdict is carried
    by the case verdict and is unaffected by this rendering.
    """
    if case.verdict == CONTROL_VERDICT_NOT_RUN or case.base_passed is None:
        return "not run"
    return "passed" if case.base_passed else "failed"


@dataclass(frozen=True)
class CaseControlResult:
    """Per-case verdict under control (S2, S7)."""

    rel: str
    # None when the case was never executed (`not-run`), so the report cannot
    # claim a failed base run for a case that never ran.
    base_passed: Optional[bool]
    classes: Tuple[ClassControlResult, ...]
    uncovered: Tuple[str, ...]
    verdict: str
    reasons: Tuple[str, ...]
    source_tree_unchanged: bool

    @property
    def instance_results(self) -> Tuple[ControlInstanceResult, ...]:
        return tuple(result for control_class in self.classes
                     for result in control_class.instance_results)


def evaluate_case_control(
    case: TestCase, base_passed: bool,
    instance_results: List[ControlInstanceResult],
    source_tree_unchanged: bool,
) -> CaseControlResult:
    """Evaluate the per-case verdict of a controlled case (S2).

    A case is green under control iff its base run is green, every applicable
    class produced at least one valid instance, every count-shaped assertion is
    covered by at least one valid instance of class 3 or class 4, and no
    instance was reported invalid. A valid sibling instance of the same class
    does not mask an invalid one: the case-level verdict aggregates every
    executed instance, so any invalid control of a mandatory case makes the
    case red (S7, epic criterion 1).
    """
    applicability = control_applicability(case)
    assertions = enumerate_case_assertions(case)
    grouped: Dict[str, List[ControlInstanceResult]] = {token: [] for token in CONTROL_CLASSES}
    for result in instance_results:
        grouped.setdefault(result.class_token, []).append(result)

    classes: List[ClassControlResult] = []
    for token in CONTROL_CLASSES:
        applicable = applicability[token]
        results = grouped.get(token, [])
        if not applicable.applicable:
            classes.append(ClassControlResult(
                token, False, CONTROL_VERDICT_INAPPLICABLE, applicable.reason))
            continue
        valid = [r for r in results if r.verdict == CONTROL_VERDICT_VALID]
        if valid:
            classes.append(ClassControlResult(
                token, True, CONTROL_VERDICT_VALID, None, tuple(results)))
            continue
        reason = next((r.reason for r in results if r.reason),
                      "no valid control instance")
        classes.append(ClassControlResult(
            token, True, CONTROL_VERDICT_INVALID, reason, tuple(results)))

    covered = set()
    for result in instance_results:
        if result.verdict != CONTROL_VERDICT_VALID:
            continue
        if result.class_token not in (CONTROL_CLASS_COUNT, CONTROL_CLASS_ADDRESS):
            continue
        assertion = result.instance.assertion
        if assertion is not None:
            covered.add(assertion.key())
    uncovered = tuple(assertion.label() for assertion in assertions.count_shaped
                      if assertion.key() not in covered)

    reasons: List[str] = []
    if not base_passed:
        reasons.append("base case did not pass")
    if not any(control_class.applicable for control_class in classes):
        reasons.append("no applicable control")
    for control_class in classes:
        if control_class.applicable and control_class.verdict != CONTROL_VERDICT_VALID:
            reasons.append(
                f"class {control_class.class_token}: {control_class.reason}")
    # Every invalid instance is a case error on its own: a class that still has
    # a valid sibling instance must not hide an invalid one, because the
    # per-case verdict (and with it the run exit code, S7) aggregates every
    # executed instance, not only the per-class summary.
    for result in instance_results:
        if result.verdict != CONTROL_VERDICT_INVALID:
            continue
        reasons.append(
            f"invalid control instance(s): {result.class_token} "
            f"{result.instance.variant_id}: {result.reason or 'invalid'}")
    if uncovered:
        reasons.append("uncovered count-shaped assertion(s): " + "; ".join(uncovered))
    if not source_tree_unchanged:
        reasons.append("source tree modified")
    return CaseControlResult(
        case.rel, base_passed, tuple(classes), uncovered,
        "green" if not reasons else "red", tuple(reasons),
        source_tree_unchanged,
    )


# ---------------------------------------------------------------------------
# Variant materialisation and byte preservation (S5)
# ---------------------------------------------------------------------------


def control_variant_rel(case_rel: str, variant_id: str) -> str:
    """Run-relative directory of one variant: `controls/<case>/<variant>`."""
    return f"{CONTROL_DIR_NAME}/{case_rel}/{variant_id}"


def control_variant_dir(artifacts_root, case_rel: str, variant_id: str) -> Path:
    return Path(artifacts_root) / CONTROL_DIR_NAME / case_rel / variant_id


def _ensure_artifact_path(path: Path) -> None:
    """Refuse to materialise a variant inside the test source tree (S5)."""
    resolved = path.resolve()
    test_root = TEST_DIR.resolve()
    if resolved == test_root or test_root in resolved.parents:
        raise ValueError(
            f"refusing to write inside the test source tree: {path}")


def materialize_case_copy(case: TestCase, artifacts_root, variant_id: str) -> TestCase:
    """Copy the complete input set of a case under the artifact root (S5).

    The variant `TestCase` refers to files inside the variant directory only;
    neither the case source nor its `expected_output.txt` is opened for
    writing.
    """
    variant_rel = control_variant_rel(case.rel, variant_id)
    variant_dir = Path(artifacts_root) / variant_rel
    _ensure_artifact_path(variant_dir)
    variant_dir.mkdir(parents=True, exist_ok=True)
    source = variant_dir / case.source.name
    shutil.copyfile(case.source, source)
    siblings: List[Path] = []
    for sibling in case.siblings:
        destination = variant_dir / sibling.name
        shutil.copyfile(sibling, destination)
        siblings.append(destination)
    expected = variant_dir / case.expected.name
    if case.expected.exists():
        shutil.copyfile(case.expected, expected)
    return TestCase(variant_rel, source, expected, siblings=siblings)


def append_control_sentinel(expected_path: Path) -> str:
    """Append exactly one sentinel line to a variant copy of the expectation.

    For an originally empty file the copy is exactly that single line, which
    makes `expected.strip()` truthy and thereby activates the stdout
    comparison of the derived variant.
    """
    content = expected_path.read_text(encoding="utf-8", errors="replace")
    if content and not content.endswith("\n"):
        content += "\n"
    expected_path.write_text(content + CONTROL_SENTINEL + "\n", encoding="utf-8")
    return CONTROL_SENTINEL


def _rewrite_annotation_line(source: Path, instance: ControlInstance) -> None:
    """Replace exactly the mutated annotation line of a variant source copy."""
    if instance.line is None or instance.mutant_line is None:
        raise ValueError(f"instance {instance.variant_id} has no line mutation")
    lines = source.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)
    index = instance.line - 1
    if index < 0 or index >= len(lines):
        raise ValueError(
            f"instance {instance.variant_id} names line {instance.line}, "
            f"but the source has {len(lines)} lines")
    ending = "\n" if lines[index].endswith("\n") else ""
    lines[index] = instance.mutant_line + ending
    source.write_text("".join(lines), encoding="utf-8")


def materialize_variant(case: TestCase, instance: ControlInstance,
                        artifacts_root) -> TestCase:
    """Materialise one derived variant under the artifact root (S5)."""
    variant = materialize_case_copy(case, artifacts_root, instance.variant_id)
    if instance.class_token == CONTROL_CLASS_EXPECTATION:
        append_control_sentinel(variant.expected)
    elif instance.class_token in (CONTROL_CLASS_COUNT, CONTROL_CLASS_ADDRESS):
        if instance.mutant_line is None:
            raise ValueError(
                f"instance {instance.variant_id} has no mutation to materialise")
        _rewrite_annotation_line(variant.source, instance)
    return variant


def materialize_base_variant(case: TestCase, artifacts_root) -> TestCase:
    """Materialise the unmutated `base` copy of a case (S5)."""
    return materialize_case_copy(case, artifacts_root, CONTROL_VARIANT_BASE)


def digest_case_inputs(case: TestCase) -> Dict[str, Optional[str]]:
    """SHA-256 of the entry source, the sibling sources and the expectation."""
    digests: Dict[str, Optional[str]] = {}
    for path in [case.source, *case.siblings, case.expected]:
        try:
            digests[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        except OSError:
            digests[str(path)] = None
    return digests


def case_tree_unchanged(before: Dict[str, Optional[str]],
                        after: Dict[str, Optional[str]]) -> bool:
    """Whether the digests of the controlled inputs are unchanged (S5)."""
    return before == after


# ---------------------------------------------------------------------------
# Control report (S7)
# ---------------------------------------------------------------------------


def _md_cell(value: object) -> str:
    """One-line markdown table cell."""
    if value is None:
        return ""
    return " ".join(str(value).split()).replace("|", "\\|")


@dataclass(frozen=True)
class ControlsHeader:
    flag: bool
    platform: str
    mode: str
    git_head: Optional[str]
    include: Tuple[str, ...]
    exclude: Tuple[str, ...]
    case_count: int


@dataclass(frozen=True)
class ControlsReport:
    header: ControlsHeader
    cases: Tuple[CaseControlResult, ...]
    control_elapsed: float = 0.0
    source_tree_unchanged: bool = True


def render_controls_md(report: ControlsReport) -> str:
    """Render the mandatory `controls.md` report (S7)."""
    header = report.header
    lines = [
        "# ArkSteed Sensitivity Controls",
        "",
        f"- flag: `--sensitivity-controls` ({'on' if header.flag else 'off'})",
        f"- platform: `{header.platform}`  mode: `{header.mode}`",
        f"- git HEAD: `{header.git_head or 'unknown'}`",
        f"- selection: include={list(header.include)} exclude={list(header.exclude)}",
        f"- cases: {header.case_count}",
        "- source-tree-unchanged: "
        + ("yes" if report.source_tree_unchanged else "no"),
        "",
    ]
    for case in report.cases:
        lines.append(f"## `{case.rel}`")
        lines.append("")
        lines.append(f"- base run: {base_run_state(case)}")
        lines.append(f"- verdict: {case.verdict}")
        for reason in case.reasons:
            lines.append(f"- reason: {_md_cell(reason)}")
        lines.append("")
        lines.append("| Class | Instance | Target | Observation | Mutation | "
                     "Expected | Actual | Verdict | Evidence | Reason |")
        lines.append("|---|---|---|---|---|---|---|---|---|---|")
        for control_class in case.classes:
            if not control_class.instance_results:
                lines.append(
                    f"| {control_class.class_token} | - | - | - | - | "
                    f"{CONTROL_EXPECTED_CLASS[control_class.class_token]} | - | "
                    f"{control_class.verdict} | - | {_md_cell(control_class.reason)} |")
                continue
            for result in control_class.instance_results:
                instance = result.instance
                lines.append(
                    f"| {instance.class_token} | {instance.variant_id} | "
                    f"{_md_cell(instance.target)} | {_md_cell(instance.observation)} | "
                    f"{_md_cell(instance.mutation)} | {instance.expected_class} | "
                    f"{_md_cell(sorted(result.actual_classes))} | {result.verdict} | "
                    f"{_md_cell(result.evidence)} | {_md_cell(result.reason)} |")
        lines.append("")
        lines.append("### Assertion coverage")
        lines.append("")
        lines.append("| Assertion | Covered |")
        lines.append("|---|---|")
        covered_keys = {
            result.instance.assertion.key()
            for result in case.instance_results
            if result.verdict == CONTROL_VERDICT_VALID
            and result.class_token in (CONTROL_CLASS_COUNT, CONTROL_CLASS_ADDRESS)
            and result.instance.assertion is not None
        }
        count_shaped = [
            result.instance.assertion
            for result in case.instance_results
            if result.class_token == CONTROL_CLASS_COUNT
            and result.instance.assertion is not None
        ]
        for assertion in count_shaped:
            covered = assertion.key() in covered_keys
            lines.append(
                f"| {_md_cell(assertion.label())} | {'yes' if covered else 'no'} |")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def render_controls_json(report: ControlsReport) -> str:
    """Render the mandatory `controls.json` report (S7)."""
    header = report.header
    payload = {
        "flag": "--sensitivity-controls",
        "flag_enabled": header.flag,
        "platform": header.platform,
        "mode": header.mode,
        "git_head": header.git_head,
        "include": list(header.include),
        "exclude": list(header.exclude),
        "case_count": header.case_count,
        "source_tree_unchanged": report.source_tree_unchanged,
        "control_elapsed": report.control_elapsed,
        "cases": [],
    }
    for case in report.cases:
        payload["cases"].append({
            "rel": case.rel,
            "base_run": base_run_state(case),
            "verdict": case.verdict,
            "reasons": list(case.reasons),
            "classes": [
                {
                    "class": control_class.class_token,
                    "expected_class": CONTROL_EXPECTED_CLASS[control_class.class_token],
                    "verdict": control_class.verdict,
                    "reason": control_class.reason,
                }
                for control_class in case.classes
            ],
            "instances": [
                {
                    "class": result.instance.class_token,
                    "variant": result.instance.variant_id,
                    "target": result.instance.target,
                    "observation": result.instance.observation,
                    "derived": result.instance.derived,
                    "substitute": result.instance.substitute,
                    "candidate_rule": result.instance.candidate_rule,
                    "mutation": result.instance.mutation,
                    "expected_class": result.instance.expected_class,
                    "actual_classes": sorted(result.actual_classes),
                    "verdict": result.verdict,
                    "evidence": result.evidence,
                    "reason": result.reason,
                }
                for result in case.instance_results
            ],
            "uncovered_assertions": list(case.uncovered),
        })
    return json.dumps(payload, indent=2, ensure_ascii=False) + "\n"


def render_summary_controls_section(report: ControlsReport) -> str:
    """Render the `## Sensitivity controls` section of `summary.md` (S7)."""
    cases = report.cases
    instances = [result for case in cases for result in case.instance_results]
    green = sum(1 for case in cases if case.verdict == "green")
    red = len(cases) - green
    lines = [
        "## Sensitivity controls",
        "",
        f"- Cases controlled: {len(cases)}  Green: {green}  Red: {red}",
        f"- Instances: valid={sum(1 for r in instances if r.verdict == CONTROL_VERDICT_VALID)} "
        f"invalid={sum(1 for r in instances if r.verdict == CONTROL_VERDICT_INVALID)} "
        f"inapplicable={sum(1 for r in instances if r.verdict == CONTROL_VERDICT_INAPPLICABLE)}",
    ]
    for token in CONTROL_CLASSES:
        token_instances = [r for r in instances if r.class_token == token]
        lines.append(
            f"- `{token}`: valid={sum(1 for r in token_instances if r.verdict == CONTROL_VERDICT_VALID)} "
            f"invalid={sum(1 for r in token_instances if r.verdict == CONTROL_VERDICT_INVALID)} "
            f"inapplicable={sum(1 for r in token_instances if r.verdict == CONTROL_VERDICT_INAPPLICABLE)}")
    lines.append(
        f"- Uncovered assertions: {sum(len(case.uncovered) for case in cases)}")
    lines.append(f"- Controls elapsed: {report.control_elapsed:.1f}s")
    lines.append("- source-tree-unchanged: "
                 + ("yes" if report.source_tree_unchanged else "no"))
    # Every red case is listed with its own reasons, not only the red class
    # verdicts: a case can be red while every class verdict is valid (a valid
    # sibling instance of the same class masks an invalid one), and uncovered
    # assertions, a source-tree modification and a failed base run have the
    # same shape. Without the per-case entry the summary would contradict its
    # own totals and the exit code (S7: lists every red control verdict with
    # its reason).
    red_cases = [case for case in cases
                 if case.verdict != "green" and case.reasons]
    reds = [
        (case.rel, control_class.class_token, control_class.reason)
        for case in cases for control_class in case.classes
        if control_class.applicable and control_class.verdict != CONTROL_VERDICT_VALID
    ]
    if red_cases or reds:
        lines.append("- Red control verdicts:")
        for case in red_cases:
            lines.append(
                f"  - `{case.rel}` case (verdict `{case.verdict}`): "
                f"{_md_cell('; '.join(case.reasons))}")
        for rel, token, reason in reds:
            lines.append(f"  - `{rel}` `{token}`: {_md_cell(reason)}")
    else:
        lines.append("- Red control verdicts: none")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Control stage: orchestration (S1, S2, S5, S6, S7)
# ---------------------------------------------------------------------------
#
# The VM-free core above is the class model, the construction and the report.
# This section drives it: the flag surface, the usage errors, the base run plus
# one variant run per class-2/3/4 instance, the per-case verdict, the report
# artifacts and the exit codes. Class 1 (`JIT-off`) is a command-line override
# owned by the following leaf (T3); the model already reports it applicable for
# a case with positive JIT evidence, so this stage reports the unmet obligation
# instead of silently skipping the class or running an unmutated command.

CONTROL_JIT_OFF_UNBUILT = (
    "class JIT-off is not built in this stage: its command override "
    "(--compiler-enable-jit=false on the derived variant) is the following "
    "leaf's obligation, so the applicable class-1 control is reported unmet")


def control_stage_blocker(instance: ControlInstance) -> Optional[str]:
    """Why this stage cannot execute an instance, or None when it can."""
    if instance.class_token == CONTROL_CLASS_JIT_OFF:
        return CONTROL_JIT_OFF_UNBUILT
    return None


# One report cell is one line: the recorded failure evidence is truncated.
CONTROL_EVIDENCE_LIMIT = 300


def control_instance_evidence(
    instance: ControlInstance, reasons: List[str],
) -> Optional[str]:
    """Recorded failure evidence of an executed instance (S7).

    The anchor-bearing line of the variant's recorded reasons is the audit
    record of the control: the class-2 mismatch diff line carrying the sentinel,
    or the violation line of the mutated assertion (`"<label>" violated, found
    <n>`). Without an anchor the first reason is recorded. A missing reason
    (the variant passed) yields no evidence, because a passed variant is never
    a valid control.
    """
    for reason in reasons:
        for line in reason.splitlines():
            if instance.anchor and instance.anchor in line:
                recorded = line.strip()
                if len(recorded) > CONTROL_EVIDENCE_LIMIT:
                    recorded = recorded[:CONTROL_EVIDENCE_LIMIT] + " ..."
                return recorded
    if not reasons:
        return None
    recorded = " ".join(reasons[0].split())
    if len(recorded) > CONTROL_EVIDENCE_LIMIT:
        recorded = recorded[:CONTROL_EVIDENCE_LIMIT] + " ..."
    return recorded


def control_usage_error(
    cases: List[TestCase], opcode_order: Optional[List[str]],
) -> Optional[str]:
    """Usage error of the control mode (exit code 2), or None (S1, S7).

    A foreign case class together with the flag is rejected visibly: the mode
    covers the embedded class only and a selected corpus case must never be
    silently skipped or reported as controlled (E-CORPUS). An unreadable
    opcode-name header is a usage error as well while the mode needs it, that
    is while any selected case carries an address assertion.
    """
    foreign = sorted({case.case_class for case in cases
                      if case.case_class != CASE_CLASS_EMBEDDED})
    if foreign:
        return (
            f"--sensitivity-controls is defined for the {CASE_CLASS_EMBEDDED} "
            f"case class only: a selected case of class {', '.join(foreign)} "
            f"is controlled by its class invariant (JIT activity) and the "
            f"differential interpreter/JIT stdout comparison (E-CORPUS), and "
            f"controls are not defined for that class. The selection is "
            f"rejected instead of being silently skipped or reported as "
            f"controlled")
    if opcode_order is None and any(
            enumerate_case_assertions(case).address for case in cases):
        return (
            f"--sensitivity-controls needs the IR opcode names from "
            f"{OPCODE_LIST_HEADER} for the address-substitution class, but "
            f"the header could not be read")
    return None


def not_run_case_control(case: TestCase) -> CaseControlResult:
    """Verdict of a selected case left uncontrolled by `--stops-on-error` (S7)."""
    applicability = control_applicability(case)
    classes = tuple(
        ClassControlResult(token, applicability[token].applicable,
                           CONTROL_VERDICT_NOT_RUN,
                           "run stopped early (--stops-on-error)")
        for token in CONTROL_CLASSES)
    # The case never ran, so its base state is unknown rather than failed; the
    # `not-run` verdict and the exit code are unchanged.
    return CaseControlResult(
        case.rel, None, classes, (), CONTROL_VERDICT_NOT_RUN,
        ("run stopped early (--stops-on-error)",), True)


def run_case_controls(
    case: TestCase,
    base_result: CaseResult,
    cfg: BuildConfig,
    args: argparse.Namespace,
    artifacts_root: Path,
    log_fragment_res: List[re.Pattern],
    valid_opcodes: Optional[set],
    opcode_order: Optional[List[str]],
) -> Tuple[CaseControlResult, float]:
    """Execute every derived variant of one case and return its verdict (S2).

    The base observation is built from the base run of the same case; every
    variant is an independent case run in its own `controls/<case>/<variant>/`
    directory under the artifact root and uses the existing per-case pipeline
    and budget. The input digests are taken before the first variant and after
    the last one, so a violated isolation makes the case red (S5). The return
    value carries the control time separately from the case timing (S7).
    """
    start = time.time()
    observation = observe_case(
        case, base_result.stdout, valid_opcodes, base_result.disassembly)
    # The observation now holds the aggregated measurements; the raw captures
    # are dropped so a large control run does not retain every case's log.
    base_result.stdout = ""
    base_result.disassembly = None
    instances = build_control_instances(case, observation, opcode_order)
    digests_before = digest_case_inputs(case)
    results: List[ControlInstanceResult] = []
    for instance in instances:
        blocker = control_stage_blocker(instance)
        if blocker is not None:
            results.append(ControlInstanceResult(
                instance, CONTROL_VERDICT_INVALID, reason=blocker))
            continue
        if not base_result.passed or instance.inapplicable_reason is not None \
                or instance.construction_error is not None:
            # No variant run without a passing base case and without a usable
            # construction: the verdict stays explicit, never a pass.
            results.append(evaluate_control_instance(
                instance, base_result.passed, []))
            continue
        try:
            variant = materialize_variant(case, instance, artifacts_root)
            parse_annotations(variant, valid_opcodes)
            variant_result = run_case(
                variant, cfg, args, artifacts_root, log_fragment_res, valid_opcodes)
        except (OSError, ValueError) as error:
            results.append(ControlInstanceResult(
                instance, CONTROL_VERDICT_INVALID,
                reason=f"variant materialisation failed: {error}"))
            continue
        results.append(replace(
            evaluate_control_instance(
                instance, base_result.passed, list(variant_result.reasons),
                variant_result.stdout),
            evidence=control_instance_evidence(
                instance, list(variant_result.reasons))))
    digests_after = digest_case_inputs(case)
    unchanged = case_tree_unchanged(digests_before, digests_after)
    return (evaluate_case_control(case, base_result.passed, results, unchanged),
            time.time() - start)


def write_control_reports(artifacts_root: Path, report: ControlsReport) -> None:
    """Write the mandatory `controls.md` and `controls.json` (S7)."""
    (artifacts_root / "controls.md").write_text(
        render_controls_md(report), encoding="utf-8")
    (artifacts_root / "controls.json").write_text(
        render_controls_json(report), encoding="utf-8")


# ---------------------------------------------------------------------------
# Per-case pipeline and main
# ---------------------------------------------------------------------------


def run_case(
    case: TestCase,
    cfg: BuildConfig,
    args: argparse.Namespace,
    artifacts_root: Path,
    log_fragment_res: List[re.Pattern],
    valid_opcodes: Optional[set],
) -> CaseResult:
    result = CaseResult(case.rel)
    start = time.time()
    art_dir = artifacts_root / case.rel
    art_dir.mkdir(parents=True, exist_ok=True)

    if case.config_errors:
        result.reasons.extend(case.config_errors)
        result.duration = time.time() - start
        return result

    abc_path = art_dir / f"{case.entry}.abc"
    ok, message = compile_case(case, abc_path, cfg)
    if not ok:
        result.reasons.append(f"compilation failed: {message}")
        result.duration = time.time() - start
        return result

    disasm_path = art_dir / f"{case.entry}.disasm.txt"
    disasm_error = disassemble_case(case, abc_path, disasm_path, cfg, result.log)
    bytecode_errors: List[str] = []
    if case.bytecode_constraints:
        if disasm_error is not None:
            bytecode_errors.append(
                f"bytecode constraints unavailable: {disasm_error}")
        else:
            bytecode_errors.extend(
                check_bytecode_constraints(disasm_path, case.bytecode_constraints))
    result.reasons.extend(bytecode_errors)
    if getattr(args, "sensitivity_controls", False) and disasm_error is None:
        # The control stage observes BYTECODE_COUNT assertions on the base
        # run; the disassembly is not read again for a default run.
        try:
            result.disassembly = disasm_path.read_text(
                encoding="utf-8", errors="replace")
        except OSError:
            result.disassembly = None

    cmd, returncode, stdout, stderr, timed_out = execute_abc(
        case, abc_path, cfg, args, result.log)
    result.repro_cmd = build_repro_command(cmd, cfg)
    if getattr(args, "sensitivity_controls", False):
        # Retained only for the control stage's base observation; a run without
        # the flag keeps no reference to the (large) raw output.
        result.stdout = stdout
    (art_dir / f"{case.entry}.stdout.txt").write_text(stdout, encoding="utf-8")
    (art_dir / f"{case.entry}.stderr.txt").write_text(stderr, encoding="utf-8")

    if timed_out:
        result.reasons.append("execution timed out")
    elif returncode is None:
        result.reasons.append("ark_js_vm could not be executed")
    else:
        if is_crash(returncode):
            result.reasons.append(
                f"ark_js_vm crashed with {crash_signal_name(returncode)} "
                f"(return code {returncode})"
            )
        elif returncode != 0:
            result.reasons.append(f"ark_js_vm exited with return code {returncode}")

    # Evidence annotations imply extra log components (E7: GC_COUNT_GE -> gc),
    # so those component lines must also be filtered out of the expected-output
    # comparison for this case. The deopt traces implied by DEOPT_COUNT* are
    # multi-line log messages (event line, stack-trace frames, message
    # newline): they are removed as one record first, before the plain
    # per-component filter cuts only their first line.
    case_fragment_res = list(log_fragment_res)
    if _needs_deopt_trace(case):
        case_fragment_res.insert(0, DEOPT_TRACE_LOG_RE)
    for component in _implied_log_components(case):
        case_fragment_res.append(
            re.compile(r"\[%s\] [^\n]*\n?" % re.escape(component)))
    diff = compare_output(stdout, case.expected, case_fragment_res)
    if diff is not None:
        result.reasons.append(f"stdout mismatch:\n{diff}")

    constraint_errors = check_constraints(
        case.constraints, collect_opcode_counts(stdout, valid_opcodes)
    )
    if args.verbose and case.constraints and not constraint_errors:
        detail: List[str] = []
        for method, constraints in case.constraints.items():
            detail.append(f"METHOD {method}:")
            detail.extend(f"  {constraint}" for constraint in constraints)
        result.log.append("All IR constraints are satisfied:\n" + "\n".join(detail))
    result.reasons.extend(constraint_errors)
    codegen_errors = check_codegen_constraints(
        case.codegen_constraints, collect_codegen_comments(stdout)
    )
    if args.verbose and case.codegen_constraints and not codegen_errors:
        codegen_detail: List[str] = []
        for method, constraints in case.codegen_constraints.items():
            codegen_detail.append(f"METHOD {method}:")
            codegen_detail.extend(f"  {constraint.label()}" for constraint in constraints)
        result.log.append(
            "All codegen constraints are satisfied:\n" + "\n".join(codegen_detail))
    result.reasons.extend(codegen_errors)
    evidence_errors = check_evidence_constraints(
        case.evidence_constraints, collect_evidence(stdout)
    )
    if args.verbose and case.evidence_constraints and not evidence_errors:
        evidence_detail: List[str] = [
            f"  {constraint.label()}" for constraint in case.evidence_constraints
        ]
        result.log.append(
            "All evidence constraints are satisfied:\n" + "\n".join(evidence_detail))
    result.reasons.extend(evidence_errors)
    if args.verbose and case.bytecode_constraints and not bytecode_errors:
        bytecode_detail: List[str] = []
        for method, constraints in case.bytecode_constraints.items():
            bytecode_detail.append(f"METHOD {method}:")
            bytecode_detail.extend(
                f"  {constraint.label()}" for constraint in constraints
            )
        result.log.append(
            "All bytecode constraints are satisfied:\n" +
            "\n".join(bytecode_detail))
    result.reasons.extend(check_live_ranges(stdout))
    result.duration = time.time() - start
    return result


def make_artifacts_root(platform: str, mode: str) -> Path:
    stamp = time.strftime("%Y%m%d-%H%M%S")
    root = Path("/tmp") / f"arksteed-{platform}-{mode}-{stamp}"
    suffix = 1
    while root.exists():
        root = Path("/tmp") / f"arksteed-{platform}-{mode}-{stamp}-{suffix}"
        suffix += 1
    root.mkdir(parents=True)
    return root


MAX_LISTED_FAILED_CASES = 20


def format_failed_case_lines(failed: List[CaseResult]) -> List[str]:
    """Terminal listing of failed cases, capped with an overflow hint."""

    lines = [f"  {r.rel}" for r in failed[:MAX_LISTED_FAILED_CASES]]
    hidden = len(failed) - MAX_LISTED_FAILED_CASES
    if hidden > 0:
        lines.append(f"  ... and {hidden} more (all listed in summary.md)")
    return lines


def write_summary_md(
    path: Path,
    results: List[CaseResult],
    total_cases: int,
    cfg: BuildConfig,
    elapsed: float,
    wall: float,
    stopped_early: bool,
    controls_section: Optional[str] = None,
) -> None:
    """Write a markdown summary listing every failed case with its reasons."""
    failed = [r for r in results if not r.passed]
    lines = [
        "# ArkSteed Test Run Summary",
        "",
        f"- Date: {time.strftime('%Y-%m-%d %H:%M:%S')}",
        f"- Platform: `{cfg.platform}`  Mode: `{cfg.mode}`",
        f"- Total: {len(results)}  Passed: {len(results) - len(failed)}  "
        f"Failed: {len(failed)}",
        f"- Elapsed: {elapsed:.1f}s  Wall: {wall:.1f}s",
    ]
    if stopped_early:
        lines.append(f"- Stopped on first failure; {total_cases - len(results)} case(s) not run")
    lines.append("")
    if not failed:
        lines.append("No failed case.")
    else:
        lines.append(f"## Failed Cases ({len(failed)})")
        lines.append("")
        for result in failed:
            lines.append(f"### `{result.rel}` ({result.duration:.1f}s)")
            lines.append("")
            for reason in result.reasons:
                lines.append("```")
                lines.append(reason)
                lines.append("```")
                lines.append("")
            if result.repro_cmd is not None:
                lines.append("Reproduce under GDB (single copy-paste):")
                lines.append("")
                lines.append("```bash")
                lines.append(result.repro_cmd)
                lines.append("```")
                lines.append("")
    if controls_section is not None:
        # The control totals and the control time are reported separately from
        # the case timing; a run without the flag never adds this section.
        lines.append("")
        lines.append(controls_section)
    try:
        path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")
    except OSError as e:
        print(f"warning: failed to write {path}: {e}", file=sys.stderr)


def detect_git_head() -> Optional[str]:
    """Revision of the checkout the run reports (run metadata and report)."""
    try:
        result = subprocess.run(
            ["git", "-C", str(SCRIPT_DIR), "rev-parse", "HEAD"],
            capture_output=True, text=True,
        )
    except OSError:
        return None
    return result.stdout.strip() or None


def write_run_meta(path: Path, args: argparse.Namespace, cfg: BuildConfig) -> None:
    git_head = detect_git_head()
    meta = {
        "argv": sys.argv,
        "platform": cfg.platform,
        "mode": cfg.mode,
        "git_head": git_head,
        "ark_js_vm": str(cfg.ark_js_vm),
        "es2abc": str(cfg.es2abc),
        "ark_disasm": str(cfg.ark_disasm),
    }
    path.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Test case runner for the ArkSteed JIT compiler."
    )
    parser.add_argument(
        "-p", "--platform", choices=("x64", "arm64"), default=None,
        help="Target platform; arm64 runs under QEMU (default: host platform)."
    )
    parser.add_argument(
        "-m", "--mode", choices=("debug", "release"), default="debug",
        help="Build mode to use (default: debug)."
    )
    parser.add_argument("-f", "--skip-stub", action="store_true",
                        help="Skip building stub.an (safe only if stub IR is unchanged).")
    parser.add_argument("-F", "--skip-build", action="store_true",
                        help="Skip building all the targets.")
    parser.add_argument("-I", "--include", "--filter", dest="include", action="append",
                        default=[], metavar="DIR",
                        help="Keep only the test cases under the given directory prefix; "
                             "repeatable.")
    parser.add_argument("-X", "--exclude", action="append", default=[], metavar="DIR",
                        help="Exclude the test cases under the given directory prefix; "
                             "repeatable.")
    parser.add_argument("-d", "--log-debug", action="store_true",
                        help="Set the runtime log level to debug.")
    parser.add_argument("-l", "--log-components", default="", metavar="COMPONENTS",
                        help="Log components besides the always-enabled compiler log, "
                             "e.g. -l gc.")
    parser.add_argument("--print-asm-code", action="store_true",
                        help="Dump generated ASM code (with comments) to the compiler log.")
    parser.add_argument("-c", "--colored", action="store_true",
                        help="Enable colored text on the terminal.")
    parser.add_argument("-C", "--fully-colored", action="store_true",
                        help="Enable colored text both on the terminal and in the "
                             "compiler log.")
    parser.add_argument("-s", "--stops-on-error", action="store_true",
                        help="Stop the runner as soon as any test case fails.")
    parser.add_argument(
        "--sensitivity-controls", action="store_true", default=False,
        help="Control mode: turn every selected case into a mandatory set of "
             "derived, knowingly wrong variants (classes JIT-off, expectation "
             "substitution, count perturbation, address substitution), run the "
             "base case plus one variant per instance under the run artifacts "
             "and report the failure class, the verdict and the reason per case "
             "and per class (controls.md/controls.json plus the summary "
             "section). Off by default; not part of the default set and not "
             "hooked into the project verifier. Class JIT-off is not built in "
             "this stage.")
    parser.add_argument("-j", "--num-workers", type=int, default=1, metavar="N",
                        help="Number of parallel workers (default: 1, serial).")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Show verbose information.")
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    if args.num_workers < 1:
        print("Error: --num-workers expects a positive integer", file=sys.stderr)
        return 2
    colored = args.colored or args.fully_colored
    extras = [c.strip() for c in args.log_components.split(",") if c.strip()]
    # The compiler log is unconditionally enabled and cannot be removed via
    # -l; the -l items are added on top (duplicates dropped).
    components = ["compiler"] + [c for c in dict.fromkeys(extras) if c != "compiler"]
    # LogComponent lines are prefixed with "[<component>]"; filter exactly the
    # enabled ones so that program output is never mistaken for a log line.
    # The VM parses --log-components as a COLON-separated list - a comma in
    # the value becomes one unknown component name and silences ALL logs.
    args.log_components_value = ":".join(components)
    # Each enabled component logs lines prefixed with "[<component>] "; those
    # fragments are cut out of the raw stdout before comparison (wherever they
    # occur - see compare_output).
    log_fragment_res = [re.compile(r"\[%s\] [^\n]*\n?" % re.escape(c)) for c in components]

    # Resolve lazily so an explicit -p works even on an unsupported machine.
    if args.platform is None:
        args.platform = detect_host_platform()
    cfg = BuildConfig(args.mode, args.platform)
    if cfg.is_cross_build and cfg.platform == "arm64" \
            and not shutil.which("qemu-aarch64-static") \
            and not shutil.which("qemu-aarch64"):
        print("Error: qemu-aarch64 (or qemu-aarch64-static) is required for cross-platform "
              "ARM64 testing", file=sys.stderr)
        return 2

    if not args.skip_build:
        if not build_ark(cfg, args.skip_stub):
            return 2
    if not check_arksteed_gn_args(cfg):
        return 2
    for binary in (cfg.ark_js_vm, cfg.es2abc):
        if not binary.exists():
            print(f"Error: {binary} does not exist", file=sys.stderr)
            return 2

    if args.sensitivity_controls:
        # The address-substitution class needs the header's own declaration
        # order as the candidate order; the header is read once.
        opcode_order = load_opcode_order()
        valid_opcodes = None if opcode_order is None else set(opcode_order)
    else:
        opcode_order = None
        valid_opcodes = load_opcode_names()
    cases = discover_cases(args.include, args.exclude, args.verbose)
    if not cases:
        print("No test case selected", file=sys.stderr)
        return 2
    for case in cases:
        parse_annotations(case, valid_opcodes)
    if args.sensitivity_controls:
        usage_error = control_usage_error(cases, opcode_order)
        if usage_error is not None:
            print(f"Error: {usage_error}", file=sys.stderr)
            return 2
    print(f"Collected {len(cases)} test case(s) for {cfg.platform}.{cfg.mode}\n")

    artifacts_root = make_artifacts_root(cfg.platform, args.mode)
    write_run_meta(artifacts_root / "run.meta.json", args, cfg)

    results: List[CaseResult] = []
    stopped_early = False
    control_results: Dict[str, CaseControlResult] = {}
    control_elapsed = 0.0
    controls_report: Optional[ControlsReport] = None

    def report(result: CaseResult,
               control_result: Optional[CaseControlResult] = None) -> None:
        badge = "[PASS]" if result.passed else "[FAIL]"
        if colored:
            badge = (ANSI_GREEN if result.passed else ANSI_RED) + badge + ANSI_RESET
        print(f"[{len(results):>4}/{len(cases)}] {badge} {result.rel}")
        for message in result.log:
            print("         " + message.replace("\n", "\n         "))
        for reason in result.reasons:
            print("         " + reason.replace("\n", "\n         "))
        if control_result is None:
            return
        print(f"         [controls] {control_result.verdict}")
        for control_class in control_result.classes:
            detail = (f"           {control_class.class_token}: "
                      f"{control_class.verdict}")
            if control_class.reason:
                detail += f" ({control_class.reason})"
            print(detail.replace("\n", " "))

    def run_one(case: TestCase
                ) -> Tuple[CaseResult, Optional[CaseControlResult], float]:
        result = run_case(
            case, cfg, args, artifacts_root, log_fragment_res, valid_opcodes)
        if not args.sensitivity_controls:
            return result, None, 0.0
        case_control, duration = run_case_controls(
            case, result, cfg, args, artifacts_root, log_fragment_res,
            valid_opcodes, opcode_order)
        return result, case_control, duration

    def account(control_result: Optional[CaseControlResult],
                duration: float) -> None:
        nonlocal control_elapsed
        if control_result is None:
            return
        control_results[control_result.rel] = control_result
        control_elapsed += duration

    wall_start = time.time()
    if args.num_workers > 1:
        with ThreadPoolExecutor(max_workers=args.num_workers) as pool:
            futures = {pool.submit(run_one, case): case for case in cases}
            for future in as_completed(futures):
                result, control_result, duration = future.result()
                results.append(result)
                account(control_result, duration)
                report(result, control_result)
                if args.stops_on_error and not result.passed:
                    stopped_early = True
                    for pending in futures:
                        pending.cancel()
                    break
    else:
        for case in cases:
            result, control_result, duration = run_one(case)
            results.append(result)
            account(control_result, duration)
            report(result, control_result)
            if args.stops_on_error and not result.passed:
                stopped_early = True
                break

    failed = [r for r in results if not r.passed]
    elapsed = sum(r.duration for r in results)
    wall = time.time() - wall_start
    if args.sensitivity_controls:
        # Report in selection order, so the artifacts do not depend on the
        # completion order of parallel workers; a case left uncontrolled by
        # --stops-on-error keeps the explicit `not-run` verdict (S7).
        ordered_controls = [control_results.get(case.rel) or not_run_case_control(case)
                            for case in cases]
        controls_report = ControlsReport(
            ControlsHeader(True, cfg.platform, args.mode, detect_git_head(),
                           tuple(args.include), tuple(args.exclude), len(cases)),
            tuple(ordered_controls), control_elapsed,
            all(case_control.source_tree_unchanged
                for case_control in ordered_controls))
        write_control_reports(artifacts_root, controls_report)
    write_summary_md(
        artifacts_root / "summary.md", results, len(cases), cfg,
        elapsed, wall, stopped_early,
        None if controls_report is None
        else render_summary_controls_section(controls_report),
    )
    if stopped_early:
        print(f"\nStopped on first failure; {len(cases) - len(results)} case(s) not run")
    failed_label = f"Failed: {len(failed)}"
    if colored and failed:
        failed_label = ANSI_RED + failed_label + ANSI_RESET
    print(f"\nTotal: {len(results)}  Passed: {len(results) - len(failed)}  "
          f"{failed_label}  Elapsed: {elapsed:.1f}s  Wall: {wall:.1f}s")
    if failed:
        print("\nFailed cases:")
        for line in format_failed_case_lines(failed):
            print(line)
    exit_code = 1 if failed else 0
    if controls_report is not None:
        red = [case_control for case_control in controls_report.cases
               if case_control.verdict != "green"]
        print(f"\nSensitivity controls: cases={len(controls_report.cases)} "
              f"red={len(red)} elapsed={controls_report.control_elapsed:.1f}s")
        for case_control in red:
            reasons = "; ".join(reason.replace("\n", " ")
                                for reason in case_control.reasons)
            print(f"  [CONTROL-RED] {case_control.rel}: {reasons}")
        if red:
            exit_code = 1
    print(f"\nArtifacts: {artifacts_root}")
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
