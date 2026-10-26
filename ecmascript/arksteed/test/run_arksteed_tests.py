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

With `--corpus` the runner selects the external corpus case class instead of
the embedded one (design `arksteed-corpus-case-class-and-parity`, C1/C2): the
corpus root comes from the `ARKSTEED_EXTERNAL_TESTS` environment variable, the
corpus runner module is imported lazily as a read-only library and its case
identifiers live in the `corpus/<suite>/<case>` namespace. Without the flag
the corpus module is never imported and the run is unchanged. Every selected
case also gets the mandatory reference execution of the module itself (design
C7): the verdict the corpus runner produces for the case with its own
defaults and its own budget is recorded per case and compared with the class
base verdict as `matched`, `diverged` or `not-applicable`. The behavioural
class defaults are derived from the loaded module (its `BASE_ARGS`, its
`ALWAYS_ARGS` and its own CLI defaults, design C5) and are cross-checked
against the reference namespace before the case loop; the optional per-case
descriptor `test/corpus/<suite>/<case>.annotations` reuses the `//!` grammar
and adds the class-specific `TIMEOUT <seconds> <justification>` and
`NO_EXEC_MARKER <justification>` keys, whose problems are config errors of a
red case and never a skip. Each selected case is then compiled with the
module's own `compile_single`/`compile_with_file_info` and run by the
class-specific streaming executor (design C4): stdout and stderr are read as
streams, the compilation and execution markers are counted while the stream
passes, the filtered program output is kept under the class limit and the
marker-dominated component trace is never captured. The corpus program output
is the result of exactly the same ordered substitution chain the embedded
expected-output comparison applies (`case_filter_chain`, design
`arksteed-corpus-filter-and-consumer-integration` S1-S5): the GC record, the
deopt record, then one stage per ordered explicit and implied component name,
executed by incremental transducers over the decoded stream. The raw
observation branch counts the markers and named descriptor patterns before any
deletion and writes the bounded retained component log. The program-output
limit, the per-record-stage candidate limit and the retained-log limit are
independent; exceeding one, or failing to read/filter/observe the stream, is a
named sticky invalidity (`program_output_truncated`, `record_candidate_capped`,
`program_output_error`) that a consumer must treat as red and never as a
complete, comparable or empty result. The class-level
JIT-activity invariant (`COMPILE_COUNT_GE * 1` and, unless a justified
`NO_EXEC_MARKER` descriptor exempts it, `EXEC_COUNT_GE * 1`) is injected into
every corpus case before the loop (design C3), and an overrun of a class
limit is a red case with a named reason, never a skip.

Exit status is 0 iff every selected case passes.
"""

import argparse
from builtins import str
import codecs
import collections
import difflib
import fnmatch
from fnmatch import fnmatchcase
import hashlib
import importlib.util
import inspect
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import threading
import time
from abc import ABC, abstractmethod
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any, Callable, ClassVar, Dict, List, NamedTuple, Optional, Tuple

SCRIPT_DIR = Path(__file__).resolve().parent
TEST_DIR = SCRIPT_DIR
# The build tree of a plain runtime checkout is the directory four levels above
# the test directory. A per-task checkout under `workdirs/<WD>` has no `out/`
# there: the project verifier links and builds the checkout in the workspace's
# `builds/<WD>` context instead. `ARKSTEED_ARK_ROOT` names that build context so
# a recorded command can be executed verbatim from the test directory; when the
# variable is unset the historical resolution above is unchanged.
ARK_ROOT_ENV_VAR = "ARKSTEED_ARK_ROOT"
_ENV_ARK_ROOT = os.environ.get(ARK_ROOT_ENV_VAR, "").strip()
ARK_ROOT = Path(_ENV_ARK_ROOT) if _ENV_ARK_ROOT else SCRIPT_DIR.parents[4]
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


# GC record parser required by the selected ISSUE-0108 filter chain.
GC_TRACE_LOG_RE = re.compile(
    r"(?ms)^\[gc\][^\n]*GCReason:[^\n]*\n.*?^\[gc\] HandleStorage[^\n]*\n"
)

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

# External corpus class (`--corpus`). The corpus root is resolved from the
# environment only (there is no implicit default), the corpus runner module is
# a read-only library and its case identifiers live in the `corpus/<rel>`
# namespace. Nothing in the corpus tree is written: the import runs with the
# bytecode cache disabled and is restored afterwards.
CORPUS_ROOT_ENV_VAR = "ARKSTEED_EXTERNAL_TESTS"
CORPUS_NAMESPACE = CASE_CLASS_CORPUS
CORPUS_RUNNER_FILENAME = "run_arksteed_external_tests.py"
CORPUS_MODULE_NAME = "arksteed_external_corpus_runner"
CORPUS_RECOMMENDED_ROOT = ("misc/src/arksteed_external_tests (relative to the "
                           "project workspace)")

# The API of the corpus runner module the corpus class reuses as a library.
# A missing element is a usage error before any case runs, never a silent
# skip: the adapter must not reimplement the corpus rules, and a renamed API
# has to fail loudly instead of silently changing the verdict rule.
CORPUS_REQUIRED_API = (
    "discover_cases",
    "compile_single",
    "compile_with_file_info",
    "run_case",
    "parse_args",
    "BuildConfig",
    "TestCase",
    "CaseKind",
    "BASE_ARGS",
    "ALWAYS_ARGS",
    "EXECUTION_TIMEOUT",
    "TEST_ROOT",
    "detect_host_platform",
    "is_crash",
    "crash_signal_name",
)

# A selected corpus case is executed by the class-specific streaming executor
# of the section below once its source is compiled by the corpus runner
# module itself; the class base verdict, the JIT-activity invariant and the
# parity are reported per case.

# Base verdicts of a corpus case and the verdict-parity field (C3, C7, C10).
# The values are the module's own `CaseStatus` values: parity is defined on the
# base verdict, never on stdout.
CORPUS_VERDICT_PASS = "PASS"
CORPUS_VERDICT_FAIL = "FAIL"
CORPUS_VERDICT_TIMEOUT = "TIMEOUT"
CORPUS_PARITY_MATCHED = "matched"
CORPUS_PARITY_DIVERGED = "diverged"
# `not-applicable` is reserved for a case whose descriptor gave a config error:
# such a case has no executions at all and is red by the descriptor itself.
CORPUS_PARITY_NOT_APPLICABLE = "not-applicable"

# The reference execution of the corpus runner module owns its own subtree of
# the run artifacts (the module writes its abc, stdout and stderr captures of
# one case under `<case.rel>/`), so it never shares a path with the class run.
CORPUS_REFERENCE_ARTIFACTS_DIR = "corpus-reference"
# The module's own reproducer of the reference execution, kept next to its
# artifacts: a divergence reports it together with the class command (C7).
CORPUS_REFERENCE_COMMAND_FILENAME = "reference.cmd.txt"

# The optional per-case descriptor of the corpus class (C5). A corpus case is
# `corpus/<suite>/<case>`; its descriptor is the file
# `test/corpus/<suite>/<case>.annotations` in the runtime repository, in the
# existing `//!` grammar plus the class-specific keywords below. The corpus
# tree is read-only, so the descriptor is the only annotation channel of a
# corpus case: the read-only corpus source is never scanned for `//!` lines.
# The directory holds no JS/TS source and no `expected_output.txt`, so the
# embedded discovery never selects anything from it.
CORPUS_DESCRIPTOR_DIRNAME = CASE_CLASS_CORPUS
CORPUS_DESCRIPTOR_SUFFIX = ".annotations"
CORPUS_DESCRIPTOR_KEYWORDS = ("TIMEOUT", "NO_EXEC_MARKER")

# Class execution and evidence collection of the corpus class (C4). The class
# run is streamed: stdout and stderr are read as streams, the compilation and
# execution markers are counted while the stream passes, the filtered program
# output is kept (under the program-output limit), and the component log lines
# *without* the execution markers go to a temporary log that lives only until
# the evidence pass and is then deleted. The limits bound what the run may
# hold: a case whose filtered program output exceeds the limit is red with the
# named reason (`program output exceeds cap`), and a case whose descriptor
# declares a method-scoped (IR/codegen) constraint and whose retained log
# exceeds the class log limit reports that constraint evidence is unavailable
# (`constraint evidence unavailable: compiler log exceeds cap`). A limit is
# never a silent skip.
CORPUS_PROGRAM_OUTPUT_CAP = 1024 * 1024
# The retained component log is the CFG dump and the other component lines
# without the marker-dominated execution trace (measured at hundreds of
# megabytes per case for the markers alone); a single CFG dump is tens of
# kilobytes, so this limit bounds memory while leaving thousands of
# compilations of constraint evidence available.
CORPUS_COMPILER_LOG_CAP = 64 * 1024 * 1024
# The unresolved decoded text of each enabled record stage of the corpus
# filter is separately bounded (S3/F4): a genuinely unresolved candidate is
# charged, removable complete matches and safely emitted text are not, and an
# earlier stage's limit never reduces a later stage's limit. The final program
# output and the retained component log keep their own existing limits.
CORPUS_RECORD_CANDIDATE_CAP = 4 * 1024 * 1024
# The runner-managed observation option the injected execution requirement
# implies for the case (E7): a corpus case never names it itself.
CORPUS_EXEC_MARKER_OPTION = "--compiler-arksteed-print-method-name"
# The artifact names of one class execution (C6).
CORPUS_PROGRAM_OUTPUT_SUFFIX = ".program.txt"
CORPUS_CLASS_COMMAND_SUFFIX = ".cmd.txt"
CORPUS_LIVE_LOG_SUFFIX = ".compiler.log"
# The interpreter twins of the differential evidence (TW1/TW2): the actual
# class command with the value of its single `--compiler-enable-jit` token
# replaced by `false`, run as two fresh sequential processes in the per-case
# class budget. The twin's own temporary component logs are distinct files so
# the three executions of one case never share a log sink (C4).
CORPUS_JIT_OPTION = "--compiler-enable-jit"
CORPUS_JIT_OFF_VALUE = "false"
CORPUS_TWIN_LOG_SUFFIXES = (".twin1.compiler.log", ".twin2.compiler.log")
# The two differential statuses the execution-eligibility gate of this slice
# decides (TW2/TW3). The complete five-status vocabulary, the mask and the
# class comparison belong to the classification change of the same epic; the
# status of a launched twin pair is therefore left unset here.
CORPUS_DIFFERENTIAL_NOT_COMPARABLE = "not-comparable"
CORPUS_DIFFERENTIAL_SKIPPED_EMPTY = "skipped-empty-program-output"
# The numeric-field grammar of the twin-only reproducibility mask (TW4): a
# maximal left-to-right match of an optionally signed decimal integer or
# fraction (including the leading-dot form `[0-9]+(?:\.[0-9]*)?|\.[0-9]+`) with
# an optional exponent. A word character or a dot is forbidden on either side,
# and the boundaries are Python's Unicode-aware `\w`, so digits inside
# identifiers, hexadecimal spellings, `NaN` and `Infinity` stay exact text.
NUMERIC_FIELD_RE = re.compile(
    r"(?<![\w.])[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)"
    r"(?:[eE][+-]?[0-9]+)?(?![\w.])"
)

# The injected class-level invariant of the JIT activity (C3): every corpus
# case must show at least one compilation marker and, unless a descriptor
# exempts the execution marker with a justification, at least one execution
# marker. The wildcard pattern matches every method name. The injected
# requirements are class-managed: a descriptor may add requirements, but it can
# never remove, weaken or cancel them, and a zero compilation marker can never
# be exempted.
CORPUS_INVARIANT_PATTERN = "*"
CORPUS_INVARIANT_MIN = 1
# Invariant states the report carries (C10).
CORPUS_INVARIANT_BOTH = "compile+exec"
CORPUS_INVARIANT_COMPILE_ONLY = "compile-only"
CORPUS_INVARIANT_NONE = "none"


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
    # Mode-local class-1 override of the control stage: the derived `jit-off`
    # variant is a plain copy of the case whose VM command line carries
    # `--compiler-enable-jit=false` *instead of* the runner default and of any
    # value the case names itself. The flag is set only by the control stage's
    # variant materialisation, so a run without `--sensitivity-controls`
    # never builds such a command line (S1, acceptance criterion 6).
    jit_off: bool = False
    # External corpus case class: the `TestCase` object of the corpus runner
    # module this selected case was discovered from (`None` for an embedded
    # case). The corpus class reuses the module's own discovery, compilation
    # and per-case path, so the adapter keeps the module's object instead of
    # re-deriving the case shape.
    corpus_case: Optional[Any] = None
    # External corpus case class: the `test/corpus/<suite>/<case>.annotations`
    # descriptor of this case when the file exists (C5). `None` for an
    # embedded case and for a corpus case without a descriptor.
    descriptor_path: Optional[Path] = None
    # `TIMEOUT <seconds> <justification>` of that descriptor: the absolute
    # class budget override of the case, with the justification the report
    # carries (C5, C6).
    corpus_timeout_seconds: Optional[int] = None
    corpus_timeout_reason: Optional[str] = None
    # `NO_EXEC_MARKER <justification>` of that descriptor: the justified
    # exemption from the execution-marker half of the class invariant (C3,
    # C5). The compilation marker can never be exempted.
    corpus_no_exec_marker_reason: Optional[str] = None

    @property
    def entry(self) -> str:
        return self.source.stem


@dataclass
class CaseResult:
    rel: str
    reasons: List[str] = field(default_factory=list)
    duration: float = 0.0
    repro_cmd: Optional[str] = None
    # Run-level usage error (TW1): the actual class command of this case
    # carries no or more than one `--compiler-enable-jit` token, so no twin can
    # be derived and `main` exits 2 instead of reporting a verdict.
    usage_error: Optional[str] = None
    # Per-case runner messages (verbose cmd line, disasm warnings); printed
    # together with the verdict so parallel runs stay grouped per case.
    log: List[str] = field(default_factory=list)
    # External corpus case class: the mandatory reference execution of the
    # corpus runner module for this case (C7). `None` for an embedded case and
    # for a corpus case whose descriptor gave a config error (no executions).
    corpus_reference: Optional[Any] = None
    # External corpus case class: the class execution of this case with the
    # streamed evidence (C4). `None` for an embedded case, for a corpus case
    # whose descriptor gave a config error and for a case whose compilation
    # failed before any execution.
    corpus_execution: Optional[Any] = None
    # External corpus case class: the interpreter twin run of this case with
    # both attempts, their full execution facts and the eligibility status
    # (TW1/TW2). `None` for an embedded case and for a corpus case whose
    # descriptor gave a config error (no executions at all).
    corpus_twins: Optional[Any] = None
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
    """Fill case options and constraints from the source; record problems."""

    try:
        lines = case.source.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as e:
        case.config_errors.append(f"cannot read source file: {e}")
        return
    _parse_annotation_lines(case, lines, valid_opcodes, corpus_class=False,
                            class_option_names=frozenset())


def corpus_case_relative(case: TestCase) -> str:
    """`<suite>/<case>` path of a corpus case inside the corpus root."""
    rel = getattr(case.corpus_case, "rel", None)
    if rel:
        return str(rel)
    prefix = CORPUS_NAMESPACE + "/"
    return case.rel[len(prefix):] if case.rel.startswith(prefix) else case.rel


def corpus_descriptor_path(case: TestCase) -> Path:
    """`test/corpus/<suite>/<case>.annotations` of a corpus case (C5)."""
    return (TEST_DIR / CORPUS_DESCRIPTOR_DIRNAME
            / f"{corpus_case_relative(case)}{CORPUS_DESCRIPTOR_SUFFIX}")


def parse_corpus_descriptor(
    case: TestCase, valid_opcodes: Optional[set], class_option_names,
) -> None:
    """Parse the optional corpus descriptor of a case (C5).

    The descriptor is `test/corpus/<suite>/<case>.annotations` and is the only
    annotation channel of a corpus case: the corpus source is never scanned
    for `//!` lines, so a stray annotation in read-only corpus code cannot
    create a configuration error. A missing descriptor is not an error; a
    descriptor problem is a config error that makes the case red, never a
    skip. `class_option_names` are the derived class defaults, which `PARAMS`
    must not override.
    """
    path = corpus_descriptor_path(case)
    if not path.is_file():
        return
    case.descriptor_path = path
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as e:
        case.config_errors.append(f"cannot read descriptor file: {e}")
        return
    _parse_annotation_lines(case, lines, valid_opcodes, corpus_class=True,
                            class_option_names=frozenset(class_option_names))


def _parse_corpus_timeout(case: TestCase, tokens: List[str], err) -> None:
    """`TIMEOUT <seconds> <justification>` of a corpus descriptor (C5).

    The value is an absolute positive integer number of seconds and the
    justification is mandatory: a budget override has to be explained and the
    justification is reported with the case.
    """
    if len(tokens) < 3:
        err("TIMEOUT expects '<seconds> <justification>'")
        return
    try:
        seconds = int(tokens[1])
    except ValueError:
        err(f"TIMEOUT expects a positive integer number of seconds, got "
            f"'{tokens[1]}'")
        return
    if seconds <= 0:
        err(f"TIMEOUT expects a positive integer number of seconds, got "
            f"'{tokens[1]}'")
        return
    if case.corpus_timeout_seconds is not None:
        err("TIMEOUT may appear at most once")
        return
    case.corpus_timeout_seconds = seconds
    case.corpus_timeout_reason = " ".join(tokens[2:])


def _parse_corpus_no_exec_marker(case: TestCase, tokens: List[str], err) -> None:
    """`NO_EXEC_MARKER <justification>` of a corpus descriptor (C3, C5)."""
    reason = " ".join(tokens[1:]).strip()
    if not reason:
        err("NO_EXEC_MARKER expects a justification")
        return
    if case.corpus_no_exec_marker_reason is not None:
        err("NO_EXEC_MARKER may appear at most once")
        return
    case.corpus_no_exec_marker_reason = reason


def _parse_annotation_lines(
    case: TestCase, lines: List[str], valid_opcodes: Optional[set],
    corpus_class: bool, class_option_names: frozenset,
) -> None:
    """Fill case options and constraints; record problems in config_errors."""

    compile_mode: Optional[str] = None
    params: Optional[List[str]] = None
    current_method: Optional[str] = None

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
                    elif corpus_class and name in class_option_names:
                        # A class default is derived from the corpus module
                        # configuration: overriding it through the descriptor
                        # would silently break the invariant or the parity.
                        err(f"'{name}' is a {CASE_CLASS_CORPUS} class default "
                            "(class-managed) and cannot be set via PARAMS")
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
        elif keyword in CORPUS_DESCRIPTOR_KEYWORDS:
            if not corpus_class:
                # The class-specific keys belong to the corpus descriptor
                # only: an embedded case carrying one is a configuration
                # error, not a silently ignored line.
                err(f"'{keyword}' is a {CASE_CLASS_CORPUS} descriptor keyword "
                    f"and cannot appear in an {CASE_CLASS_EMBEDDED} case")
            elif keyword == "TIMEOUT":
                _parse_corpus_timeout(case, tokens, err)
            else:
                _parse_corpus_no_exec_marker(case, tokens, err)
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


def _strip_option(tokens: List[str], name: str) -> List[str]:
    """Drop every token that sets `name` from a token list (S1 class 1).

    Covers both the `--opt=value` and the space-separated `--opt value` form,
    so a suppressed option cannot leave its value behind as a stray positional
    argument.
    """
    stripped: List[str] = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if _option_name(token) == name:
            index += 1
            if "=" not in token and index < len(tokens) \
                    and not tokens[index].startswith("--"):
                index += 1  # the separate value of the suppressed option
            continue
        stripped.append(token)
        index += 1
    return stripped


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

    params = list(case.params)
    if case.jit_off:
        # Class 1 `JIT-off`: the runner substitutes the value of the derived
        # variant rather than letting the case name it through `//! PARAMS`
        # (S1). The case's own `--compiler-enable-jit`, should it carry one, is
        # suppressed together with the runner default, so the variant command
        # line names the option exactly once and carries no contradicting
        # duplicate. The case source is never touched: only the command line of
        # this one variant changes.
        defaults = _strip_option(defaults, CONTROL_JIT_OPTION)
        params = _strip_option(params, CONTROL_JIT_OPTION)
        params.append(f"{CONTROL_JIT_OPTION}=false")

    cmd = cfg.command_prefix + [str(cfg.ark_js_vm)] + defaults + params
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


class FilterConfigurationError(Exception):
    """Named configuration error of the shared filter chain (S1).

    It is raised instead of returning an empty substitution list: an
    unsupported construction must become a named red configuration reason,
    never a silently unfiltered comparison. `str()` always starts with
    `filter configuration error: <detail>`, where `<detail>` names the
    offending input.
    """


# ---------------------------------------------------------------------------
# Shared filter chain of the built-in and corpus program-output paths (S1)
# ---------------------------------------------------------------------------
# The built-in expected-output comparison and the corpus streaming filter must
# apply the *same* ordered substitution chain, so both obtain it from the single
# constructor below: GC record (iff the case asserts GC evidence), deopt record
# (iff the case needs a deopt trace), then one component pattern per ordered
# explicit component name, then one per implied component name. Repetitions are
# preserved and no deduplication happens: the input is the ordered list of
# explicit names, not the command's effective observation set.


def _filter_component_names(components) -> List[str]:
    """Validated ordered explicit component names of one filter chain (S1)."""
    if components is None:
        raise FilterConfigurationError(
            "filter configuration error: components is None; an ordered sequence "
            "of explicit component names is required")
    try:
        names = list(components)
    except TypeError as error:
        raise FilterConfigurationError(
            f"filter configuration error: components is not iterable: "
            f"{type(components).__name__}") from error
    for name in names:
        if not isinstance(name, str) or not name:
            raise FilterConfigurationError(
                f"filter configuration error: component name {name!r} is not a "
                f"non-empty string")
    return names


def _filter_case_names(case, components) -> List[str]:
    """Explicit names plus the validated evidence collection of the case (S1)."""
    names = _filter_component_names(components)
    constraints = getattr(case, "evidence_constraints", None)
    if constraints is None:
        raise FilterConfigurationError(
            f"filter configuration error: case {getattr(case, 'rel', case)!r} has "
            f"no evidence constraint collection")
    return names


def case_filter_component_names(case, components) -> List[str]:
    """Ordered explicit-then-implied component names of one chain (S1).

    The explicit names come first in the given order with repetitions kept, the
    implied names (annotation-derived, no deduplication) follow. This is the
    name sequence the pattern list of `case_filter_chain` is built from, and
    the streaming pipeline pairs its stage entries with it.
    """
    return _filter_case_names(case, components) + _implied_log_components(case)


def _case_filter_spec(case, components) -> List[Tuple[str, Optional[str]]]:
    """The exact ordered filter stages of one case (S1).

    `('gc', None)` iff the case asserts GC evidence, then `('deopt', None)` iff
    it needs a deopt trace, then one `('component', <name>)` entry for every
    explicit name followed by every implied name, in that order and with
    repetitions preserved.
    """
    spec: List[Tuple[str, Optional[str]]] = []
    # Validate the case's annotation collection and the component names first:
    # an unsupported construction must raise the named configuration error
    # before any annotation predicate reads the case.
    names = case_filter_component_names(case, components)
    if _case_has_evidence(case, GcCountGeConstraint):
        spec.append(("gc", None))
    if _needs_deopt_trace(case):
        spec.append(("deopt", None))
    for name in names:
        spec.append(("component", name))
    return spec


def _component_filter_literal(component: str) -> str:
    """The literal a component substitution pattern matches (`[<c>] `)."""
    return f"[{component}] "


def _component_pattern(component: str) -> re.Pattern:
    """The per-component substitution pattern of one chain entry (S1)."""
    return re.compile(r"\[%s\] [^\n]*\n?" % re.escape(component))


def _filter_pattern(kind: str, component: Optional[str]) -> re.Pattern:
    if kind == "gc":
        return GC_TRACE_LOG_RE
    if kind == "deopt":
        return DEOPT_TRACE_LOG_RE
    assert component is not None
    return _component_pattern(component)


def case_filter_chain(case, components) -> List[re.Pattern]:
    r"""The exact ordered substitution chain of one case (S1).

    Output: the GC record pattern iff the case asserts GC evidence, then the
    deopt record pattern iff the case needs a deopt trace, then one
    ``re.compile(r'\[<component>\] [^\n]*\n?')`` per element of `components` in
    the given order, then one per element of `_implied_log_components(case)`. The
    function is pure: no I/O, no retries, no timeouts and no mutation of `case`
    or `components`; equal inputs yield equivalent pattern/flag lists.

    An unsupported construction is a named `FilterConfigurationError` whose
    text starts with `filter configuration error:`, never an empty substitute
    list. An empty chain is correct only for a valid case without any stage.
    """
    return [_filter_pattern(kind, component)
            for kind, component in _case_filter_spec(case, components)]


def compare_output(
    stdout: str, expected_file: Path, case, components
) -> Optional[str]:
    """Return None on match (or skipped comparison), otherwise a unified diff.

    The substitution chain is obtained from `case_filter_chain` only; the
    remaining behaviour (expected-file UTF-8 replacement, unreadable-file
    diagnostic, empty-expectation skip, successive substitution, `splitlines`
    with `rstrip` and the unified diff) is the existing behaviour and is not
    part of the filter.
    """
    try:
        chain = case_filter_chain(case, components)
    except FilterConfigurationError as error:
        # A filter configuration error is never a match: it is reported as a
        # diagnostic so the caller turns the case red.
        return str(error)
    try:
        expected = expected_file.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        return f"cannot read {expected_file}: {e}"
    if not expected.strip():
        # Empty expectation means "nothing to check" (mirrors corpus convention).
        return None

    filtered = stdout
    for fragment_re in chain:
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
            mutation=(f"the runner substitutes {CONTROL_JIT_OPTION}=false "
                      f"into the derived variant's command line (the case "
                      f"never names the option through //! PARAMS)"),
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
    # Recorded observation of the executed variant: its own captured stdout.
    # The class-1 effectiveness rule is decided on the variant's stdout and not
    # on the base observation, so that text is what the report records for the
    # command-line-only class (S6). `stdout_bytes` and `stdout_path` name the
    # full capture that stays next to the variant in the run artifacts.
    observed_stdout: Optional[str] = None
    stdout_bytes: Optional[int] = None
    stdout_path: Optional[str] = None

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
    uncovered = tuple(pending.label() for pending in assertions.count_shaped
                      if pending.key() not in covered)

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
    if instance.class_token == CONTROL_CLASS_JIT_OFF:
        # Class 1 varies the VM command line only (S1/S5): the variant source
        # stays a byte-identical copy of the case and the runner substitutes
        # `--compiler-enable-jit=false` into the command it builds for this
        # variant. Nothing in the source tree is opened for writing.
        variant.jit_off = True
    elif instance.class_token == CONTROL_CLASS_EXPECTATION:
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


def _recorded_observation(result: "ControlInstanceResult") -> Optional[str]:
    """Recorded observation of one instance (S7).

    The class-1 variant has no count-shaped measurement: its observation is the
    stdout of the derived variant, on which the effectiveness of the JIT-off
    override is decided. Every other class records the observed count of the
    mutated measurement.
    """
    if result.class_token == CONTROL_CLASS_JIT_OFF \
            and result.observed_stdout is not None:
        return f"stdout: {result.observed_stdout}"
    return result.instance.observation


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
                    f"{_md_cell(instance.target)} | "
                    f"{_md_cell(_recorded_observation(result))} | "
                    f"{_md_cell(instance.mutation)} | {instance.expected_class} | "
                    f"{_md_cell(sorted(result.actual_classes))} | {result.verdict} | "
                    f"{_md_cell(result.evidence)} | {_md_cell(result.reason)} |")
        lines.append("")
        lines.append("### Assertion coverage")
        lines.append("")
        lines.append("| Assertion | Covered |")
        lines.append("|---|---|")
        covered_keys = {
            instance_result.instance.assertion.key()
            for instance_result in case.instance_results
            if instance_result.verdict == CONTROL_VERDICT_VALID
            and instance_result.class_token in (CONTROL_CLASS_COUNT, CONTROL_CLASS_ADDRESS)
            and instance_result.instance.assertion is not None
        }
        count_shaped = [
            instance_result.instance.assertion
            for instance_result in case.instance_results
            if instance_result.class_token == CONTROL_CLASS_COUNT
            and instance_result.instance.assertion is not None
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
                    "observation": _recorded_observation(result),
                    "derived": result.instance.derived,
                    "substitute": result.instance.substitute,
                    "candidate_rule": result.instance.candidate_rule,
                    "mutation": result.instance.mutation,
                    "expected_class": result.instance.expected_class,
                    "actual_classes": sorted(result.actual_classes),
                    "verdict": result.verdict,
                    "evidence": result.evidence,
                    "reason": result.reason,
                    "observed_stdout": result.observed_stdout,
                    "stdout_bytes": result.stdout_bytes,
                    "stdout_path": result.stdout_path,
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
# one variant run per instance of every applicable class (the class-1 `JIT-off`
# variant included), the per-case verdict, the report artifacts and the exit
# codes. Every class is executed by the same per-case pipeline and reported in
# the same `controls.md`/`controls.json` record.

# One report cell is one line: the recorded failure evidence is truncated.
CONTROL_EVIDENCE_LIMIT = 300

# The class-1 observation is the variant's own captured stdout. The report
# records a bounded prefix of it and, next to the variant, the size and the
# artifact-relative path of the full capture (`<entry>.stdout.txt`), so the
# effectiveness evidence stays auditable without bloating the report (S6/S7).
CONTROL_STDOUT_OBSERVATION_LIMIT = 500


def control_stdout_observation(stdout: str) -> str:
    """Bounded record of a variant's own captured stdout (S7, class 1)."""
    if len(stdout) <= CONTROL_STDOUT_OBSERVATION_LIMIT:
        return stdout
    return stdout[:CONTROL_STDOUT_OBSERVATION_LIMIT] + " ... [truncated]"


def control_stdout_path(variant: TestCase) -> str:
    """Artifact-relative path of a variant's captured stdout."""
    return f"{variant.rel}/{variant.entry}.stdout.txt"


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
    filter_components: List[str],
    valid_opcodes: Optional[set],
    opcode_order: Optional[List[str]],
) -> Tuple[CaseControlResult, float]:
    """Execute every derived variant of one case and return its verdict (S2).

    The base observation is built from the base run of the same case; every
    variant - the class-1 command-line variant included - is an independent
    case run in its own `controls/<case>/<variant>/` directory under the
    artifact root and uses the existing per-case pipeline and budget. The input
    digests are taken before the first variant and after the last one, so a
    violated isolation makes the case red (S5). The return value carries the
    control time separately from the case timing (S7).
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
                variant, cfg, args, artifacts_root, filter_components, valid_opcodes)
        except (OSError, ValueError) as error:
            results.append(ControlInstanceResult(
                instance, CONTROL_VERDICT_INVALID,
                reason=f"variant materialisation failed: {error}"))
            continue
        # The recorded observation is the variant's own stdout: the class-1
        # effectiveness rule and the class-2 mismatch diff are both read from
        # it, never from the base run.
        results.append(replace(
            evaluate_control_instance(
                instance, base_result.passed, list(variant_result.reasons),
                variant_result.stdout),
            evidence=control_instance_evidence(
                instance, list(variant_result.reasons)),
            observed_stdout=control_stdout_observation(variant_result.stdout),
            stdout_bytes=len(variant_result.stdout.encode("utf-8", "replace")),
            stdout_path=control_stdout_path(variant)))
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
    filter_components: List[str],
    valid_opcodes: Optional[set],
) -> CaseResult:
    result = CaseResult(case.rel)
    start = time.time()
    case_artifacts = artifacts_root / case.rel
    case_artifacts.mkdir(parents=True, exist_ok=True)

    if case.config_errors:
        result.reasons.extend(case.config_errors)
        result.duration = time.time() - start
        return result

    # The shared filter chain is built before the comparison evidence is
    # collected: an unsupported construction is a named red configuration
    # reason of the case, never an empty substitute. A valid case without any
    # stage yields the exact empty chain and an identical comparison.
    try:
        # Validation only: `compare_output` builds the same chain again at the
        # comparison boundary, so the built-in path cannot drift from it.
        case_filter_chain(case, filter_components)
    except FilterConfigurationError as error:
        result.reasons.append(str(error))
        result.duration = time.time() - start
        return result

    abc_path = case_artifacts / f"{case.entry}.abc"
    ok, message = compile_case(case, abc_path, cfg)
    if not ok:
        result.reasons.append(f"compilation failed: {message}")
        result.duration = time.time() - start
        return result

    disasm_path = case_artifacts / f"{case.entry}.disasm.txt"
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
    (case_artifacts / f"{case.entry}.stdout.txt").write_text(stdout, encoding="utf-8")
    (case_artifacts / f"{case.entry}.stderr.txt").write_text(stderr, encoding="utf-8")

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

    # The chain built above is the exact ordered chain of this case: the GC
    # record, the deopt record, then every explicit and implied component
    # pattern. `compare_output` obtains the same chain from the same
    # constructor; the expected-output comparison itself is unchanged.
    diff = compare_output(stdout, case.expected, case, filter_components)
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


def make_artifacts_root(platform_name, mode: str) -> Path:
    stamp = time.strftime("%Y%m%d-%H%M%S")
    root = Path("/tmp") / f"arksteed-{platform_name}-{mode}-{stamp}"
    suffix = 1
    while root.exists():
        root = Path("/tmp") / f"arksteed-{platform_name}-{mode}-{stamp}-{suffix}"
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


def write_run_meta(path: Path, args: argparse.Namespace, cfg: BuildConfig,
                   corpus_meta: Optional[Dict[str, Any]] = None) -> None:
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
    if corpus_meta is not None:
        # The corpus class fixes its resolved root, its revision and the number
        # of cases it selected; a run without the flag never adds this field.
        meta["corpus"] = corpus_meta
    path.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# External corpus case class: `--corpus` selection and root resolution
# ---------------------------------------------------------------------------
# The corpus class is a separate selection of the same runner (design C1/C2):
# the root comes from the environment only, the corpus runner module is a
# read-only library, and the class never shares its cases with the embedded
# tree. Every failure of this layer is a usage error (exit code 2) that names
# the variable, element or reason; a case is never silently skipped.


def resolve_corpus_root() -> Tuple[Optional[Path], Optional[str]]:
    """Resolve the corpus root from `ARKSTEED_EXTERNAL_TESTS` (C1).

    There is no implicit value: an unset, empty or unusable variable is a
    usage error naming the variable and the required layout. This runs before
    the build stage and before the tool checks.
    """
    raw = os.environ.get(CORPUS_ROOT_ENV_VAR)
    if raw is None:
        return None, (
            f"--corpus needs the external corpus root in the {CORPUS_ROOT_ENV_VAR} "
            f"environment variable, but it is not set; expected layout: a directory "
            f"with {CORPUS_RUNNER_FILENAME} in it (recommended value: "
            f"{CORPUS_RECOMMENDED_ROOT})")
    if not raw.strip():
        return None, (
            f"--corpus needs the external corpus root in the {CORPUS_ROOT_ENV_VAR} "
            f"environment variable, but it is set to an empty value; expected "
            f"layout: a directory with {CORPUS_RUNNER_FILENAME} in it (recommended "
            f"value: {CORPUS_RECOMMENDED_ROOT})")
    # The probe is fallible: `expanduser()` raises `RuntimeError` for a `~`
    # prefix naming an unknown user, and `is_dir()`/`resolve()` can raise
    # `OSError` (permission denied, unreadable parent, symlink loop). Those
    # values are unresolvable and must be the named usage error, not a
    # traceback (C1).
    try:
        root = Path(raw).expanduser()
        is_dir = root.is_dir()
    except (RuntimeError, OSError) as e:
        return None, (
            f"--corpus: {CORPUS_ROOT_ENV_VAR} does not resolve to a directory: "
            f"{raw}; expected layout: a directory with {CORPUS_RUNNER_FILENAME} "
            f"in it (recommended value: {CORPUS_RECOMMENDED_ROOT}) "
            f"(resolution failed: {e})")
    if not is_dir:
        return None, (
            f"--corpus: {CORPUS_ROOT_ENV_VAR} does not resolve to a directory: "
            f"{root}; expected layout: a directory with {CORPUS_RUNNER_FILENAME} "
            f"in it (recommended value: {CORPUS_RECOMMENDED_ROOT})")
    try:
        resolved_root = root.resolve()
    except OSError as e:
        return None, (
            f"--corpus: {CORPUS_ROOT_ENV_VAR} does not resolve to a directory: "
            f"{root}; expected layout: a directory with {CORPUS_RUNNER_FILENAME} "
            f"in it (recommended value: {CORPUS_RECOMMENDED_ROOT}) "
            f"(resolution failed: {e})")
    return resolved_root, None


def load_corpus_module(root: Path) -> Tuple[Optional[Any], Optional[str]]:
    """Import the corpus runner module lazily as a read-only library (C2).

    The import is lazy (it happens only for `--corpus`), runs with the
    bytecode cache disabled and restores it afterwards, so no `__pycache__`
    appears in the corpus tree, and it checks the mandatory API and that the
    module's own `TEST_ROOT` is the resolved root. Every failure is a usage
    error naming the missing item, never a silent skip.
    """
    module_path = root / CORPUS_RUNNER_FILENAME
    if not module_path.is_file():
        return None, (
            f"--corpus needs {CORPUS_RUNNER_FILENAME} in the corpus root {root} "
            f"({CORPUS_ROOT_ENV_VAR}); the module was not found")
    spec = importlib.util.spec_from_file_location(CORPUS_MODULE_NAME, module_path)
    if spec is None or spec.loader is None:
        return None, f"--corpus cannot load the corpus runner module {module_path}"
    module = importlib.util.module_from_spec(spec)
    sys.modules[CORPUS_MODULE_NAME] = module
    saved_dont_write_bytecode = sys.dont_write_bytecode
    sys.dont_write_bytecode = True
    try:
        spec.loader.exec_module(module)
    except Exception as e:  # noqa: BLE001 - report any import failure as usage
        sys.modules.pop(CORPUS_MODULE_NAME, None)
        return None, f"--corpus cannot import the corpus runner module {module_path}: {e}"
    finally:
        sys.dont_write_bytecode = saved_dont_write_bytecode
    missing = [name for name in CORPUS_REQUIRED_API if not hasattr(module, name)]
    if missing:
        return None, (
            f"--corpus: the corpus runner module {module_path} does not provide the "
            f"required API element {missing[0]!r}"
            + (f" (also missing: {', '.join(missing[1:])})" if len(missing) > 1 else ""))
    build_config_fields = getattr(module.BuildConfig, "__dataclass_fields__", None)
    has_ark_root = ("ark_root" in build_config_fields
                    if build_config_fields is not None else False)
    if not has_ark_root:
        try:
            has_ark_root = "ark_root" in inspect.signature(module.BuildConfig).parameters
        except (TypeError, ValueError):
            has_ark_root = False
    if not has_ark_root:
        return None, (
            f"--corpus: the corpus runner module {module_path} does not provide the "
            f"required API element 'BuildConfig.ark_root'")
    module_test_root = Path(module.TEST_ROOT).resolve()
    if module_test_root != root:
        return None, (
            f"--corpus: the corpus runner module {module_path} reports TEST_ROOT="
            f"{module_test_root}, which is not the resolved corpus root {root}")
    return module, None


def _corpus_namespace_pattern(pattern: str) -> bool:
    """Whether a filter pattern names the `corpus/<suite>/<case>` namespace."""
    stripped = pattern.rstrip("/")
    return stripped == CORPUS_NAMESPACE or stripped.startswith(CORPUS_NAMESPACE + "/")


def corpus_filter_usage_error(
    includes: List[str], excludes: List[str], corpus_flag: bool,
) -> Optional[str]:
    """Usage error of the `-I`/`-X` patterns for the selected class (C1).

    With the flag the patterns must live in the `corpus/<rel>` namespace;
    without it a pattern naming that namespace is rejected with the missing
    flag, so a corpus selection can never be requested by accident and then
    silently run nothing.
    """
    for pattern in list(includes) + list(excludes):
        in_namespace = _corpus_namespace_pattern(pattern)
        if corpus_flag and not in_namespace:
            return (
                f"--corpus selects the {CORPUS_NAMESPACE}/<suite>/<case> namespace: "
                f"the filter pattern '{pattern}' has no leading "
                f"'{CORPUS_NAMESPACE}' segment")
        if not corpus_flag and in_namespace:
            return (
                f"the filter pattern '{pattern}' selects the "
                f"{CASE_CLASS_CORPUS} case class ({CORPUS_NAMESPACE}/<suite>/<case>), "
                f"but --corpus was not given: pass --corpus to run the external "
                f"corpus class")
    return None


def corpus_selection_usage_error(args: argparse.Namespace) -> Optional[str]:
    """Usage error of the `--corpus` selection itself (C1, S1).

    The sensitivity-control mode is defined for the embedded class only, so
    the two flags cannot be combined. The guard reads the control flag with
    `getattr`, so it stays correct when the control surface is not part of the
    parsed options.
    """
    if getattr(args, "sensitivity_controls", False):
        return (
            f"--corpus selects the {CASE_CLASS_CORPUS} case class, whose controls "
            f"are its class invariant (JIT activity) and the differential "
            f"interpreter/JIT comparison; --sensitivity-controls is defined for "
            f"the {CASE_CLASS_EMBEDDED} class only, so the two flags cannot be "
            f"combined")
    return corpus_filter_usage_error(args.include, args.exclude, True)


def corpus_relative_patterns(patterns: List[str]) -> List[str]:
    """Drop the `corpus` namespace segment from filter patterns (C1).

    The corpus discovery knows nothing about the `corpus/` namespace, so it
    receives corpus-relative patterns; the class root pattern (`corpus` itself)
    selects the whole class, which an empty pattern list expresses.
    """
    mapped: List[str] = []
    for pattern in patterns:
        stripped = pattern.rstrip("/")
        if stripped == CORPUS_NAMESPACE:
            continue
        mapped.append(stripped[len(CORPUS_NAMESPACE) + 1:])
    return mapped


def discover_corpus_cases(
    module: Any, root: Path, includes: List[str], excludes: List[str],
    verbose: bool = False,
) -> List[TestCase]:
    """Select the corpus class with one call to the module's discovery (C1).

    The selected set is the result of exactly one `discover_cases` call on the
    resolved root; the corpus-relative patterns are passed to that call, and
    the full `corpus/<rel>` identifiers are then matched with this runner's
    `_pattern_matches`, so both mechanisms apply the same selection semantics.
    Each selected case keeps the module's own `TestCase` object for the
    class-specific stages.
    """
    selected: List[TestCase] = []
    for corpus_case in module.discover_cases(
            corpus_relative_patterns(includes),
            corpus_relative_patterns(excludes), verbose):
        rel = f"{CORPUS_NAMESPACE}/{corpus_case.rel}"
        if includes and not _pattern_matches(rel, includes):
            continue
        if excludes and _pattern_matches(rel, excludes):
            continue
        case_dir = Path(getattr(corpus_case, "case_dir", root / corpus_case.rel))
        source = getattr(corpus_case, "source", None) or case_dir
        selected.append(TestCase(
            rel, Path(source), case_dir / EXPECTED_OUTPUT_NAME,
            case_class=CASE_CLASS_CORPUS, corpus_case=corpus_case,
        ))
    return selected


def corpus_tools_error(module: Any, cfg: BuildConfig) -> Optional[str]:
    """Check the tools of both build configurations (C2).

    The corpus class runs its own `BuildConfig` (with `ark_root`); the tools it
    resolves must be the very paths this runner resolves, and they must exist.
    The check runs after the runner's build stage and before the case loop, so
    a missing tool is reported after the build that produces it, not before.
    """
    corpus_cfg = build_reference_config(module, cfg)
    for option in ("ark_js_vm", "es2abc"):
        ours = Path(getattr(cfg, option))
        theirs = Path(getattr(corpus_cfg, option))
        if theirs != ours:
            return (
                f"--corpus: the corpus runner's BuildConfig resolves {option} to "
                f"{theirs}, but this runner resolves it to {ours}; the two build "
                f"configurations must name the same tool")
        if not theirs.exists():
            return f"--corpus: {theirs} does not exist"
    return None


# ---------------------------------------------------------------------------
# External corpus case class: the reference verdict and the verdict parity
# ---------------------------------------------------------------------------
# The corpus runner module is the source of the per-case reference verdict
# (design C7): its own `parse_args` supplies the run namespace, the fields its
# `main` derives are reproduced, the runner's platform and mode are injected
# after parsing, and its `run_case` performs the reference execution with its
# own `BuildConfig`, its own defaults and its own budget. The reference
# execution is mandatory and has no switch: it runs for every selected case,
# including the cases the class already considers red, so a divergence is
# observed instead of being assumed away. The class base verdict is compared
# with it on the verdict alone (the module stores stdout as a reproducer and
# never compares it); a divergence is a red case reported with both
# reproducing commands.


def corpus_revision(root: Path) -> Optional[str]:
    """Revision of the corpus the run report fixes (C7).

    The corpus is a read-only dependency, so its revision is read from its own
    git metadata only; a root without git metadata reports no revision instead
    of failing the run.
    """
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            capture_output=True, text=True,
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    return result.stdout.strip() or None


def build_reference_config(module: Any, cfg: BuildConfig) -> Any:
    """The corpus runner's own `BuildConfig` for the reference run (C2, C7).

    The class is the module's own and is built from the parameters this runner
    resolved (`ARK_ROOT`, platform and mode), so the reference execution runs
    the very binaries under test and `ark_root` is filled in. The mode comes
    from the runner and not from the module CLI, so `fastverify` is reachable.
    """
    return module.BuildConfig(cfg.mode, cfg.platform, ARK_ROOT)


def build_reference_namespace(module: Any, cfg: BuildConfig) -> argparse.Namespace:
    """The corpus runner's own run namespace for the reference run (C7).

    The module's own CLI supplies its own defaults. It is called with an
    explicit empty argv, so the runner's command line is never parsed by the
    module and the module's own `debug|release` choices cannot constrain the
    run. The fields its `main` derives are reproduced (`log_components_value`
    when `--log-components` is not given, the host platform when `-p` is not
    given) and the runner's platform and mode are injected after parsing: the
    injected mode survives parsing because it is not re-parsed, so
    `fastverify` is reachable. The namespace carries the module's own budget
    (`EXECUTION_TIMEOUT`, without the cross-build multiplier of the class: the
    reference budget is the module's own and is not overridable).
    """
    reference_args = module.parse_args([])
    if getattr(reference_args, "log_components", None):
        # The VM parses --log-components as a colon-separated list; commas on
        # the command line are normalized exactly as the module's `main` does.
        parts = [c for c in re.split(r"[,:]", reference_args.log_components) if c]
        reference_args.log_components_value = ":".join(parts)
    else:
        reference_args.log_components_value = None
    if getattr(reference_args, "platform", None) is None:
        reference_args.platform = module.detect_host_platform()
    reference_args.platform = cfg.platform
    reference_args.mode = cfg.mode
    return reference_args


def _option_token_parts(token: str) -> Optional[Tuple[str, str]]:
    """`--name=value` token as `(name, value)`; `--flag` becomes `(name, 'true')`."""
    name = _option_name(token)
    if name is None:
        return None
    if "=" in token:
        return name, token.split("=", 1)[1]
    return name, "true"


def corpus_behavioural_options(
    module: Any, args: argparse.Namespace,
) -> "collections.OrderedDict[str, str]":
    """Behavioural options of the module's own run configuration (C5).

    The set is derived from the loaded corpus module and never from a literal
    list: its `BASE_ARGS` and `ALWAYS_ARGS`, plus the values its own CLI
    defaults give `--compiler-jit-hotness-threshold`, `--enable-heap-verify`
    and `--compiler-enable-jit-lazy-deopt`. A change of a module constant or
    of one of its CLI defaults therefore changes the class command line.
    """
    options: "collections.OrderedDict[str, str]" = collections.OrderedDict()
    for token in list(module.BASE_ARGS) + list(module.ALWAYS_ARGS):
        parts = _option_token_parts(token)
        if parts is None:
            continue
        options[parts[0]] = parts[1]
    options["--compiler-jit-hotness-threshold"] = str(
        getattr(args, "hotness_threshold", None))
    if not getattr(args, "disable_heap_verify", False):
        options["--enable-heap-verify"] = "true"
    options["--compiler-enable-jit-lazy-deopt"] = str(
        not getattr(args, "disable_lazy_deopt", False)).lower()
    return options


def corpus_class_defaults(module: Any) -> "collections.OrderedDict[str, str]":
    """Class defaults: the module's behavioural options minus observation (C5).

    The runner-managed options (the observation and the environment paths) are
    not behavioural class defaults: the class supplies its own observation
    (the JIT-activity invariant) and the environment paths come from the run
    configuration. The values come from the module's own CLI defaults, so no
    literal copy of the corpus configuration lives in this repository.
    """
    options = corpus_behavioural_options(module, module.parse_args([]))
    return collections.OrderedDict(
        (name, value) for name, value in options.items()
        if name not in RUNNER_MANAGED_OPTIONS)


def corpus_class_command_options(module: Any) -> List[str]:
    """Class command tokens of the derived class defaults (C5, C6).

    Only the class branch builds them, so an embedded case never gains a
    corpus option and the derived set changes with the module constants.
    """
    return [f"{name}={value}"
            for name, value in corpus_class_defaults(module).items()]


def corpus_defaults_usage_error(
    module: Any, reference_args: argparse.Namespace,
    class_defaults: "collections.OrderedDict[str, str]",
) -> Optional[str]:
    """Cross-check the class defaults against the reference namespace (C5).

    The behavioural options of the reference run must equal the class defaults
    name by name; only the runner-managed options (observation and environment
    paths) may differ, because the class replaces the observation with its own
    invariant. A difference is a usage error naming the option, never a silent
    divergence of verdicts.
    """
    reference = corpus_behavioural_options(module, reference_args)
    names = list(dict.fromkeys(
        list(reference) + [name for name in class_defaults if name not in reference]))
    for name in names:
        if name in RUNNER_MANAGED_OPTIONS:
            continue
        if reference.get(name) != class_defaults.get(name):
            return (
                f"--corpus: the class defaults do not match the reference run "
                f"behavioural options: '{name}' is "
                f"{class_defaults.get(name)!r} in the class defaults and "
                f"{reference.get(name)!r} in the reference namespace; only the "
                f"runner-managed options may differ "
                f"({', '.join(RUNNER_MANAGED_OPTIONS)})")
    return None


def _status_value(status: Any) -> str:
    """Plain verdict of a corpus module status value (or of a string)."""
    return str(getattr(status, "value", status))


@dataclass
class CorpusReferenceResult:
    """Outcome of the mandatory reference execution of one corpus case (C7)."""

    verdict: str
    reason: Optional[str] = None
    # The module's own reproducer of the reference execution (C10).
    command: Optional[str] = None
    duration: float = 0.0
    artifacts: Optional[Path] = None


def run_reference_case(
    module: Any, case: TestCase, cfg: Any, reference_args: argparse.Namespace,
    artifacts_root: Path,
) -> CorpusReferenceResult:
    """Run one corpus case with the corpus runner's own per-case path (C7).

    The module compiles and executes the case itself (its own compilation
    rules, its `BASE_ARGS`/`ALWAYS_ARGS`, its own defaults and its own budget)
    and its own classification of that outcome is the reference verdict. Its
    stdout capture stays in its own artifacts subtree next to the reproducer
    it produces. The reference run takes no descriptor, no case requirement
    and no class budget, so no class-side override can reach it.
    """
    reference_root = artifacts_root / CORPUS_REFERENCE_ARTIFACTS_DIR
    case_rel = getattr(case.corpus_case, "rel", case.rel)
    start = time.time()
    try:
        result = module.run_case(case.corpus_case, cfg, reference_args,
                                 reference_root)
    except Exception as e:  # noqa: BLE001 - a failed reference is a red verdict
        return CorpusReferenceResult(
            CORPUS_VERDICT_FAIL, f"reference run failed: {e}", None,
            time.time() - start, reference_root / case_rel)
    verdict = _status_value(getattr(result, "status", CORPUS_VERDICT_FAIL))
    reasons = [str(reason) for reason in getattr(result, "reasons", []) or []]
    command = getattr(result, "repro_cmd", None)
    duration = float(getattr(result, "duration", 0.0) or 0.0)
    case_artifacts = reference_root / case_rel
    if command:
        try:
            case_artifacts.mkdir(parents=True, exist_ok=True)
            (case_artifacts / CORPUS_REFERENCE_COMMAND_FILENAME).write_text(
                command + "\n", encoding="utf-8")
        except OSError:
            pass
    return CorpusReferenceResult(verdict, "; ".join(reasons) or None, command,
                                 duration, case_artifacts)


def classify_corpus_base_verdict(
    module: Any, *, compile_ok: bool, compile_timed_out: bool,
    returncode: Optional[int], timed_out: bool,
) -> Tuple[str, Optional[str]]:
    """Base verdict of a class execution by the module's own rule (C3, C7).

    The branch order mirrors the module's own `run_case`: a failed compilation
    is a TIMEOUT when it timed out and a FAIL otherwise; an execution timeout
    is a TIMEOUT; an unstartable VM, a crash (classified with the module's own
    `is_crash`/`crash_signal_name`) and any other nonzero exit code are FAIL;
    everything else is PASS.
    """
    if not compile_ok:
        if compile_timed_out:
            return CORPUS_VERDICT_TIMEOUT, "compilation timed out"
        return CORPUS_VERDICT_FAIL, "compilation failed"
    if timed_out:
        return CORPUS_VERDICT_TIMEOUT, "execution timed out"
    if returncode is None:
        return CORPUS_VERDICT_FAIL, "ark_js_vm could not be executed"
    if module.is_crash(returncode):
        return (CORPUS_VERDICT_FAIL,
                f"ark_js_vm crashed with {module.crash_signal_name(returncode)} "
                f"(return code {returncode})")
    if returncode != 0:
        return CORPUS_VERDICT_FAIL, f"ark_js_vm exited with return code {returncode}"
    return CORPUS_VERDICT_PASS, None


def classify_parity(
    base_verdict: str, reference_verdict: str, config_error: bool = False,
) -> str:
    """Parity of the class base verdict with the reference verdict (C7).

    The comparison is on the base verdict only: the corpus runner stores the
    case stdout as a reproducer and never compares it, so stdout is not the
    oracle of the parity. A descriptor config error means the case has no
    executions at all and is red by the descriptor itself, so the parity is
    `not-applicable`; otherwise equal verdicts are `matched` and any
    difference is `diverged`.
    """
    if config_error:
        return CORPUS_PARITY_NOT_APPLICABLE
    if base_verdict == reference_verdict:
        return CORPUS_PARITY_MATCHED
    return CORPUS_PARITY_DIVERGED


def parity_reasons(
    base_verdict: str, class_command: Optional[str], reference_verdict: str,
    reference_command: Optional[str],
) -> List[str]:
    """Reasons of a `diverged` parity: both verdicts and both reproducers.

    A divergence is an adapter defect, so the class command and the reference
    command are reported together (C7, C10); the class run records its command
    in the case artifacts and the reference command is the module's own
    reproducer.
    """
    return [
        f"parity divergence: class={base_verdict} reference={reference_verdict}",
        f"class command: {class_command or 'not recorded'}",
        f"reference command: {reference_command or 'not recorded'}",
    ]


@dataclass
class CorpusContext:
    """Per-run corpus class state built once before the case loop (C2, C7)."""

    module: Any
    root: Path
    revision: Optional[str]
    # The module's own build configuration and its own run namespace; every
    # reference execution of the run reuses them, so the reference verdict of
    # a case is produced from the module's defaults and its own budget.
    build_config: Any
    reference_args: argparse.Namespace
    # The behavioural class defaults derived from the loaded module (C5): the
    # class command line is built from them, they are checked against the
    # reference namespace before the case loop and no embedded case ever sees
    # them.
    class_defaults: "collections.OrderedDict[str, str]" = field(
        default_factory=collections.OrderedDict)
    # The IR opcode names the descriptor constraints are validated against
    # (`None` when the authoritative header is unavailable); the class
    # execution reuses the run-level value and never reloads the header.
    valid_opcodes: Optional[set] = None
    # The prepared `(command, components)` of every selected corpus case, built
    # and validated on the actual per-case inputs before the case loop (TW1).
    # The class execution of a case reuses its prepared command, so the
    # validated command and the executed command are the same subject and an
    # absent or duplicated `--compiler-enable-jit` token can never launch a
    # process. A case whose command the pre-flight could not construct keeps no
    # entry and is handled by the ordinary per-case configuration path.
    class_commands: Dict[str, Tuple[List[str], List[str]]] = field(
        default_factory=dict)


def corpus_run_meta(context: Optional[CorpusContext],
                    case_count: int) -> Optional[Dict[str, Any]]:
    """Run metadata of the corpus class the report fixes (C7, C10).

    The number of cases (the single discovery call), the resolved root and the
    revision of the corpus are recorded for the report header.
    """
    if context is None:
        return None
    return {
        "case_class": CASE_CLASS_CORPUS,
        "root": str(context.root),
        "revision": context.revision,
        "cases": case_count,
    }


# ---------------------------------------------------------------------------
# External corpus case class: the class execution and the JIT-activity
# invariant
# ---------------------------------------------------------------------------
# The class execution compiles the case with the corpus runner module's own
# functions and runs the produced abc with a class-specific streaming executor
# (design C2/C3/C4): the compilation and execution markers are counted while
# the stream passes, the filtered program output is kept under the class limit
# and the component log lines without the execution markers go to a temporary
# log that is deleted after the evidence pass. The class-level invariant is
# injected into the case requirements before the case loop, so every corpus
# case must show a compilation marker and, unless a justified descriptor
# exempts it, an execution marker.


def corpus_case_stem(case: TestCase) -> str:
    """Base name of the class artifacts of a corpus case."""
    corpus_case = case.corpus_case
    stem = getattr(corpus_case, "stem", None)
    if stem:
        return str(stem)
    case_dir = getattr(corpus_case, "case_dir", None)
    if case_dir is not None:
        return Path(case_dir).name
    return Path(case.rel).name


def corpus_case_entry(case: TestCase) -> str:
    """VM `--entry-point` of a corpus case."""
    entry = getattr(case.corpus_case, "entry", None)
    if entry:
        return str(entry)
    return corpus_case_stem(case)


def corpus_case_is_file_info(module: Any, case: TestCase) -> bool:
    """Whether a corpus case is a fileInfo package (the module's own kind)."""
    kind = getattr(case.corpus_case, "case_kind", None)
    if kind is None:
        return False
    if getattr(kind, "name", None) == "FILE_INFO":
        return True
    return str(getattr(kind, "value", kind)) == "file_info"


def corpus_invariant_constraints(case: TestCase) -> List[EvidenceConstraint]:
    """The injected class-level JIT-activity requirements of a case (C3).

    A zero compilation marker can never be exempted, so the compilation
    requirement is always injected; the execution requirement is injected
    unless the descriptor declared the justified `NO_EXEC_MARKER` exemption.
    """
    constraints: List[EvidenceConstraint] = [
        CompileCountGeConstraint(CORPUS_INVARIANT_PATTERN, CORPUS_INVARIANT_MIN)]
    if not case.corpus_no_exec_marker_reason:
        constraints.append(
            ExecCountGeConstraint(CORPUS_INVARIANT_PATTERN, CORPUS_INVARIANT_MIN))
    return constraints


def inject_corpus_invariant(case: TestCase) -> None:
    """Add the class-level requirements to a corpus case (C3).

    The injected requirements live next to the descriptor requirements, so
    both are evaluated and reported: a descriptor can add requirements but
    never remove or weaken the injected ones.
    """
    case.evidence_constraints.extend(corpus_invariant_constraints(case))


def corpus_invariant_state(case: TestCase, compile_markers: int,
                           exec_markers: int) -> str:
    """Reported state of the class invariant of one case (C3, C10).

    `compile-only` without a declared exemption is red: the injected execution
    requirement is then violated and reported with its actual count.
    """
    if compile_markers < CORPUS_INVARIANT_MIN:
        return CORPUS_INVARIANT_NONE
    if case.corpus_no_exec_marker_reason:
        return (f"{CORPUS_INVARIANT_COMPILE_ONLY} "
                f"(exempt: {case.corpus_no_exec_marker_reason})")
    if exec_markers < CORPUS_INVARIANT_MIN:
        return CORPUS_INVARIANT_COMPILE_ONLY
    return CORPUS_INVARIANT_BOTH


def corpus_class_budget(module: Any, case: TestCase,
                        cfg: BuildConfig) -> Tuple[int, Optional[str]]:
    """Class budget of one corpus case; `(seconds, override justification)` (C6).

    The default is the corpus runner module's own budget (`EXECUTION_TIMEOUT`,
    1800 s on the pinned revision) with the embedded runner's cross-build
    multiplier; the descriptor's `TIMEOUT <seconds> <justification>` replaces
    it with an absolute value. The reference budget is untouched: the override
    never reaches the module's own run.
    """
    budget = int(module.EXECUTION_TIMEOUT)
    if cfg.is_cross_build:
        budget *= QEMU_TIMEOUT_FACTOR
    if case.corpus_timeout_seconds is not None:
        return int(case.corpus_timeout_seconds), case.corpus_timeout_reason
    return budget, None


@dataclass
class CorpusStreamEvidence:
    """Streamed evidence of one corpus class execution (C4).

    The pass counts the markers and keeps only what a verdict needs: the
    filtered program output (bounded), the byte volume of the retained
    component log and the counts of the descriptor evidence patterns that name
    methods. The marker-dominated component trace is never captured.
    """

    compile_markers: int = 0
    exec_markers: int = 0
    deopt_markers: int = 0
    gc_markers: int = 0
    program_output: str = ""
    program_output_bytes: int = 0
    program_output_truncated: bool = False
    compiler_log_bytes: int = 0
    compiler_log_capped: bool = False
    # Additive invalidity fields of the completed collection result (S3/S5):
    # an unresolved record candidate exceeded its own limit, or the collection
    # could not be completed at all (read/filter/observation/log-sink failure).
    # Both are sticky and make the output unusable for comparison and for an
    # emptiness decision, even when the retained prefix is empty.
    record_candidate_capped: bool = False
    program_output_error: Optional[str] = None
    # Counts of the descriptor evidence constraints whose pattern is not the
    # wildcard of the injected invariant (the wildcard is answered by totals).
    named_counts: Dict[EvidenceConstraint, int] = field(default_factory=dict)


def corpus_program_output_usable(evidence: CorpusStreamEvidence) -> bool:
    """Whether a *completed* collection result certifies program output (S5).

    This is a predicate on a completed collection result, never permission to
    compare a live accumulator: a caller may decide emptiness/comparability
    only when the output is complete and neither invalidity flag nor the
    collection error is set.
    """
    return (not evidence.program_output_truncated
            and not evidence.record_candidate_capped
            and evidence.program_output_error is None)


def _corpus_collection_error(operation: str, error: BaseException) -> str:
    """Named collection failure of one operation (S5)."""
    return (f"program output collection failed: {operation}: "
            f"{type(error).__name__}: {error}")


def _record_corpus_collection_error(evidence: CorpusStreamEvidence,
                                    operation: str, error: BaseException) -> None:
    """Record the first named collection failure without overwriting a later one."""
    if evidence.program_output_error is None:
        evidence.program_output_error = _corpus_collection_error(operation, error)


def _evidence_matches(constraint: EvidenceConstraint, candidate: str) -> bool:
    """Whether a streamed candidate matches a descriptor evidence pattern."""
    return any(fnmatchcase(spelling, constraint.pattern)
               for spelling in constraint.spellings(candidate))


def _partial_literal_suffix(text: str, literal: str) -> int:
    """Longest suffix of `text` that is a proper prefix of `literal`.

    The retained suffix is the only text of a literal stage whose match
    decision is still unresolved across a chunk boundary.
    """
    limit = min(len(literal) - 1, len(text))
    for keep in range(limit, 0, -1):
        if text.endswith(literal[:keep]):
            return keep
    return 0


def _match_prefix_candidates(
    text: str, candidates: List[str], final: bool = False,
) -> Tuple[Optional[int], bool]:
    """The first still-decided candidate `text` starts with, if any.

    Candidates are ordered by priority. While the text can still grow, a later
    candidate is committed only once every higher-priority candidate is ruled
    out, so the recognized identity of a line is a pure function of its
    characters and does not depend on how those characters arrived: a longer,
    lower-priority supplied prefix can never shadow a higher-priority fixed
    marker identity in one chunk and lose to it character by character (S5).
    `done == False` means `text` is still a proper prefix of a higher-priority
    candidate, so more characters may change the answer.

    `final=True` marks the end of the growable text (a line end): no candidate
    can grow any further, so the first complete match is the answer even when a
    higher-priority candidate is still an unterminated prefix of it.
    """
    for index, candidate in enumerate(candidates):
        if text.startswith(candidate):
            return index, True
        if not final and candidate.startswith(text):
            return None, False
    return None, True


def _glob_tokens(pattern: str) -> List[Tuple[str, Any]]:
    """Token stream of one shell-style wildcard pattern (S5).

    The scanning rules and the character-class construction mirror
    `fnmatch.translate`, so the streamed matcher below accepts exactly the
    strings `fnmatchcase` accepts. Class bodies are compiled with
    `fnmatch.translate` itself, which keeps the class semantics exact.
    """
    tokens: List[Tuple[str, Any]] = []
    index = 0
    length = len(pattern)
    while index < length:
        char = pattern[index]
        index += 1
        if char == "*":
            tokens.append(("*", None))
        elif char == "?":
            tokens.append(("?", None))
        elif char == "[":
            closing = index
            if closing < length and pattern[closing] == "!":
                closing += 1
            if closing < length and pattern[closing] == "]":
                closing += 1
            while closing < length and pattern[closing] != "]":
                closing += 1
            if closing >= length:
                tokens.append(("c", "["))
            else:
                body = pattern[index - 1:closing + 1]
                tokens.append(("[", re.compile(fnmatch.translate(body))))
                index = closing + 1
        else:
            tokens.append(("c", char))
    return tokens


class _GlobStreamMatcher:
    """Streaming equivalent of `fnmatchcase(payload, pattern)` (S5).

    The payload of a marker line can be arbitrarily long, so a named evidence
    count cannot buffer it. The matcher keeps a set of pattern positions - the
    state is descriptor-sized and independent of the payload length - and
    consumes one character at a time; `matched()` is meaningful once the
    payload ended.
    """

    def __init__(self, pattern: str):
        self._tokens = _glob_tokens(pattern)
        self._states = self._closure({0})

    def reset(self) -> None:
        self._states = self._closure({0})

    def feed(self, text: str) -> None:
        if not text:
            return
        for char in text:
            following: set = set()
            for position in self._states:
                if position >= len(self._tokens):
                    continue
                kind, value = self._tokens[position]
                if kind == "*":
                    following.add(position)
                elif kind == "?":
                    following.add(position + 1)
                elif kind == "[":
                    if value.match(char):
                        following.add(position + 1)
                elif value == char:
                    following.add(position + 1)
            self._states = self._closure(following)
            if not self._states:
                return

    def matched(self) -> bool:
        return len(self._tokens) in self._states

    def snapshot(self):
        return frozenset(self._states)

    def restore(self, snapshot) -> None:
        self._states = set(snapshot)

    def _closure(self, states) -> set:
        closed: set = set()
        stack = list(states)
        while stack:
            position = stack.pop()
            if position in closed:
                continue
            closed.add(position)
            if position < len(self._tokens) and self._tokens[position][0] == "*" \
                    and position + 1 not in closed:
                stack.append(position + 1)
        return closed


class _NamedMatchers:
    """Streaming named-evidence matchers of one evidence kind (S5).

    The matcher set is descriptor-sized. The deopt records carry an alternative
    spelling (`Deoptimize <record>`) for the eager path that skips the
    GC-capable stack trace, so every deopt entry keeps a second matcher that is
    fed the constant spelling prefix first; a record is counted once when any
    of its spellings matches, exactly as `_evidence_matches` does.
    """

    def __init__(self, constraints, kind: str):
        self._entries = []
        for constraint in constraints:
            if constraint.EVIDENCE_KEY != kind:
                continue
            if constraint.pattern == CORPUS_INVARIANT_PATTERN:
                # The wildcard totals are answered by the marker counters of
                # `corpus_evidence_actual`; no per-payload matcher is needed.
                continue
            plain = _GlobStreamMatcher(constraint.pattern)
            alternative = _GlobStreamMatcher(constraint.pattern) \
                if kind == "deopt" else None
            self._entries.append((constraint, plain, alternative))

    @property
    def active(self) -> bool:
        """Whether any named payload of this kind has to be matched at all."""
        return bool(self._entries)

    def reset(self) -> None:
        for _, plain, alternative in self._entries:
            plain.reset()
            if alternative is not None:
                alternative.reset()
                alternative.feed(DEOPT_EAGER_PREFIX + " ")

    def feed(self, text: str) -> None:
        for _, plain, alternative in self._entries:
            plain.feed(text)
            if alternative is not None:
                alternative.feed(text)

    def matched(self, eager_spelling: bool = False) -> List[EvidenceConstraint]:
        return [constraint for constraint, plain, alternative in self._entries
                if plain.matched()
                or (eager_spelling and alternative is not None
                    and alternative.matched())]

    def snapshot(self):
        return [(plain.snapshot(),
                 None if alternative is None else alternative.snapshot())
                for _, plain, alternative in self._entries]

    def restore(self, snapshot) -> None:
        for (_, plain, alternative), (plain_state, alternative_state) \
                in zip(self._entries, snapshot):
            plain.restore(plain_state)
            if alternative is not None:
                alternative.restore(alternative_state)


class _CorpusDiscardSink:
    """Write-free log sink used when the retained log cannot be opened (S5).

    `error` carries the retained-log failure that made this sink necessary (the
    open failure). The collector consults it before it consumes any input, so
    the committed diagnostic prefix is frozen at the failure boundary and the
    first named collection error survives a later read failure.
    """

    def __init__(self, error: Optional[BaseException] = None):
        self.error = error

    def write(self, data) -> None:
        return None

    def close(self) -> None:
        return None


def _utf8_bytes(text: str) -> int:
    """UTF-8 byte volume of decoded text (ASCII is the fast path)."""
    return len(text) if text.isascii() else len(text.encode("utf-8"))


class _TextWindow:
    """Segmented unresolved-text window of one incremental stage (S2/S3).

    The live text is the concatenation of the stored segments minus the
    consumed prefix of the first one. Appending never copies the stored text:
    a supplied chunk becomes a segment of its own, and a run of short appends
    is joined into a single segment only once that run reached `_FLUSH_MIN`
    characters. Appending therefore costs a constant amortized amount even
    when a growing candidate arrives one character at a time, instead of
    copying the whole unresolved window per chunk. Literal searches and
    indexed reads resume from the last visited segment, so scanning the same
    growing window at every symbol transition stays bounded work too.

    The window is a drop-in replacement for the previous immutable-string
    accumulator: it supports `len()`, integer and slice indexing, `find`,
    `endswith` and consuming a prefix.
    """

    __slots__ = ("_segments", "_head", "_flush_index", "_flush_length",
                 "_length", "_bytes", "_memo_index", "_memo_segment",
                 "_memo_offset")

    # A run of appends is joined into one segment when it reaches this many
    # characters or this many parts, whichever comes first: the short-chunk
    # partition then costs linear total work instead of per-character object
    # copying, while the segment count and the number of segments a search has
    # to walk stay bounded.
    _FLUSH_MIN = 512
    _FLUSH_PARTS = 16

    def __init__(self) -> None:
        self._segments: List[str] = []
        self._head = 0
        self._flush_index = 0
        self._flush_length = 0
        self._length = 0
        self._bytes = 0
        self._memo_index = -1
        self._memo_segment = 0
        self._memo_offset = 0

    def __len__(self) -> int:
        return self._length

    @property
    def bytes(self) -> int:
        """UTF-8 byte volume of the live unresolved text."""
        return self._bytes

    def clear(self) -> None:
        self._segments = []
        self._head = 0
        self._flush_index = 0
        self._flush_length = 0
        self._length = 0
        self._bytes = 0
        self._memo_index = -1
        self._memo_segment = 0
        self._memo_offset = 0

    def append(self, text: str) -> None:
        if not text:
            return
        self._segments.append(text)
        self._flush_length += len(text)
        self._length += len(text)
        self._bytes += _utf8_bytes(text)
        if self._flush_length >= self._FLUSH_MIN \
                or len(self._segments) - self._flush_index >= self._FLUSH_PARTS:
            self._join_run()

    def _join_run(self) -> None:
        """Join the unflushed run into one segment, keeping the memo valid."""
        segments = self._segments
        first = self._flush_index
        if self._memo_index >= 0 and self._memo_segment >= first:
            base = 0
            for index in range(first, self._memo_segment):
                base += len(segments[index])
            self._memo_segment = first
            self._memo_offset = base + self._memo_offset
        segments[first:] = ["".join(segments[first:])]
        self._flush_index = len(segments)
        self._flush_length = 0

    def consume(self, count: int) -> str:
        """Remove and return the first `count` characters of the window."""
        if count <= 0:
            return ""
        if count > self._length:
            count = self._length
        segments = self._segments
        head = self._head
        remaining = count
        first = 0
        while first < len(segments):
            available = len(segments[first]) - head
            if available > remaining:
                break
            remaining -= available
            first += 1
            head = 0
        parts: List[str] = []
        for index in range(first):
            parts.append(segments[index][self._head if index == 0 else 0:])
        if remaining:
            parts.append(segments[first][head:head + remaining])
            self._head = head + remaining
        else:
            self._head = 0
        if first:
            del segments[:first]
            if first <= self._flush_index:
                self._flush_index -= first
            else:
                self._flush_index = 0
                self._flush_length = self._length - count
        text = "".join(parts)
        self._length -= count
        self._bytes -= _utf8_bytes(text)
        self._memo_index = -1
        return text

    def __getitem__(self, index):
        if isinstance(index, slice):
            start, stop, step = index.indices(self._length)
            if step == 1:
                return self._slice(start, stop)
            return "".join(self[position]
                           for position in range(start, stop, step))
        if index < 0:
            index += self._length
        if index < 0 or index >= self._length:
            raise IndexError(index)
        segment, offset = self._seek(index)
        return self._segments[segment][offset]

    def find(self, sub: str, start: int = 0) -> int:
        """`str.find` over the live text, resumed from the last visit (S2).

        The scan walks the remaining segments in bounded batches, so a search
        over a window that grew in many small chunks costs one string search
        per batch instead of one per appended chunk, and a match spanning a
        segment boundary is still recognized. Every batch is truncated to the
        batch size (at least the needle length), so a single large supplied
        chunk is never copied whole during the search.
        """
        needle = len(sub)
        if start < 0:
            start = 0
        if needle == 0:
            return min(start, self._length)
        if start > self._length:
            return -1
        segments = self._segments
        total = len(segments)
        # A match may start a little before `start` and cross a segment
        # boundary, so the walk begins early.
        scan = start - (needle - 1)
        if scan < 0:
            scan = 0
        segment, offset = self._seek(scan)
        # A batch must be able to hold a complete needle, otherwise a match
        # crossing the batch boundary would be missed; the overlap step below
        # is then at least one character.
        minimum = _TextWindow._FLUSH_MIN
        if needle > minimum:
            minimum = needle
        while segment < total:
            parts: List[str] = []
            length = 0
            cursor = segment
            position = offset
            while cursor < total and length < minimum:
                raw = segments[cursor]
                take = minimum - length
                available = len(raw) - position
                if available > take:
                    raw = raw[position:position + take]
                elif position:
                    raw = raw[position:]
                position = 0
                parts.append(raw)
                length += len(raw)
                cursor += 1
            haystack = parts[0] if len(parts) == 1 else "".join(parts)
            difference = start - scan
            found = haystack.find(sub, difference if difference > 0 else 0)
            if found != -1:
                return scan + found
            if scan + length >= self._length:
                # The batch covered the whole remaining window.
                return -1
            # Overlap the next batch by the longest possible partial match, so
            # no admissible start position is skipped.
            step = length - (needle - 1)
            if step <= 0:
                step = 1
            scan += step
            segment, offset = self._seek(scan)
        return -1

    def endswith(self, suffix: str) -> bool:
        size = len(suffix)
        if size == 0:
            return True
        if size > self._length:
            return False
        return self._slice(self._length - size, self._length) == suffix

    def startswith(self, prefix: str) -> bool:
        size = len(prefix)
        if size == 0:
            return True
        if size > self._length:
            return False
        return self._slice(0, size) == prefix

    # -- internals --------------------------------------------------------
    def _slice(self, start: int, stop: int) -> str:
        if stop <= start:
            return ""
        segments = self._segments
        segment, offset = self._seek(start)
        parts: List[str] = []
        remaining = stop - start
        while remaining > 0 and segment < len(segments):
            piece = segments[segment][offset:offset + remaining]
            parts.append(piece)
            remaining -= len(piece)
            segment += 1
            offset = 0
        return "".join(parts)

    def _seek(self, index: int) -> Tuple[int, int]:
        """Segment index and scalar offset of the live character `index`."""
        if self._memo_index >= 0:
            if index >= self._memo_index:
                segment, offset = self._advance(
                    self._memo_segment, self._memo_offset,
                    index - self._memo_index)
            else:
                # Interleaved searches (a literal scan and the charged-prefix
                # measurement) move around one position, so the walk resumes
                # backwards instead of restarting at the window head.
                segment, offset = self._retreat(
                    self._memo_segment, self._memo_offset,
                    self._memo_index - index)
        else:
            segment, offset = self._advance(0, self._head, index)
        self._memo_index = index
        self._memo_segment = segment
        self._memo_offset = offset
        return segment, offset

    def _advance(self, segment: int, offset: int, steps: int) -> Tuple[int, int]:
        segments = self._segments
        while steps > 0 and segment < len(segments):
            available = len(segments[segment]) - offset
            if steps < available:
                return segment, offset + steps
            steps -= available
            segment += 1
            offset = 0
        return segment, offset

    def _retreat(self, segment: int, offset: int, steps: int) -> Tuple[int, int]:
        segments = self._segments
        while True:
            floor = self._head if segment == 0 else 0
            available = offset - floor
            if steps <= available:
                return segment, offset - steps
            steps -= available
            if segment == 0:
                return 0, floor
            segment -= 1
            offset = len(segments[segment])


class _StreamStage:
    """Common bounded text state of one incremental substitution stage (S2).

    The pending text is exactly the part whose emission/deletion decision is
    unresolved, so the byte volume of a record stage is also the value charged
    to its record-candidate limit.
    """

    def __init__(self, sink):
        self._sink = sink
        self._pending = _TextWindow()
        self._line_start = True
        # Per-record-stage unresolved-window limit (S3). The pipeline sets it
        # on the record stages only. Every symbol transition charges the still
        # unresolved window, so the first excess is independent of the read
        # partition and immediately halts the stage.
        self.record_cap: Optional[int] = None
        self._on_record_cap = None
        self._halted = False
        # Prefix of the window whose emission/deletion decision this stage has
        # already examined, in characters and in UTF-8 bytes. Both are
        # maintained incrementally, so charging a growing window never rescans
        # it (S3).
        self._charged_chars = 0
        self._charged_bytes = 0
        # Highest UTF-8 byte volume of an unresolved window this stage has
        # examined so far (S3). Charging is a running maximum, not a live value:
        # a chunk has to be charged exactly as its symbols would have been, so
        # an intermediate partial prefix that a later symbol of the same chunk
        # resolves is still counted at the moment it existed. That is what makes
        # the cap decision independent of the read partition.
        self._peak_bytes = 0
        # Length of the still unresolved examined prefix of the pending window.
        # A record stage sets it at every recognition transition, before any
        # later transition can release that text (S3).
        self._examined = 0

    def feed(self, text: str) -> None:
        raise NotImplementedError

    def finish(self) -> None:
        raise NotImplementedError

    def free(self) -> None:
        self._pending.clear()
        self._charged_chars = 0
        self._charged_bytes = 0
        self._peak_bytes = 0
        self._examined = 0
        self._halted = True

    def unresolved_bytes(self) -> int:
        """Highest unresolved window charged to this stage's limit so far (S3)."""
        return self._peak_bytes

    def _frontier(self) -> int:
        """Leading window characters whose decision this stage examined (S3).

        A record stage reports its own examined prefix: text beyond it is the
        unread tail of a caller-supplied chunk and is never charged, while a
        complete match that is recognized and deleted leaves nothing
        unresolved. The base implementation is used by stages without a
        record-candidate limit.
        """
        return len(self._pending)

    def _charge_record_cap(self) -> bool:
        """Charge the still-unresolved window after one symbol transition (S3).

        The charge counts only text the stage has actually examined and whose
        emission/deletion is still undecided: safely emitted text, immediately
        deleted matches and the unread tail of a supplied chunk are not
        charged, while every symbol transition is charged, so the first excess
        is independent of the read partition. Returns True when the stage must
        stop: either the pipeline already disabled every stage, or this stage's
        own window exceeded its limit. The first excess halts this stage and
        asks the pipeline to free every candidate window and disable output
        certification.
        """
        if self._halted:
            return True
        if self.record_cap is None:
            return False
        return self.note_unresolved(self._frontier())

    def _halt(self) -> None:
        """Free this stage's windows and fire the pipeline's first-excess hook.

        The limit is enforced at the symbol transition that produced the
        excess, before that transition or any later one can emit or delete
        more text, so the frozen output prefix cannot depend on the read
        partition (S3). The pending window is released immediately and the
        pipeline disables output certification for the whole execution.
        """
        self._halted = True
        self._pending.clear()
        self._charged_chars = 0
        self._charged_bytes = 0
        self._examined = 0
        if self._on_record_cap is not None:
            self._on_record_cap()

    def note_unresolved(self, frontier: int) -> bool:
        """Charge the unresolved window after one symbol transition (S3).

        The window is `pending[:frontier]`; the running maximum is what the
        limit is compared against, so a transition whose prefix a later symbol
        of the same chunk resolves still counts. Returns True when the limit
        was exceeded: the stage is halted and the caller must not emit or
        delete anything else.
        """
        if self.record_cap is None:
            return False
        value = self._prefix_bytes(frontier)
        if value > self._peak_bytes:
            self._peak_bytes = value
        if self._peak_bytes > self.record_cap:
            self._halt()
            return True
        return False

    def note_partial(self, length: int) -> bool:
        """Charge an unresolved proper literal prefix of `length` characters (S3).

        A record opener is only viable at a line start, so the unresolved
        window of such a partial is the literal prefix itself: the text before
        its line start is safe and is not charged. The record literals are
        ASCII, so their UTF-8 byte volume is their character length. Returns
        True when the limit was exceeded (the stage is halted).
        """
        if self.record_cap is None:
            return False
        if length > self._peak_bytes:
            self._peak_bytes = length
        if self._peak_bytes > self.record_cap:
            self._halt()
            return True
        return False

    def _prefix_bytes(self, frontier: int) -> int:
        """UTF-8 bytes of `pending[:frontier]`, kept incrementally (S3)."""
        if frontier >= len(self._pending):
            # The whole window has been examined, so its own byte counter is
            # the charged value: no slice is built to measure it.
            self._charged_chars = len(self._pending)
            self._charged_bytes = self._pending.bytes
            return self._charged_bytes
        if frontier > self._charged_chars:
            self._charged_bytes += _utf8_bytes(
                self._pending[self._charged_chars:frontier])
        elif frontier < self._charged_chars:
            # The window was consumed from the front or a new candidate
            # started: the charged prefix is no longer the same text.
            self._charged_bytes = _utf8_bytes(self._pending[:frontier])
        self._charged_chars = frontier
        return self._charged_bytes

    def _partial_excess_start(self, literal: str) -> Optional[int]:
        """Leftmost line-start partial opener that already exceeds the limit (S3).

        A record opener is only viable at a line start and its partial grows
        one character per symbol, so the first excess of a character-wise feed
        is the leftmost line-start position where `literal[:cap + 1]` occurs.
        The text before that position is what that feed would already have
        emitted, so the caller emits it before the stage halts and the frozen
        prefix stays identical under every chunk partition. No partial can
        exceed the limit when the limit is at least the literal length, so the
        scan is skipped there.
        """
        cap = self.record_cap
        if cap is None or cap < 0 or cap >= len(literal):
            return None
        needle = literal[:cap + 1]
        pending = self._pending
        index = pending.find(needle)
        while index != -1:
            if self._is_line_start(index):
                return index
            index = pending.find(needle, index + 1)
        return None

    def _max_partial_at_line_start(self, literal: str) -> int:
        """Longest viable partial opener prefix present at a line start (S3).

        A chunk has to be charged as if its symbols were fed one at a time, so
        a partial opener that a later character of the same chunk breaks still
        counts while it existed. Every proper prefix is only viable at a line
        start; longer prefixes imply shorter ones, so the search stops at the
        first missing prefix. The check is skipped when the limit cannot be
        reached by an opener partial at all (at most `len(literal) - 1`).
        """
        cap = self.record_cap
        if cap is None or cap >= len(literal):
            return 0
        pending = self._pending
        for keep in range(len(literal) - 1, 0, -1):
            prefix = literal[:keep]
            if pending.find("\n" + prefix) != -1:
                return keep
            if self._line_start and pending.startswith(prefix):
                return keep
        return 0

    def _append(self, text: str) -> None:
        self._pending.append(text)

    def _consume(self, count: int) -> str:
        text = self._pending.consume(count)
        if len(text) >= self._charged_chars:
            self._charged_chars = 0
            self._charged_bytes = 0
        elif text:
            self._charged_chars -= len(text)
            self._charged_bytes -= _utf8_bytes(text)
        return text

    def _emit(self, count: int) -> None:
        text = self._consume(count)
        if text:
            self._line_start = text.endswith("\n")
            self._sink(text)

    def _delete(self, count: int) -> None:
        text = self._consume(count)
        if text:
            self._line_start = text.endswith("\n")

    def _emit_all(self) -> None:
        self._emit(len(self._pending))

    def _is_line_start(self, index: int) -> bool:
        if index == 0:
            return self._line_start
        return self._pending[index - 1] == "\n"

    def _keep_partial(self, literal: str, require_line_start: bool) -> int:
        """Retained suffix: a proper literal prefix, optionally at a line start."""
        limit = min(len(literal) - 1, len(self._pending))
        for keep in range(limit, 0, -1):
            if not self._pending.endswith(literal[:keep]):
                continue
            if require_line_start \
                    and not self._is_line_start(len(self._pending) - keep):
                continue
            return keep
        return 0


class _ComponentStage(_StreamStage):
    """One independent component substitution stage (S2/F3).

    Semantics are exactly Python `re.sub('', text)` with
    `\\[<component>\\] [^\\n]*\\n?`: the literal is recognized anywhere, the
    suffix through its optional LF is discarded without retaining its payload,
    and text before the literal is forwarded without inventing a terminator.
    """

    def __init__(self, literal: str, sink):
        super().__init__(sink)
        self._literal = literal
        # The proper literal prefixes a still-unresolved suffix may be, longest
        # first, so no candidate string is built while recognizing a chunk.
        self._tail_prefixes = tuple(
            literal[:keep] for keep in range(len(literal) - 1, 0, -1))
        self._tail_characters = frozenset(
            prefix[-1] for prefix in self._tail_prefixes)
        self._discarding = False

    def feed(self, text: str) -> None:
        self._append(text)
        self._run(False)

    def finish(self) -> None:
        self._run(True)

    def free(self) -> None:
        super().free()
        self._discarding = False

    def _run(self, eof: bool) -> None:
        # Each loop iteration is one symbol transition of this stage; the
        # still-unresolved window is charged after every one of them, so a
        # caller-supplied chunk boundary cannot change the verdict (S3).
        while True:
            if self._halted:
                return
            if self._discarding:
                newline = self._pending.find("\n")
                if newline == -1:
                    # The match extends to the current end of the stream; its
                    # payload is discarded, so nothing is retained.
                    self._consume(len(self._pending))
                    break
                self._consume(newline + 1)
                self._discarding = False
                continue
            found = self._pending.find(self._literal)
            if found == -1:
                # A partial literal is only a possible future match while the
                # stream continues; at EOF it is plain unmatched text.
                keep = 0 if eof else self._keep_partial_literal()
                self._emit(len(self._pending) - keep)
                break
            self._emit(found)
            self._consume(len(self._literal))
            self._discarding = True
        self._charge_record_cap()

    def _keep_partial_literal(self) -> int:
        """Length of the retained suffix that may still complete the literal."""
        pending = self._pending
        if not pending or pending[-1] not in self._tail_characters:
            return 0
        for prefix in self._tail_prefixes:
            if pending.endswith(prefix):
                return len(prefix)
        return 0


_GC_OPENER = "[gc]"
_GC_REASON_MARKER = "GCReason:"
_GC_CLOSER = "[gc] HandleStorage"


class _GcStage(_StreamStage):
    """Incremental equivalent of the GC record substitution (S2/F2).

    Semantics are exactly Python `GC_TRACE_LOG_RE.sub('', text)`: leftmost
    non-overlapping complete matches, arbitrary intervening lines, and the
    mandatory closing LF of the `[gc] HandleStorage` record. A rejected opener
    releases only the safe prefix and the remaining suffix is reconsidered in
    this same stage.

    Every transition advances one recognition step and reports the still
    unresolved examined prefix through `_examined`, so a text that a later
    transition releases is charged before that release and the verdict does not
    depend on the read partition (S3): a partial opener, the headline line, the
    closer line suffix and an unterminated closer are all charged while their
    decision is undecided.
    """

    def __init__(self, sink):
        super().__init__(sink)
        self._mode = "idle"
        self._cursor = 0
        self._opener_lf = 0
        self._closer_cursor = 0
        self._closer_lf = 0

    def feed(self, text: str) -> None:
        self._append(text)
        self._run(False)

    def finish(self) -> None:
        self._run(True)

    def free(self) -> None:
        super().free()
        self._mode = "idle"
        self._cursor = 0
        self._opener_lf = 0
        self._closer_cursor = 0
        self._closer_lf = 0

    def _frontier(self) -> int:
        # The still unresolved examined prefix of the record candidate; text
        # beyond it is the unread tail of a supplied chunk.
        return self._examined

    def _run(self, eof: bool) -> None:
        # Each loop iteration is one symbol transition of this record stage;
        # the still-unresolved window is charged after every one of them, so a
        # caller-supplied chunk boundary cannot change the verdict (S3). The
        # charge counts only text this stage has actually examined: safely
        # emitted text, immediately deleted matches and the unread tail of a
        # supplied chunk are not charged.
        while True:
            if self._halted:
                return
            progress = self._step(eof)
            if self._charge_record_cap():
                return
            if not progress:
                break

    def _step(self, eof: bool) -> bool:
        mode = self._mode
        if mode == "idle":
            return self._idle_step(eof)
        if mode == "opener_lf":
            return self._opener_lf_step(eof)
        if mode == "opener_check":
            return self._opener_check_step()
        if mode == "closer_seek":
            return self._closer_seek_step(eof)
        if mode == "closer_lf":
            return self._closer_lf_step(eof)
        return self._closer_ready_step()

    def _idle_step(self, eof: bool) -> bool:
        found = self._pending.find(_GC_OPENER, self._cursor)
        while found != -1 and not self._is_line_start(found):
            self._cursor = found + 1
            found = self._pending.find(_GC_OPENER, self._cursor)
        # A proper opener prefix is retained (it may complete later); every
        # character before it is safely emitted and therefore not charged. A
        # partial that a character-wise feed held unresolved is charged while
        # it existed (S3): the leftmost line-start partial whose growth already
        # exceeds the limit is the first excess, and it wins when it starts
        # before the next complete opener. The text before it is safe, so it is
        # emitted before the stage halts.
        excess = self._partial_excess_start(_GC_OPENER)
        if excess is not None and (found == -1 or excess < found):
            self._emit(excess)
            self._halt()
            return False
        if found != -1:
            # A partial opener before this complete one is at most
            # `len(_GC_OPENER) - 1` characters, so the candidate that opens
            # here charges at least as much and no separate note is needed.
            self._emit(found)
            self._mode = "opener_lf"
            self._opener_lf = len(_GC_OPENER)
            self._examined = self._opener_lf
            return True
        if eof:
            self._emit_all()
            self._examined = 0
            return False
        peak = self._max_partial_at_line_start(_GC_OPENER)
        keep = self._keep_partial(_GC_OPENER, True)
        self._emit(len(self._pending) - keep)
        self.note_partial(peak)
        self._cursor = 0
        self._examined = len(self._pending)
        return False

    def _opener_lf_step(self, eof: bool) -> bool:
        newline = self._pending.find("\n", self._opener_lf)
        if newline == -1:
            if eof:
                # No LF follows, so no later line start exists: the whole
                # remainder is unmatched text.
                self._emit_all()
                self._mode = "idle"
                self._cursor = 0
                self._examined = 0
                return False
            self._opener_lf = len(self._pending)
            self._examined = len(self._pending)
            return False
        # The LF ends the headline line; the line content is still undecided
        # until the next transition checks the reason marker, so it is charged
        # before that transition can release it (S3).
        self._opener_lf = newline + 1
        self._examined = newline
        self._mode = "opener_check"
        return True

    def _opener_check_step(self) -> bool:
        newline = self._opener_lf - 1
        line = self._pending[len(_GC_OPENER):newline]
        if _GC_REASON_MARKER in line:
            self._mode = "closer_seek"
            self._closer_cursor = self._opener_lf
            self._examined = self._opener_lf
            return True
        # Not a GC headline record: release the line and keep searching in this
        # same stage (a later opener may start on a later line).
        self._emit(self._opener_lf)
        self._mode = "idle"
        self._cursor = 0
        self._examined = 0
        return True

    def _closer_seek_step(self, eof: bool) -> bool:
        found = self._pending.find(_GC_CLOSER, self._closer_cursor)
        while found != -1:
            if self._is_line_start(found):
                # The closer literal is recognized, but its mandatory line
                # suffix and LF are not resolved yet, so they stay charged.
                self._mode = "closer_lf"
                self._closer_lf = found + len(_GC_CLOSER)
                self._examined = self._closer_lf
                return True
            self._closer_cursor = found + 1
            found = self._pending.find(_GC_CLOSER, self._closer_cursor)
        if eof:
            # No complete closer exists in the remainder: neither this opener
            # nor any later one can match, so the remainder is unmatched text.
            self._emit_all()
            self._mode = "idle"
            self._cursor = 0
            self._examined = 0
            return False
        self._closer_cursor = max(
            self._closer_cursor, len(self._pending) - len(_GC_CLOSER) + 1)
        self._examined = len(self._pending)
        return False

    def _closer_lf_step(self, eof: bool) -> bool:
        newline = self._pending.find("\n", self._closer_lf)
        if newline == -1:
            if eof:
                # The candidate closer line never ends, so it cannot close the
                # record and no later line start exists.
                self._emit_all()
                self._mode = "idle"
                self._cursor = 0
                self._examined = 0
                return False
            self._closer_lf = len(self._pending)
            self._examined = len(self._pending)
            return False
        # The mandatory LF settles the deletion, so the closer suffix is
        # charged before the next transition deletes the complete match (S3).
        self._closer_lf = newline + 1
        self._examined = newline
        self._mode = "closer_ready"
        return True

    def _closer_ready_step(self) -> bool:
        self._delete(self._closer_lf)
        self._mode = "idle"
        self._cursor = 0
        self._examined = 0
        return True


_DEOPT_HEAD = "[compiler] "
# The frame literal includes its mandatory trailing space: `(?:    at [^\n]*\n)*`
# requires a space after `at`, so `    atX` is not a frame line.
_DEOPT_FRAME = "    at "


class _DeoptStage(_StreamStage):
    """Incremental equivalent of the deopt record substitution (S2/F2).

    Semantics are exactly Python
    `(?m)^\\[compiler\\] (?:Lazy )?Deoptimize\\s+at [^\\n]*\\n(?:    at [^\\n]*\\n)*\\n`:
    the lazy form, Python's Unicode whitespace class (which may cross LF), zero
    or more four-space frame lines, and the blank closing LF. A rejected start
    releases only text that cannot participate in a later match and the suffix
    is reconsidered in this same stage.
    """

    def __init__(self, sink):
        super().__init__(sink)
        self._mode = "idle"
        self._cursor = 0
        self._phase = "name"
        self._pos = 0
        self._expect: Optional[str] = None
        self._expect_at = 0
        self._spaces = 0

    def feed(self, text: str) -> None:
        self._append(text)
        self._run(False)

    def finish(self) -> None:
        self._run(True)

    def free(self) -> None:
        super().free()
        self._mode = "idle"
        self._cursor = 0
        self._phase = "name"
        self._pos = 0
        self._expect = None
        self._expect_at = 0
        self._spaces = 0

    def _frontier(self) -> int:
        # The still unresolved examined prefix: the candidate scan position in
        # candidate mode, the retained partial opener in idle mode. Text beyond
        # it is an unread chunk tail and is never charged.
        return self._examined

    def _run(self, eof: bool) -> None:
        # Each loop iteration is one symbol transition of this record stage;
        # the still-unresolved window is charged after every one of them, so a
        # caller-supplied chunk boundary cannot change the verdict (S3).
        while True:
            if self._halted:
                return
            if self._mode == "idle":
                progress = self._open(eof)
                # A retained partial opener is still undecided and is charged;
                # the safely emitted prefix before it is not.
                self._examined = self._pos if self._mode == "candidate" \
                    else len(self._pending)
            else:
                progress = self._advance(eof)
                # A rejection or a completed deletion releases the examined
                # text, so only the new candidate position stays charged.
                self._examined = self._pos if self._mode == "candidate" else 0
            if self._charge_record_cap():
                return
            if not progress:
                break

    def _open(self, eof: bool) -> bool:
        found = self._pending.find(_DEOPT_HEAD, self._cursor)
        while found != -1 and not self._is_line_start(found):
            self._cursor = found + 1
            found = self._pending.find(_DEOPT_HEAD, self._cursor)
        # The leftmost line-start partial head whose growth already exceeds the
        # limit is the first excess of a character-wise feed, and it wins when
        # it starts before the next complete head: the text before it is safe,
        # so it is emitted before the stage halts (S3).
        excess = self._partial_excess_start(_DEOPT_HEAD)
        if excess is not None and (found == -1 or excess < found):
            self._emit(excess)
            self._halt()
            return False
        if found != -1:
            # A partial head before this complete one is at most
            # `len(_DEOPT_HEAD) - 1` characters, so the candidate that opens
            # here charges at least as much and no separate note is needed.
            self._emit(found)
            self._mode = "candidate"
            self._phase = "name"
            self._pos = len(_DEOPT_HEAD)
            self._expect = None
            self._expect_at = self._pos
            self._spaces = 0
            return True
        if eof:
            self._emit_all()
            return False
        peak = self._max_partial_at_line_start(_DEOPT_HEAD)
        keep = self._keep_partial(_DEOPT_HEAD, True)
        self._emit(len(self._pending) - keep)
        self.note_partial(peak)
        self._cursor = 0
        return False

    def _match(self, expect: str) -> str:
        """Streaming literal match that remembers its own progress (S2)."""
        if self._expect != expect:
            self._expect = expect
            self._expect_at = self._pos
        consumed = self._pos - self._expect_at
        if consumed >= len(expect):
            return "ok"
        available = len(self._pending) - self._pos
        count = min(available, len(expect) - consumed)
        if count and self._pending[self._pos:self._pos + count] \
                != expect[consumed:consumed + count]:
            # Every literal character that did match leaves the candidate
            # unresolved, so it is charged while it is still viable: a chunk
            # whose later symbol already mismatches must not hide the prefix a
            # character-wise feed would have charged (S3).
            matched = 0
            while matched < count and self._pending[self._pos + matched] \
                    == expect[consumed + matched]:
                matched += 1
            if self.note_unresolved(self._pos + matched):
                # The limit was reached while the candidate was still
                # unresolved: the stage is halted and the candidate must not
                # be released downstream (S3).
                return "capped"
            return "fail"
        self._pos += count
        return "ok" if consumed + count == len(expect) else "partial"

    def _reject(self) -> bool:
        """Release only text that cannot take part in a later match (S2/F2)."""
        newline = self._pending.find("\n")
        if newline == -1:
            # Without an LF no later line start exists in the pending text, so
            # the whole remainder is unmatched.
            self._emit_all()
        else:
            self._emit(newline + 1)
        self._mode = "idle"
        self._phase = "name"
        self._pos = 0
        self._expect = None
        self._spaces = 0
        self._cursor = 0
        return True

    def _advance(self, eof: bool) -> bool:
        """One symbol transition of the deopt candidate (S2).

        Exactly one phase step per call, so the still-unresolved window is
        charged at every transition of the candidate and the verdict cannot
        depend on the read partition (S3). `True` means the candidate moved on,
        `False` that it needs more input; a rejection releases the text that
        cannot take part in a later match in this same stage.
        """
        if self._halted:
            return False
        phase = self._phase
        if phase == "name":
            if self._pos >= len(self._pending):
                if eof:
                    return self._reject()
                return False
            char = self._pending[self._pos]
            if char == "L":
                self._phase = "lazy"
                self._pos += 1
            elif char == "D":
                self._phase = "eager"
                self._pos += 1
            else:
                return self._reject()
            return True
        if phase in ("lazy", "eager"):
            expect = ("azy " + DEOPT_EAGER_PREFIX) if phase == "lazy" \
                else DEOPT_EAGER_PREFIX[1:]
            status = self._match(expect)
            if status == "capped":
                return False
            if status == "fail":
                return self._reject()
            if status == "partial":
                if eof:
                    return self._reject()
                return False
            self._spaces = 0
            self._phase = "space"
            return True
        if phase == "space":
            index = self._pos
            limit = len(self._pending)
            while index < limit and self._pending[index].isspace():
                index += 1
            self._spaces += index - self._pos
            self._pos = index
            if index == limit:
                if eof:
                    return self._reject()
                return False
            if self._spaces == 0:
                return self._reject()
            self._phase = "at"
            return True
        if phase == "at":
            status = self._match("at ")
            if status == "capped":
                return False
            if status == "fail":
                return self._reject()
            if status == "partial":
                if eof:
                    return self._reject()
                return False
            self._phase = "line"
            return True
        if phase == "line":
            newline = self._pending.find("\n", self._pos)
            if newline == -1:
                self._pos = len(self._pending)
                if eof:
                    return self._reject()
                return False
            self._pos = newline + 1
            self._phase = "frames"
            return True
        if phase == "frame_line":
            newline = self._pending.find("\n", self._pos)
            if newline == -1:
                self._pos = len(self._pending)
                if eof:
                    return self._reject()
                return False
            self._pos = newline + 1
            self._phase = "frames"
            return True
        # "frames": the blank closing LF or another four-space frame line.
        limit = len(self._pending)
        if self._pos >= limit:
            if eof:
                return self._reject()
            return False
        if self._pending[self._pos] == "\n":
            self._delete(self._pos + 1)
            self._mode = "idle"
            self._phase = "name"
            self._pos = 0
            self._expect = None
            self._cursor = 0
            return True
        count = min(limit - self._pos, len(_DEOPT_FRAME))
        piece = self._pending[self._pos:self._pos + count]
        if piece != _DEOPT_FRAME[:count]:
            # The frame characters that did match kept the candidate viable, so
            # they are charged before the rejection releases the candidate (S3).
            matched = 0
            while matched < count and piece[matched] == _DEOPT_FRAME[matched]:
                matched += 1
            if self.note_unresolved(self._pos + matched):
                return False
            return self._reject()
        if count < len(_DEOPT_FRAME):
            # A retained partial frame literal is still unresolved, so it is
            # charged exactly like a character-wise feed would charge it (S3).
            if self.note_unresolved(self._pos + count):
                return False
            if eof:
                return self._reject()
            return False
        self._pos += len(_DEOPT_FRAME)
        self._phase = "frame_line"
        return True


class _CorpusProgramSink:
    """Bounded final program-output sink of one pipeline (S3/F4).

    Only text committed after all substitutions is charged. The retained value
    is the longest contiguous character prefix that fits the cap; on the first
    excess `truncated` becomes permanently true and every later final chunk is
    dropped, so a later small chunk can never restore validity.
    """

    def __init__(self, cap: int):
        self._cap = cap
        self._parts: List[str] = []
        self._bytes = 0
        self._truncated = False

    def feed(self, text: str) -> None:
        if not text or self._truncated:
            return
        size = len(text.encode("utf-8"))
        if self._bytes + size <= self._cap:
            self._parts.append(text)
            self._bytes += size
            return
        low, high = 0, len(text)
        while low < high:
            middle = (low + high + 1) // 2
            if self._bytes + len(text[:middle].encode("utf-8")) <= self._cap:
                low = middle
            else:
                high = middle - 1
        if low:
            self._parts.append(text[:low])
            self._bytes += len(text[:low].encode("utf-8"))
        self._truncated = True

    @property
    def text(self) -> str:
        return "".join(self._parts)

    @property
    def bytes(self) -> int:
        return self._bytes

    @property
    def truncated(self) -> bool:
        return self._truncated


class _CorpusFilterPipeline:
    """Ordered streaming substitution pipeline of one case (S2/S3).

    One stage per chain entry, chained in order; the final stage feeds the
    bounded program-output sink. Each enabled record stage has its own
    unresolved-text limit, and an overrun frees every candidate window and
    permanently disables output certification while the caller keeps draining
    and observing the stream.
    """

    def __init__(self, entries: List[Tuple[str, Optional[str]]], program_cap: int,
                 record_candidate_cap: int):
        self._record_candidate_cap = record_candidate_cap
        self._sink = _CorpusProgramSink(program_cap)
        stages: List[_StreamStage] = []
        kinds: List[str] = []
        downstream = self._sink.feed
        for kind, component in reversed(entries):
            if kind == "gc":
                stage: _StreamStage = _GcStage(downstream)
            elif kind == "deopt":
                stage = _DeoptStage(downstream)
            else:
                assert component is not None
                stage = _ComponentStage(_component_filter_literal(component),
                                        downstream)
            stages.insert(0, stage)
            kinds.insert(0, kind if kind != "component"
                         else f"component:{component}")
            downstream = stage.feed
        self._stages = stages
        # The ordered stage sequence actually built, in chain order.
        self.stage_kinds = kinds
        self._feed = downstream
        self._record_stages = [record_stage for record_stage, entry in zip(stages, entries)
                               if entry[0] in ("gc", "deopt")]
        self._candidate_capped = False
        self._disabled = False
        for stage in self._record_stages:
            stage.record_cap = record_candidate_cap
            stage._on_record_cap = self._record_cap_exceeded

    @classmethod
    def from_chain(cls, chain: List[re.Pattern], names: List[str],
                   program_cap: int, record_candidate_cap: int):
        """Build the pipeline from the canonical `case_filter_chain` (S1).

        Every stage is derived from an element of the chain returned by
        `case_filter_chain`, and the component names are paired with the
        component patterns in chain order, so the streaming substitutions are
        exactly the built-in path's substitutions and an unexpected chain
        raises the named configuration error.
        """
        entries: List[Tuple[str, Optional[str]]] = []
        remaining = list(names)
        for pattern in chain:
            if pattern is GC_TRACE_LOG_RE:
                entries.append(("gc", None))
            elif pattern is DEOPT_TRACE_LOG_RE:
                entries.append(("deopt", None))
            else:
                if not remaining:
                    raise FilterConfigurationError(
                        "filter configuration error: chain pattern "
                        f"{pattern.pattern!r} has no component name")
                name = remaining.pop(0)
                if pattern.pattern != _component_pattern(name).pattern:
                    raise FilterConfigurationError(
                        f"filter configuration error: chain pattern "
                        f"{pattern.pattern!r} does not match component {name!r}")
                entries.append(("component", name))
        if remaining:
            raise FilterConfigurationError(
                f"filter configuration error: component names {remaining!r} "
                f"have no chain pattern")
        return cls(entries, program_cap, record_candidate_cap)

    @property
    def output_text(self) -> str:
        return self._sink.text

    @property
    def output_bytes(self) -> int:
        return self._sink.bytes

    @property
    def truncated(self) -> bool:
        return self._sink.truncated

    @property
    def candidate_capped(self) -> bool:
        return self._candidate_capped

    def feed(self, text: str) -> None:
        if self._disabled:
            return
        self._feed(text)
        self._check_candidates()

    def finish(self) -> None:
        if self._disabled:
            return
        # Ordered finalization: flushing an earlier stage feeds every later one
        # before that later stage is closed. A limit reached while flushing
        # disables (and frees) the whole pipeline, so the loop stops there.
        for stage in self._stages:
            if self._disabled:
                break
            stage.finish()
        self._check_candidates()

    def disable(self) -> None:
        if self._disabled:
            return
        self._disabled = True
        for stage in self._stages:
            stage.free()

    def _record_cap_exceeded(self) -> None:
        """First record-stage excess: freeze output and free every window."""
        if self._candidate_capped:
            return
        self._candidate_capped = True
        self.disable()

    def _check_candidates(self) -> None:
        if self._candidate_capped:
            return
        for stage in self._record_stages:
            if stage.unresolved_bytes() > self._record_candidate_cap:
                self._record_cap_exceeded()
                return


CORPUS_DECODE_CHUNK = 64 * 1024
CORPUS_COMPILE_MARKER_PREFIX = "======== ArkSteedCompilerTask: Starts compiling:"
CORPUS_COMPILE_MARKER_SUFFIX = " ========"
CORPUS_EXEC_MARKER_PREFIX = ">>> Running method: "
# Components whose marker identities are recognized independently of the
# retention component list (S5): the compiler component carries the compile,
# execution and deopt markers, the gc component carries the GC markers.
CORPUS_MARKER_COMPONENTS = ("compiler", "gc")


class _CorpusObserver:
    """Raw observation branch of one corpus execution (S5).

    The branch sees the original decoded characters *before* any deletion. It
    counts the compilation, execution, deopt and GC markers and the named
    descriptor patterns, and it writes the component log lines (without the
    execution-marker payloads) to the bounded retained-log sink. It never
    decides the filtered program output; that is the separate transformation
    branch. Marker identities, one-count-per-event semantics and the named
    pattern semantics are those of `collect_evidence`/`_evidence_matches` with
    `fnmatchcase`, but payloads are recognized and matched incrementally, so a
    long payload is never retained.
    """

    def __init__(self, case, components, log_file, log_cap: int):
        self.evidence = CorpusStreamEvidence()
        self._log = log_file
        self._log_cap = log_cap
        # First retained-log sink failure of this execution (S5). The sink is
        # replaced by a discard sink and observation continues: only the sink
        # failed, so the raw counts must keep their values.
        self.log_error: Optional[BaseException] = None
        self._log_broken = False
        self._named = {
            kind: _NamedMatchers(case.evidence_constraints, kind)
            for kind in ("compile", "exec", "deopt", "gc")
        }
        self._components = corpus_observation_components(case, components)
        # Marker identities and the retained-component admission are separate
        # (S5): the compile, exec and deopt markers are `[compiler]`-prefixed
        # and the GC markers are `[gc]`-prefixed, so those two component
        # prefixes are always recognized and counted, while only a component
        # of the effective observation list is eligible for the retained log.
        # A component list of `[]` is valid and must not lose raw evidence.
        # The fixed marker identities have priority over every supplied name:
        # a longer supplied prefix that extends `[compiler] ` or `[gc] ` would
        # otherwise shadow the marker identity in one chunk and lose to it
        # character by character, making the raw counts partition-dependent.
        self._recognized: List[str] = []
        for name in CORPUS_MARKER_COMPONENTS:
            if name not in self._recognized:
                self._recognized.append(name)
        for name in self._components:
            if name not in self._recognized:
                self._recognized.append(name)
        self._retained = frozenset(self._components)
        self._prefixes = tuple(f"[{recognized}] " for recognized in self._recognized)
        self._kind_prefixes = (
            CORPUS_COMPILE_MARKER_PREFIX, CORPUS_EXEC_MARKER_PREFIX
        ) + tuple(DEOPT_EVENT_PREFIXES)
        self._reset_line()

    # -- line state -------------------------------------------------------
    def _reset_line(self) -> None:
        self._head = ""
        self._touched = False
        self._state = "head"
        self._line_prefix = ""
        self._component = None
        self._type = None
        self._decided = False
        self._retain = False
        self._log_chunks: List[bytes] = []
        self._eager = False
        self._exec_leading = True
        self._exec_pending = False
        self._exec_snapshot = None
        self._c_phase = None
        self._c_pos = 0
        self._c_first9 = ""
        self._c_j = 0
        self._c_k: Optional[int] = None
        self._c_suffix = ""
        self._c_name_fed = False

    # -- streaming --------------------------------------------------------
    def feed(self, text: str) -> Optional[int]:
        """Observe one chunk; report the offset reached by a failing sink write.

        The retained-log sink is written once per observed line. The failing
        operation is the write of that line, and it runs *after* the whole line
        (including its LF) has been observed, so the returned offset is the
        position right after the failing line's LF. The caller then feeds
        exactly the text up to and including that failing line to the
        transformation branch and freezes the committed program prefix there:
        which read chunk carried the failure no longer changes the collected
        text, the invalidity fields or the candidate charge, because a
        character-wise delivery feeds the same characters before the failing
        write (S3/S5). `None` means the whole chunk was observed without a new
        sink failure. Observation of the whole chunk continues regardless: only
        the sink failed, so the later markers of the same chunk still count.
        """
        broken = self._log_broken
        failure_offset: Optional[int] = None
        start = 0
        while True:
            newline = text.find("\n", start)
            if newline == -1:
                self._add_part(text[start:])
                return failure_offset
            self._add_part(text[start:newline])
            self._end_line("\n")
            if failure_offset is None and self._log_broken and not broken:
                failure_offset = newline + 1
            start = newline + 1

    def finish(self) -> None:
        if self._touched:
            self._end_line("")

    def _add_part(self, part: str) -> None:
        if not part:
            return
        self._touched = True
        if self._state == "head":
            self._head += part
            self._advance_head(False)
        elif self._state == "payload":
            if self._decided:
                if self._retain:
                    self._log_text(part)
                self._dispatch(part)
            else:
                self._head += part
                self._advance_payload(False)
        # state "program": the line is not a component line; nothing to observe.

    def _advance_head(self, force: bool) -> None:
        if self._component is None:
            index, done = _match_prefix_candidates(
                self._head, self._prefixes, force)
            if not done and not force:
                return
            if index is None:
                self._state = "program"
                self._head = ""
                return
            self._line_prefix = self._prefixes[index]
            self._component = self._recognized[index]
            self._head = self._head[len(self._line_prefix):]
            self._state = "payload"
        self._advance_payload(force)

    def _kind_candidates(self) -> List[str]:
        if self._component == "compiler":
            return self._kind_prefixes
        return ()

    def _advance_payload(self, force: bool) -> None:
        head = self._head
        if not self._decided:
            if self._component == "gc":
                self._decided = True
                self._type = "gc"
            elif self._component == "compiler":
                candidates = self._kind_candidates()
                index, done = _match_prefix_candidates(head, candidates, force)
                if not done and not force:
                    return
                self._decided = True
                if index == 0:
                    self._type = "compile"
                elif index == 1:
                    self._type = "exec"
                elif index is not None:
                    self._type = "deopt"
                    self._eager = candidates[index] \
                        == DEOPT_EAGER_NO_STACK_TRACE_PREFIX
                else:
                    self._type = "plain"
            else:
                self._decided = True
                self._type = "plain"
            if self._type == "exec":
                # The marker-dominated execution trace is counted, never
                # retained (S5/C4).
                self._retain = False
                self._log_chunks = []
            else:
                self._retain = self._component in self._retained
                if self._retain:
                    self._log_text(self._line_prefix)
            if self._type in ("compile", "exec", "deopt", "gc"):
                # Every candidate payload is matched independently, exactly as
                # `_evidence_matches` matches one candidate at a time.
                self._named[self._type].reset()
        if head:
            self._head = ""
            if self._retain:
                self._log_text(head)
            if self._type == "compile":
                self._dispatch_compile(head[len(CORPUS_COMPILE_MARKER_PREFIX):]
                                       if head.startswith(CORPUS_COMPILE_MARKER_PREFIX)
                                       else "")
            elif self._type == "exec":
                self._dispatch_exec(head[len(CORPUS_EXEC_MARKER_PREFIX):]
                                    if head.startswith(CORPUS_EXEC_MARKER_PREFIX)
                                    else "")
            elif self._type == "deopt":
                self._dispatch_deopt(head)
            elif self._type == "gc":
                self._dispatch_gc(head)

    def _dispatch(self, text: str) -> None:
        if self._type == "compile":
            self._dispatch_compile(text)
        elif self._type == "exec":
            self._dispatch_exec(text)
        elif self._type == "deopt":
            self._dispatch_deopt(text)
        elif self._type == "gc":
            self._dispatch_gc(text)

    def _dispatch_exec(self, text: str) -> None:
        if not self._named["exec"].active:
            return
        for char in text:
            if char.isspace():
                if not self._exec_leading:
                    if not self._exec_pending:
                        self._exec_pending = True
                        self._exec_snapshot = self._named["exec"].snapshot()
                    self._named["exec"].feed(char)
                continue
            self._exec_leading = False
            self._exec_pending = False
            self._named["exec"].feed(char)

    def _dispatch_deopt(self, text: str) -> None:
        if self._named["deopt"].active:
            self._named["deopt"].feed(text)

    def _dispatch_gc(self, text: str) -> None:
        if self._named["gc"].active:
            self._named["gc"].feed(text)

    def _dispatch_compile(self, text: str) -> None:
        if self._c_phase == "done":
            return
        for char in text:
            self._compile_char(char)
            if self._c_phase == "done":
                return

    def _compile_char(self, char: str) -> None:
        index = self._c_pos
        self._c_pos += 1
        if index < len(CORPUS_COMPILE_MARKER_SUFFIX):
            self._c_first9 += char
        if index == 0:
            self._c_j = 1 if char == " " else 0
        if self._c_phase == "suffix":
            if len(self._c_suffix) < len(CORPUS_COMPILE_MARKER_SUFFIX):
                self._c_suffix += char
                if len(self._c_suffix) == len(CORPUS_COMPILE_MARKER_SUFFIX):
                    self._end_compile()
            return
        if index < self._c_j:
            return
        if char.isspace():
            # The whitespace that ends the `\S*` name is the first character
            # of the mandatory ` ========` suffix.
            self._c_k = index
            self._c_phase = "suffix"
            self._c_suffix = char
            if len(self._c_suffix) == len(CORPUS_COMPILE_MARKER_SUFFIX):
                self._end_compile()
            return
        self._c_name_fed = True
        if self._named["compile"].active:
            self._named["compile"].feed(char)

    def _compile_decision(self) -> str:
        """`streamed`, `empty` or `plain` for the buffered compile candidate."""
        suffix_ok = (self._c_k is not None
                     and self._c_suffix == CORPUS_COMPILE_MARKER_SUFFIX)
        if self._c_j == 1:
            if suffix_ok:
                return "streamed"
            if self._c_first9 == CORPUS_COMPILE_MARKER_SUFFIX:
                return "empty"
            return "plain"
        return "streamed" if suffix_ok else "plain"

    def _end_compile(self) -> None:
        if self._c_phase == "done":
            return
        self._c_phase = "done"
        decision = self._compile_decision()
        if decision == "plain":
            return
        self.evidence.compile_markers += 1
        if decision == "empty" or not self._c_name_fed:
            # An empty `(\S*)` group is the module's `(anonymous)` name.
            self._named["compile"].reset()
            self._named["compile"].feed("(anonymous)")
        for constraint in self._named["compile"].matched(False):
            self.evidence.named_counts[constraint] = \
                self.evidence.named_counts.get(constraint, 0) + 1

    # -- line end ---------------------------------------------------------
    def _end_line(self, terminator: str) -> None:
        if self._state == "head":
            self._advance_head(True)
        if self._state == "payload":
            if not self._decided:
                self._advance_payload(True)
            self._end_payload()
        if self._retain and terminator:
            self._log_bytes(terminator.encode("utf-8"))
        self._flush_line()
        self._reset_line()

    def _end_payload(self) -> None:
        if self._type == "exec":
            if self._exec_pending:
                # `.strip()` drops this trailing whitespace run.
                self._named["exec"].restore(self._exec_snapshot)
            self.evidence.exec_markers += 1
            for constraint in self._named["exec"].matched(False):
                self.evidence.named_counts[constraint] = \
                    self.evidence.named_counts.get(constraint, 0) + 1
        elif self._type == "compile":
            self._end_compile()
        elif self._type == "deopt":
            self.evidence.deopt_markers += 1
            for constraint in self._named["deopt"].matched(self._eager):
                self.evidence.named_counts[constraint] = \
                    self.evidence.named_counts.get(constraint, 0) + 1
        elif self._type == "gc":
            self.evidence.gc_markers += 1
            for constraint in self._named["gc"].matched(False):
                self.evidence.named_counts[constraint] = \
                    self.evidence.named_counts.get(constraint, 0) + 1

    def _log_text(self, text: str) -> None:
        if text:
            self._log_bytes(text.encode("utf-8"))

    def _log_bytes(self, data: bytes) -> None:
        if not data:
            return
        self.evidence.compiler_log_bytes += len(data)
        if self.evidence.compiler_log_capped:
            return
        if self.evidence.compiler_log_bytes > self._log_cap:
            self.evidence.compiler_log_capped = True
            self._log_chunks = []
            return
        self._log_chunks.append(data)

    def _flush_line(self) -> None:
        chunks, self._log_chunks = self._log_chunks, []
        if not self._retain or not chunks or self._log_broken:
            return
        try:
            self._log.write(b"".join(chunks))
        except Exception as error:  # noqa: BLE001 - handled at the observation boundary
            # A sink failure is recoverable for observation: name it once, keep
            # the line's evidence and continue with the same and every later
            # chunk. The caller disables output certification for this
            # execution.
            self._log_broken = True
            self._log = _CorpusDiscardSink()
            if self.log_error is None:
                self.log_error = error


def corpus_observation_components(case: TestCase,
                                  components: List[str]) -> List[str]:
    """Effective log components of a corpus class command (C4/C5).

    The class command logs the explicit components plus the annotation-implied
    ones, deduplicated in command order; the raw observation branch recognizes
    exactly these prefixes, so the collector and the command cannot disagree.
    """
    effective = [component for component in components if component]
    for component in _implied_log_components(case):
        if component not in effective:
            effective.append(component)
    return effective


def _iter_decoded_chunks(stream, chunk_size: int = CORPUS_DECODE_CHUNK):
    """Yield exact decoded text chunks of a binary pipe (S5).

    Fixed-size byte reads through a private incremental UTF-8 decoder with
    replacement, so a long line is consumed incrementally and the decoded text
    equals `whole_stream_bytes.decode('utf-8', errors='replace')` for every
    byte partition. A read failure propagates and is never a clean EOF, and the
    final decoder flush happens only on clean EOF.
    """
    decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
    while True:
        data = stream.read(chunk_size)
        if not data:
            break
        text = decoder.decode(data)
        if text:
            yield text
    tail = decoder.decode(b"", True)
    if tail:
        yield tail


def consume_corpus_stdout(
    chunks, case, components, log_file, *,
    program_cap: int = CORPUS_PROGRAM_OUTPUT_CAP,
    log_cap: int = CORPUS_COMPILER_LOG_CAP,
    record_candidate_cap: int = CORPUS_RECORD_CANDIDATE_CAP,
    external_failure: Optional[Callable[[], Optional[str]]] = None,
) -> CorpusStreamEvidence:
    """Consume one corpus stdout stream and collect its evidence (C4/S2-S5).

    `chunks` is an iterable of exact decoded strings with their original LF
    (line-sized or arbitrarily partitioned). `case` supplies the annotations
    and the annotation predicates; `components` supplies the ordered *explicit*
    component names (not the command's deduplicated observation list). The
    transformation branch is the exact ordered substitution chain of
    `case_filter_chain`; the raw observation branch counts the markers and the
    named descriptor patterns and writes the bounded retained log. A limit or a
    failure is a named sticky field of the returned evidence, never a silent
    pass and never a certified empty result.

    `external_failure`, when given, is polled at every chunk boundary of the
    same execution: it returns the named failure of the parallel drain (the
    stderr pipe of the class executor), or `None`. Polling it here keeps the
    failure boundary, the first named error and the frozen committed prefix of
    both pipes consistent: once the other drain has failed, no later readable
    stdout text is transformed or committed, while raw observation and bounded
    draining continue (S3/S5).
    """
    # The canonical input is an ordered sequence of explicit component names
    # and may be a one-shot iterable, so it is materialized and validated once
    # here: chain construction, name pairing and raw observation then all use
    # the same stable ordered snapshot with its repetitions preserved (S1).
    explicit = _filter_component_names(components)
    chain = case_filter_chain(case, explicit)
    names = case_filter_component_names(case, explicit)
    pipeline = _CorpusFilterPipeline.from_chain(
        chain, names, program_cap, record_candidate_cap)
    observer = _CorpusObserver(case, explicit, log_file, log_cap)
    evidence = observer.evidence
    observer_failed = False
    log_error_recorded = False
    clean_eof = True
    # A retained-log sink that could not even be opened is a collection failure
    # known before any input. It is recorded here, at the failure boundary, and
    # the transformation branch is disabled immediately: the committed program
    # prefix stays the empty state of the failure (no later readable text of
    # the stream can become a certified prefix or set the output cap), while the
    # raw observation still runs on the discard sink (S3/S5).
    initial_log_error = getattr(log_file, "error", None)
    if initial_log_error is not None:
        _record_corpus_collection_error(evidence, "retained log", initial_log_error)
        log_error_recorded = True
        pipeline.disable()
    external_recorded = False

    def note_external_failure() -> None:
        """Record a parallel-drain failure at its boundary, once (S3/S5).

        The stderr drain of the class executor runs in its own thread; when it
        fails, the transformation branch is disabled *here*, at the chunk
        boundary where the collector observes the failure, so the committed
        prefix is frozen at that boundary and the first named error is the
        stderr failure rather than a later stdout read failure. Raw observation
        and draining continue (the loop keeps consuming the iterator).
        """
        nonlocal external_recorded
        if external_recorded or external_failure is None:
            return
        message = external_failure()
        if message is None:
            return
        external_recorded = True
        if evidence.program_output_error is None:
            evidence.program_output_error = message
        pipeline.disable()

    iterator = None
    try:
        iterator = iter(chunks)
    except TypeError as error:
        # A genuinely non-iterable input is unsupported configuration, not a
        # read failure: it keeps the named configuration error.
        raise FilterConfigurationError(
            f"filter configuration error: chunks is not iterable: "
            f"{type(chunks).__name__}") from error
    except Exception as error:  # noqa: BLE001 - an initialization failure is a read failure
        # An operational failure while *initializing* the iterator is a read
        # failure exactly like a failure of `next` below: it becomes named
        # evidence instead of escaping the collector or appearing as clean EOF,
        # and the output is never certified (S5).
        _record_corpus_collection_error(evidence, "read", error)
        clean_eof = False
    while iterator is not None:
        try:
            chunk = next(iterator)
        except StopIteration:
            note_external_failure()
            break
        except Exception as error:  # noqa: BLE001 - a read failure is named
            # A failure of the other drain that is already observable is the
            # first named error of this execution; a read failure observed
            # before it is named otherwise, preserving whichever came first.
            note_external_failure()
            _record_corpus_collection_error(evidence, "read", error)
            clean_eof = False
            break
        # The stderr drain may have failed before this chunk existed: disable
        # transformation before any of it can be committed, and keep observing
        # and draining the readable stream (S3/S5).
        note_external_failure()
        if not isinstance(chunk, str):
            _record_corpus_collection_error(
                evidence, "filter",
                TypeError(f"chunk is {type(chunk).__name__}, expected str"))
            pipeline.disable()
            continue
        observed = len(chunk)
        if not observer_failed:
            try:
                offset = observer.feed(chunk)
            except Exception as error:  # noqa: BLE001 - observation failures are named
                # A raw observation failure invalidates the collection: the
                # pipeline is disabled right here, so neither this chunk nor
                # any later one is transformed, and the already committed
                # bounded prefix stays frozen. Draining and counting are
                # finished by the caller's loop (S3/S5).
                _record_corpus_collection_error(evidence, "observe", error)
                observer_failed = True
                pipeline.disable()
                continue
            else:
                # A retained-log sink failure is recoverable for the raw
                # observation (the observer already switched to a discard sink
                # and kept counting), but it makes the collected output
                # unusable. Only the text that precedes the failing line was
                # committed before that event, so it is fed to the
                # transformation branch first and the output is frozen right
                # there: the same stream then yields the same text and flags
                # under every read partition (S3/S5).
                if offset is not None:
                    observed = offset
        if observer.log_error is not None and not log_error_recorded:
            _record_corpus_collection_error(evidence, "retained log",
                                            observer.log_error)
            log_error_recorded = True
            if observed:
                try:
                    pipeline.feed(chunk[:observed])
                except Exception as error:  # noqa: BLE001
                    _record_corpus_collection_error(evidence, "filter", error)
            pipeline.disable()
            continue
        try:
            pipeline.feed(chunk)
        except Exception as error:  # noqa: BLE001 - filtering failures are named
            _record_corpus_collection_error(evidence, "filter", error)
            pipeline.disable()
    if clean_eof:
        # Completion needs a clean input EOF, the decoder flush (performed by
        # the chunk producer) and the ordered stage finalization below.
        try:
            pipeline.finish()
        except Exception as error:  # noqa: BLE001
            _record_corpus_collection_error(evidence, "filter", error)
            pipeline.disable()
        if not observer_failed:
            try:
                observer.finish()
            except Exception as error:  # noqa: BLE001
                _record_corpus_collection_error(evidence, "observe", error)
        if observer.log_error is not None and not log_error_recorded:
            _record_corpus_collection_error(evidence, "retained log",
                                            observer.log_error)
            log_error_recorded = True
            pipeline.disable()
    evidence.program_output = pipeline.output_text
    evidence.program_output_bytes = pipeline.output_bytes
    evidence.program_output_truncated = pipeline.truncated
    if pipeline.candidate_capped:
        evidence.record_candidate_capped = True
    return evidence


def corpus_explicit_components(args: argparse.Namespace) -> List[str]:
    """Ordered *explicit* log component names of one corpus run (S1).

    These are the names the shared chain is built from, not the command's
    effective observation list: the annotation-implied components are appended
    once by `case_filter_chain` itself, so this list must not contain them.
    """
    return [c for c in args.log_components_value.split(":") if c]


def build_corpus_vm_command(
    case: TestCase, abc_path: Path, cfg: BuildConfig, args: argparse.Namespace,
    module: Any,
) -> Tuple[List[str], List[str]]:
    """Assemble the class command line of one corpus case (C4, C5).

    The behavioural defaults are derived from the loaded module (C5), the
    descriptor options come from the parsed case (`PARAMS`), and the
    observation is the class's own: the compiler log, the CFG dump and the
    execution marker the injected execution requirement implies. The embedded
    command builder and the embedded execution path are untouched; this
    builder is reached only for a case tagged with the corpus class.
    """
    components = corpus_observation_components(
        case, corpus_explicit_components(args))
    # The execution marker is implied by the injected execution requirement and
    # is also what makes the `NO_EXEC_MARKER` exemption checkable: an exempt
    # case is still observed, so a declaration contradicting the observation
    # stays red instead of being silently unverifiable.
    needs_exec_marker = _case_has_evidence(
        case, ExecCountConstraint, ExecCountGeConstraint)
    if case.corpus_no_exec_marker_reason is not None:
        needs_exec_marker = True
    observation = [
        f"--log-level={'debug' if args.log_debug else 'info'}",
        f"--log-components={':'.join(components)}",
        "--compiler-arksteed-print-graph=true",
        f"{CORPUS_EXEC_MARKER_OPTION}={'true' if needs_exec_marker else 'false'}",
    ]
    if _needs_deopt_trace(case):
        observation.append("--compiler-trace-deopt=true")
        observation.append("--compiler-enable-lazy-deopt-trace=true")
    if args.print_asm_code or any(case.codegen_constraints.values()):
        observation.append("--compiler-arksteed-print-code=true")
        observation.append("--compiler-arksteed-enable-code-comment=true")
    if args.fully_colored:
        observation.append("--compiler-arksteed-print-with-colors=true")
    overridden = _collect_param_overrides(case.params)
    defaults = [token for token in corpus_class_command_options(module)
                if _option_name(token) not in overridden]
    cmd = (cfg.command_prefix + [str(cfg.ark_js_vm)] + observation + defaults
           + list(case.params))
    cmd.extend([f"--entry-point={corpus_case_entry(case)}", str(abc_path)])
    return cmd, components


def compile_corpus_case(
    module: Any, case: TestCase, abc_path: Path, cfg: BuildConfig,
    verbose: bool, log: List[str],
) -> Tuple[bool, str, bool, List[str]]:
    """Compile a corpus case with the module's own functions (C2).

    The adapter owns no compilation rule: `compile_single` rewrites the bare
    imports, the assert shim and the core-js side-effect imports into a
    temporary copy outside the corpus root (created and removed by the module
    itself), while `compile_with_file_info` drives the module's fileInfo
    batch. Both apply the module's own timeouts (30 s / 60 s). A failure of
    the module call is a compilation failure of the case, never a skip.
    """
    compile_log: List[str] = []
    try:
        if corpus_case_is_file_info(module, case):
            ok, message, timed_out = module.compile_with_file_info(
                case.corpus_case, abc_path, cfg, compile_log, verbose)
        else:
            ok, message, timed_out = module.compile_single(
                case.corpus_case, abc_path, cfg, compile_log, verbose)
    except Exception as e:  # noqa: BLE001 - a module failure is a compile failure
        log.extend(compile_log)
        return False, f"{type(e).__name__}: {e}", False, compile_log
    log.extend(compile_log)
    return bool(ok), str(message), bool(timed_out), compile_log


@dataclass
class CorpusClassExecution:
    """Outcome of the class streaming execution of one corpus case (C4)."""

    command: List[str] = field(default_factory=list)
    components: List[str] = field(default_factory=list)
    returncode: Optional[int] = None
    timed_out: bool = False
    started: bool = True
    start_error: Optional[str] = None
    # Named run-level usage error of a command whose JIT token cannot identify
    # an interpreter twin (TW1): zero or more than one
    # `--compiler-enable-jit` token. Such a command is never launched, so the
    # case is a usage error (`main` exits 2) instead of a red execution.
    command_error: Optional[str] = None
    evidence: CorpusStreamEvidence = field(default_factory=CorpusStreamEvidence)
    compiler_log_path: Optional[Path] = None
    duration: float = 0.0


def corpus_execution_environment(cfg: BuildConfig) -> Dict[str, str]:
    """Environment of one corpus execution (TW1, C4).

    Both the class execution and each interpreter twin use this exact
    environment: the library search path and, on a cross build, the QEMU
    loader prefix. Rebuilding it in more than one place would let a twin drift
    from the class command it must reproduce.
    """
    env = os.environ.copy()
    env[LIB_PATH_ENV_VAR] = ":".join(str(p) for p in cfg.lib_paths)
    if cfg.qemu_ld_prefix is not None:
        env["QEMU_LD_PREFIX"] = str(cfg.qemu_ld_prefix)
    return env


def run_corpus_stream_execution(
    command: List[str], components: List[str], case: TestCase,
    explicit_components: List[str], budget: int, env: Dict[str, str],
    log_path: Path,
) -> CorpusClassExecution:
    """Execute one prepared corpus command with the streaming consumer (C4/S5).

    This is the single reusable execution entry point of the corpus class: it
    receives the prepared command, the command's observation components, the
    ordered *explicit* component names of the shared filter chain, the case
    (its annotations), the environment and the per-execution class budget, and
    it returns the full `CorpusClassExecution` with the complete
    `CorpusStreamEvidence`. The class execution and each interpreter twin go
    through it, so a twin result can never be a narrower shape that drops
    `program_output_truncated`, `record_candidate_capped` or
    `program_output_error` before the classifier.

    stdout and stderr are read as streams (a separate process, output through
    pipes; stderr is drained in parallel so a large stderr cannot block the
    child), the markers are counted while the stream passes and the raw log is
    never captured. The budget covers the draining and the filtering of the
    whole execution: the timer kills the child with the class executor's
    existing convention and sets `timed_out`. There are no retries: a second
    run of the same command could change the reproducibility evidence.
    """
    start = time.time()
    try:
        proc = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    except OSError as e:
        return CorpusClassExecution(
            command=list(command), components=list(components), started=False,
            start_error=str(e), duration=time.time() - start)
    timeout_state = {"timed_out": False}

    def on_timeout() -> None:
        timeout_state["timed_out"] = True
        try:
            proc.kill()
        except OSError:
            pass

    timer = threading.Timer(budget, on_timeout)
    timer.daemon = True
    timer.start()

    stream_errors: List[str] = []

    def drain_stderr() -> None:
        # Bounded byte reads so a large stderr cannot block the child; a drain
        # failure is collected explicitly instead of being swallowed.
        try:
            while True:
                if not proc.stderr.read(CORPUS_DECODE_CHUNK):
                    return
        except (OSError, ValueError) as error:
            stream_errors.append(_corpus_collection_error("stderr", error))

    def stderr_failure() -> Optional[str]:
        # The stdout collector polls this at every chunk boundary so a stderr
        # failure disables transformation at the failure boundary and stays the
        # first named error of the execution (S3/S5). Appending to and reading
        # this list are atomic, so the parallel drain needs no further lock.
        return stream_errors[0] if stream_errors else None

    stderr_thread = threading.Thread(target=drain_stderr, daemon=True)
    stderr_thread.start()
    log_file = None
    log_open_error: Optional[BaseException] = None
    log_close_error: Optional[BaseException] = None
    evidence = CorpusStreamEvidence()
    drain_state = {"exhausted": False}
    try:
        try:
            log_file = open(log_path, "wb")
        except OSError as error:
            # The observation still runs and drains on the discard sink, but the
            # failure is handed to the collector so the committed prefix is
            # frozen at the failure boundary and this first named error survives
            # a later read failure (S3/S5).
            log_file = _CorpusDiscardSink(error)
            log_open_error = error

        def decoded_chunks():
            for chunk in _iter_decoded_chunks(proc.stdout):
                yield chunk
            drain_state["exhausted"] = True

        try:
            evidence = consume_corpus_stdout(
                decoded_chunks(), case, explicit_components, log_file,
                external_failure=stderr_failure)
        except FilterConfigurationError as error:
            evidence = CorpusStreamEvidence(program_output_error=str(error))
        except Exception as error:  # noqa: BLE001 - the read is drained by the caller
            evidence = CorpusStreamEvidence(
                program_output_error=_corpus_collection_error("read", error))
    finally:
        if log_file is not None:
            try:
                log_file.close()
            except Exception as error:  # noqa: BLE001 - reported below if first
                log_close_error = error
    if not drain_state["exhausted"]:
        # The stdout drain could not reach a clean EOF, so the child may still
        # block on a full pipe: terminate it (the timeout path uses the same
        # convention) before waiting, without claiming a timeout.
        try:
            proc.kill()
        except OSError:
            pass
    returncode = proc.wait()
    timer.cancel()
    stderr_thread.join(timeout=5.0)
    for stream in (proc.stdout, proc.stderr):
        try:
            stream.close()
        except (OSError, ValueError):
            pass
    if log_open_error is not None and evidence.program_output_error is None:
        evidence.program_output_error = _corpus_collection_error(
            "retained log", log_open_error)
    if log_close_error is not None and evidence.program_output_error is None:
        # A delayed buffered-write failure on close is a named collection
        # failure like any other retained-log failure, never a silent success.
        evidence.program_output_error = _corpus_collection_error(
            "retained log", log_close_error)
    if stream_errors and evidence.program_output_error is None:
        evidence.program_output_error = stream_errors[0]
    return CorpusClassExecution(
        command=list(command), components=list(components),
        returncode=returncode, timed_out=timeout_state["timed_out"],
        started=True, evidence=evidence, compiler_log_path=log_path,
        duration=time.time() - start)


def run_corpus_class_execution(
    case: TestCase, abc_path: Path, cfg: BuildConfig, args: argparse.Namespace,
    context: CorpusContext, budget: int, case_artifacts: Path, log: List[str],
    prepared_command: Optional[Tuple[List[str], List[str]]] = None,
) -> CorpusClassExecution:
    """Run one corpus case with the class streaming executor (C4).

    The class command and its environment are built once here; the execution
    itself is the shared streaming entry point, which the interpreter twins
    reuse with the derived command. The filtered program output is kept under
    the class limit and the component lines without the execution markers go to
    a temporary log that is deleted after the evidence pass (or kept where it
    is the evidence of a declared constraint, C4). `prepared_command` is the
    `(command, components)` the run-level pre-flight already built and
    validated from this case's actual inputs; it is reused verbatim so the
    validated command is the executed one.
    """
    # The streaming filter chain is built from the ordered *explicit* names; the
    # command's observation list also carries the annotation-implied components
    # and must not be used as the chain input (S1). The canonical chain is
    # constructed *first*, before any corpus command predicate reads the case
    # annotations: an unsupported construction (a missing evidence collection, a
    # non-iterable or ill-typed component list) is a named configuration failure
    # of this case, reported consistently here instead of escaping as a raw
    # TypeError from a predicate (S1/S4).
    explicit_components = corpus_explicit_components(args)
    try:
        case_filter_chain(case, explicit_components)
    except FilterConfigurationError as error:
        return CorpusClassExecution(
            components=explicit_components, started=False,
            start_error=str(error),
            evidence=CorpusStreamEvidence(program_output_error=str(error)),
            duration=0.0)
    if prepared_command is not None:
        cmd = list(prepared_command[0])
        components = list(prepared_command[1])
    else:
        cmd, components = build_corpus_vm_command(
            case, abc_path, cfg, args, context.module)
    # The command that is about to be launched must carry exactly one
    # `--compiler-enable-jit` token (TW1). The run-level pre-flight in `main`
    # turns the same condition into usage code 2 before the case loop; this
    # guard on the constructed actual command keeps an invalid experiment from
    # running when the entry point is reached directly, and refuses to launch
    # rather than reporting a green run for a command no twin can be derived
    # from.
    command_error = corpus_twin_command_usage_error(cmd)
    if command_error is not None:
        return CorpusClassExecution(
            command=list(cmd), components=list(components), started=False,
            command_error=command_error,
            evidence=CorpusStreamEvidence(program_output_error=command_error),
            duration=0.0)
    if args.verbose:
        log.append(f"class cmd: {' '.join(cmd)}")
    log_path = case_artifacts / f"{corpus_case_stem(case)}{CORPUS_LIVE_LOG_SUFFIX}"
    return run_corpus_stream_execution(
        cmd, components, case, explicit_components, budget,
        corpus_execution_environment(cfg), log_path)


class CorpusTwinCommandError(ValueError):
    """A class command whose JIT token cannot identify an interpreter twin (TW1)."""


def corpus_twin_command_usage_error(class_command: List[str]) -> Optional[str]:
    """Named usage error of a class command that cannot yield a twin (TW1).

    The interpreter twin is the actual class command with the value of its
    single `--compiler-enable-jit` token replaced by `false`. Exactly one such
    token is required: its absence means the class command never enables the
    JIT explicitly (nothing can be turned off), and a duplicate makes the twin's
    JIT state ambiguous. The check reads the option *name*, so the neighbouring
    `--compiler-enable-jit-lazy-deopt` is never mistaken for the JIT switch.
    """
    positions = [index for index, token in enumerate(class_command)
                 if _option_name(token) == CORPUS_JIT_OPTION]
    if len(positions) == 1:
        return None
    if not positions:
        return (f"the class command line has no '{CORPUS_JIT_OPTION}' token; "
                "the interpreter twin is derived from exactly one")
    return (f"the class command line has {len(positions)} "
            f"'{CORPUS_JIT_OPTION}' tokens; exactly one is required to derive "
            "the interpreter twin")


def derive_corpus_twin_command(class_command: List[str]) -> List[str]:
    """Interpreter twin command of an actual class command (TW1).

    Every other token, the abc, the entry point, the observation, the
    descriptor `PARAMS` and the derived class defaults are preserved; only the
    value of the single `--compiler-enable-jit` token becomes `false`. A
    validated class command is required; an unidentifiable JIT token raises
    `CorpusTwinCommandError` instead of silently running something other than
    the twin.
    """
    error = corpus_twin_command_usage_error(class_command)
    if error is not None:
        raise CorpusTwinCommandError(error)
    twin = list(class_command)
    opt_index = next(index for index, token in enumerate(twin)
                     if _option_name(token) == CORPUS_JIT_OPTION)
    token = twin[opt_index]
    if "=" in token:
        twin[opt_index] = f"{CORPUS_JIT_OPTION}={CORPUS_JIT_OFF_VALUE}"
    elif opt_index + 1 < len(twin) and not twin[opt_index + 1].startswith("--"):
        # The space-separated form: the value is the following token.
        twin[opt_index + 1] = CORPUS_JIT_OFF_VALUE
    else:
        # A bare flag means `true`; the only way to turn the JIT off is to name
        # the value explicitly.
        twin[opt_index] = f"{CORPUS_JIT_OPTION}={CORPUS_JIT_OFF_VALUE}"
    return twin


def split_numeric_fields(line: str) -> Tuple[Tuple[str, ...], Tuple[str, ...]]:
    """Numeric spellings of one line and the exact segments between them (TW4).

    The line is scanned with `NUMERIC_FIELD_RE`; the returned segments are the
    exact text before, between and after the spellings, so
    `len(segments) == len(fields) + 1` and the whole line is preserved verbatim.
    No normalisation happens here: `rstrip` and whitespace folding are
    deliberately absent, because the reproducibility evidence must not treat
    two different skeletons as one. Equal segment sequences and an equal field
    count define an aligned skeleton.
    """
    fields: List[str] = []
    segments: List[str] = []
    position = 0
    for match in NUMERIC_FIELD_RE.finditer(line):
        segments.append(line[position:match.start()])
        fields.append(match.group(0))
        position = match.end()
    segments.append(line[position:])
    return tuple(fields), tuple(segments)


@dataclass(frozen=True)
class CorpusMaskPosition:
    """One 1-based volatile numeric-field position of the two twins (TW4)."""

    line: int
    field: int
    twin1: str
    twin2: str

    def label(self) -> str:
        """Human-readable position with both numeric spellings."""
        return (f"line {self.line} field {self.field}: "
                f"{self.twin1!r} != {self.twin2!r}")


@dataclass(frozen=True)
class CorpusReproducibilityMask:
    """Result of the twin-only mask inference (TW4/TW5).

    `positions` is the complete ordered list of the volatile numeric fields; it
    is empty whenever `reason` names an alignment failure, because a failed
    inference never certifies a partial mask. `available` is true exactly for a
    successful inference, including the zero-position one of two identical
    twins.
    """

    positions: Tuple[CorpusMaskPosition, ...] = ()
    reason: Optional[str] = None

    @property
    def available(self) -> bool:
        return self.reason is None

    @property
    def count(self) -> int:
        return len(self.positions)


def derive_corpus_reproducibility_mask(
    twin1_text: str, twin2_text: str,
) -> CorpusReproducibilityMask:
    """Infer the reproducibility mask from the two interpreter twins only (TW4).

    The function takes nothing but the two complete twin outputs: no class
    execution, no descriptor and no case is an input, so neither the class run
    nor the case author can extend the mask over the difference being checked.
    Lines are split with `str.splitlines()`, exactly as the embedded comparison
    does, without `rstrip` or any whitespace normalisation; the spellings and
    their exact separating segments come from `split_numeric_fields`. A mask
    position is a 1-based `(line, numeric field)` pair whose numeric spelling
    differs between the aligned twins.

    A line-count, field-count or segment mismatch is an alignment failure: it
    is a divergence rather than additional masked fields, and the failure keeps
    `positions` empty instead of publishing a partial mask. There is no float
    conversion and no tolerance, so two textually different spellings of the
    same value remain a volatile position. The function is pure and
    deterministic: no I/O, no timeout and no retry, and equal inputs give the
    same result.
    """
    twin1_lines = twin1_text.splitlines()
    twin2_lines = twin2_text.splitlines()
    if len(twin1_lines) != len(twin2_lines):
        return CorpusReproducibilityMask(reason=(
            "twin alignment failed: line counts differ "
            f"({len(twin1_lines)} vs {len(twin2_lines)})"))
    positions: List[CorpusMaskPosition] = []
    for line_number, (line1, line2) in enumerate(
            zip(twin1_lines, twin2_lines), start=1):
        fields1, segments1 = split_numeric_fields(line1)
        fields2, segments2 = split_numeric_fields(line2)
        if len(fields1) != len(fields2):
            return CorpusReproducibilityMask(reason=(
                f"twin alignment failed: line {line_number} has "
                f"{len(fields1)} numeric field(s) in twin 1 and "
                f"{len(fields2)} in twin 2"))
        if segments1 != segments2:
            return CorpusReproducibilityMask(reason=(
                f"twin alignment failed: line {line_number} has a different "
                "non-numeric skeleton"))
        for field_number, (value1, value2) in enumerate(
                zip(fields1, fields2), start=1):
            if value1 != value2:
                positions.append(CorpusMaskPosition(
                    line_number, field_number, value1, value2))
    return CorpusReproducibilityMask(tuple(positions))


@dataclass
class CorpusTwinRun:
    """Outcome of the two interpreter twins of one corpus case (TW1/TW2).

    `status` holds the eligibility status when the twins were not launched
    (`not-comparable` for an absent, unstarted or unusable class output;
    `skipped-empty-program-output` for a complete empty one). It is `None` once
    both attempts were made, because the differential verdict is produced from
    the complete execution facts by the classification change of this epic.
    `executions` holds both attempts in order, including an attempt that failed
    to start, and each one keeps the full `CorpusClassExecution` evidence: a
    narrowing conversion that copies only part of it would drop
    `program_output_truncated`, `record_candidate_capped` or
    `program_output_error` before the classifier.
    """

    status: Optional[str] = None
    reasons: List[str] = field(default_factory=list)
    executions: List[CorpusClassExecution] = field(default_factory=list)

    @property
    def commands(self) -> List[List[str]]:
        """Commands of both attempts, in order, whatever the outcome."""
        return [list(execution.command) for execution in self.executions]


def corpus_twin_eligibility(
    class_execution: Optional[CorpusClassExecution],
) -> Optional[Tuple[str, str]]:
    """`(status, reason)` when the class output admits no twin run (TW2).

    The class execution must have started and its collected program output must
    be complete, usable and nonempty. An unusable output is decided before the
    emptiness question, so a truncated output, an over-limit record candidate
    or a failed collection is never reported as a skip; a complete empty output
    is the only skipped branch. Other red class reasons (the JIT-activity
    invariant, the parity, a descriptor requirement, an ordinary nonzero exit,
    the independent retained-log limit) do not suppress the twins, because they
    do not make the collected output unusable.
    """
    if class_execution is None or not class_execution.started:
        return (CORPUS_DIFFERENTIAL_NOT_COMPARABLE,
                "differential not comparable: the class execution did not "
                "start")
    evidence = class_execution.evidence
    if not corpus_program_output_usable(evidence):
        return (CORPUS_DIFFERENTIAL_NOT_COMPARABLE,
                "differential not comparable: the class program output is not "
                "usable (" + _corpus_program_invalidity_labels(evidence) + ")")
    if evidence.program_output == "":
        return (CORPUS_DIFFERENTIAL_SKIPPED_EMPTY,
                "differential skipped-empty-program-output: the class program "
                "output is complete and empty")
    return None


def _finalize_corpus_twin_logs(run: CorpusTwinRun) -> None:
    """Delete the temporary component logs of both twin executions (C4).

    The twin evidence is the streamed counts, the filtered program output and
    the named invalidity flags; the marker-dominated component log has no
    consumer on the twin side, so it is bounded by its own cap and removed
    after the evidence pass. The recorded path stays a fact of the execution.
    """
    for execution in run.executions:
        if execution.compiler_log_path is None:
            continue
        try:
            execution.compiler_log_path.unlink()
        except OSError:
            pass


def run_corpus_twin_execution(
    case: TestCase, cfg: BuildConfig, args: argparse.Namespace, budget: int,
    class_execution: Optional[CorpusClassExecution], case_artifacts: Path,
    log: List[str],
) -> CorpusTwinRun:
    """Run the two interpreter twins of one corpus case (TW1/TW2).

    Both twins are the actual class command with the value of its single
    `--compiler-enable-jit` token replaced by `false`, run as two fresh
    sequential processes, each with the whole per-case class budget and the
    class command's environment, observation and abc. There are no retries and
    no state or buffer is shared between the processes: a second run of the
    same command could change the reproducibility evidence. A failure of the
    first twin does not suppress the second, so both commands and both outcomes
    stay observable exactly where a reviewer needs them.
    """
    run = CorpusTwinRun()
    ineligibility = corpus_twin_eligibility(class_execution)
    if ineligibility is not None:
        run.status, reason = ineligibility
        run.reasons.append(reason)
        return run
    assert class_execution is not None
    error = corpus_twin_command_usage_error(class_execution.command)
    if error is not None:
        # The run-level check rejects such a class command before any execution;
        # this defensive branch keeps the twins from being derived from an
        # ambiguous command when the entry point is reached directly.
        run.status = CORPUS_DIFFERENTIAL_NOT_COMPARABLE
        run.reasons.append(f"differential not comparable: {error}")
        return run
    twin_command = derive_corpus_twin_command(class_execution.command)
    explicit_components = corpus_explicit_components(args)
    env = corpus_execution_environment(cfg)
    stem = corpus_case_stem(case)
    for attempt, suffix in enumerate(CORPUS_TWIN_LOG_SUFFIXES):
        log_path = case_artifacts / f"{stem}{suffix}"
        if args.verbose:
            log.append(f"twin{attempt + 1} cmd: {' '.join(twin_command)}")
        run.executions.append(run_corpus_stream_execution(
            twin_command, class_execution.components, case,
            explicit_components, budget, env, log_path))
    _finalize_corpus_twin_logs(run)
    return run


def _evidence_constraint_satisfied(constraint: EvidenceConstraint,
                                   actual: int) -> bool:
    """The verdict of `EvidenceConstraint.violated`, from a streamed count."""
    if isinstance(constraint, (CompileCountGeConstraint, ExecCountGeConstraint,
                               DeoptCountGeConstraint, GcCountGeConstraint)):
        return actual >= constraint.count
    return actual == constraint.count


def corpus_evidence_actual(constraint: EvidenceConstraint,
                           evidence: CorpusStreamEvidence) -> int:
    """Actual count of one evidence requirement over the streamed evidence.

    The wildcard of the injected invariant is answered by the totals of the
    streamed pass; a descriptor pattern that names methods is counted in the
    same pass, so no marker list is ever captured.
    """
    if constraint.pattern == CORPUS_INVARIANT_PATTERN:
        return {
            "compile": evidence.compile_markers,
            "exec": evidence.exec_markers,
            "deopt": evidence.deopt_markers,
            "gc": evidence.gc_markers,
        }[constraint.EVIDENCE_KEY]
    return evidence.named_counts.get(constraint, 0)


def corpus_log_constraint_reasons(
    case: TestCase, execution: CorpusClassExecution,
    valid_opcodes: Optional[set],
) -> List[str]:
    """Evaluate the descriptor constraints that need the retained log (C4).

    A method-scoped (IR/codegen) constraint is evaluated against the component
    lines without the execution markers. If that retained log exceeded the
    class limit, the evidence is unavailable and the case is red with the
    named reason: a truncated log is never a passed check.

    A collection failure (read, filter, observation or retained-log I/O)
    leaves the retained component log incomplete as well. An incomplete graph
    is not a complete one: `iter_graph_dumps` treats EOF as the end of the
    open dump, so the missing remainder of a compilation would be read as
    proof that an opcode is absent and turn unavailable IR evidence into a
    false constraint result. Any `program_output_error` therefore makes the
    constraint evidence unavailable with a named reason *before* parsing.
    Only the independent program-output and candidate caps leave a complete
    retained log usable: they do not interrupt observation, so they are not
    treated here (S3/S5).
    """
    if not (case.constraints or case.codegen_constraints):
        return []
    if execution.evidence.compiler_log_capped:
        return ["constraint evidence unavailable: compiler log exceeds cap"]
    if execution.evidence.program_output_error is not None:
        return ["constraint evidence unavailable: "
                + execution.evidence.program_output_error]
    log_path = execution.compiler_log_path
    if log_path is None or not Path(log_path).is_file():
        return ["constraint evidence unavailable: compiler log is missing"]
    try:
        log_text = Path(log_path).read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        return [f"constraint evidence unavailable: {e}"]
    reasons: List[str] = []
    reasons.extend(check_constraints(
        case.constraints, collect_opcode_counts(log_text, valid_opcodes)))
    reasons.extend(check_codegen_constraints(
        case.codegen_constraints, collect_codegen_comments(log_text)))
    return reasons


def corpus_bytecode_constraint_reasons(
    case: TestCase, abc_path: Path, cfg: BuildConfig, case_artifacts: Path,
    stem: str, log: List[str],
) -> List[str]:
    """Evaluate the bytecode constraints of a corpus descriptor (C4, C5).

    `BYTECODE_COUNT` is part of the descriptor grammar, so it is evaluated
    against the disassembly of the class abc; an unavailable disassembly is a
    named red reason, never a silently skipped requirement.
    """
    if not case.bytecode_constraints:
        return []
    disasm_path = case_artifacts / f"{stem}.disasm.txt"
    disasm_error = disassemble_case(case, abc_path, disasm_path, cfg, log)
    if disasm_error is not None:
        return [f"bytecode constraints unavailable: {disasm_error}"]
    return check_bytecode_constraints(disasm_path, case.bytecode_constraints)


@dataclass
class CorpusClassVerdict:
    """Base verdict, invariant and parity of one class execution (C3, C4)."""

    base_verdict: str = CORPUS_VERDICT_FAIL
    reasons: List[str] = field(default_factory=list)
    invariant_state: str = CORPUS_INVARIANT_NONE
    parity: str = CORPUS_PARITY_NOT_APPLICABLE
    log: List[str] = field(default_factory=list)

    @property
    def passed(self) -> bool:
        return not self.reasons


def evaluate_corpus_class_run(
    case: TestCase, module: Any, *,
    compile_ok: bool, compile_message: str, compile_timed_out: bool,
    execution: Optional[CorpusClassExecution], reference_verdict: str,
    valid_opcodes: Optional[set],
) -> CorpusClassVerdict:
    """Verdict of one class execution before the differential twin exists.

    The base verdict is the module's own rule over the class execution inputs
    (compilation status, return code, crash, timeout); the class-level
    invariant and every descriptor requirement are evaluated over the streamed
    counts; the parity compared with the reference verdict is the parity of the
    base verdict alone.
    """
    verdict = CorpusClassVerdict()
    log = verdict.log
    if not compile_ok:
        verdict.base_verdict, _ = classify_corpus_base_verdict(
            module, compile_ok=False, compile_timed_out=compile_timed_out,
            returncode=None, timed_out=False)
        verdict.reasons.append(f"compilation failed: {compile_message}")
    else:
        assert execution is not None
        evidence = execution.evidence
        base_verdict, base_reason = classify_corpus_base_verdict(
            module, compile_ok=True, compile_timed_out=False,
            returncode=execution.returncode, timed_out=execution.timed_out)
        verdict.base_verdict = base_verdict
        if execution.returncode is None and execution.start_error is not None:
            verdict.reasons.append(
                f"ark_js_vm could not be executed: {execution.start_error}")
        elif base_reason is not None:
            verdict.reasons.append(base_reason)
        log.append(f"markers: compile={evidence.compile_markers} "
                   f"exec={evidence.exec_markers}")
        verdict.invariant_state = corpus_invariant_state(
            case, evidence.compile_markers, evidence.exec_markers)
        log.append(f"invariant: {verdict.invariant_state}")
        # Every output invalidity is reported as its own named red reason
        # *before* any emptiness/comparison decision, even when the retained
        # prefix is empty and the class invariant is satisfied (S3/S5). The
        # base verdict parity below is independent of this: a `matched` parity
        # denotes base-verdict parity only and never certifies output validity.
        if evidence.program_output_truncated:
            verdict.reasons.append("program output exceeds cap")
        if evidence.record_candidate_capped:
            verdict.reasons.append("record candidate exceeds cap")
        if evidence.program_output_error is not None:
            verdict.reasons.append(evidence.program_output_error)
        for constraint in case.evidence_constraints:
            actual = corpus_evidence_actual(constraint, evidence)
            if not _evidence_constraint_satisfied(constraint, actual):
                verdict.reasons.append(
                    f'"{constraint.label()}" violated, found {actual}')
        if case.corpus_no_exec_marker_reason and evidence.exec_markers > 0:
            verdict.reasons.append(
                f"NO_EXEC_MARKER declared, but {evidence.exec_markers} "
                f"execution marker(s) observed")
        verdict.reasons.extend(
            corpus_log_constraint_reasons(case, execution, valid_opcodes))
    verdict.parity = classify_parity(verdict.base_verdict, reference_verdict)
    log.append(f"parity: {verdict.parity}")
    log.append(f"base verdict: {verdict.base_verdict}")
    return verdict


def _write_corpus_artifact(path: Path, text: str) -> Optional[str]:
    """Write one required class artifact; a write failure is a named reason (S5).

    The bounded program prefix and the reproducing command are the auditable
    evidence of a case, so an unwritable artifact is reported at the case
    boundary and reaches the verdict, summary and console; it is never silently
    discarded while the case advertises a delivered result.
    """
    try:
        path.write_text(text, encoding="utf-8")
    except OSError as error:
        return f"cannot write case artifact {path.name}: {error}"
    return None


def _corpus_program_invalidity_labels(evidence: CorpusStreamEvidence) -> str:
    """Named labels of every output invalidity of one evidence object (S5)."""
    labels: List[str] = []
    if evidence.program_output_truncated:
        labels.append("program output exceeds cap")
    if evidence.record_candidate_capped:
        labels.append("record candidate exceeds cap")
    if evidence.program_output_error is not None:
        labels.append(evidence.program_output_error)
    return ", ".join(labels) if labels else "none"


def _corpus_program_artifact(evidence: CorpusStreamEvidence) -> str:
    """Bounded program artifact of one class execution (S3/S5).

    A usable output is written as-is. An unusable one is the bounded retained
    diagnostic prefix, explicitly labelled incomplete and naming the flags or
    the collection error that make it unusable.
    """
    if corpus_program_output_usable(evidence):
        return evidence.program_output
    return ("# incomplete program output prefix (diagnostic only): "
            + _corpus_program_invalidity_labels(evidence) + "\n"
            + evidence.program_output)


def _finalize_corpus_live_log(case: TestCase, execution: Optional[CorpusClassExecution],
                              passed: bool, verbose: bool) -> None:
    """Keep the retained component log only where it is declared evidence (C4).

    The temporary log lives only until the evidence pass: it is kept when a
    descriptor declares a method-scoped constraint (its evidence) or when the
    case is red and `-v` asks for it, and deleted otherwise.
    """
    if execution is None or execution.compiler_log_path is None:
        return
    needed = bool(case.constraints or case.codegen_constraints)
    if needed or (not passed and verbose):
        return
    try:
        execution.compiler_log_path.unlink()
    except OSError:
        pass
    execution.compiler_log_path = None


def run_corpus_case(
    case: TestCase, cfg: BuildConfig, args: argparse.Namespace,
    artifacts_root: Path, context: CorpusContext,
) -> CaseResult:
    """Verdict of a selected corpus case (C3, C4, C7).

    The reference execution of the module comes first and is mandatory (C7):
    it is performed for every selected case, including the cases the class
    already considers red, so the parity is observed rather than derived. The
    only exception is a case whose descriptor gave a config error (C5): such a
    case has no executions at all, is red by the descriptor itself and its
    parity field is `not-applicable`, never a silent skip. The case is then
    compiled with the module's own functions and run by the class streaming
    executor, and its verdict is the base verdict, the JIT-activity invariant,
    the descriptor requirements and the parity of the base verdict with the
    module's own verdict. The differential twin is delivered by the following
    change of the same epic.
    """
    if case.config_errors:
        return CaseResult(
            case.rel, reasons=list(case.config_errors),
            log=[f"parity: {CORPUS_PARITY_NOT_APPLICABLE}"])
    # The canonical filter chain is validated at the case boundary before the
    # mandatory reference execution and before any corpus command predicate
    # reads the case annotations (S1/S4): an unsupported construction (a missing
    # evidence collection, a non-iterable or ill-typed component list) is a red
    # case configuration result, never an escaped raw exception.
    try:
        case_filter_chain(case, corpus_explicit_components(args))
    except FilterConfigurationError as error:
        return CaseResult(
            case.rel, reasons=[str(error)],
            log=[f"parity: {CORPUS_PARITY_NOT_APPLICABLE}"])
    module = context.module
    start = time.time()
    log: List[str] = []

    reference = run_reference_case(module, case, context.build_config,
                                   context.reference_args, artifacts_root)
    log.append(f"reference verdict: {reference.verdict}")
    if reference.reason:
        log.append(f"reference reason: {reference.reason}")
    if reference.command:
        log.append(f"reference command: {reference.command}")

    result = CaseResult(case.rel)
    result.corpus_reference = reference
    case_artifacts = artifacts_root / case.rel
    try:
        case_artifacts.mkdir(parents=True, exist_ok=True)
    except OSError as e:
        result.reasons.append(f"cannot create case artifacts: {e}")
        result.duration = time.time() - start
        result.log = log
        return result
    stem = corpus_case_stem(case)
    abc_path = case_artifacts / f"{stem}.abc"

    compile_ok, compile_message, compile_timed_out, _ = compile_corpus_case(
        module, case, abc_path, cfg, args.verbose, log)

    budget, budget_reason = corpus_class_budget(module, case, cfg)
    if budget_reason:
        log.append(f"class budget: {budget}s "
                   f"(descriptor TIMEOUT: {budget_reason})")
    else:
        log.append(f"class budget: {budget}s")

    execution: Optional[CorpusClassExecution] = None
    class_command: Optional[str] = None
    if compile_ok:
        execution = run_corpus_class_execution(
            case, abc_path, cfg, args, context, budget, case_artifacts, log,
            prepared_command=context.class_commands.get(case.rel))
        if execution.command_error is not None:
            # The actual class command of this case cannot identify an
            # interpreter twin; the command was never launched and the run is
            # a usage error (`main` exits 2), never a red-but-green experiment.
            result.usage_error = execution.command_error
        class_command = build_repro_command(execution.command, cfg)
        for artifact_suffix, artifact_text in (
                (CORPUS_CLASS_COMMAND_SUFFIX,
                 "\n".join(execution.command) + "\n"),
                # The program artifact is the bounded retained prefix. When the
                # output is not usable it is labelled as an incomplete
                # diagnostic prefix that names the flags/error: a truncated
                # prefix is never advertised as the complete program output
                # (S3/S5).
                (CORPUS_PROGRAM_OUTPUT_SUFFIX,
                 _corpus_program_artifact(execution.evidence))):
            artifact_error = _write_corpus_artifact(
                case_artifacts / f"{stem}{artifact_suffix}", artifact_text)
            if artifact_error is not None:
                # A required artifact that cannot be recorded is a named red
                # reason of this case; every other invalidity reason below is
                # preserved unchanged.
                result.reasons.append(artifact_error)
        if not corpus_program_output_usable(execution.evidence):
            log.append(
                "program output not usable: retained prefix is incomplete "
                "(diagnostic only): "
                + _corpus_program_invalidity_labels(execution.evidence))
    result.corpus_execution = execution
    # The interpreter twins are executed from the collected class output (TW2):
    # the eligibility gate decides before a process exists, the twins reuse the
    # actual class command, its environment, its observations and the per-case
    # class budget, and both attempts and their full execution facts are kept
    # on the result for the differential classification (a later change of this
    # epic). This call is reached only for a case tagged with the corpus class,
    # so an embedded case never runs a twin and never gains the field.
    result.corpus_twins = run_corpus_twin_execution(
        case, cfg, args, budget, execution, case_artifacts, log)

    verdict = evaluate_corpus_class_run(
        case, module, compile_ok=compile_ok, compile_message=compile_message,
        compile_timed_out=compile_timed_out, execution=execution,
        reference_verdict=reference.verdict, valid_opcodes=context.valid_opcodes)
    log.extend(verdict.log)
    result.reasons.extend(verdict.reasons)
    if compile_ok:
        result.reasons.extend(corpus_bytecode_constraint_reasons(
            case, abc_path, cfg, case_artifacts, stem, log))
    if verdict.parity == CORPUS_PARITY_DIVERGED:
        result.reasons.extend(parity_reasons(
            verdict.base_verdict, class_command, reference.verdict,
            reference.command))
    result.duration = time.time() - start
    result.log = log
    _finalize_corpus_live_log(case, execution, result.passed, args.verbose)
    return result


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
        "--corpus", action="store_true", default=False,
        help="Select the external corpus case class instead of the embedded one. "
             "The corpus root is taken from the ARKSTEED_EXTERNAL_TESTS "
             "environment variable and must hold run_arksteed_external_tests.py "
             f"(recommended value: {CORPUS_RECOMMENDED_ROOT}). Case identifiers "
             f"are {CORPUS_NAMESPACE}/<suite>/<case>, and -I/-X patterns are "
             "matched against those identifiers. The optional per-case "
             "descriptor test/corpus/<suite>/<case>.annotations adds the "
             "class-specific TIMEOUT <seconds> <justification> and "
             "NO_EXEC_MARKER <justification> keys to the //! grammar. Every "
             "case is compiled with the corpus runner module's own functions "
             "and executed by a streaming class executor that requires a JIT "
             "compilation marker and (unless the descriptor exempts it) a "
             "JIT execution marker. Off by "
             "default; the corpus class is not part of the default set, and a "
             "run without the flag does not import the corpus module.")
    parser.add_argument(
        "--sensitivity-controls", action="store_true", default=False,
        help="Control mode: turn every selected case into a mandatory set of "
             "derived, knowingly wrong variants (classes JIT-off, expectation "
             "substitution, count perturbation, address substitution), run the "
             "base case plus one variant per instance under the run artifacts "
             "and report the failure class, the verdict and the reason per case "
             "and per class (controls.md/controls.json plus the summary "
             "section). The class-1 variant keeps its own VM command line: the "
             "runner substitutes --compiler-enable-jit=false for it instead of "
             "the case naming the option through //! PARAMS, and its recorded "
             "observation is the variant's own stdout. Off by default; not part "
             "of the default set and not hooked into the project verifier.")
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
    # fragments are the ordered explicit component names of the shared filter
    # chain (`case_filter_chain`), which cuts them out of the raw stdout before
    # comparison. The GC/deopt record stages and the implied components are
    # added by that single constructor, so the built-in and the corpus paths
    # cannot drift apart.

    # Resolve lazily so an explicit -p works even on an unsupported machine.
    if args.platform is None:
        args.platform = detect_host_platform()
    cfg = BuildConfig(args.mode, args.platform)

    # The corpus class is resolved before the build stage and before the tool
    # checks (C1/C2): the flag combination, the namespace of the filter
    # patterns, the root from the environment, the lazy read-only module import
    # and its mandatory API. Every failure is a usage error that names the
    # variable, element or reason, and the build stage is not run.
    corpus_module: Optional[Any] = None
    corpus_root: Optional[Path] = None
    if args.corpus:
        usage_error = corpus_selection_usage_error(args)
        if usage_error is not None:
            print(f"Error: {usage_error}", file=sys.stderr)
            return 2
        corpus_root, root_error = resolve_corpus_root()
        if root_error is not None:
            print(f"Error: {root_error}", file=sys.stderr)
            return 2
        corpus_module, module_error = load_corpus_module(corpus_root)
        if module_error is not None:
            print(f"Error: {module_error}", file=sys.stderr)
            return 2
    else:
        usage_error = corpus_filter_usage_error(args.include, args.exclude, False)
        if usage_error is not None:
            print(f"Error: {usage_error}", file=sys.stderr)
            return 2

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

    if args.corpus:
        # The corpus class resolves its own tools through its own BuildConfig;
        # the two configurations must name the same existing tools. This check
        # belongs after the runner's build stage and before the case loop.
        tools_error = corpus_tools_error(corpus_module, cfg)
        if tools_error is not None:
            print(f"Error: {tools_error}", file=sys.stderr)
            return 2

    corpus_context: Optional[CorpusContext] = None
    if args.corpus:
        opcode_order = None
        valid_opcodes = load_opcode_names()
        # The class defaults are derived from the loaded module before the
        # case loop (C5): they are the behavioural option set of the class
        # command line and they are cross-checked against the reference
        # namespace below. A discrepancy is a usage error, never a silent
        # divergence of verdicts.
        class_defaults = corpus_class_defaults(corpus_module)
        cases = discover_corpus_cases(
            corpus_module, corpus_root, args.include, args.exclude, args.verbose)
        # The class state is built once, after the tools check and after the
        # single discovery call: the module's own build configuration and its
        # own run namespace, together with the root and the revision the
        # report fixes. The reference execution of every case reuses them.
        corpus_context = CorpusContext(
            module=corpus_module,
            root=corpus_root,
            revision=corpus_revision(corpus_root),
            build_config=build_reference_config(corpus_module, cfg),
            reference_args=build_reference_namespace(corpus_module, cfg),
            class_defaults=class_defaults,
            valid_opcodes=valid_opcodes,
        )
        defaults_error = corpus_defaults_usage_error(
            corpus_module, corpus_context.reference_args, class_defaults)
        if defaults_error is not None:
            print(f"Error: {defaults_error}", file=sys.stderr)
            return 2
        # The descriptor is the only annotation channel of a corpus case: the
        # read-only corpus source is never scanned for `//!` lines. Descriptor
        # problems are config errors that make the case red, and the injected
        # class-level JIT-activity requirements are added to every case before
        # the loop (C3): a descriptor may add requirements, never remove them.
        for case in cases:
            parse_corpus_descriptor(case, valid_opcodes, class_defaults)
            inject_corpus_invariant(case)
        # The interpreter twin is derived from the actual class command, whose
        # `--compiler-enable-jit` token can only come from the derived class
        # defaults (the observation carries no such option and the descriptor
        # cannot set a class-managed one, C5). This cheap guard on the derived
        # defaults runs before the case loop; the actual per-case command is
        # additionally constructed and validated after the run artifacts root
        # exists, which is the authoritative pre-flight (TW1).
        twin_error = corpus_twin_command_usage_error(
            corpus_class_command_options(corpus_module))
        if twin_error is not None:
            print(f"Error: {twin_error}", file=sys.stderr)
            return 2
    else:
        if args.sensitivity_controls:
            # The address-substitution class needs the header's own declaration
            # order as the candidate order; the header is read once.
            opcode_order = load_opcode_order()
            valid_opcodes = None if opcode_order is None else set(opcode_order)
        else:
            opcode_order = None
            valid_opcodes = load_opcode_names()
        cases = discover_cases(args.include, args.exclude, args.verbose)
        for case in cases:
            parse_annotations(case, valid_opcodes)
        if args.sensitivity_controls:
            usage_error = control_usage_error(cases, opcode_order)
            if usage_error is not None:
                print(f"Error: {usage_error}", file=sys.stderr)
                return 2
    if not cases:
        class_suffix = (f" in the {CASE_CLASS_CORPUS} case class (--corpus)"
                        if args.corpus else "")
        print(f"No test case selected{class_suffix}", file=sys.stderr)
        return 2
    if args.corpus:
        print(f"Collected {len(cases)} {CASE_CLASS_CORPUS} test case(s) for "
              f"{cfg.platform}.{cfg.mode}")
        print(f"  corpus root: {corpus_context.root}")
        print(f"  corpus revision: {corpus_context.revision or 'unknown'}\n")
    else:
        print(f"Collected {len(cases)} test case(s) for {cfg.platform}.{cfg.mode}\n")

    artifacts_root = make_artifacts_root(cfg.platform, args.mode)
    if args.corpus:
        # Authoritative pre-flight of the interpreter twins (TW1): the actual
        # class command of every *executable* selected case is constructed
        # from the same per-case inputs the case loop uses, and a command whose
        # single `--compiler-enable-jit` token is absent or duplicated is
        # rejected with a named usage error before any class or twin process
        # exists. The validated command is kept on the run context and reused
        # verbatim by the case loop, so the validated command is the executed
        # one. A case whose command cannot be constructed at all (an
        # unsupported evidence collection) is left to the ordinary per-case
        # configuration path, which names it as a red case; the same guard
        # inside the class executor refuses to launch such a command in any
        # case.
        for case in cases:
            if case.config_errors:
                # A descriptor configuration error makes the case red with no
                # executions at all (C5), and its diagnostic is the required
                # per-case report. Such a case never runs a class or twin
                # process, so it is not part of the twin pre-flight: replacing
                # its diagnostic with a run-level usage error would abort the
                # whole run before `run_corpus_case` reports it and before the
                # other selected cases execute.
                continue
            abc_path = artifacts_root / case.rel / f"{corpus_case_stem(case)}.abc"
            try:
                command, components = build_corpus_vm_command(
                    case, abc_path, cfg, args, corpus_module)
            except Exception:  # noqa: BLE001 - the per-case path names it
                continue
            twin_error = corpus_twin_command_usage_error(command)
            if twin_error is not None:
                print(f"Error: {twin_error}", file=sys.stderr)
                return 2
            corpus_context.class_commands[case.rel] = (command, components)
    write_run_meta(artifacts_root / "run.meta.json", args, cfg,
                   corpus_run_meta(corpus_context, len(cases)))

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

    def run_one(one_case
                ) -> Tuple[CaseResult, Optional[CaseControlResult], float]:
        if one_case.case_class == CASE_CLASS_CORPUS:
            # Class dispatch by the case tag: an embedded case keeps the
            # existing path, a corpus case goes through its own class branch.
            return run_corpus_case(one_case, cfg, args, artifacts_root,
                                   corpus_context), None, 0.0
        result = run_case(
            one_case, cfg, args, artifacts_root, components, valid_opcodes)
        if not args.sensitivity_controls:
            return result, None, 0.0
        case_control, duration = run_case_controls(
            one_case, result, cfg, args, artifacts_root, components,
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
            futures = {pool.submit(run_one, submitted): submitted for submitted in cases}
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
    usage_errors = [case_result.usage_error for case_result in results
                    if case_result.usage_error]
    if usage_errors:
        # The actual class command of a case cannot identify an interpreter
        # twin (TW1). The pre-flight rejects such a run before the case loop;
        # this fallback keeps a run that reached a case anyway a named usage
        # error instead of a verdict. No such command was ever launched.
        print(f"Error: {usage_errors[0]}", file=sys.stderr)
        return 2
    elapsed = sum(r.duration for r in results)
    wall = time.time() - wall_start
    if args.sensitivity_controls:
        # Report in selection order, so the artifacts do not depend on the
        # completion order of parallel workers; a case left uncontrolled by
        # --stops-on-error keeps the explicit `not-run` verdict (S7).
        ordered_controls = [control_results.get(case_key.rel) or not_run_case_control(case_key)
                            for case_key in cases]
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
