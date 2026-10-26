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

"""Self-test of the ArkSteed runner's evidence annotations and controls.

The integrated runner is driven by JIT test cases, which cannot express the
failure modes of the runner itself:

  * the deopt evidence set must contain deopt-event lines only, so compilation
    markers, execution markers, CFG dumps and method names containing
    `Deoptimize` are never counted as deopt (false positive), while real eager
    and lazy events are counted once each (false negative);
  * a case asserting `DEOPT_COUNT`/`DEOPT_COUNT_GE` must not be able to
    disable the deopt traces through `//! PARAMS`; the conflicting `PARAMS`
    must be rejected as a configuration error;
  * the `--sensitivity-controls` mode's VM-free core must build knowingly wrong
    variants (class model, count-perturbation and address-substitution values,
    variant isolation, failure classification, verdict and report) exactly as
    the design fixes it, and a run without the mode must not gain an option;
  * the external corpus class (design `arksteed-corpus-case-class-and-parity`,
    C1/C2) must resolve its root from `ARKSTEED_EXTERNAL_TESTS` only, import the
    corpus runner module lazily without writing to the corpus tree, map the
    `-I`/`-X` patterns onto the `corpus/<suite>/<case>` namespace with usage
    errors for every unresolvable case, check the tools of both build
    configurations after the build stage and before the case loop, and leave a
    run without `--corpus` byte-for-byte unchanged;
  * the reference verdict of that class (design C7) must be produced by the
    corpus runner module itself: its own `parse_args` namespace with the fields
    its `main` derives, the runner's platform and mode injected after parsing
    (`fastverify` reachable), its own `BuildConfig` with `ark_root`, its own
    defaults and its own budget; the class base verdict is classified by the
    module's own rule (its `is_crash`/`crash_signal_name`, the branch order of
    its `run_case`) and parity is `matched`/`diverged`/`not-applicable`, with
    both reproducing commands reported on a divergence;
  * the class defaults and the per-case descriptor of that class (design C5)
    must be derived from the loaded module (its `BASE_ARGS`, its `ALWAYS_ARGS`
    and its own CLI defaults), cross-checked against the reference namespace
    with a usage error naming any non-runner-managed difference, and the
    `test/corpus/<suite>/<case>.annotations` descriptor must reuse the `//!`
    grammar plus `TIMEOUT <seconds> <justification>` and
    `NO_EXEC_MARKER <justification>`, rejecting every malformed form as a
    config error of a red case and never adding a case to the embedded set;
  * the class execution and the JIT-activity invariant of that class (design
    C3/C4/C6): the streamed evidence pass counts the compilation and execution
    markers while filtering the component lines, retains the non-marker log
    and bounds the program output (an overrun is a named red reason, never a
    skip), the injected `COMPILE_COUNT_GE * 1`/`EXEC_COUNT_GE * 1`
    requirements are class-managed and the justified `NO_EXEC_MARKER`
    exemption only lowers the execution half and is contradiction-checked, the
    class budget is the module's own with the cross-build multiplier and the
    descriptor override, and the class verdict assembled from those inputs is
    green only when every one of them holds;
  * the build tree of a recorded selective command must be selectable by
    `ARKSTEED_ARK_ROOT` without changing the historical default, so a
    `workdirs/<WD>` checkout can execute its recorded command verbatim.

This script checks those mechanics directly, with no VM and no build required:

    python3 ./runner_self_test.py

Exit status is 0 iff every check passes.
"""

import argparse
import collections
import contextlib
import dataclasses
import hashlib
import importlib.util
import inspect
import io
import json
import os
import random
import re
import sys
import time
import tracemalloc
import subprocess as real_subprocess
import tempfile
import threading
import types
from fnmatch import fnmatchcase
from pathlib import Path
from typing import Callable, Dict, List, Optional

# Never leave bytecode caches in the source tree (this script imports the
# runner from the test directory).
sys.dont_write_bytecode = True

SCRIPT_DIR = Path(__file__).resolve().parent
RUNNER_PATH = SCRIPT_DIR / "run_arksteed_tests.py"

# Temporary case directories; keeping the objects alive cleans them up on exit.
_TEMP_DIRS: List[tempfile.TemporaryDirectory] = []

CHECK_RESULTS = Callable[[List[str]], List[str]]


def load_runner():
    spec = importlib.util.spec_from_file_location("arksteed_runner_under_test", RUNNER_PATH)
    if spec is None or spec.loader is None:
        raise SystemExit(f"cannot load runner: {RUNNER_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


COMPILER = "[compiler] "
GC = "[gc] "

# Line forms that mention deopt machinery without being deopt events.
NON_EVENT_LINES = [
    COMPILER + "======== ArkSteedCompilerTask: Starts compiling: DeoptimizeTarget ========",
    COMPILER + "======== ArkSteedCompilerTask: Finished compiling: DeoptimizeTarget ========",
    COMPILER + ">>> Running method: deopt_count_compile_name.#*#DeoptimizeTarget",
    COMPILER + "Constants:",
    COMPILER + "  v14:  DeoptIfNotBoolean  (v9)  bpc = 2",
    COMPILER + "CodeGen: Visiting v1705: DeoptIfHClassNotInVertex",
    GC + "ArkSteed trigger full gc",
]

GENERIC_EAGER_LINE = COMPILER + "Deoptimize    at target (case.js:24:1)"
LAZY_LINE = COMPILER + "Lazy Deoptimize    at func_main_0 (case.js:31:1)"
ARKSTEED_EAGER_LINE = (
    COMPILER + "Eager deopt disables GC; skip GC-capable JS stack trace construction."
)
# The resume bytecode instruction an eager event logs after its message; it is
# part of the event but carries no deopt identity and must not be a record.
RESUME_INSTRUCTION_LINE = COMPILER + "tonumber 0"


def _deopt_count(module, pattern: str, count: int):
    return module.DeoptCountConstraint(pattern, count)


def _violation(module, pattern: str, count: int, stdout: str):
    evidence = module.collect_evidence(stdout)
    return _deopt_count(module, pattern, count).violated(evidence)


def check_deopt_events_only(module) -> List[str]:
    """Defect 1: ordinary compiler lines are never deopt evidence."""
    failures: List[str] = []
    stdout = "".join(line + "\n" for line in NON_EVENT_LINES) + "42\n"
    evidence = module.collect_evidence(stdout)
    if evidence["deopt"]:
        failures.append(
            f"non-event compiler lines collected as deopt evidence: {evidence['deopt']}")
    for pattern in ("*Deoptimize*", "Deoptimize*", "*deopt*", "*"):
        message = _violation(module, pattern, 0, stdout)
        if message is not None:
            failures.append(f"'{pattern} 0' should hold on a deopt-free stdout: {message}")
    return failures


def check_real_deopt_events_counted_once(module) -> List[str]:
    """Defect 1: eager (both paths) and lazy events are counted once each."""
    failures: List[str] = []
    stdout = "".join(
        line + "\n" for line in
        NON_EVENT_LINES
        + [GENERIC_EAGER_LINE, RESUME_INSTRUCTION_LINE, LAZY_LINE,
           ARKSTEED_EAGER_LINE, RESUME_INSTRUCTION_LINE]
    )
    evidence = module.collect_evidence(stdout)
    expected = [
        GENERIC_EAGER_LINE[len(COMPILER):],
        LAZY_LINE[len(COMPILER):],
        ARKSTEED_EAGER_LINE[len(COMPILER):],
    ]
    if evidence["deopt"] != expected:
        failures.append(
            f"unexpected deopt records: {evidence['deopt']} (expected {expected})")
    # Three events, one record each: `*Deoptimize*` matches the two eager
    # records (the ArkSteed one through its normalized spelling) and the lazy
    # record, and a record with two matching spellings is counted once.
    cases = [
        ("*Deoptimize*", 3, True),
        ("*Deoptimize*", 4, False),
        ("Deoptimize*", 2, True),
        ("Lazy Deoptimize*", 1, True),
        ("Eager deopt disables*", 1, True),
        ("Eager deopt*", 1, True),
        ("Deoptimize*", 1, False),
        ("Lazy Deoptimize*", 0, False),
    ]
    for pattern, count, satisfied in cases:
        message = _violation(module, pattern, count, stdout)
        if (message is None) != satisfied:
            failures.append(
                f"'{pattern} {count}' expected "
                f"{'satisfied' if satisfied else 'violated'}, got: {message}")
    return failures


def _make_case(module, text: str, rel: str = "self_test/case", expected: str = "OK\n"):
    """Write `text` into a temporary case directory and parse its annotations."""
    tmp_dir = tempfile.TemporaryDirectory(prefix="arksteed-self-test-")
    _TEMP_DIRS.append(tmp_dir)
    case_dir = Path(tmp_dir.name) / rel
    case_dir.mkdir(parents=True)
    source = case_dir / "case.js"
    source.write_text(text, encoding="utf-8")
    expected_file = case_dir / "expected_output.txt"
    expected_file.write_text(expected, encoding="utf-8")
    case = module.TestCase(rel, source, expected_file)
    module.parse_annotations(case, None)
    return case


def check_conflicting_params_rejected(module) -> List[str]:
    """Defect 2: PARAMS cannot disable the deopt traces an annotation needs."""
    failures: List[str] = []
    conflicting = [
        ("//! PARAMS --compiler-trace-deopt=false\n//! DEOPT_COUNT *Deoptimize* 0\n",
         "--compiler-trace-deopt"),
        ("//! PARAMS --compiler-trace-deopt false\n//! DEOPT_COUNT *Deoptimize* 0\n",
         "--compiler-trace-deopt"),
        ("//! PARAMS --compiler-trace-deopt=true\n//! DEOPT_COUNT_GE *Deoptimize* 1\n",
         "--compiler-trace-deopt"),
        ("//! PARAMS --compiler-enable-lazy-deopt-trace=false\n"
         "//! DEOPT_COUNT Lazy Deoptimize* 0\n",
         "--compiler-enable-lazy-deopt-trace"),
        ("//! PARAMS --compiler-trace-deopt=false\n", "--compiler-trace-deopt"),
    ]
    for text, option in conflicting:
        case = _make_case(module, text)
        if not case.config_errors:
            failures.append(f"conflicting PARAMS '{option}' was accepted: {text!r}")
            continue
        if not any(option in error for error in case.config_errors):
            failures.append(
                f"config error for '{option}' does not name the option: "
                f"{case.config_errors}")
    allowed = (
        "//! PARAMS --compiler-jit-hotness-threshold=10000 --enable-force-gc=true\n"
        "//! DEOPT_COUNT *Deoptimize* 0\n"
    )
    case = _make_case(module, allowed)
    if case.config_errors:
        failures.append(f"unrelated PARAMS rejected: {case.config_errors}")
    return failures


def check_deopt_trace_options_implied(module) -> List[str]:
    """Defect 2: the annotations guarantee enabled deopt observation."""
    failures: List[str] = []
    args = argparse.Namespace(
        log_debug=False, log_components_value="compiler", print_asm_code=False,
        fully_colored=False, verbose=False,
    )
    cfg = module.BuildConfig("debug", "x64")
    abc_path = Path("/tmp/arksteed-self-test.abc")

    def command(text: str) -> List[str]:
        return module.build_vm_command(_make_case(module, text), abc_path, cfg, args)

    deopt_case = "//! DEOPT_COUNT *Deoptimize* 0\n"
    cmd = command(deopt_case)
    for option in ("--compiler-trace-deopt=true", "--compiler-enable-lazy-deopt-trace=true"):
        if option not in cmd:
            failures.append(f"'{option}' missing for a DEOPT_COUNT case: {' '.join(cmd)}")

    with_params = "//! PARAMS --compiler-enable-jit=false\n//! DEOPT_COUNT_GE *Deoptimize* 1\n"
    cmd = command(with_params)
    for option in ("--compiler-trace-deopt=true", "--compiler-enable-lazy-deopt-trace=true"):
        if option not in cmd:
            failures.append(
                f"'{option}' missing for a DEOPT_COUNT case with PARAMS: {' '.join(cmd)}")
    if "--compiler-enable-jit=false" not in cmd:
        failures.append(f"case PARAMS dropped from the command: {' '.join(cmd)}")

    plain = "print(1);\n"
    cmd = command(plain)
    for option in ("--compiler-trace-deopt", "--compiler-enable-lazy-deopt-trace"):
        if any(token.startswith(option) for token in cmd):
            failures.append(
                f"'{option}' enabled without a DEOPT annotation: {' '.join(cmd)}")
    return failures


def check_deopt_trace_message_filtered(module) -> List[str]:
    """The multi-line deopt trace is filtered whole from the comparison."""
    failures: List[str] = []
    trace = (
        "[compiler] Lazy Deoptimize    at level3 (/tmp/x/case.js:12:1)\n"
        "    at level2 (/tmp/x/case.js:11:1)\n"
        "    at func_main_0 (/tmp/x/case.js:13:1)\n"
        "\n"
    )
    # The canonical chain enables the deopt record stage iff the case asserts
    # deopt evidence, so the synthetic case carries that annotation; the
    # explicit component list is the ordered explicit name only.
    case = _make_case(module, "//! DEOPT_COUNT *Deoptimize* 0\nprint(1);\n",
                      expected="1\n2\nOK\n")
    stdout = "1\n" + trace + "2\nOK\n"
    diff = module.compare_output(stdout, case.expected, case, ["compiler"])
    if diff is not None:
        failures.append(f"deopt trace message not fully filtered:\n{diff}")

    # A program line that merely looks like a stack-trace frame is untouched
    # when it does not continue a traced deopt event.
    case = _make_case(module, "//! DEOPT_COUNT *Deoptimize* 0\nprint(1);\n",
                      expected="    at program output\n42\n")
    stdout = "[compiler] >>> Running method: case.#*#f\n    at program output\n42\n"
    diff = module.compare_output(stdout, case.expected, case, ["compiler"])
    if diff is not None:
        failures.append(f"program output mistaken for a trace frame:\n{diff}")
    return failures


# ---------------------------------------------------------------------------
# Sensitivity controls: VM-free checks of the control core (design S8)
# ---------------------------------------------------------------------------

COMPILER_PREFIX = "[compiler] "
GC_PREFIX = "[gc] "

# The stdout of the synthetic `CONTROL_CASE_TEXT` case: it satisfies the
# original `OK\n` expectation and must not satisfy the sentinel copy.
SYNTHETIC_STDOUT = "OK\n"


def _make_synthetic_case(module, text: str, rel: str = "self_test/control",
                         expected: str = "OK\n", create_expected: bool = True):
    """Write a synthetic case and parse its annotations without a VM.

    `create_expected=False` leaves no `expected_output.txt` on disk, which is
    the only state in which the class-2 control is inapplicable.
    """
    tmp_dir = tempfile.TemporaryDirectory(prefix="arksteed-self-test-")
    _TEMP_DIRS.append(tmp_dir)
    case_dir = Path(tmp_dir.name) / rel
    case_dir.mkdir(parents=True)
    source = case_dir / "case.js"
    source.write_text(text, encoding="utf-8")
    expected_file = case_dir / "expected_output.txt"
    if create_expected:
        expected_file.write_text(expected, encoding="utf-8")
    case = module.TestCase(rel, source, expected_file)
    module.parse_annotations(case, module.load_opcode_names())
    return case


# A synthetic case carrying every count-shaped assertion family, both address
# assertion forms (plain opcode and stub-call) and positive JIT evidence.
CONTROL_CASE_TEXT = (
    "//! METHOD target\n"
    "//! HAS LoadTaggedField\n"
    "//! HAS_NOT StoreTaggedField\n"
    "//! HAS CallRuntime PrintMethodName\n"
    "//! COUNT I32Add 3\n"
    "//! COUNT_GE I32Add 1\n"
    "//! COUNT_LE LoadTaggedField 5\n"
    "//! COUNT_LE StoreTaggedField 0\n"
    "//! BYTECODE_COUNT Add2 2\n"
    "//! CODEGEN_COUNT ArkSteed* 1\n"
    "//! COMPILE_COUNT target 1\n"
    "//! COMPILE_COUNT_GE target 1\n"
    "//! EXEC_COUNT *target 2\n"
    "//! EXEC_COUNT_GE *target 1\n"
    "//! DEOPT_COUNT *Deoptimize* 0\n"
    "//! DEOPT_COUNT_GE *Deoptimize* 0\n"
    "//! GC_COUNT_GE *gc* 1\n"
    "function target(n: number): number { return n; }\n"
)


def _control_observation(module, counts: Optional[Dict] = None):
    """Synthetic base observation consistent with CONTROL_CASE_TEXT.

    `Call` is observed 6 times although no assertion names it: it is the
    substitution candidate that makes the upper-bound assertion
    `COUNT_LE LoadTaggedField 5` violated, so every address assertion of the
    synthetic case has an accepted candidate and the case can be green.
    """
    if counts is None:
        counts = {
            "target": module.MethodCounts(
                collections.Counter({
                    "LoadTaggedField": 2, "StoreTaggedField": 0,
                    "I32Add": 3, "CallRuntime": 2, "Invoke": 1, "Call": 6,
                }),
                collections.Counter({("CallRuntime", "RTStub_PrintMethodName"): 2})),
        }
    return module.ControlObservation(
        counts=counts,
        instructions={"target": [("Add2", ""), ("Add2", ""), ("Lda", "x")]},
        comments={"target": ["ArkSteed pipeline"]},
        evidence={
            "compile": ["target"],
            "exec": ["case.#*#target", "case.#*#target"],
            "deopt": [],
            "gc": ["ArkSteed trigger full gc"],
        },
        stdout="",
    )


def _graph_dump(*lines: str) -> str:
    """A synthetic CFG dump section for the live-range checker."""
    return "".join(
        [COMPILER_PREFIX + "===== Starts Graph Dump =====\n"]
        + [COMPILER_PREFIX + "  " + line + "\n" for line in lines]
        + [COMPILER_PREFIX + "===== Finishes Graph Dump =====\n"])


def _instance_for(module, instances, label: str, class_token: str):
    for instance in instances:
        if instance.class_token != class_token:
            continue
        if instance.assertion is not None and instance.assertion.label() == label:
            return instance
    return None


def check_control_evidence_collection(module) -> List[str]:
    """(a) compile/exec/gc evidence collection and its annotations."""
    failures: List[str] = []
    stdout = "".join(line + "\n" for line in [
        COMPILER_PREFIX + "======== ArkSteedCompilerTask: Starts compiling: target ========",
        COMPILER_PREFIX + "======== ArkSteedCompilerTask: Starts compiling:  ========",
        COMPILER_PREFIX + "======== ArkSteedCompilerTask: Starts compiling: DeoptimizeTarget ========",
        COMPILER_PREFIX + ">>> Running method: case.#*#target",
        COMPILER_PREFIX + ">>> Running method: case.#*#target",
        GC_PREFIX + "ArkSteed trigger full gc",
        GC_PREFIX + "ArkSteed trigger full gc",
    ])
    evidence = module.collect_evidence(stdout)
    if evidence["compile"] != ["target", "(anonymous)", "DeoptimizeTarget"]:
        failures.append(
            f"compile evidence with an anonymous marker: {evidence['compile']}")
    if evidence["exec"] != ["case.#*#target"] * 2:
        failures.append(f"exec evidence: {evidence['exec']}")
    if evidence["gc"] != ["ArkSteed trigger full gc"] * 2:
        failures.append(f"gc evidence: {evidence['gc']}")
    if evidence["deopt"]:
        failures.append(
            f"a compilation marker for DeoptimizeTarget leaked into deopt: "
            f"{evidence['deopt']}")

    cases = [
        (module.CompileCountConstraint("target", 1), True),
        (module.CompileCountConstraint("target", 2), False),
        (module.CompileCountConstraint("(anonymous)", 1), True),
        (module.CompileCountGeConstraint("tar*", 1), True),
        (module.CompileCountGeConstraint("tar*", 2), False),
        (module.CompileCountConstraint("Deoptimize*", 1), True),
        (module.ExecCountConstraint("*target", 2), True),
        (module.ExecCountConstraint("*target", 3), False),
        (module.ExecCountGeConstraint("*target", 2), True),
        (module.GcCountGeConstraint("*gc", 2), True),
        (module.GcCountGeConstraint("*gc", 3), False),
        (module.DeoptCountConstraint("*Deoptimize*", 0), True),
        (module.DeoptCountConstraint("*Deoptimize*", 1), False),
    ]
    for constraint, satisfied in cases:
        message = constraint.violated(evidence)
        if (message is None) != satisfied:
            failures.append(
                f"'{constraint.label()}' expected "
                f"{'satisfied' if satisfied else 'violated'}, got: {message}")
    return failures


def check_control_applicability(module) -> List[str]:
    """(b) the class applicability rule on synthetic cases."""
    failures: List[str] = []
    tokens = {
        module.CONTROL_CLASS_JIT_OFF, module.CONTROL_CLASS_EXPECTATION,
        module.CONTROL_CLASS_COUNT, module.CONTROL_CLASS_ADDRESS,
    }
    full = _make_synthetic_case(module, CONTROL_CASE_TEXT)
    applicability = module.control_applicability(full)
    if set(applicability) != tokens:
        failures.append(f"unexpected class tokens: {sorted(applicability)}")
    for token, entry in applicability.items():
        if not entry.applicable:
            failures.append(
                f"class '{token}' unexpectedly inapplicable: {entry.reason}")

    # No positive JIT evidence -> class 1 inapplicable, named precondition.
    no_jit = _make_synthetic_case(
        module, "//! METHOD target\n//! HAS LoadTaggedField\n"
                "//! COMPILE_COUNT target 0\n")
    entry = module.control_applicability(no_jit)[module.CONTROL_CLASS_JIT_OFF]
    if entry.applicable or entry.reason != module.CONTROL_PRECONDITION_JIT_OFF:
        failures.append(f"class 1 without positive JIT evidence: {entry}")

    # An empty expectation file keeps class 2 applicable (only the file's
    # presence matters), and so does a non-empty one.
    empty_expected = _make_synthetic_case(module, "print(1);\n", expected="")
    entry = module.control_applicability(empty_expected)[module.CONTROL_CLASS_EXPECTATION]
    if not entry.applicable:
        failures.append(f"class 2 inapplicable for an empty expectation: {entry}")
    non_empty = _make_synthetic_case(module, "print(1);\n", expected="1\n")
    if not module.control_applicability(non_empty)[module.CONTROL_CLASS_EXPECTATION].applicable:
        failures.append("class 2 inapplicable for a non-empty expectation")

    # No expectation file at all -> class 2 inapplicable with that reason.
    absent = _make_synthetic_case(module, "print(1);\n", create_expected=False)
    entry = module.control_applicability(absent)[module.CONTROL_CLASS_EXPECTATION]
    if entry.applicable or entry.reason != "no expectation file":
        failures.append(f"class 2 without an expectation file: {entry}")

    # No assertions at all -> classes 3 and 4 inapplicable with named reasons.
    no_ir = _make_synthetic_case(module, "print(1);\n")
    no_ir_applicability = module.control_applicability(no_ir)
    for token, reason in ((module.CONTROL_CLASS_COUNT, module.CONTROL_PRECONDITION_COUNT),
                          (module.CONTROL_CLASS_ADDRESS, module.CONTROL_PRECONDITION_ADDRESS)):
        entry = no_ir_applicability[token]
        if entry.applicable or entry.reason != reason:
            failures.append(f"class '{token}' without IR constraints: {entry}")

    # Every applicable class of the full synthetic case yields instances.
    instances = module.build_control_instances(
        full, _control_observation(module), module.load_opcode_order())
    for token in tokens:
        if not any(i.class_token == token for i in instances):
            failures.append(f"class '{token}' produced no instance")
    return failures


def check_control_construction(module) -> List[str]:
    """(b) construction of derived assertions and the S3/S4 value rules."""
    failures: List[str] = []
    case = _make_synthetic_case(module, CONTROL_CASE_TEXT)
    if case.config_errors:
        failures.append(f"synthetic case config errors: {case.config_errors}")
    observation = _control_observation(module)
    assertions = module.enumerate_case_assertions(case)

    for assertion in assertions.count_shaped:
        message = module.assertion_violated(assertion, observation)
        if message is not None:
            failures.append(
                f"original assertion '{assertion.label()}' does not hold: {message}")

    instances = module.build_control_instances(
        case, observation, module.load_opcode_order())

    for instance in instances:
        if instance.class_token not in (module.CONTROL_CLASS_COUNT,
                                        module.CONTROL_CLASS_ADDRESS):
            continue
        if instance.construction_error or instance.inapplicable_reason:
            continue
        if instance.mutant_line is None or instance.assertion is None:
            failures.append(f"{instance.variant_id}: constructed without a mutation")
            continue
        mutated = module.parse_control_annotation(instance.mutant_line)
        if mutated.label() != instance.anchor:
            failures.append(
                f"{instance.variant_id}: re-parsed anchor '{mutated.label()}' "
                f"differs from '{instance.anchor}'")
        probe = module.ControlAssertion(
            instance.assertion.kind, instance.assertion.record, mutated)
        message = module.assertion_violated(probe, observation)
        if message is None:
            failures.append(
                f"{instance.variant_id}: mutated assertion is not violated on "
                f"the observation")
        if instance.derived is not None and module.assertion_count(mutated) != instance.derived:
            failures.append(
                f"{instance.variant_id}: derived value {instance.derived} not "
                f"present in the re-parsed assertion {mutated.label()}")
        expected_kind = "count" if instance.class_token == module.CONTROL_CLASS_COUNT else "address"
        if instance.variant_id != (f"{expected_kind}-{instance.line}"):
            failures.append(f"{instance.variant_id}: unexpected variant id")

    # S3: equality -> n + 1, lower bound -> observed + 1, upper bound -> observed - 1.
    def count_instance(label):
        return _instance_for(module, instances, label, module.CONTROL_CLASS_COUNT)

    expected_values = {
        "COMPILE_COUNT target == 1": 2,
        "COMPILE_COUNT_GE target >= 1": 2,
        "COUNT_LE LoadTaggedField <= 5": 1,
        "COUNT I32Add == 3": 4,
        "COUNT_GE I32Add >= 1": 4,
        "BYTECODE_COUNT Add2 == 2": 3,
        "CODEGEN_COUNT ArkSteed* == 1": 2,
        "EXEC_COUNT_GE *target >= 1": 3,
        "DEOPT_COUNT *Deoptimize* == 0": 1,
        "DEOPT_COUNT_GE *Deoptimize* >= 0": 1,
        "GC_COUNT_GE *gc* >= 1": 2,
    }
    for label, derived in expected_values.items():
        instance = count_instance(label)
        if instance is None:
            failures.append(f"no class-3 instance for '{label}'")
            continue
        if instance.derived != derived:
            failures.append(
                f"class-3 '{label}': derived {instance.derived}, expected {derived}")

    # `HAS`/`HAS_NOT` are flipped, keeping opcode and stub pattern.
    flipped = count_instance("HAS CallRuntime PrintMethodName")
    if flipped is None or "HAS_NOT CallRuntime PrintMethodName" not in (flipped.mutant_line or ""):
        failures.append(f"HAS was not flipped: {flipped}")
    unflipped = count_instance("HAS_NOT StoreTaggedField")
    if unflipped is None or "HAS StoreTaggedField" not in (unflipped.mutant_line or ""):
        failures.append(f"HAS_NOT was not flipped: {unflipped}")

    # `COUNT_LE` with observation 0 is inapplicable by construction.
    zero_le = count_instance("COUNT_LE StoreTaggedField <= 0")
    if zero_le is None or not zero_le.inapplicable_reason \
            or "COUNT_LE" not in zero_le.inapplicable_reason:
        failures.append(f"COUNT_LE with observation 0: {zero_le}")

    # S4 rule (a): the other stub-call opcode with the same stub pattern.
    address = _instance_for(module, instances, "HAS CallRuntime PrintMethodName",
                            module.CONTROL_CLASS_ADDRESS)
    if address is None:
        failures.append("no class-4 instance for 'HAS CallRuntime PrintMethodName'")
    else:
        if address.candidate_rule != "a" or address.substitute != "CallCommonStub":
            failures.append(f"unexpected stub-call substitution: {address}")
        if "CallCommonStub PrintMethodName" not in (address.mutant_line or ""):
            failures.append(f"stub-call mutant: {address.mutant_line!r}")
        if address.observation != 0:
            failures.append(f"stub-call observed count: {address.observation}")

    # S4 rule (b): opcode-list order, stub pattern dropped for non-stub opcodes.
    plain = _instance_for(module, instances, "COUNT_LE StoreTaggedField <= 0",
                          module.CONTROL_CLASS_ADDRESS)
    if plain is None:
        failures.append("no class-4 instance for 'COUNT_LE StoreTaggedField <= 0'")
    elif plain.candidate_rule != "b" or plain.substitute is None:
        failures.append(f"unexpected opcode substitution: {plain}")

    # A candidate-less address assertion is invalid, never a pass: a
    # zero-valued assertion over an all-zero observation admits no substitute
    # that makes the mutation fail.
    hostile = module.MethodCounts(collections.Counter(), collections.Counter())
    hostile_case = _make_synthetic_case(
        module, "//! METHOD target\n//! HAS_NOT LoadTaggedField\n")
    hostile_instances = module.build_control_instances(
        hostile_case,
        module.ControlObservation(counts={"target": hostile},
                                  evidence={"compile": [], "exec": [], "deopt": [], "gc": []}),
        module.load_opcode_order())
    address_instances = [i for i in hostile_instances
                         if i.class_token == module.CONTROL_CLASS_ADDRESS]
    if len(address_instances) != 1 or address_instances[0].construction_error \
            != "no substitution candidate":
        failures.append(
            "an address assertion with no violating candidate was not reported "
            f"as invalid: {address_instances}")
    return failures


def check_control_failure_classification(module) -> List[str]:
    """(c) failure-class classification and the control verdict rules."""
    failures: List[str] = []
    if set(module.FAILURE_CLASSES) != (module.VALID_FAILURE_CLASSES
                                       | module.INVALID_FAILURE_CLASSES):
        failures.append("the valid/invalid partitions do not cover the tokens")
    if module.VALID_FAILURE_CLASSES & module.INVALID_FAILURE_CLASSES:
        failures.append("a failure token is both valid and invalid")

    # Four of the anchors are taken from the runner's own producers, so the
    # classifier is checked against the real reason vocabulary.
    assertion = module.HasConstraint("LoadTaggedField", None)
    zero_counts = {"target": module.MethodCounts(collections.Counter(),
                                                 collections.Counter())}
    violation_reasons = module.check_constraints({"target": [assertion]}, zero_counts)
    missing_dump_reasons = module.check_constraints({"target": [assertion]}, {})
    expected_case = _make_synthetic_case(module, "print(1);\n", expected="1\n")
    diff = module.compare_output("2\n", expected_case.expected, expected_case, [])
    live_range_reasons = module.check_live_ranges(
        _graph_dump("v1: Int32Constant[1] \u2192 live range: [1-2]",
                    "v2: I32Add(v1, v3) \u2192 live range: [3-4]"))
    for label, produced in (("violation", violation_reasons),
                            ("missing-cfg-dump", missing_dump_reasons),
                            ("mismatch", [diff] if diff is not None else []),
                            ("live-range", live_range_reasons)):
        if not produced:
            failures.append(
                f"the runner produced no {label} reason to classify")
    violation_reason = violation_reasons[0] if violation_reasons else ""
    missing_dump_reason = missing_dump_reasons[0] if missing_dump_reasons else ""
    mismatch_reason = "stdout mismatch:\n" + (diff or "")
    live_range_reason = live_range_reasons[0] if live_range_reasons else ""
    crash_reason = (f"ark_js_vm crashed with {module.crash_signal_name(-6)} "
                    f"(return code -6)")

    reason_sets = [
        (violation_reason, module.FAILURE_VIOLATION),
        (missing_dump_reason, module.FAILURE_MISSING_CFG_DUMP),
        ("method 'target' was never JIT-compiled "
         "(no generated-code section in compiler log)",
         module.FAILURE_MISSING_CFG_DUMP),
        (mismatch_reason, module.FAILURE_MISMATCH),
        (live_range_reason, module.FAILURE_LIVE_RANGE),
        ("target: v3 is used as a Phi input but has no live range",
         module.FAILURE_LIVE_RANGE),
        ("target: live range info present but unparsable (log format drift?)",
         module.FAILURE_LIVE_RANGE),
        (crash_reason, module.FAILURE_CRASH),
        ("execution timed out", module.FAILURE_TIMEOUT),
        ("compilation failed: es2abc exited with 1:\nboom", module.FAILURE_COMPILE_ERROR),
        ("ark_js_vm exited with return code 3", module.FAILURE_NONZERO_EXIT),
        ("line 4: unknown annotation keyword 'FOO'", module.FAILURE_CONFIG_ERROR),
        ("bytecode constraints unavailable: ark_disasm does not exist: /x",
         module.FAILURE_UNCLASSIFIED),
    ]
    for reason, expected in reason_sets:
        classes = module.classify_failure_classes([reason])
        if classes != frozenset({expected}):
            failures.append(
                f"reason {reason!r} classified as {sorted(classes)}, "
                f"expected {{{expected}}}")
    if module.classify_failure_classes([]) != frozenset():
        failures.append("an empty reason list must classify as a pass")
    if module.classify_failure_classes([], ("line 1: boom",)) \
            != frozenset({module.FAILURE_CONFIG_ERROR}):
        failures.append("a case config error must classify as config-error")

    # Every reason is classified independently (S6): an unrecognized or
    # infrastructure reason next to a recognized one still contributes
    # `unclassified`, so an instance whose variant run recorded such a reason
    # can never be reported valid. Reasons that all match a pattern contribute
    # no `unclassified`.
    infra_reason = "bytecode constraints unavailable: ark_disasm does not exist: /x"
    mixed_sets = [
        ([violation_reason, infra_reason],
         {module.FAILURE_VIOLATION, module.FAILURE_UNCLASSIFIED}),
        ([infra_reason, violation_reason],
         {module.FAILURE_VIOLATION, module.FAILURE_UNCLASSIFIED}),
        ([violation_reason, crash_reason],
         {module.FAILURE_VIOLATION, module.FAILURE_CRASH}),
        ([infra_reason, "ark_js_vm could not be executed"],
         {module.FAILURE_UNCLASSIFIED}),
    ]
    for reasons, expected in mixed_sets:
        classes = module.classify_failure_classes(list(reasons))
        if classes != frozenset(expected):
            failures.append(
                f"reasons {reasons!r} classified as {sorted(classes)}, "
                f"expected {sorted(expected)}")

    # Verdict evaluation on a constructed instance.
    case = _make_synthetic_case(module, CONTROL_CASE_TEXT)
    observation = _control_observation(module)
    instances = module.build_control_instances(
        case, observation, module.load_opcode_order())
    instance = _instance_for(module, instances, "HAS_NOT StoreTaggedField",
                             module.CONTROL_CLASS_COUNT)
    if instance is None:
        failures.append("no class-3 instance for the verdict checks")
        return failures
    violation_reason = f'"{instance.anchor}" violated, found 0'

    verdicts = [
        ([], True, "", "case passed"),
        ([violation_reason], True, "", None),
        (["unrelated infrastructure failure"], True, "",
         "expected failure class missing (violation)"),
        (['"SomeOtherAssertion" violated, found 0'], True, "",
         "anchor missing"),
        ([violation_reason, "ark_js_vm crashed with SIGSEGV (return code -11)"],
         True, "", "failure class not valid (crash)"),
        # The expected class and the anchor are both present, so only the
        # per-reason `unclassified` token makes this instance invalid (S6:
        # environment failures are not passes).
        ([violation_reason, infra_reason], True, "",
         "failure class not valid (unclassified)"),
    ]
    for reasons, base_passed, stdout, expected_reason in verdicts:
        result = module.evaluate_control_instance(instance, base_passed, reasons, stdout)
        if expected_reason is None:
            if result.verdict != module.CONTROL_VERDICT_VALID:
                failures.append(
                    f"expected a valid verdict for {reasons}, got {result.verdict}: "
                    f"{result.reason}")
            if result.actual_classes != frozenset({module.FAILURE_VIOLATION}):
                failures.append(f"recorded classes: {sorted(result.actual_classes)}")
            continue
        if result.verdict != module.CONTROL_VERDICT_INVALID or \
                expected_reason not in (result.reason or ""):
            failures.append(
                f"expected invalid ({expected_reason}) for {reasons}, got "
                f"{result.verdict}: {result.reason}")
    if module.evaluate_control_instance(instance, False, [violation_reason]).verdict \
            != module.CONTROL_VERDICT_INVALID:
        failures.append("an instance of a red base case must be invalid")

    # A passed variant is invalid; a still-compiling JIT-off variant is invalid
    # with the specific reason.
    passed = module.evaluate_control_instance(instance, True, [])
    if passed.verdict != module.CONTROL_VERDICT_INVALID or passed.reason != "case passed":
        failures.append(f"passed variant verdict: {passed}")
    jit_off = _instance_for(module, instances, "COMPILE_COUNT target == 1",
                            module.CONTROL_CLASS_JIT_OFF)
    if jit_off is None:
        failures.append("no class-1 instance for the witness checks")
    else:
        if jit_off.anchor != "COMPILE_COUNT target == 1":
            failures.append(f"class-1 witness anchor: {jit_off.anchor}")
        marker = "[compiler] ======== ArkSteedCompilerTask: Starts compiling: target ========\n"
        ineffective = module.evaluate_control_instance(
            jit_off, True, [], marker)
        if ineffective.verdict != module.CONTROL_VERDICT_INVALID \
                or ineffective.reason != "jit-off ineffective":
            failures.append(f"ineffective JIT-off verdict: {ineffective}")
        valid = module.evaluate_control_instance(
            jit_off, True, [f'"{jit_off.anchor}" violated, found 0'], "")
        if valid.verdict != module.CONTROL_VERDICT_VALID:
            failures.append(f"JIT-off verdict: {valid}")

    # The per-case verdict: coverage by class 3/4 and the two inapplicable
    # states. `COUNT_LE ... <= 0` is covered by its class-4 instance.
    results = []
    for item in instances:
        if item.class_token == module.CONTROL_CLASS_EXPECTATION:
            results.append(module.evaluate_control_instance(
                item, True,
                ["stdout mismatch:\n--- expected\n+++ actual\n+" + module.CONTROL_SENTINEL]))
        else:
            results.append(module.evaluate_control_instance(
                item, True, [f'"{item.anchor}" violated, found 0']))
    case_result = module.evaluate_case_control(case, True, results, True)
    if case_result.verdict != "green":
        failures.append(f"synthetic controlled case is not green: {case_result.reasons}")
    if case_result.uncovered:
        failures.append(f"unexpected uncovered assertions: {case_result.uncovered}")
    if not case_result.source_tree_unchanged:
        failures.append("source_tree_unchanged flag lost")
    # An invalid instance is a case error on its own: it must not be masked by
    # a valid sibling instance of its class while the assertion stays covered.
    # Before this rule the case was reported green, so a mandatory case could
    # pass with a knowingly invalid control (S2/S7 and epic criterion 1: any
    # invalid control of a mandatory case makes the run exit non-zero).
    address_covered = {
        instance_result.instance.assertion.key() for instance_result in results
        if instance_result.class_token == module.CONTROL_CLASS_ADDRESS
        and instance_result.verdict == module.CONTROL_VERDICT_VALID
        and instance_result.instance.assertion is not None}
    masked_index = next(
        (index for index, instance_result in enumerate(results)
         if instance_result.class_token == module.CONTROL_CLASS_COUNT
         and instance_result.verdict == module.CONTROL_VERDICT_VALID
         and instance_result.instance.assertion is not None
         and instance_result.instance.assertion.key() in address_covered), None)
    if masked_index is None:
        failures.append("no class-3 instance with a valid class-4 sibling to invalidate")
    else:
        masked = list(results)
        masked[masked_index] = module.evaluate_control_instance(
            masked[masked_index].instance, True, [])
        masked_result = module.evaluate_case_control(case, True, masked, True)
        if masked_result.uncovered:
            failures.append(
                f"the invalid-instance probe lost coverage: {masked_result.uncovered}")
        if masked_result.verdict != "red":
            failures.append(
                "an invalid instance with a valid sibling and full coverage left "
                f"the case {masked_result.verdict}: {masked_result.reasons}")
        instance_id = masked[masked_index].instance.variant_id
        class_token = masked[masked_index].class_token
        if not any(class_token in failure_reason and instance_id in failure_reason
                   for failure_reason in masked_result.reasons):
            failures.append(
                f"no case reason names the invalid instance {instance_id}: "
                f"{masked_result.reasons}")
    red = module.evaluate_case_control(
        case, True, [module.evaluate_control_instance(
            results[0].instance, True, [])], True)
    if red.verdict != "red" or not any("source" not in r for r in red.reasons):
        failures.append(f"red case verdict: {red}")
    if module.evaluate_case_control(case, True, results, False).verdict != "red":
        failures.append("a modified source tree must make the case red")
    # A case without any applicable class stays a defensive red verdict, and an
    # inapplicable class is never a pass (S2). Such a selection is the corpus
    # class, which S1 rejects as a usage error; the verdict path itself is
    # checked here without a VM.
    bare = _make_synthetic_case(module, "print(1);\n", create_expected=False)
    bare_result = module.evaluate_case_control(bare, True, [], True)
    if bare_result.verdict != "red" or not any(
            "no applicable control" in failure_reason for failure_reason in bare_result.reasons):
        failures.append(
            f"a case without an applicable class is not a defensive red verdict: "
            f"{bare_result.verdict} {bare_result.reasons}")
    if any(control_class.verdict == module.CONTROL_VERDICT_VALID
           for control_class in bare_result.classes):
        failures.append("an inapplicable class was reported valid")
    return failures


def check_control_live_range_violation(module) -> List[str]:
    """(d) the controlled live-range violation of matrix row G6.4."""
    failures: List[str] = []

    died = module.check_live_ranges(
        _graph_dump("v1: Int32Constant[1] \u2192 live range: [1-2]",
                    "v2: I32Add(v1, v3) \u2192 live range: [3-4]"))
    if not any("dies before its use" in error for error in died):
        failures.append(f"a value dying before its use was not rejected: {died}")
    if not any("v1 [1-2] dies before its use v2" in error for error in died):
        failures.append(f"the live-range message is not specific: {died}")

    corrected = module.check_live_ranges(
        _graph_dump("v1: Int32Constant[1] \u2192 live range: [1-3]",
                    "v2: I32Add(v1, v3) \u2192 live range: [3-4]"))
    if corrected:
        failures.append(f"the corrected dump was rejected: {corrected}")

    unparsable = module.check_live_ranges(_graph_dump("live range: garbage"))
    if not any("unparsable" in error for error in unparsable):
        failures.append(f"an unparsable live-range section was accepted: {unparsable}")

    orphan_phi = module.check_live_ranges(
        _graph_dump("v1: Phi(v2) \u2192 live range: [1-3]"))
    if not any("Phi input" in error and "no live range" in error
               for error in orphan_phi):
        failures.append(f"a Phi input without a live range was accepted: {orphan_phi}")

    healthy = module.check_live_ranges(
        _graph_dump("v1: Int32Constant[1] \u2192 live range: [1-4]",
                    "v2: I32Add(v1, v3) \u2192 live range: [3-4]"))
    if healthy:
        failures.append(f"a healthy dump was rejected: {healthy}")
    return failures


def check_control_variant_isolation(module) -> List[str]:
    """(e) variant isolation and byte preservation of the source tree."""
    failures: List[str] = []
    case = _make_synthetic_case(module, CONTROL_CASE_TEXT, rel="self_test/isolated")
    observation = _control_observation(module)
    instances = module.build_control_instances(
        case, observation, module.load_opcode_order())
    before = module.digest_case_inputs(case)

    tmp_dir = tempfile.TemporaryDirectory(prefix="arksteed-self-test-")
    _TEMP_DIRS.append(tmp_dir)
    artifacts_root = Path(tmp_dir.name) / "artifacts"

    base = module.materialize_base_variant(case, artifacts_root)
    if base.rel != f"controls/{case.rel}/base":
        failures.append(f"unexpected base variant rel: {base.rel}")
    if artifacts_root.resolve() not in base.source.resolve().parents:
        failures.append(f"base variant source escapes the artifact root: {base.source}")
    if base.source.read_text(encoding="utf-8") != case.source.read_text(encoding="utf-8"):
        failures.append("the base variant source is not a plain copy")

    rewritten = [i for i in instances if i.mutant_line is not None]
    if not rewritten:
        failures.append("no mutable instance to materialise")
    for instance in rewritten:
        variant = module.materialize_variant(case, instance, artifacts_root)
        if artifacts_root.resolve() not in variant.source.resolve().parents:
            failures.append(
                f"variant source escapes the artifact root: {variant.source}")
        if variant.expected != case.expected and \
                artifacts_root.resolve() not in variant.expected.resolve().parents:
            failures.append(
                f"variant expectation escapes the artifact root: {variant.expected}")
        original = case.source.read_text(encoding="utf-8").splitlines(keepends=True)
        mutated = variant.source.read_text(encoding="utf-8").splitlines(keepends=True)
        if len(original) != len(mutated):
            failures.append(
                f"{instance.variant_id}: line count changed "
                f"({len(original)} -> {len(mutated)})")
        else:
            differing = [index for index, (a, b) in enumerate(zip(original, mutated))
                         if a != b]
            if differing != [instance.line - 1]:
                failures.append(
                    f"{instance.variant_id}: differing lines {differing}, "
                    f"expected [{instance.line - 1}]")
        if instance.class_token == module.CONTROL_CLASS_EXPECTATION:
            continue
        module.parse_annotations(variant, module.load_opcode_names())
        if variant.config_errors:
            failures.append(
                f"{instance.variant_id}: mutated source has config errors: "
                f"{variant.config_errors}")

    expectation_instance = next(
        (i for i in instances if i.class_token == module.CONTROL_CLASS_EXPECTATION), None)
    if expectation_instance is None:
        failures.append("no class-2 instance to materialise")
    else:
        variant = module.materialize_variant(case, expectation_instance, artifacts_root)
        lines = variant.expected.read_text(encoding="utf-8").splitlines()
        original_lines = case.expected.read_text(encoding="utf-8").splitlines()
        if lines != original_lines + [module.CONTROL_SENTINEL]:
            failures.append(
                f"the class-2 copy is not the original plus one sentinel line: {lines}")
        # S8(b): both the non-empty and the empty original expectation make the
        # class-2 control applicable, and the runner's own stdout comparison
        # must reject the sentinel copy while the same stdout satisfies the
        # original. The production behaviour is what this check pins down.
        if module.compare_output(SYNTHETIC_STDOUT, case.expected, case, []) is not None:
            failures.append(
                f"the synthetic stdout does not satisfy the original expectation: "
                f"{SYNTHETIC_STDOUT!r}")
        diff = module.compare_output(SYNTHETIC_STDOUT, variant.expected, variant, [])
        if diff is None or module.CONTROL_SENTINEL not in diff:
            failures.append(
                f"the non-empty class-2 copy produced no sentinel mismatch: {diff!r}")

    empty = _make_synthetic_case(module, "print(1);\n", expected="", rel="self_test/empty")
    empty_instances = module.build_control_instances(
        empty, module.ControlObservation(evidence={"compile": [], "exec": [],
                                                   "deopt": [], "gc": []}),
        module.load_opcode_order())
    empty_instance = next(i for i in empty_instances
                          if i.class_token == module.CONTROL_CLASS_EXPECTATION)
    empty_variant = module.materialize_variant(empty, empty_instance, artifacts_root)
    if empty_variant.expected.read_text(encoding="utf-8") != module.CONTROL_SENTINEL + "\n":
        failures.append(
            f"an empty expectation copy is not the sentinel alone: "
            f"{empty_variant.expected.read_text(encoding='utf-8')!r}")
    if module.compare_output("", empty.expected, empty, []) is not None:
        failures.append("an empty original expectation must skip the stdout comparison")
    empty_diff = module.compare_output("", empty_variant.expected, empty_variant, [])
    if empty_diff is None or module.CONTROL_SENTINEL not in empty_diff:
        failures.append(
            f"the empty-expectation class-2 copy produced no sentinel mismatch: "
            f"{empty_diff!r}")

    after = module.digest_case_inputs(case)
    if not module.case_tree_unchanged(before, after):
        failures.append("materialising variants modified the originals")

    for root in (module.TEST_DIR, module.TEST_DIR / "self_test_scratch"):
        try:
            module.materialize_case_copy(case, root, module.CONTROL_VARIANT_BASE)
            failures.append(f"materialisation inside the test tree was allowed: {root}")
        except ValueError:
            pass
    return failures


def check_control_default_path_invariance(module) -> List[str]:
    """(f) a run without the mode adds no option to a case command."""
    failures: List[str] = []
    case = _make_synthetic_case(module, CONTROL_CASE_TEXT)
    args = argparse.Namespace(
        log_debug=False, log_components_value="compiler", print_asm_code=False,
        fully_colored=False, verbose=False,
    )
    cfg = module.BuildConfig("debug", "x64")
    abc_path = Path("/tmp/arksteed-self-test.abc")
    cmd = module.build_vm_command(case, abc_path, cfg, args)
    if cmd != module.build_vm_command(case, abc_path, cfg, args):
        failures.append("build_vm_command is not deterministic")
    if "--compiler-enable-jit=true" not in cmd:
        failures.append(f"the JIT default was lost: {' '.join(cmd)}")
    if any(token.startswith("--compiler-enable-jit=false") for token in cmd):
        failures.append(f"a JIT-off override leaked into the default command: {cmd}")

    expected_names = {
        "--compiler-enable-jit", "--compiler-enable-litecg", "--enable-force-gc",
        "--open-ark-tools", "--log-level", "--log-components",
        "--compiler-arksteed-print-graph", "--compiler-arksteed-print-method-name",
        "--compiler-trace-deopt", "--compiler-enable-lazy-deopt-trace",
        "--compiler-arksteed-print-code", "--compiler-arksteed-enable-code-comment",
        "--entry-point",
    }
    actual_names = {module._option_name(token) for token in cmd if module._option_name(token)}
    actual_names -= {"--icu-data-path", "--stub-file"}  # environment-dependent paths
    added = actual_names - expected_names
    if added:
        failures.append(f"the default command gained option(s): {sorted(added)}")
    missing = expected_names - actual_names
    if missing:
        failures.append(f"the default command lost option(s): {sorted(missing)}")
    for value in ("--compiler-arksteed-print-method-name=true",
                  "--compiler-trace-deopt=true",
                  "--compiler-enable-lazy-deopt-trace=true",
                  "--compiler-arksteed-print-code=true",
                  "--compiler-arksteed-enable-code-comment=true",
                  "--log-components=compiler:gc"):
        if value not in cmd:
            failures.append(f"'{value}' missing for the annotated case: {' '.join(cmd)}")
    return failures


# Three synthetic cases for the class-1 command construction: one names the
# JIT option in the `--opt=value` form, the other in the space-separated form
# (so the suppression of the case's own value has to cover both spellings),
# and the third names it not at all, so the command line falls back to the
# runner default of `BASE_ARGS` and its suppression is exercised instead of
# being masked by the `overridden` filter. The source order of the positive
# JIT evidence fixes the class-1 witness anchor.
CONTROL_JIT_OFF_CASE_TEXT = (
    "//! PARAMS --compiler-enable-jit=true --compiler-jit-hotness-threshold=60000\n"
    "//! EXEC_COUNT *target 2\n"
    "//! COMPILE_COUNT target 1\n"
    "function target(n: number): number { return n; }\n"
)
CONTROL_JIT_OFF_SPACED_CASE_TEXT = (
    "//! PARAMS --compiler-enable-jit true\n"
    "//! EXEC_COUNT *target 2\n"
    "//! COMPILE_COUNT target 1\n"
    "function target(n: number): number { return n; }\n"
)
CONTROL_JIT_OFF_DEFAULT_CASE_TEXT = (
    "//! PARAMS --compiler-jit-hotness-threshold=60000\n"
    "//! EXEC_COUNT *target 2\n"
    "//! COMPILE_COUNT target 1\n"
    "function target(n: number): number { return n; }\n"
)


def check_control_jit_off_command(module) -> List[str]:
    """Class 1: the derived variant's own JIT-off command line (S1).

    The class-1 value is substituted by the runner into the command line of the
    derived variant, never through `//! PARAMS`: the runner default and any
    value the case names itself (`--opt=value` and the space-separated form)
    are suppressed, so the variant command carries exactly one
    `--compiler-enable-jit=false` and no contradicting duplicate or stray value
    token. The variant source stays a byte-identical copy of the case, the
    witness anchor is the first positive JIT-evidence annotation in source
    order, and the base command of the same case is unchanged.
    """
    failures: List[str] = []
    args = argparse.Namespace(
        log_debug=False, log_components_value="compiler", print_asm_code=False,
        fully_colored=False, verbose=False,
    )
    cfg = module.BuildConfig("debug", "x64")
    abc_path = Path("/tmp/arksteed-self-test.abc")
    tmp_dir = tempfile.TemporaryDirectory(prefix="arksteed-self-test-")
    _TEMP_DIRS.append(tmp_dir)
    artifacts_root = Path(tmp_dir.name) / "artifacts"

    def jit_tokens(cmd):
        return [token for token in cmd
                if module._option_name(token) == module.CONTROL_JIT_OPTION]

    def case_owned_jit_values(params):
        """Bare value tokens the case itself supplies for the JIT option.

        The space-separated spelling `--compiler-enable-jit true` puts `true`
        into the parameter list as a positional token: suppressing the option
        without consuming that value leaves it in the command line.
        """
        values = []
        for position, token in enumerate(params):
            if module._option_name(token) == module.CONTROL_JIT_OPTION \
                    and "=" not in token and position + 1 < len(params):
                values.append(params[position + 1])
        return values

    def orphaned_value_tokens(tokens, values):
        """`values` tokens that no space-separated option consumes as its own."""
        orphans = []
        position = 0
        while position < len(tokens):
            token = tokens[position]
            if token.startswith("--"):
                position += 2 if "=" not in token else 1
                continue
            if token in values:
                orphans.append(token)
            position += 1
        return orphans

    for index, text in enumerate((CONTROL_JIT_OFF_CASE_TEXT,
                                  CONTROL_JIT_OFF_SPACED_CASE_TEXT,
                                  CONTROL_JIT_OFF_DEFAULT_CASE_TEXT)):
        case = _make_synthetic_case(module, text, rel=f"self_test/jit_off_{index}")
        if case.config_errors:
            failures.append(
                f"JIT-off synthetic case {index} config errors: {case.config_errors}")
            continue
        instances = module.build_control_instances(
            case, module.ControlObservation(
                evidence={"compile": ["target"], "exec": ["case.#*#target"],
                          "deopt": [], "gc": []}),
            module.load_opcode_order())
        jit_off = next((instance for instance in instances
                        if instance.class_token == module.CONTROL_CLASS_JIT_OFF), None)
        if jit_off is None:
            failures.append(f"JIT-off synthetic case {index} has no class-1 instance")
            continue
        # The anchor is the first positive JIT-evidence annotation of the
        # source, not the first `COMPILE_COUNT` by keyword priority.
        if jit_off.anchor != "EXEC_COUNT *target == 2":
            failures.append(
                f"JIT-off synthetic case {index}: witness anchor "
                f"{jit_off.anchor!r} is not the first positive JIT evidence")

        variant = module.materialize_variant(case, jit_off, artifacts_root)
        # The production control stage re-parses every materialised variant
        # before it runs it (`run_case_controls`), and only that re-parse puts
        # the case's own `--compiler-enable-jit` tokens into `variant.params`.
        # Reproduce the step here, so the suppression of a case-owned value in
        # `build_vm_command` is actually exercised by this check.
        module.parse_annotations(variant, module.load_opcode_names())
        if variant.config_errors:
            failures.append(
                f"JIT-off synthetic case {index}: the materialised variant has "
                f"config errors: {variant.config_errors}")
            continue
        if not variant.jit_off:
            failures.append(
                f"JIT-off synthetic case {index}: the variant carries no override")
        if variant.rel != f"controls/{case.rel}/{module.CONTROL_VARIANT_JIT_OFF}":
            failures.append(f"unexpected class-1 variant rel: {variant.rel}")
        if variant.source.read_bytes() != case.source.read_bytes():
            failures.append(
                f"JIT-off synthetic case {index}: the variant source is not a "
                f"byte-identical copy of the case")
        if variant.expected.read_bytes() != case.expected.read_bytes():
            failures.append(
                f"JIT-off synthetic case {index}: the variant expectation changed")

        cmd = module.build_vm_command(variant, abc_path, cfg, args)
        if jit_tokens(cmd) != [f"{module.CONTROL_JIT_OPTION}=false"]:
            failures.append(
                f"JIT-off synthetic case {index}: the variant command does not "
                f"name the option exactly once: {jit_tokens(cmd)}")
        if f"{module.CONTROL_JIT_OPTION}=false" not in cmd:
            failures.append(
                f"JIT-off synthetic case {index}: the variant command lost the "
                f"JIT-off value: {cmd}")
        if any(module._option_name(token) == module.CONTROL_JIT_OPTION
               and not token.endswith("=false") for token in cmd):
            failures.append(
                f"JIT-off synthetic case {index}: a contradicting JIT value "
                f"survived: {cmd}")
        # The suppressed option must not leave its separate value behind as a
        # stray positional token. `build_vm_command` appends
        # `--compiler-enable-jit=false` after the case's parameters, so that
        # appended token is always the one directly in front of the entry
        # point; an orphaned value of the space-separated spelling sits one
        # token further left. Inspect the whole parameter region before the
        # entry point, not only its last token.
        entry_index = next(i for i, token in enumerate(cmd)
                           if token.startswith("--entry-point="))
        if cmd[entry_index - 1] != f"{module.CONTROL_JIT_OPTION}=false":
            failures.append(
                f"JIT-off synthetic case {index}: unexpected token before the "
                f"entry point: {cmd[entry_index - 1]!r}")
        orphans = orphaned_value_tokens(
            cmd[:entry_index], case_owned_jit_values(case.params))
        if orphans:
            failures.append(
                f"JIT-off synthetic case {index}: the suppression left the "
                f"case's own separate value token(s) {orphans} for "
                f"{module.CONTROL_JIT_OPTION} in the variant command: {cmd}")

        # The same case without the controls keeps its own command line: the
        # override is mode-local and never leaks into a default run.
        base_cmd = module.build_vm_command(case, abc_path, cfg, args)
        if any(token.endswith("=false") for token in jit_tokens(base_cmd)):
            failures.append(
                f"JIT-off synthetic case {index}: the override leaked into the "
                f"base command: {base_cmd}")
    return failures


def check_control_report_rendering(module) -> List[str]:
    """The mandatory report artifacts carry the S7 fields."""
    failures: List[str] = []
    case = _make_synthetic_case(module, CONTROL_CASE_TEXT)
    observation = _control_observation(module)
    instances = module.build_control_instances(
        case, observation, module.load_opcode_order())
    results = []
    for instance in instances:
        if instance.class_token == module.CONTROL_CLASS_EXPECTATION:
            results.append(module.evaluate_control_instance(
                instance, True,
                ["stdout mismatch:\n--- expected\n+++ actual\n+" + module.CONTROL_SENTINEL]))
        else:
            results.append(module.evaluate_control_instance(
                instance, True, [f'"{instance.anchor}" violated, found 0']))
    case_result = module.evaluate_case_control(case, True, results, True)
    if case_result.verdict != "green":
        failures.append(f"the rendered case is not green: {case_result.reasons}")
    header = module.ControlsHeader(
        True, "x64", "debug", "deadbeef", (case.rel,), (), 1)
    report = module.ControlsReport(header, (case_result,), 12.5, True)
    markdown = module.render_controls_md(report)
    payload = module.render_controls_json(report)
    summary = module.render_summary_controls_section(report)

    for token in module.CONTROL_CLASSES:
        if token not in markdown:
            failures.append(f"controls.md does not name class '{token}'")
    for field in ("Target", "Observation", "Mutation", "Expected", "Actual",
                  "Verdict", "Reason"):
        if field not in markdown:
            failures.append(f"controls.md table lacks the '{field}' column")
    if "source-tree-unchanged: yes" not in markdown:
        failures.append("controls.md lacks the source-tree-unchanged field")
    if module.CONTROL_SENTINEL not in markdown:
        failures.append("controls.md lacks the class-2 target/anchor")

    try:
        document = json.loads(payload)
    except ValueError as error:
        failures.append(f"controls.json is not valid JSON: {error}")
        document = {}
    for key in ("flag", "platform", "mode", "git_head", "include", "exclude",
                "case_count", "source_tree_unchanged", "cases"):
        if key not in document:
            failures.append(f"controls.json lacks the '{key}' header field")
    instance_records = document.get("cases", [{}])[0].get("instances", [])
    if not instance_records:
        failures.append("controls.json carries no instance records")
    else:
        for key in ("class", "target", "observation", "mutation", "expected_class",
                    "actual_classes", "verdict", "reason"):
            if key not in instance_records[0]:
                failures.append(f"controls.json instance lacks the '{key}' field")
    if "## Sensitivity controls" not in summary:
        failures.append("the summary section heading is missing")
    for fragment in ("Cases controlled", "Instances:", "Controls elapsed", "Uncovered"):
        if fragment not in summary:
            failures.append(f"the summary section lacks '{fragment}'")

    # A case can be red while every class verdict is valid (a valid sibling
    # instance of the same class masks an invalid one). The summary must name
    # that case and its reason instead of printing "none" next to a red total
    # (S7: lists every red control verdict with its reason).
    masked_instance = next(
        control_instance for control_instance in instances
        if control_instance.class_token == module.CONTROL_CLASS_COUNT)
    masked_results = list(results) + [
        module.ControlInstanceResult(
            masked_instance, module.CONTROL_VERDICT_INVALID,
            reason="expected failure class missing (violation)")]
    masked_case = module.evaluate_case_control(case, True, masked_results, True)
    if masked_case.verdict != "red":
        failures.append(
            f"the masking case is not red: {masked_case.verdict} "
            f"{masked_case.reasons}")
    if any(control_class.verdict != module.CONTROL_VERDICT_VALID
           for control_class in masked_case.classes
           if control_class.applicable):
        failures.append(
            f"the masking case has a red class verdict: {masked_case.classes}")
    masked_report = module.ControlsReport(header, (masked_case,), 12.5, True)
    masked_summary = module.render_summary_controls_section(masked_report)
    if "Red control verdicts: none" in masked_summary:
        failures.append(
            "the summary reports no red verdict for a red case whose classes "
            "are all valid")
    if masked_case.rel not in masked_summary:
        failures.append("the summary does not name the red case")
    for reason in masked_case.reasons:
        if reason not in masked_summary:
            failures.append(f"the summary drops the red-case reason '{reason}'")

    # A case left uncontrolled by `--stops-on-error` never ran: its base state
    # is unknown and must not be rendered as a failed base run (S7).
    not_run_case = module.not_run_case_control(case)
    not_run_report = module.ControlsReport(header, (not_run_case,), 0.0, True)
    not_run_markdown = module.render_controls_md(not_run_report)
    if "- base run: not run" not in not_run_markdown:
        failures.append(
            "controls.md does not render the unknown base state of a not-run "
            "case")
    if "base run: failed" in not_run_markdown:
        failures.append(
            "controls.md reports a failed base run for a case that never ran")
    not_run_document = json.loads(module.render_controls_json(not_run_report))
    if not_run_document["cases"][0]["base_run"] != "not run":
        failures.append(
            f"controls.json reports a base run of "
            f"{not_run_document['cases'][0]['base_run']!r} for a not-run case")
    if not_run_case.verdict != module.CONTROL_VERDICT_NOT_RUN:
        failures.append(
            f"the not-run verdict changed: {not_run_case.verdict}")
    return failures


# ---------------------------------------------------------------------------
# External corpus class: VM-free checks of selection and resolution (C1/C2)
# ---------------------------------------------------------------------------
# The corpus root is a synthetic temporary directory holding a minimal corpus
# runner module that provides the mandatory API; the class selection, the
# namespace mapping, the usage errors, the bytecode-cache ban and the stage
# order are checked without a build, a VM or the real corpus.

CORPUS_CASES = (
    "weekly_preheat/air",
    "weekly_preheat/bitops",
    "third_lib_js_preheat/Drools_test",
)

# A synthetic corpus runner module: the mandatory API of the real module (its
# own `TestCase`, `CaseKind`, `BuildConfig` with `ark_root`, `discover_cases`,
# `parse_args`, `run_case`, the constants and the crash helpers) and nothing the
# class must not use. `CALLS` records the discovery arguments (the namespace
# mapping), `PARSE_ARGS_CALLS` and `BUILDCONFIG_CALLS`/`DETECT_HOST_CALLS`
# record the reference namespace construction, and `RUN_CALLS` records every
# reference execution with the configuration and the defaults it received. The
# module's own compilation is never reachable from the runner: `run_case`
# returns a synthetic result.
_CORPUS_MODULE_SOURCE = '''\
import argparse
from enum import Enum
from pathlib import Path

TEST_ROOT = Path("__TEST_ROOT__")
BASE_ARGS = ["--asm-interpreter=true", "--compiler-enable-jit=true"]
ALWAYS_ARGS = ["--compiler-arksteed-print-method-name=false"]
EXECUTION_TIMEOUT = 1800
CASES = __CASES__
CALLS = []
RUN_CALLS = []
PARSE_ARGS_CALLS = []
BUILDCONFIG_CALLS = []
DETECT_HOST_CALLS = []
CRASH_SIGNALS = {6: "SIGABRT", 11: "SIGSEGV"}


class CaseKind:
    FILE_INFO = "file_info"
    SINGLE = "single"


class TestCase:
    def __init__(self, rel):
        self.rel = rel
        self.case_dir = TEST_ROOT / rel
        self.source = self.case_dir / "case.js"
        self.entry = rel.split("/")[-1]


class CaseStatus(Enum):
    PASS = "PASS"
    FAIL = "FAIL"
    TIMEOUT = "TIMEOUT"


class CaseResult:
    def __init__(self, status, reasons=None, repro_cmd=None, duration=0.0):
        self.status = status
        self.reasons = list(reasons or [])
        self.repro_cmd = repro_cmd
        self.duration = duration

    @property
    def passed(self):
        return self.status is CaseStatus.PASS


class BuildConfig:
    def __init__(self, mode, platform, ark_root, host=None):
        BUILDCONFIG_CALLS.append((mode, platform, str(ark_root)))
        self.mode = mode
        self.platform = platform
        self.ark_root = Path(ark_root)
        self.host = host

    @property
    def _out_prefix(self):
        return "out/" + self.platform + "." + self.mode

    @property
    def ark_js_vm(self):
        return self.ark_root / self._out_prefix / "arkcompiler/ets_runtime/ark_js_vm"

    @property
    def es2abc(self):
        return self.ark_root / self._out_prefix / "arkcompiler/ets_frontend/es2abc"


def discover_cases(includes, excludes, verbose=False):
    CALLS.append((list(includes), list(excludes), verbose))
    return [TestCase(rel) for rel in CASES]


def compile_single(*args, **kwargs):
    raise AssertionError("compile_single must not run: the class reuses the module's run_case")


def compile_with_file_info(*args, **kwargs):
    raise AssertionError("compile_with_file_info must not run: the class reuses the module's run_case")


def parse_args(argv=None):
    PARSE_ARGS_CALLS.append(list(argv) if argv is not None else None)
    parser = argparse.ArgumentParser()
    parser.add_argument("-R", "--ark-root", type=Path, default=TEST_ROOT.parent)
    parser.add_argument("-p", "--platform", choices=("x64", "arm64"), default=None)
    parser.add_argument("-m", "--mode", choices=("debug", "release"), default="debug")
    parser.add_argument("--timeout", type=int, default=EXECUTION_TIMEOUT)
    parser.add_argument("--hotness-threshold", type=int, default=1)
    parser.add_argument("--log-level", default=None)
    parser.add_argument("--log-components", default=None)
    parser.add_argument("--disable-heap-verify", action="store_true")
    parser.add_argument("--disable-lazy-deopt", action="store_true")
    parser.add_argument("-v", "--verbose", action="store_true")
    return parser.parse_args(argv)


def detect_host_platform():
    DETECT_HOST_CALLS.append(True)
    return "x64"


def is_crash(returncode):
    if returncode is None:
        return False
    if returncode < 0:
        return abs(returncode) in CRASH_SIGNALS
    return returncode in range(128, 160) and (returncode - 128) in CRASH_SIGNALS


def crash_signal_name(returncode):
    if returncode is None:
        return None
    signal_number = abs(returncode) if returncode < 0 else returncode - 128
    return CRASH_SIGNALS.get(signal_number)


def run_case(case, cfg, args, artifacts_root):
    RUN_CALLS.append((case.rel, cfg.mode, cfg.platform, str(cfg.ark_root),
                      args.mode, args.platform, args.timeout,
                      args.hotness_threshold, args.disable_heap_verify,
                      args.disable_lazy_deopt, args.log_level,
                      args.log_components_value, str(artifacts_root)))
    Path(artifacts_root).mkdir(parents=True, exist_ok=True)
    return CaseResult(CaseStatus.PASS, repro_cmd="synthetic reference command",
                      duration=0.25)
'''

# A corpus module that fails loudly at import: it proves that a run without
# `--corpus` never imports the corpus module.
_CORPUS_BOOM_MODULE_SOURCE = '''\
raise RuntimeError("the corpus module must not be imported without --corpus")
'''


def _temp_dir(prefix: str) -> Path:
    tmp_dir = tempfile.TemporaryDirectory(prefix=prefix)
    _TEMP_DIRS.append(tmp_dir)
    return Path(tmp_dir.name)


def _write_corpus_root(cases=None, name: str = "corpus-root",
                       module_source: Optional[str] = None,
                       module_name: str = "run_arksteed_external_tests.py",
                       test_root: Optional[Path] = None,
                       delete_api: Optional[str] = None) -> Path:
    """Create a synthetic corpus root around a minimal runner module."""
    root = _temp_dir("arksteed-self-test-corpus-") / name
    root.mkdir(parents=True)
    if module_source is None:
        module_source = _CORPUS_MODULE_SOURCE
    module_source = (module_source
                     .replace("__TEST_ROOT__", str(test_root or root))
                     .replace("__CASES__", repr(list(cases or []))))
    if delete_api is not None:
        module_source += f"\ndel {delete_api}\n"
    (root / module_name).write_text(module_source, encoding="utf-8")
    return root


def _fake_ark_root(mode: str = "debug", platform_name: str = "x64",
                   with_tools: bool = True) -> Path:
    """A fixture Ark build root with the GN marker and (optionally) both tools."""
    root = _temp_dir("arksteed-self-test-ark-") / "ark-root"
    out = root / "out" / f"{platform_name}.{mode}"
    out.mkdir(parents=True, exist_ok=True)
    if with_tools:
        for rel in ("arkcompiler/ets_runtime/ark_js_vm",
                    "arkcompiler/ets_frontend/es2abc"):
            path = out / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("", encoding="utf-8")
    (out / "args.gn").write_text(
        "ets_runtime_enable_ark_steed = true\n", encoding="utf-8")
    return root


def _fake_embedded_tree() -> Path:
    """A one-case embedded tree, so a default run stays cheap."""
    root = _temp_dir("arksteed-self-test-embedded-") / "test"
    case_dir = root / "self_test" / "embedded"
    case_dir.mkdir(parents=True, exist_ok=True)
    (case_dir / "case.js").write_text("print(1);\n", encoding="utf-8")
    (case_dir / "expected_output.txt").write_text("1\n", encoding="utf-8")
    return root


@contextlib.contextmanager
def _patched(obj, **attributes):
    saved = {name: getattr(obj, name) for name in attributes}
    for name, value in attributes.items():
        setattr(obj, name, value)
    try:
        yield
    finally:
        for name, value in saved.items():
            setattr(obj, name, value)


@contextlib.contextmanager
def _patched_module_attr(module, name, value):
    """Patch a module global that may not exist yet (e.g. the `open` builtin)."""
    sentinel = object()
    saved = module.__dict__.get(name, sentinel)
    module.__dict__[name] = value
    try:
        yield
    finally:
        if saved is sentinel:
            module.__dict__.pop(name, None)
        else:
            module.__dict__[name] = saved


def _call_main(module, argv: List[str], env: Optional[Dict[str, Optional[str]]] = None):
    """Call `main` with captured output and a temporary environment overlay.

    A `None` value removes the variable for the duration of the call.
    """
    saved = {name: os.environ.get(name) for name in (env or {})}
    for name, value in (env or {}).items():
        if value is None:
            os.environ.pop(name, None)
        else:
            os.environ[name] = value
    output = io.StringIO()
    try:
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            returncode = module.main(argv)
    finally:
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
    return returncode, output.getvalue()


def _call_resolve(module, env_value):
    """`resolve_corpus_root` with the corpus variable set to `env_value`."""
    name = module.CORPUS_ROOT_ENV_VAR
    saved = os.environ.get(name)
    if env_value is None:
        os.environ.pop(name, None)
    else:
        os.environ[name] = env_value
    try:
        return module.resolve_corpus_root()
    finally:
        if saved is None:
            os.environ.pop(name, None)
        else:
            os.environ[name] = saved


def _tree_digest(root: Path) -> Dict[str, str]:
    digest: Dict[str, str] = {}
    for path in sorted(root.rglob("*")):
        if path.is_file():
            digest[path.relative_to(root).as_posix()] = hashlib.sha256(
                path.read_bytes()).hexdigest()
    return digest


def _stub_corpus_module(ark_js_vm: Path, es2abc: Path):
    """A module-shaped object whose BuildConfig resolves the given tools."""
    class _Config:
        def __init__(self, mode, platform, ark_root, host=None):
            self.ark_js_vm = Path(ark_js_vm)
            self.es2abc = Path(es2abc)

    return types.SimpleNamespace(BuildConfig=_Config)


def _fail_if_called(name: str):
    def _fail(*args, **kwargs):
        raise AssertionError(f"{name} must not be called without --corpus")
    return _fail


def _raising_path_class(method_name: str, exc: BaseException):
    """A `Path` whose named probe raises `exc`.

    `is_dir()` and `resolve()` can raise `OSError` for a directory whose stat
    the runner user cannot perform or for a symlink loop; resolution must
    report that as the named usage error (C1). The class makes the otherwise
    environment-dependent failure deterministic in a VM-free check.
    """
    base = type(Path())

    def is_dir(self):
        if method_name == "is_dir":
            raise exc
        return base.is_dir(self)

    def resolve(self, *args, **kwargs):
        if method_name == "resolve":
            raise exc
        return base.resolve(self, *args, **kwargs)

    return type("_RaisingPath", (base,), {"is_dir": is_dir, "resolve": resolve})


def check_build_root_override(module) -> List[str]:
    """`ARKSTEED_ARK_ROOT` selects the build tree without changing the default.

    A per-task checkout under `workdirs/<WD>` has no `out/` four levels above
    the test directory; the closure-record selective commands name the
    workspace build context (`builds/<WD>`) in that variable, so the runner
    must resolve it there and keep the historical root when it is unset. This
    is the VM-free half of the R6 reproduction contract: the recorded command
    is executable verbatim from the test directory.
    """
    failures: List[str] = []
    name = module.ARK_ROOT_ENV_VAR
    default_root = SCRIPT_DIR.parents[4]
    saved = os.environ.get(name)
    try:
        os.environ.pop(name, None)
        unset_root = Path(load_runner().ARK_ROOT)
        if unset_root != default_root:
            failures.append(
                "без %s корень сборки не равен историческому %s: %s"
                % (name, default_root, unset_root)
            )
        with tempfile.TemporaryDirectory() as build_root:
            os.environ[name] = build_root
            override_root = Path(load_runner().ARK_ROOT)
            if override_root != Path(build_root):
                failures.append(
                    "%s не задаёт корень сборки: ожидался %s, получен %s"
                    % (name, build_root, override_root)
                )
    finally:
        if saved is None:
            os.environ.pop(name, None)
        else:
            os.environ[name] = saved
    return failures


def check_corpus_root_resolution(module) -> List[str]:
    """(1) Root resolution and the mandatory API of the corpus module."""
    failures: List[str] = []
    env_name = module.CORPUS_ROOT_ENV_VAR
    root = _write_corpus_root(cases=CORPUS_CASES)

    resolved, error = _call_resolve(module, None)
    if resolved is not None or error is None or env_name not in error:
        failures.append(f"an unset variable was not a usage error: {resolved!r} {error!r}")
    resolved, error = _call_resolve(module, "")
    if resolved is not None or error is None or env_name not in error:
        failures.append(f"an empty variable was not a usage error: {resolved!r} {error!r}")
    resolved, error = _call_resolve(module, " ")
    if resolved is not None or error is None or env_name not in error:
        failures.append(f"a blank variable was not a usage error: {resolved!r} {error!r}")
    resolved, error = _call_resolve(module, str(root / "does-not-exist"))
    if resolved is not None or error is None:
        failures.append(f"a nonexistent directory was not a usage error: {resolved!r} {error!r}")

    # An unresolvable value must be the named usage error, never a traceback
    # and never exit code 1: `expanduser()` raises for a `~` prefix naming an
    # unknown user, and the directory probe can raise `OSError`.
    resolved, error = _call_resolve(
        module, f"~arksteed_self_test_no_such_user_{os.getpid()}/corpus")
    if resolved is not None or error is None or env_name not in error \
            or "does not resolve to a directory" not in error:
        failures.append(
            f"an unresolvable `~` value was not a named usage error: {resolved!r} {error!r}")
    for method, exc in (("is_dir", PermissionError(13, "Permission denied")),
                        ("resolve", OSError(40, "Too many levels of symbolic links"))):
        with _patched(module, Path=_raising_path_class(method, exc)):
            resolved, error = _call_resolve(module, str(root))
        if resolved is not None or error is None or env_name not in error:
            failures.append(
                f"a {method}() failure was not a named usage error: {resolved!r} {error!r}")

    resolved, error = _call_resolve(module, str(root))
    if error is not None or resolved != root:
        failures.append(f"the resolved root is not the corpus root: {resolved!r} {error!r}")

    missing_module = _write_corpus_root(cases=[], name="no-module",
                                        module_name="other.py")
    loaded, error = module.load_corpus_module(missing_module)
    if loaded is not None or error is None \
            or module.CORPUS_RUNNER_FILENAME not in error:
        failures.append(
            f"a missing corpus runner module was not a usage error naming it: {error!r}")

    for element in ("discover_cases", "run_case", "crash_signal_name"):
        broken = _write_corpus_root(cases=CORPUS_CASES, name=f"no-{element}",
                                    delete_api=element)
        loaded, error = module.load_corpus_module(broken)
        if loaded is not None or error is None or element not in error:
            failures.append(
                f"the missing API element '{element}' was not a usage error naming it: "
                f"{error!r}")

    mismatch = _write_corpus_root(cases=CORPUS_CASES, name="other-test-root",
                                  test_root=root)
    loaded, error = module.load_corpus_module(mismatch)
    if loaded is not None or error is None or "TEST_ROOT" not in error:
        failures.append(f"a TEST_ROOT mismatch was not a usage error: {error!r}")

    loaded, error = module.load_corpus_module(root)
    if error is not None or loaded is None:
        failures.append(f"the valid corpus module did not load: {error!r}")
    else:
        if Path(loaded.TEST_ROOT) != root:
            failures.append(f"the loaded module TEST_ROOT is {loaded.TEST_ROOT}, expected {root}")
        for element in module.CORPUS_REQUIRED_API:
            if not hasattr(loaded, element):
                failures.append(f"the mandatory API element '{element}' is missing")
    return failures


def check_corpus_class_selection(module) -> List[str]:
    """(2) Class selection, the `corpus/<rel>` namespace and the filters."""
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]

    def select(includes, excludes):
        loaded.CALLS.clear()
        return module.discover_corpus_cases(loaded, root, includes, excludes)

    cases = select([], [])
    if [case.rel for case in cases] != [f"corpus/{rel}" for rel in CORPUS_CASES]:
        failures.append(f"the namespace mapping produced {[c.rel for c in cases]}")
    if any(case.case_class != module.CASE_CLASS_CORPUS for case in cases):
        failures.append(
            f"a selected case is not tagged as the corpus class: "
            f"{[c.case_class for c in cases]}")
    if any(case.corpus_case is None for case in cases):
        failures.append("a selected case does not keep the corpus module TestCase")
    if loaded.CALLS != [([], [], False)]:
        failures.append(f"one unfiltered selection made {loaded.CALLS} discovery call(s)")

    cases = select(["corpus/weekly_preheat"], [])
    if [case.rel for case in cases] != ["corpus/weekly_preheat/air",
                                        "corpus/weekly_preheat/bitops"]:
        failures.append(f"-I corpus/weekly_preheat selected {[c.rel for c in cases]}")
    if loaded.CALLS != [(["weekly_preheat"], [], False)]:
        failures.append(
            f"the namespace segment was not dropped before discovery: {loaded.CALLS}")

    cases = select(["corpus"], [])
    if len(cases) != len(CORPUS_CASES):
        failures.append(f"-I corpus selected {len(cases)} case(s), expected {len(CORPUS_CASES)}")
    if loaded.CALLS != [([], [], False)]:
        failures.append(f"-I corpus mapped to {loaded.CALLS}")

    cases = select(["corpus/*"], [])
    if len(cases) != len(CORPUS_CASES):
        failures.append(f"-I corpus/* selected {len(cases)} case(s)")

    cases = select([], ["corpus/weekly_preheat"])
    if [case.rel for case in cases] != ["corpus/third_lib_js_preheat/Drools_test"]:
        failures.append(f"-X corpus/weekly_preheat selected {[c.rel for c in cases]}")

    cases = select([], ["corpus"])
    if cases:
        failures.append(
            f"-X corpus must exclude the whole class, got {[c.rel for c in cases]}")

    cases = select(["corpus/weekly_preheat"], ["corpus/weekly_preheat/air"])
    if [case.rel for case in cases] != ["corpus/weekly_preheat/bitops"]:
        failures.append(f"the include/exclude combination selected {[c.rel for c in cases]}")

    # The selection stage selects cases only: it never compiles and never runs
    # the module's per-case path (the reference execution belongs to the case
    # loop of the class pipeline).
    if loaded.RUN_CALLS:
        failures.append(
            f"the selection stage ran the module per-case path: {loaded.RUN_CALLS}")

    # Usage errors of the patterns: the class namespace is mandatory with the
    # flag, and a case-class pattern without the flag names the missing flag.
    namespace_error = module.corpus_filter_usage_error(["weekly_preheat"], [], True)
    if namespace_error is None or "corpus/<suite>/<case>" not in namespace_error:
        failures.append(f"a pattern outside the namespace was accepted: {namespace_error!r}")
    namespace_error = module.corpus_filter_usage_error([], ["weekly_preheat"], True)
    if namespace_error is None:
        failures.append("an exclude pattern outside the namespace was accepted")
    flag_error = module.corpus_filter_usage_error(["corpus/weekly_preheat"], [], False)
    if flag_error is None or "--corpus" not in flag_error:
        failures.append(f"a corpus pattern without the flag was accepted: {flag_error!r}")
    flag_error = module.corpus_filter_usage_error([], ["corpus"], False)
    if flag_error is None or "--corpus" not in flag_error:
        failures.append(f"the class pattern without the flag was accepted: {flag_error!r}")
    if module.corpus_filter_usage_error(["corpus/weekly_preheat"], ["corpus/air"], True):
        failures.append("valid namespaced patterns were rejected")

    args = argparse.Namespace(include=[], exclude=[], sensitivity_controls=False)
    if module.corpus_selection_usage_error(args) is not None:
        failures.append("the corpus selection without the control flag was rejected")
    args = argparse.Namespace(include=[], exclude=[], sensitivity_controls=True)
    combined = module.corpus_selection_usage_error(args)
    if combined is None or module.CASE_CLASS_CORPUS not in combined:
        failures.append(f"--corpus with the control flag was accepted: {combined!r}")
    # The guard must not depend on the control flag being part of the parsed
    # options: the corpus class is rejected when the other surface is absent.
    args = argparse.Namespace(include=[], exclude=[])
    if module.corpus_selection_usage_error(args) is not None:
        failures.append("the guard depends on the control flag being present")

    # `--corpus --sensitivity-controls` is rejected by `main` before the build
    # stage; a corpus pattern without the flag is rejected as well.
    ark_root = _fake_ark_root()
    artifacts = _temp_dir("arksteed-self-test-artifacts-")
    events: List[str] = []
    with _patched(module, ARK_ROOT=ark_root,
                  build_ark=lambda cfg, skip: (events.append("build"), True)[1],
                  make_artifacts_root=lambda *a: artifacts):
        rc, output = _call_main(
            module,
            ["-p", "x64", "-m", "debug", "--corpus", "--sensitivity-controls"])
        if rc != 2 or events:
            failures.append(
                f"--corpus with the control flag ran stages: rc={rc} events={events}")
        rc, output = _call_main(
            module, ["-p", "x64", "-m", "debug", "-I", "corpus/weekly_preheat/air"])
        if rc != 2 or "--corpus" not in output or events:
            failures.append(
                f"a corpus pattern without the flag did not name the flag: "
                f"rc={rc} output={output!r}")
        rc, output = _call_main(
            module, ["-p", "x64", "-m", "debug", "--corpus", "-I", "weekly_preheat"])
        if rc != 2 or events:
            failures.append(
                f"a pattern outside the namespace ran stages: rc={rc} events={events}")
    return failures


def check_corpus_root_immutability(module) -> List[str]:
    """(9) The corpus root is read-only and the bytecode ban is restored."""
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    before = _tree_digest(root)
    saved_dont_write_bytecode = sys.dont_write_bytecode

    sys.dont_write_bytecode = False
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        failures.append(f"the synthetic corpus module did not load: {error}")
    if sys.dont_write_bytecode is not False:
        failures.append("the bytecode-cache flag was not restored after the import")
    if loaded is not None:
        module.discover_corpus_cases(loaded, root, ["corpus"], [], False)

    boom = _write_corpus_root(module_source=_CORPUS_BOOM_MODULE_SOURCE)
    loaded, error = module.load_corpus_module(boom)
    if loaded is not None or error is None:
        failures.append("an unimportable corpus module was accepted")
    if sys.dont_write_bytecode is not False:
        failures.append("the bytecode-cache flag was not restored after a failed import")
    sys.dont_write_bytecode = saved_dont_write_bytecode

    after = _tree_digest(root)
    for name in sorted(set(before) | set(after)):
        if before.get(name) != after.get(name):
            failures.append(f"the corpus tree changed: {name}")
    for corpus_root in (root, boom):
        for path in corpus_root.rglob("__pycache__"):
            failures.append(f"a bytecode cache appeared in the corpus tree: {path}")
    return failures


def check_corpus_stage_order(module) -> List[str]:
    """(C1/C2) Resolution before the build stage; the tool check after it."""
    failures: List[str] = []
    ark_root = _fake_ark_root()
    artifacts = _temp_dir("arksteed-self-test-artifacts-")
    env_name = module.CORPUS_ROOT_ENV_VAR
    events: List[str] = []
    real_discover = module.discover_corpus_cases
    real_tools = module.corpus_tools_error
    real_corpus_case = module.run_corpus_case

    def build_recorder(cfg, skip_stub):
        events.append("build")
        return True

    def tools_recorder(corpus_module, cfg):
        events.append("tools")
        return real_tools(corpus_module, cfg)

    def discover_recorder(*args, **kwargs):
        events.append("discover")
        return real_discover(*args, **kwargs)

    def case_recorder(*args, **kwargs):
        events.append("loop")
        return real_corpus_case(*args, **kwargs)

    def run(args, env_value):
        events.clear()
        with _patched(module, ARK_ROOT=ark_root, build_ark=build_recorder,
                      make_artifacts_root=lambda *a: artifacts,
                      corpus_tools_error=tools_recorder,
                      discover_corpus_cases=discover_recorder,
                      run_corpus_case=case_recorder):
            return _call_main(module, args, env={env_name: env_value})

    valid_root = _write_corpus_root(cases=CORPUS_CASES)
    error_cases = [
        ("unset variable", None, env_name),
        ("empty variable", "", env_name),
        ("nonexistent directory", str(artifacts / "missing-root"), None),
        ("missing module", str(_write_corpus_root(cases=[], name="no-module",
                                                  module_name="other.py")),
         module.CORPUS_RUNNER_FILENAME),
        ("missing API element",
         str(_write_corpus_root(cases=CORPUS_CASES, name="no-run-case",
                                delete_api="run_case")), "run_case"),
        ("TEST_ROOT mismatch",
         str(_write_corpus_root(cases=CORPUS_CASES, name="wrong-root",
                                test_root=valid_root)), "TEST_ROOT"),
    ]
    for label, env_value, expected_fragment in error_cases:
        rc, output = run(["-p", "x64", "-m", "debug", "--corpus"], env_value)
        if rc != 2:
            failures.append(f"{label}: expected exit code 2, got {rc} ({output!r})")
        if expected_fragment is not None and expected_fragment not in output:
            failures.append(
                f"{label}: the message does not name '{expected_fragment}': {output!r}")
        if events:
            failures.append(
                f"{label}: the build stage or a later stage ran before the usage "
                f"error: {events}")

    rc, output = run(["-p", "x64", "-m", "debug", "--corpus"], str(valid_root))
    expected_events = ["build", "tools", "discover"] + ["loop"] * len(CORPUS_CASES)
    if events != expected_events:
        failures.append(f"the stage order is {events}, expected {expected_events}")
    if rc != 1:
        failures.append(f"a selected corpus case must be red in this revision, got {rc}")
    if f"corpus/{CORPUS_CASES[0]}" not in output:
        failures.append(f"the selected case identifier is missing from the report: {output!r}")

    # The tool check itself: a corpus BuildConfig that resolves another path or
    # a missing tool is a usage error naming the path.
    with _patched(module, ARK_ROOT=ark_root):
        cfg = module.BuildConfig("debug", "x64")
        if module.corpus_tools_error(_stub_corpus_module(cfg.ark_js_vm, cfg.es2abc), cfg):
            failures.append("matching, existing tools were rejected")
        mismatch = module.corpus_tools_error(
            _stub_corpus_module(cfg.ark_js_vm, cfg.es2abc.parent / "other-es2abc"), cfg)
        if mismatch is None or "es2abc" not in mismatch:
            failures.append(f"a tool path mismatch was not rejected: {mismatch!r}")
    missing_root = _fake_ark_root(with_tools=False)
    with _patched(module, ARK_ROOT=missing_root):
        missing_cfg = module.BuildConfig("debug", "x64")
        missing = module.corpus_tools_error(
            _stub_corpus_module(missing_cfg.ark_js_vm, missing_cfg.es2abc), missing_cfg)
        if missing is None or "does not exist" not in missing:
            failures.append(f"a missing tool was not rejected: {missing!r}")
    return failures


def _corpus_runner_case(module, loaded, rel: str):
    """A selected corpus case object of the synthetic module."""
    return module.TestCase(
        f"{module.CORPUS_NAMESPACE}/{rel}", Path("/nonexistent"),
        Path("/nonexistent") / "expected_output.txt",
        case_class=module.CASE_CLASS_CORPUS, corpus_case=loaded.TestCase(rel))


def check_corpus_reference_execution(module) -> List[str]:
    """(T2) The reference namespace, configuration and run of the module (C7).

    The reference verdict belongs to the corpus runner module: its own
    namespace, its own `BuildConfig`, its own defaults and its own per-case
    path. The class never reimplements them, and no class-side input (a
    descriptor, a case requirement or a class budget) can reach the reference.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    ark_root = _fake_ark_root()
    with _patched(module, ARK_ROOT=ark_root):
        # The reference namespace is the module's own CLI with an explicit
        # empty argv (the runner's command line is never parsed by the module),
        # the derived fields of its `main` and the runner's platform and mode
        # injected after parsing; `fastverify` is what makes the injection
        # necessary, because the module CLI knows only debug|release.
        loaded.PARSE_ARGS_CALLS.clear()
        loaded.DETECT_HOST_CALLS.clear()
        # The cross target is the one that is not the host, so the check stays
        # host-independent; the injected platform/mode is what the runner
        # resolved, not what the module CLI would accept.
        host = module.BuildConfig("debug", "x64").host
        cross_platform = "arm64" if host == "x64" else "x64"
        cross_cfg = module.BuildConfig("fastverify", cross_platform)
        reference_args = module.build_reference_namespace(loaded, cross_cfg)
        if loaded.PARSE_ARGS_CALLS != [[]]:
            failures.append(
                f"the module parser was not called with an explicit empty argv: "
                f"{loaded.PARSE_ARGS_CALLS}")
        if not loaded.DETECT_HOST_CALLS:
            failures.append(
                "the module did not derive the platform when -p was not given")
        if reference_args.platform != cross_platform \
                or reference_args.mode != "fastverify":
            failures.append(
                f"the injected platform/mode did not survive parsing: "
                f"{reference_args.platform}/{reference_args.mode}")
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                loaded.parse_args(["-m", "fastverify"])
            failures.append("the synthetic module CLI accepted fastverify")
        except SystemExit:
            pass
        if cross_cfg.is_cross_build is not True:
            failures.append("the synthetic cross configuration is not a cross build")
        # The module's own defaults and its own budget are preserved; the
        # cross-build multiplier of the class never reaches the reference.
        expected_defaults = (
            ("timeout", loaded.EXECUTION_TIMEOUT),
            ("hotness_threshold", 1),
            ("disable_heap_verify", False),
            ("disable_lazy_deopt", False),
            ("log_level", None),
            ("log_components_value", None),
        )
        for name, expected in expected_defaults:
            actual = getattr(reference_args, name, "<missing>")
            if actual != expected:
                failures.append(
                    f"the reference namespace {name} is {actual!r}, expected {expected!r}")
        # The `--log-components` derived field of the module's `main`.
        original_parse_args = loaded.parse_args

        def parse_with_components(argv=None):
            args = original_parse_args(argv)
            args.log_components = "compiler,gc"
            return args

        loaded.parse_args = parse_with_components
        try:
            derived = module.build_reference_namespace(loaded, cross_cfg)
        finally:
            loaded.parse_args = original_parse_args
        if derived.log_components_value != "compiler:gc":
            failures.append(
                f"the --log-components derived field is {derived.log_components_value!r}")

        # The reference configuration is the module's own BuildConfig class,
        # built from the resolved root, platform and mode with ark_root filled
        # in (the mode is the runner's, so fastverify reaches the reference).
        loaded.BUILDCONFIG_CALLS.clear()
        reference_cfg = module.build_reference_config(loaded, cross_cfg)
        if loaded.BUILDCONFIG_CALLS != [("fastverify", cross_platform, str(ark_root))]:
            failures.append(
                f"the reference BuildConfig was built with {loaded.BUILDCONFIG_CALLS}")
        if not isinstance(reference_cfg, loaded.BuildConfig):
            failures.append("the reference configuration is not the module's own class")
        if str(getattr(reference_cfg, "ark_root", None)) != str(ark_root):
            failures.append(
                f"the reference BuildConfig ark_root is "
                f"{getattr(reference_cfg, 'ark_root', None)!r}, expected {ark_root}")

        # The reference execution: the module's own case object, its own
        # configuration, its own defaults and its own budget, with the
        # module's own reproducer kept in the reference artifacts subtree.
        artifacts = _temp_dir("arksteed-self-test-artifacts-")
        rel = CORPUS_CASES[0]
        case = _corpus_runner_case(module, loaded, rel)
        loaded.RUN_CALLS.clear()
        reference = module.run_reference_case(loaded, case, reference_cfg,
                                              reference_args, artifacts)
        if len(loaded.RUN_CALLS) != 1:
            failures.append(
                f"the reference execution ran {len(loaded.RUN_CALLS)} time(s): "
                f"{loaded.RUN_CALLS}")
        else:
            recorded = loaded.RUN_CALLS[0]
            expected_head = (rel, "fastverify", cross_platform, str(ark_root))
            if recorded[:4] != expected_head:
                failures.append(
                    f"the reference run received {recorded[:4]!r}, expected {expected_head!r}")
            if recorded[4:6] != ("fastverify", cross_platform):
                failures.append(
                    f"the injected platform/mode did not reach the reference: {recorded[4:6]!r}")
            if recorded[6:12] != (loaded.EXECUTION_TIMEOUT, 1, False, False, None, None):
                failures.append(
                    f"the reference run did not keep the module defaults: {recorded[6:12]!r}")
            if module.CORPUS_REFERENCE_ARTIFACTS_DIR not in recorded[12]:
                failures.append(
                    f"the reference artifacts are outside their subtree: {recorded[12]!r}")
            command_file = (Path(recorded[12]) / rel
                            / module.CORPUS_REFERENCE_COMMAND_FILENAME)
            if not command_file.is_file() or "synthetic reference command" not in \
                    command_file.read_text(encoding="utf-8"):
                failures.append(
                    f"the module's own reproducer was not recorded: {command_file}")
        if reference.verdict != module.CORPUS_VERDICT_PASS:
            failures.append(f"the reference verdict is {reference.verdict!r}")
        if reference.command != "synthetic reference command":
            failures.append(f"the reference command is {reference.command!r}")

        # The reference interface takes no descriptor, no case requirement and
        # no class budget, so nothing of the class side can change it.
        expected_params = {"module", "case", "cfg", "reference_args", "artifacts_root"}
        actual_params = set(inspect.signature(module.run_reference_case).parameters)
        if actual_params != expected_params:
            failures.append(
                f"the reference run takes {sorted(actual_params)}, expected "
                f"{sorted(expected_params)}")
        actual_params = set(inspect.signature(module.build_reference_namespace).parameters)
        if actual_params != {"module", "cfg"}:
            failures.append(
                f"the reference namespace takes {sorted(actual_params)}, "
                f"expected ['cfg', 'module']")

        # The reference execution is mandatory and has no switch: a `--corpus`
        # run of a case the class considers red still asks the module for the
        # reference verdict and reports it.
        artifacts = _temp_dir("arksteed-self-test-artifacts-")
        loaded.RUN_CALLS.clear()
        env_name = module.CORPUS_ROOT_ENV_VAR
        with _patched(module, build_ark=lambda cfg, skip: True,
                      make_artifacts_root=lambda *a: artifacts,
                      load_corpus_module=lambda r: (loaded, None)):
            rc, output = _call_main(
                module, ["-p", "x64", "-m", "debug", "--corpus",
                         "-I", f"{module.CORPUS_NAMESPACE}/{rel}"],
                env={env_name: str(root)})
        if rc != 1:
            failures.append(
                f"a corpus case without the class execution must be red, got {rc}")
        if len(loaded.RUN_CALLS) != 1:
            failures.append(
                f"the class branch ran the reference {len(loaded.RUN_CALLS)} time(s), "
                f"expected once: {loaded.RUN_CALLS}")
        if f"reference verdict: {module.CORPUS_VERDICT_PASS}" not in output:
            failures.append(f"the reference verdict is missing from the run: {output!r}")
        if f"corpus/{rel}" not in output:
            failures.append(f"the case identifier is missing from the run: {output!r}")
        if str(root) not in output:
            failures.append(f"the resolved corpus root is missing from the run: {output!r}")

        # The report header data the class fixes: the resolved root, the
        # revision of the corpus and the number of cases of the single
        # discovery call (C7). A run without the flag never adds the field.
        meta_path = artifacts / "run.meta.json"
        try:
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            meta = {}
            failures.append(f"the corpus run metadata was not written: {e}")
        corpus_meta = meta.get("corpus") or {}
        if corpus_meta.get("case_class") != module.CASE_CLASS_CORPUS:
            failures.append(f"the run metadata misses the case class: {corpus_meta!r}")
        if corpus_meta.get("root") != str(root):
            failures.append(f"the run metadata root is {corpus_meta.get('root')!r}")
        if corpus_meta.get("cases") != 1:
            failures.append(f"the run metadata case count is {corpus_meta.get('cases')!r}")
        if "revision" not in corpus_meta:
            failures.append(f"the run metadata misses the corpus revision: {corpus_meta!r}")
    return failures


def check_corpus_verdict_and_parity(module) -> List[str]:
    """(T2) The base verdict rule of the module and the verdict parity (C7).

    The base verdict is classified by the module's own rule (its
    `is_crash`/`crash_signal_name` and the branch order of its `run_case`),
    and parity is defined on that verdict alone: `matched`, `diverged` (red,
    both reproducing commands) or `not-applicable` for a descriptor config
    error. A green case never comes out of a divergence or a failed rule.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]

    def base(**overrides):
        inputs = dict(compile_ok=True, compile_timed_out=False, returncode=0,
                      timed_out=False)
        inputs.update(overrides)
        return module.classify_corpus_base_verdict(loaded, **inputs)

    reason_sets = (
        ("a clean run", {}, module.CORPUS_VERDICT_PASS, None),
        ("a failed compilation", {"compile_ok": False},
         module.CORPUS_VERDICT_FAIL, "compilation failed"),
        ("a compile timeout", {"compile_ok": False, "compile_timed_out": True},
         module.CORPUS_VERDICT_TIMEOUT, "compilation timed out"),
        ("an execution timeout", {"timed_out": True},
         module.CORPUS_VERDICT_TIMEOUT, "execution timed out"),
        ("an unstartable VM", {"returncode": None},
         module.CORPUS_VERDICT_FAIL, "could not be executed"),
        ("SIGSEGV", {"returncode": -11}, module.CORPUS_VERDICT_FAIL, "SIGSEGV"),
        ("the shell return code of SIGABRT", {"returncode": 134},
         module.CORPUS_VERDICT_FAIL, "SIGABRT"),
        ("another nonzero exit code", {"returncode": 1},
         module.CORPUS_VERDICT_FAIL, "return code 1"),
    )
    for label, overrides, expected_verdict, expected_reason in reason_sets:
        verdict, reason = base(**overrides)
        if verdict != expected_verdict:
            failures.append(f"{label}: the base verdict is {verdict!r}, "
                            f"expected {expected_verdict!r}")
        if expected_reason is None:
            if reason is not None:
                failures.append(f"{label}: a passing verdict carried {reason!r}")
        elif reason is None or expected_reason not in reason:
            failures.append(f"{label}: the reason is {reason!r}, "
                            f"expected to name {expected_reason!r}")

    # The branch order of the module's own `run_case`: a compile timeout wins
    # over the crash and execution-timeout inputs, an execution timeout over
    # the crash; a crash is never mistaken for a plain nonzero exit code.
    precedence = (
        ("a compile timeout with a crash",
         {"compile_ok": False, "compile_timed_out": True, "returncode": -11,
          "timed_out": True}, module.CORPUS_VERDICT_TIMEOUT),
        ("an execution timeout with a crash",
         {"timed_out": True, "returncode": -11}, module.CORPUS_VERDICT_TIMEOUT),
    )
    for label, overrides, expected_verdict in precedence:
        verdict, _ = base(**overrides)
        if verdict != expected_verdict:
            failures.append(f"{label}: the base verdict is {verdict!r}, "
                            f"expected {expected_verdict!r}")

    parity_sets = (
        (module.CORPUS_VERDICT_PASS, module.CORPUS_VERDICT_PASS, False,
         module.CORPUS_PARITY_MATCHED),
        (module.CORPUS_VERDICT_FAIL, module.CORPUS_VERDICT_FAIL, False,
         module.CORPUS_PARITY_MATCHED),
        (module.CORPUS_VERDICT_TIMEOUT, module.CORPUS_VERDICT_TIMEOUT, False,
         module.CORPUS_PARITY_MATCHED),
        (module.CORPUS_VERDICT_PASS, module.CORPUS_VERDICT_FAIL, False,
         module.CORPUS_PARITY_DIVERGED),
        (module.CORPUS_VERDICT_FAIL, module.CORPUS_VERDICT_PASS, False,
         module.CORPUS_PARITY_DIVERGED),
        (module.CORPUS_VERDICT_TIMEOUT, module.CORPUS_VERDICT_PASS, False,
         module.CORPUS_PARITY_DIVERGED),
        (module.CORPUS_VERDICT_FAIL, module.CORPUS_VERDICT_PASS, True,
         module.CORPUS_PARITY_NOT_APPLICABLE),
    )
    for base_verdict, reference_verdict, config_error, expected in parity_sets:
        actual = module.classify_parity(base_verdict, reference_verdict, config_error)
        if actual != expected:
            failures.append(
                f"parity {base_verdict}/{reference_verdict} "
                f"(config_error={config_error}) is {actual!r}, expected {expected!r}")

    # A divergence reports both verdicts and both reproducing commands; a
    # matched parity carries no divergence reason at all.
    reasons = module.parity_reasons(module.CORPUS_VERDICT_FAIL, "class-cmd",
                                    module.CORPUS_VERDICT_PASS, "reference-cmd")
    joined = "\n".join(reasons)
    if "parity divergence: class=FAIL reference=PASS" not in joined:
        failures.append(f"the divergence does not name both verdicts: {joined!r}")
    if "class-cmd" not in joined or "reference-cmd" not in joined:
        failures.append(f"the divergence does not carry both commands: {joined!r}")

    # The parity is defined on the verdict alone: stdout is the module's
    # reproducer, never the oracle of the comparison (C7).
    if list(inspect.signature(module.classify_parity).parameters) != \
            ["base_verdict", "reference_verdict", "config_error"]:
        failures.append("the parity classifier does not take exactly the two verdicts")

    # The number of cases and the corpus revision the report fixes (C7).
    if module.corpus_revision(root) is not None:
        failures.append("a root without git metadata reported a revision")
    script_revision = module.corpus_revision(SCRIPT_DIR)
    if script_revision is not None and not re.fullmatch(r"[0-9a-f]{40}", script_revision):
        failures.append(f"the runner revision is not a git revision: {script_revision!r}")
    return failures


def _descriptor_relpath(rel: str) -> str:
    """`corpus/<rel>.annotations` inside a test directory."""
    return f"corpus/{rel}.annotations"


def _write_descriptor(workspace: Path, rel: str, text: str) -> Path:
    """Write `corpus/<rel>.annotations` under a synthetic test directory."""
    path = workspace / _descriptor_relpath(rel)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def check_corpus_descriptor_grammar(module) -> List[str]:
    """(3) The corpus descriptor grammar and its config errors (C5).

    The descriptor is the only annotation channel of a corpus case and reuses
    the existing `//!` grammar plus `TIMEOUT <seconds> <justification>` and
    `NO_EXEC_MARKER <justification>`. Every malformed form is a config error
    that makes the case red, never a silently ignored line, and a
    class-specific key in an embedded case is rejected as well.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    workspace = _temp_dir("arksteed-self-test-descriptors-") / "test"
    workspace.mkdir(parents=True)
    class_options = set(module.corpus_class_defaults(loaded))

    with _patched(module, TEST_DIR=workspace):
        accepted_rel = "weekly_preheat/air"
        descriptor = _write_descriptor(
            workspace, accepted_rel,
            "//! TIMEOUT 60 slow preheat case\n"
            "//! NO_EXEC_MARKER the compiled method is entered once\n"
            "//! PARAMS --compiler-enable-osr=true\n")
        case = _corpus_runner_case(module, loaded, accepted_rel)
        module.parse_corpus_descriptor(case, None, class_options)
        if case.config_errors:
            failures.append(
                f"an accepted descriptor was rejected: {case.config_errors}")
        if case.corpus_timeout_seconds != 60:
            failures.append(
                f"the TIMEOUT budget is {case.corpus_timeout_seconds!r}, expected 60")
        if case.corpus_timeout_reason != "slow preheat case":
            failures.append(
                f"the TIMEOUT justification is {case.corpus_timeout_reason!r}")
        if case.corpus_no_exec_marker_reason != \
                "the compiled method is entered once":
            failures.append(
                f"the NO_EXEC_MARKER justification is "
                f"{case.corpus_no_exec_marker_reason!r}")
        if case.params != ["--compiler-enable-osr=true"]:
            failures.append(f"the descriptor PARAMS are {case.params!r}")
        if case.descriptor_path != descriptor:
            failures.append(
                f"the descriptor path is {case.descriptor_path!r}, "
                f"expected {descriptor!r}")

        rejected = (
            ("unknown keyword", "//! BOGUS foo\n", "unknown annotation keyword"),
            ("method-scoped constraint before METHOD",
             "//! COUNT_LE LoadTaggedField 5\n", "before any METHOD"),
            ("runner-managed PARAMS", "//! PARAMS --log-level=info\n",
             "--log-level"),
            ("class-managed PARAMS",
             "//! PARAMS --compiler-jit-hotness-threshold=2\n",
             "--compiler-jit-hotness-threshold"),
            ("non-numeric TIMEOUT", "//! TIMEOUT soon because\n",
             "positive integer"),
            ("zero TIMEOUT", "//! TIMEOUT 0 because\n", "positive integer"),
            ("negative TIMEOUT", "//! TIMEOUT -5 because\n", "positive integer"),
            ("TIMEOUT without justification", "//! TIMEOUT 60\n",
             "justification"),
            ("NO_EXEC_MARKER without justification", "//! NO_EXEC_MARKER\n",
             "justification"),
        )
        for index, (label, text, expected) in enumerate(rejected):
            rel = f"self_test/rejected_{index}"
            _write_descriptor(workspace, rel, text)
            case = _corpus_runner_case(module, loaded, rel)
            module.parse_corpus_descriptor(case, None, class_options)
            if not case.config_errors:
                failures.append(f"{label}: the descriptor was accepted")
            elif not any(expected in item for item in case.config_errors):
                failures.append(
                    f"{label}: the config error does not name '{expected}': "
                    f"{case.config_errors}")

        # The corpus source is never scanned for `//!` lines: without a
        # descriptor a stray annotation in read-only corpus code is invisible.
        source_rel = "self_test/source_annotation"
        source_dir = root / source_rel
        source_dir.mkdir(parents=True, exist_ok=True)
        (source_dir / "case.js").write_text("//! BOGUS in the corpus source\n",
                                            encoding="utf-8")
        case = _corpus_runner_case(module, loaded, source_rel)
        module.parse_corpus_descriptor(case, None, class_options)
        if case.config_errors:
            failures.append(
                f"the corpus source `//!` line was parsed: {case.config_errors}")
        if case.descriptor_path is not None:
            failures.append(
                f"a missing descriptor set the path {case.descriptor_path!r}")

    # The class-specific keys belong to the corpus descriptor only.
    embedded = _make_case(
        module,
        "//! TIMEOUT 60 slow\n//! NO_EXEC_MARKER exempt\nprint(1);\n")
    if not embedded.config_errors:
        failures.append("an embedded case accepted the class-specific keys")
    else:
        joined = " ".join(embedded.config_errors)
        for keyword in module.CORPUS_DESCRIPTOR_KEYWORDS:
            if keyword not in joined:
                failures.append(
                    f"the embedded rejection does not name {keyword}: {joined!r}")
    return failures


def check_corpus_class_defaults(module) -> List[str]:
    """(4) The derived class defaults and their cross-check (C5).

    The behavioural set is derived from the loaded module (its `BASE_ARGS`,
    its `ALWAYS_ARGS` and its own CLI defaults), never from a literal list, so
    a module change changes the class command line. Before the case loop the
    class compares the reference namespace with its own defaults and rejects a
    non-runner-managed difference with a usage error naming the option.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]

    class_defaults = module.corpus_class_defaults(loaded)
    expected = collections.OrderedDict([
        ("--asm-interpreter", "true"),
        ("--compiler-enable-jit", "true"),
        ("--compiler-jit-hotness-threshold", "1"),
        ("--enable-heap-verify", "true"),
        ("--compiler-enable-jit-lazy-deopt", "true"),
    ])
    if list(class_defaults.items()) != list(expected.items()):
        failures.append(
            f"the derived class defaults are {list(class_defaults.items())}, "
            f"expected {list(expected.items())}")
    if set(class_defaults) & set(module.RUNNER_MANAGED_OPTIONS):
        failures.append(
            f"a runner-managed option is a class default: "
            f"{sorted(set(class_defaults) & set(module.RUNNER_MANAGED_OPTIONS))}")
    expected_tokens = [f"{name}={value}" for name, value in expected.items()]
    if module.corpus_class_command_options(loaded) != expected_tokens:
        failures.append(
            f"the class command tokens are "
            f"{module.corpus_class_command_options(loaded)!r}")

    # The derivation follows the module: changing one of its constants changes
    # the class command line.
    changed = list(loaded.BASE_ARGS) + ["--enable-cmc-gc=false"]
    with _patched(loaded, BASE_ARGS=changed):
        if "--enable-cmc-gc=false" not in module.corpus_class_command_options(loaded):
            failures.append(
                "a changed module BASE_ARGS did not change the class command")

    # The reference namespace of the T2 builder is the behavioural reference
    # of the class defaults; only the runner-managed options may differ.
    host = module.BuildConfig("debug", "x64").host
    cross_platform = "arm64" if host == "x64" else "x64"
    cfg = module.BuildConfig("fastverify", cross_platform)
    reference_args = module.build_reference_namespace(loaded, cfg)
    if module.corpus_defaults_usage_error(loaded, reference_args,
                                          class_defaults) is not None:
        failures.append(
            "the class defaults diverged from the reference namespace: "
            f"{module.corpus_defaults_usage_error(loaded, reference_args, class_defaults)}")
    reference_args.hotness_threshold = 2
    error_text = module.corpus_defaults_usage_error(loaded, reference_args,
                                                    class_defaults)
    if error_text is None or "--compiler-jit-hotness-threshold" not in error_text:
        failures.append(
            f"a changed hotness threshold was not a named usage error: {error_text!r}")
    reference_args.hotness_threshold = 1
    reference_args.disable_heap_verify = True
    error_text = module.corpus_defaults_usage_error(loaded, reference_args,
                                                    class_defaults)
    if error_text is None or "--enable-heap-verify" not in error_text:
        failures.append(
            f"a disabled heap verify was not a named usage error: {error_text!r}")

    # The class defaults are class-specific: the embedded command line never
    # carries a corpus-specific option.
    embedded_names = {module._option_name(token) for token in module.BASE_ARGS}
    corpus_specific = [name for name in class_defaults
                       if name not in embedded_names]
    if not corpus_specific:
        failures.append("the synthetic defaults share every name with the embedded base")
    case = _make_case(module, "print(1);\n")
    args = argparse.Namespace(
        log_debug=False, log_components_value="compiler", print_asm_code=False,
        fully_colored=False, verbose=False, sensitivity_controls=False)
    cmd = module.build_vm_command(case, Path("/tmp/arksteed-self-test.abc"),
                                  module.BuildConfig("debug", "x64"), args)
    command_names = {module._option_name(token) for token in cmd}
    leaked = sorted(name for name in corpus_specific if name in command_names)
    if leaked:
        failures.append(f"the embedded command carries class defaults: {leaked}")

    # The mismatch is a usage error (exit code 2) naming the option before the
    # case loop, not a run that silently diverges.
    ark_root = _fake_ark_root()
    artifacts = _temp_dir("arksteed-self-test-artifacts-")
    workspace = _temp_dir("arksteed-self-test-descriptors-") / "test"
    workspace.mkdir(parents=True)
    perturbed = module.build_reference_namespace(loaded, cfg)
    perturbed.hotness_threshold = 2
    env_name = module.CORPUS_ROOT_ENV_VAR
    with _patched(module, ARK_ROOT=ark_root, TEST_DIR=workspace,
                  build_ark=lambda build_cfg, skip: True,
                  make_artifacts_root=lambda *a: artifacts,
                  load_corpus_module=lambda r: (loaded, None),
                  build_reference_namespace=lambda m, c: perturbed):
        rc, output = _call_main(
            module, ["-p", "x64", "-m", "debug", "--corpus",
                     "-I", f"{module.CORPUS_NAMESPACE}/{CORPUS_CASES[0]}"],
            env={env_name: str(root)})
    if rc != 2:
        failures.append(
            f"a changed reference default must be a usage error, got {rc}: {output!r}")
    if "--compiler-jit-hotness-threshold" not in output:
        failures.append(
            f"the defaults usage error does not name the option: {output!r}")
    return failures


def check_corpus_descriptor_config_error_red(module) -> List[str]:
    """(3) A descriptor config error is red and has no executions (C5, C7)."""
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    ark_root = _fake_ark_root()
    workspace = _temp_dir("arksteed-self-test-descriptors-") / "test"
    rel = CORPUS_CASES[0]
    _write_descriptor(workspace, rel, "//! BOGUS descriptor line\n")
    artifacts = _temp_dir("arksteed-self-test-artifacts-")
    loaded.RUN_CALLS.clear()
    env_name = module.CORPUS_ROOT_ENV_VAR
    with _patched(module, ARK_ROOT=ark_root, TEST_DIR=workspace,
                  build_ark=lambda cfg, skip: True,
                  make_artifacts_root=lambda *a: artifacts,
                  load_corpus_module=lambda r: (loaded, None)):
        rc, output = _call_main(
            module, ["-p", "x64", "-m", "debug", "--corpus",
                     "-I", f"{module.CORPUS_NAMESPACE}/{rel}"],
            env={env_name: str(root)})
    if rc != 1:
        failures.append(
            f"a descriptor config error must be red, got {rc}: {output!r}")
    if "unknown annotation keyword 'BOGUS'" not in output:
        failures.append(f"the config error is missing from the run: {output!r}")
    if loaded.RUN_CALLS:
        failures.append(
            f"a config error case executed the reference: {loaded.RUN_CALLS}")
    if f"parity: {module.CORPUS_PARITY_NOT_APPLICABLE}" not in output:
        failures.append(
            f"the not-applicable parity is missing from the run: {output!r}")
    return failures


def check_corpus_descriptor_isolation(module) -> List[str]:
    """(3) The descriptor tree adds no case to the embedded class."""
    failures: List[str] = []
    test_dir = _fake_embedded_tree()
    _write_descriptor(test_dir, CORPUS_CASES[0], "//! TIMEOUT 60 slow\n")
    _write_descriptor(test_dir, CORPUS_CASES[1], "//! NO_EXEC_MARKER exempt\n")
    with _patched(module, TEST_DIR=test_dir):
        cases = module.discover_cases([], [], False)
    if len(cases) != 1:
        failures.append(
            f"the corpus descriptor tree changed the embedded set: "
            f"{[case.rel for case in cases]}")
    elif cases[0].rel != "self_test/embedded":
        failures.append(f"the embedded case is {cases[0].rel!r}")
    return failures


def check_corpus_default_path_invariance(module) -> List[str]:
    """(10) Without `--corpus` nothing of the class is imported or added."""
    failures: List[str] = []
    ark_root = _fake_ark_root()
    test_dir = _fake_embedded_tree()
    artifacts = _temp_dir("arksteed-self-test-artifacts-")
    boom_root = _write_corpus_root(module_source=_CORPUS_BOOM_MODULE_SOURCE)
    env_name = module.CORPUS_ROOT_ENV_VAR

    with _patched(module, ARK_ROOT=ark_root, TEST_DIR=test_dir,
                  make_artifacts_root=lambda *a: artifacts,
                  build_ark=_fail_if_called("build_ark"),
                  resolve_corpus_root=_fail_if_called("resolve_corpus_root"),
                  load_corpus_module=_fail_if_called("load_corpus_module"),
                  corpus_class_defaults=_fail_if_called("corpus_class_defaults"),
                  parse_corpus_descriptor=_fail_if_called("parse_corpus_descriptor")):
        rc, output = _call_main(
            module, ["-p", "x64", "-m", "debug", "-F"],
            env={env_name: str(boom_root)})
    if rc not in (0, 1):
        failures.append(f"a default run with the corpus variable set returned {rc}")

    # The embedded command line carries no corpus-class option and the case is
    # not tagged with the corpus class.
    case = _make_case(module, "print(1);\n")
    if case.case_class != module.CASE_CLASS_EMBEDDED:
        failures.append(f"a fresh case is tagged '{case.case_class}'")
    args = argparse.Namespace(
        log_debug=False, log_components_value="compiler", print_asm_code=False,
        fully_colored=False, verbose=False, sensitivity_controls=False)
    cfg = module.BuildConfig("debug", "x64")
    cmd = module.build_vm_command(case, Path("/tmp/arksteed-self-test.abc"), cfg, args)
    if any(token == "--corpus" or token.startswith("--corpus=") for token in cmd):
        failures.append(f"the embedded command carries a corpus option: {' '.join(cmd)}")
    return failures


# ---------------------------------------------------------------------------
# External corpus class: the class execution and the JIT-activity invariant
# (design `arksteed-corpus-case-class-and-parity`, C3/C4/C5/C6)
# ---------------------------------------------------------------------------
# The class execution is streamed: the markers are counted while the stream
# passes, the filtered program output and the component lines without the
# execution markers are the only things kept, and the class limits turn an
# overrun into a named red reason instead of a silent pass. The class-level
# JIT-activity invariant is injected into every case requirement set and its
# exemption (`NO_EXEC_MARKER`) is justified and contradiction-checked. All of
# that is exercised on synthetic streams and synthetic executions, without a
# build, a VM or the real corpus.

CORPUS_CLASS_ARGS = argparse.Namespace(
    log_debug=False, log_components_value="compiler", print_asm_code=False,
    fully_colored=False, verbose=False)


def _corpus_stream_lines():
    """A synthetic stream with markers, a CFG dump and program output."""
    return [
        COMPILER + "loaded stub file successfully\n",
        COMPILER + "======== ArkSteedCompilerTask: Starts compiling: foo ========\n",
        COMPILER + ">>> Running method: rec.foo\n",
        COMPILER + ">>> Running method: rec.foo\n",
        COMPILER + "===== Starts Graph Dump =====\n",
        COMPILER + "  v0:  Int32Constant [Int32]  value = 1\n",
        "program line 1\n",
        "program line 2\n",
    ]


def _corpus_stream_case(module, annotations: str = ""):
    """A synthetic corpus case for the streaming consumer checks.

    The class-level invariant is injected exactly as the corpus case loop does
    it, so the streamed counts and the named requirements are evaluated on a
    real case requirement set rather than on a bare fixture.
    """
    case = _make_synthetic_case(module, annotations + "print(1);\n",
                                rel="self_test/corpus_stream")
    module.inject_corpus_invariant(case)
    return case


def check_corpus_streaming_evidence(module) -> List[str]:
    """(5) The streamed evidence pass, its filters and its class limits."""
    failures: List[str] = []
    lines = _corpus_stream_lines()
    case = _corpus_stream_case(module)
    with tempfile.NamedTemporaryFile("wb", delete=False) as log_file:
        log_path = Path(log_file.name)
        evidence = module.consume_corpus_stdout(
            lines, case, ["compiler"], log_file)
    if (evidence.compile_markers, evidence.exec_markers) != (1, 2):
        failures.append(
            f"the streamed counts are {evidence.compile_markers}/"
            f"{evidence.exec_markers}, expected 1/2")
    if evidence.program_output != "program line 1\nprogram line 2\n":
        failures.append(f"the filtered program output is {evidence.program_output!r}")
    if evidence.program_output_truncated:
        failures.append("the program output was truncated below the cap")
    if evidence.record_candidate_capped or evidence.program_output_error is not None:
        failures.append(f"a clean stream is invalid: {evidence!r}")
    if not module.corpus_program_output_usable(evidence):
        failures.append("a clean stream is not usable program output")
    retained = log_path.read_text(encoding="utf-8")
    if ">>> Running method" in retained:
        failures.append("the retained log contains the execution markers")
    if "Starts compiling" not in retained:
        failures.append("the retained log lost the compilation marker")
    if "v0:  Int32Constant" not in retained:
        failures.append("the retained log lost the CFG dump")
    if "program line" in retained:
        failures.append("the retained log contains the program output")
    log_path.unlink(missing_ok=True)

    # An empty stream is zero counts and empty output, never a crash.
    with tempfile.NamedTemporaryFile("wb") as log_file:
        empty = module.consume_corpus_stdout([], case, ["compiler"], log_file)
    if (empty.compile_markers, empty.exec_markers, empty.program_output) != (0, 0, ""):
        failures.append(f"an empty stream produced {empty!r}")
    if not module.corpus_program_output_usable(empty):
        failures.append("a clean empty stream is not a valid empty result")

    # The program-output limit: an overrun is reported, never dropped.
    with tempfile.NamedTemporaryFile("wb") as log_file:
        capped = module.consume_corpus_stdout(
            lines, case, ["compiler"], log_file, program_cap=10)
    if not capped.program_output_truncated:
        failures.append("a program output above the cap was not reported as truncated")
    if module.corpus_program_output_usable(capped):
        failures.append("a truncated program output was certified usable")

    # The log limit is measured over the component lines *without* the
    # execution markers: a marker-dominated stream is not an unavailable log.
    with tempfile.NamedTemporaryFile("wb") as log_file:
        marker_only = module.consume_corpus_stdout(
            [COMPILER + ">>> Running method: a.b\n"] * 100, case, ["compiler"],
            log_file, log_cap=50)
    if marker_only.compiler_log_capped:
        failures.append("the execution markers alone triggered the log limit")
    if marker_only.exec_markers != 100 or marker_only.compiler_log_bytes != 0:
        failures.append(f"the marker-only stream counted {marker_only!r}")
    with tempfile.NamedTemporaryFile("wb") as log_file:
        log_capped = module.consume_corpus_stdout(
            [COMPILER + f"dump line {i}\n" for i in range(100)], case,
            ["compiler"], log_file, log_cap=50)
    if not log_capped.compiler_log_capped:
        failures.append("the non-marker log volume did not trigger the log limit")
    return failures


def _corpus_case_with_invariant(module, loaded, rel: str):
    """A synthetic corpus case with the class-level invariant injected."""
    case = _corpus_runner_case(module, loaded, rel)
    module.inject_corpus_invariant(case)
    return case


def _corpus_class_execution(module, command=("ark_js_vm", "case.abc"), **overrides):
    """A synthetic class execution of a fully active case by default."""
    overrides.setdefault("compile_markers", 1)
    overrides.setdefault("exec_markers", 1)
    evidence = module.CorpusStreamEvidence()
    for name in ("compile_markers", "exec_markers", "deopt_markers", "gc_markers",
                 "program_output", "program_output_truncated",
                 "record_candidate_capped", "program_output_error",
                 "compiler_log_bytes", "compiler_log_capped"):
        if name in overrides:
            setattr(evidence, name, overrides.pop(name))
    for constraint in overrides.pop("named_counts", ()):
        evidence.named_counts[constraint] = 1
    overrides.setdefault("returncode", 0)
    return module.CorpusClassExecution(
        command=list(command), evidence=evidence, **overrides)


def check_corpus_invariant(module) -> List[str]:
    """(6) The injected JIT-activity invariant and its justified exemption."""
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    rel = CORPUS_CASES[0]
    case = _corpus_case_with_invariant(module, loaded, rel)
    labels = [constraint.label() for constraint in case.evidence_constraints]
    for expected in ("COMPILE_COUNT_GE * >= 1", "EXEC_COUNT_GE * >= 1"):
        if expected not in labels:
            failures.append(f"the injection did not add '{expected}': {labels}")
    if not module._case_has_evidence(case, module.ExecCountGeConstraint):
        failures.append("the injection did not imply the execution marker")
    cmd, _ = module.build_corpus_vm_command(
        case, Path("/tmp/arksteed-self-test.abc"), module.BuildConfig("debug", "x64"),
        CORPUS_CLASS_ARGS, loaded)
    if f"{module.CORPUS_EXEC_MARKER_OPTION}=true" not in cmd:
        failures.append(f"the class command misses the execution marker: {' '.join(cmd)}")
    if any(f"{module.CORPUS_EXEC_MARKER_OPTION}=false" in token for token in cmd):
        failures.append("the class command disables the execution marker")

    # Each streamed evidence set is evaluated by the same rule as the embedded
    # annotations: markers satisfy the requirements, zero markers violate them
    # with the actual count.
    verdict = module.evaluate_corpus_class_run(
        case, loaded, compile_ok=True, compile_message="", compile_timed_out=False,
        execution=_corpus_class_execution(module), reference_verdict="PASS",
        valid_opcodes=None)
    if not verdict.passed or verdict.invariant_state != module.CORPUS_INVARIANT_BOTH:
        failures.append(f"a fully active case is not green: {verdict!r}")
    verdict = module.evaluate_corpus_class_run(
        case, loaded, compile_ok=True, compile_message="", compile_timed_out=False,
        execution=_corpus_class_execution(module, compile_markers=0, exec_markers=0),
        reference_verdict="PASS", valid_opcodes=None)
    if verdict.passed:
        failures.append("a case with zero compilation markers is green")
    if '"COMPILE_COUNT_GE * >= 1" violated, found 0' not in verdict.reasons:
        failures.append(f"the zero-compilation violation is not named: {verdict.reasons}")
    if verdict.invariant_state != module.CORPUS_INVARIANT_NONE:
        failures.append(f"the zero-activity state is {verdict.invariant_state!r}")
    verdict = module.evaluate_corpus_class_run(
        case, loaded, compile_ok=True, compile_message="", compile_timed_out=False,
        execution=_corpus_class_execution(module, compile_markers=1, exec_markers=0),
        reference_verdict="PASS", valid_opcodes=None)
    if '"EXEC_COUNT_GE * >= 1" violated, found 0' not in verdict.reasons:
        failures.append(f"the zero-execution violation is not named: {verdict.reasons}")
    if verdict.invariant_state != module.CORPUS_INVARIANT_COMPILE_ONLY:
        failures.append(f"the compile-only state is {verdict.invariant_state!r}")

    # The justified exemption lowers only the execution requirement, requires
    # its justification and stays checkable: an observed marker makes the
    # declaration a red contradiction.
    exempt = _corpus_runner_case(module, loaded, rel)
    exempt.corpus_no_exec_marker_reason = "the compiled code is never entered"
    module.inject_corpus_invariant(exempt)
    exempt_labels = [c.label() for c in exempt.evidence_constraints]
    if any("EXEC_COUNT_GE" in label for label in exempt_labels):
        failures.append(f"the exemption kept the execution requirement: {exempt_labels}")
    if "COMPILE_COUNT_GE * >= 1" not in " ".join(exempt_labels):
        failures.append(f"the exemption dropped the compilation requirement: {exempt_labels}")
    clean = module.evaluate_corpus_class_run(
        exempt, loaded, compile_ok=True, compile_message="", compile_timed_out=False,
        execution=_corpus_class_execution(module, compile_markers=1, exec_markers=0),
        reference_verdict="PASS", valid_opcodes=None)
    if not clean.passed:
        failures.append(f"a justified exemption is red: {clean.reasons}")
    if clean.invariant_state != \
            f"{module.CORPUS_INVARIANT_COMPILE_ONLY} " \
            f"(exempt: {exempt.corpus_no_exec_marker_reason})":
        failures.append(f"the exemption state is {clean.invariant_state!r}")
    contradicting = module.evaluate_corpus_class_run(
        exempt, loaded, compile_ok=True, compile_message="", compile_timed_out=False,
        execution=_corpus_class_execution(module, compile_markers=1, exec_markers=7),
        reference_verdict="PASS", valid_opcodes=None)
    if contradicting.passed:
        failures.append("a contradiction with the observed markers is green")
    if "NO_EXEC_MARKER declared, but 7 execution marker(s) observed" \
            not in contradicting.reasons:
        failures.append(
            f"the contradiction is not named: {contradicting.reasons}")
    return failures


def check_corpus_class_execution_verdict(module) -> List[str]:
    """(5/6/7) The class verdict, its limits and the class budget."""
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    rel = CORPUS_CASES[0]
    case = _corpus_case_with_invariant(module, loaded, rel)

    # A failed compilation has no execution, its base verdict follows the
    # module rule and the parity is still classified.
    verdict = module.evaluate_corpus_class_run(
        case, loaded, compile_ok=False, compile_message="es2abc exited with 1",
        compile_timed_out=False, execution=None, reference_verdict="PASS",
        valid_opcodes=None)
    if verdict.base_verdict != module.CORPUS_VERDICT_FAIL:
        failures.append(f"a failed compilation gave {verdict.base_verdict!r}")
    if not any("compilation failed" in reason for reason in verdict.reasons):
        failures.append(f"the compilation failure is not named: {verdict.reasons}")
    if verdict.parity != module.CORPUS_PARITY_DIVERGED:
        failures.append(f"the failed-compilation parity is {verdict.parity!r}")
    verdict = module.evaluate_corpus_class_run(
        case, loaded, compile_ok=False, compile_message="es2abc timed out",
        compile_timed_out=True, execution=None, reference_verdict="TIMEOUT",
        valid_opcodes=None)
    if verdict.base_verdict != module.CORPUS_VERDICT_TIMEOUT:
        failures.append(f"a compile timeout gave {verdict.base_verdict!r}")

    # The class limits are red with the named reason, never a skip.
    verdict = module.evaluate_corpus_class_run(
        case, loaded, compile_ok=True, compile_message="", compile_timed_out=False,
        execution=_corpus_class_execution(module, program_output_truncated=True),
        reference_verdict="PASS", valid_opcodes=None)
    if "program output exceeds cap" not in verdict.reasons:
        failures.append(f"the program-output overrun is not named: {verdict.reasons}")
    method_case = _corpus_case_with_invariant(module, loaded, "weekly_preheat/bitops")
    method_case.constraints["foo"] = [module.HasConstraint("CallRuntime", "x")]
    verdict = module.evaluate_corpus_class_run(
        method_case, loaded, compile_ok=True, compile_message="",
        compile_timed_out=False,
        execution=_corpus_class_execution(module, compiler_log_capped=True),
        reference_verdict="PASS", valid_opcodes=None)
    if "constraint evidence unavailable: compiler log exceeds cap" \
            not in verdict.reasons:
        failures.append(
            f"the capped constraint evidence is not named: {verdict.reasons}")

    # The class budget: the module's own budget, the cross-build multiplier
    # and the justified descriptor override.
    cfg = module.BuildConfig("debug", "x64")
    budget, reason = module.corpus_class_budget(loaded, case, cfg)
    if budget != loaded.EXECUTION_TIMEOUT or reason is not None:
        failures.append(f"the class budget is {budget!r}/{reason!r}")
    host = cfg.host
    cross_platform = "arm64" if host == "x64" else "x64"
    cross_cfg = module.BuildConfig("debug", cross_platform)
    budget, _ = module.corpus_class_budget(loaded, case, cross_cfg)
    if budget != loaded.EXECUTION_TIMEOUT * module.QEMU_TIMEOUT_FACTOR:
        failures.append(f"the cross class budget is {budget!r}")
    case.corpus_timeout_seconds = 1200
    case.corpus_timeout_reason = "a slow preheat case"
    budget, reason = module.corpus_class_budget(loaded, case, cross_cfg)
    if (budget, reason) != (1200, "a slow preheat case"):
        failures.append(f"the descriptor budget override is {budget!r}/{reason!r}")

    # The class command: the derived defaults exactly once, the descriptor
    # options and the marker implied by the invariant; the module's own
    # observation-off value never leaks into it.
    plain = _corpus_case_with_invariant(module, loaded, rel)
    plain.params = ["--compiler-enable-osr=true"]
    cmd, components = module.build_corpus_vm_command(
        plain, Path("/tmp/arksteed-self-test.abc"), cfg, CORPUS_CLASS_ARGS, loaded)
    for name, value in module.corpus_class_defaults(loaded).items():
        token = f"{name}={value}"
        if cmd.count(token) != 1:
            failures.append(f"the class command carries '{token}' {cmd.count(token)} time(s)")
    if "--compiler-enable-osr=true" not in cmd:
        failures.append(f"the descriptor option is missing from the class command: {cmd}")
    if components != ["compiler"]:
        failures.append(f"the class components are {components!r}")

    # A declared BYTECODE_COUNT requirement is evaluated against the class
    # disassembly; an unavailable disassembly is a named red reason, never a
    # silently skipped requirement.
    bytecode_case = _corpus_case_with_invariant(module, loaded, rel)
    bytecode_case.bytecode_constraints["foo"] = [
        module.BytecodeCountConstraint("ldai", None, 1)]
    bare_root = _fake_ark_root(with_tools=True)
    bytecode_artifacts = _temp_dir("arksteed-self-test-artifacts-")
    with _patched(module, ARK_ROOT=bare_root):
        bare_cfg = module.BuildConfig("debug", "x64")
        bytecode_reasons = module.corpus_bytecode_constraint_reasons(
            bytecode_case, Path("/nonexistent.abc"), bare_cfg, bytecode_artifacts,
            "case", [])
    if not bytecode_reasons or "bytecode constraints unavailable" \
            not in bytecode_reasons[0]:
        failures.append(
            f"an unavailable bytecode evidence is not named: {bytecode_reasons}")

    # The class verdict end to end through `main`, which injects the invariant
    # into the selected cases before the loop: a marker-free execution is red
    # with the named reason, a fully active one is green.
    ark_root = _fake_ark_root()
    artifacts = _temp_dir("arksteed-self-test-artifacts-")
    env_name = module.CORPUS_ROOT_ENV_VAR
    for markers, expected_rc in (((1, 1), 0), ((0, 0), 1)):
        compile_markers, exec_markers = markers

        def fake_compile(*args, **kwargs):
            return True, "", False, []

        def fake_execute(*args, **kwargs):
            return _corpus_class_execution(
                module, compile_markers=compile_markers, exec_markers=exec_markers)

        with _patched(module, ARK_ROOT=ark_root, build_ark=lambda build_cfg, skip: True,
                      make_artifacts_root=lambda *a: artifacts,
                      load_corpus_module=lambda r: (loaded, None),
                      compile_corpus_case=fake_compile,
                      run_corpus_class_execution=fake_execute):
            rc, output = _call_main(
                module, ["-p", "x64", "-m", "debug", "--corpus",
                         "-I", f"{module.CORPUS_NAMESPACE}/{rel}"],
                env={env_name: str(root)})
        if rc != expected_rc:
            failures.append(
                f"a class execution with markers {markers} returned {rc}: {output!r}")
        if expected_rc == 1 and '"COMPILE_COUNT_GE * >= 1" violated, found 0' \
                not in output:
            failures.append(f"the named invariant violation is missing: {output!r}")
    return failures


def check_corpus_constraint_evidence_availability(module) -> List[str]:
    """(S3/S5) A failed collection never certifies the retained compiler log.

    A method-scoped (`IR`/`codegen`) constraint is evaluated over the retained
    component log. That log describes a complete compilation only when the
    collection completed without a collection failure: an interrupted read
    ends the open CFG dump at EOF, and `iter_graph_dumps` would then read the
    missing remainder as proof that an opcode is absent, turning unavailable
    evidence into a false constraint result. The program-output and candidate
    caps are independent: they do not interrupt observation, so a complete
    retained log stays usable for the constraint evaluation.
    """
    failures: List[str] = []
    case = _filter_case(module, "//! METHOD foo\n//! COUNT_LE I32Add 0\n",
                        "self_test/constraint_availability")
    compile_marker = (COMPILER + "======== ArkSteedCompilerTask: Starts "
                      "compiling: foo ========\n")
    graph_start = module.GRAPH_DUMP_START + "\n"
    vertex = COMPILER + "v0: Int32Constant 1 (0x1)\n"
    graph_end = module.GRAPH_DUMP_END + "\n"

    def _execution(evidence, log_path):
        return module.CorpusClassExecution(
            command=["ark_js_vm", "case.abc"], components=["compiler"],
            returncode=0, timed_out=False, started=True, evidence=evidence,
            compiler_log_path=log_path, duration=0.0)

    def _collect(chunks, path, **kwargs):
        with open(path, "wb") as log_file:
            return module.consume_corpus_stdout(
                chunks, case, ["compiler"], log_file, **kwargs)

    def _unavailable(reasons) -> bool:
        return any("constraint evidence unavailable" in reason
                   for reason in reasons)

    with tempfile.TemporaryDirectory(prefix="arksteed-self-test-") as tmp:
        # A complete retained log is evaluated normally, even when the final
        # program output overflowed its independent cap: the truncated
        # *output* does not make the complete *component log* unavailable.
        path = os.path.join(tmp, "complete.compiler.log")
        evidence = _collect(
            [compile_marker, graph_start, vertex, graph_end, "program output\n"],
            path, program_cap=1)
        if not evidence.program_output_truncated:
            failures.append("the small program cap did not truncate the output")
        reasons = module.corpus_log_constraint_reasons(
            case, _execution(evidence, path), None)
        if _unavailable(reasons):
            failures.append(
                "the independent output cap made the complete retained log "
                f"unavailable: {reasons}")
        if reasons:
            failures.append(
                f"the complete retained log did not evaluate the constraint: {reasons}")

        # A read failure mid-graph leaves the open CFG dump incomplete: the
        # missing remainder must never be read as proof that I32Add is absent.
        def failing_chunks():
            yield compile_marker
            yield graph_start
            yield vertex
            raise OSError("injected missing remainder of compiler graph")

        path = os.path.join(tmp, "read.compiler.log")
        evidence = _collect(failing_chunks(), path)
        if evidence.program_output_error is None:
            failures.append("the injected read failure is not named by the collector")
        reasons = module.corpus_log_constraint_reasons(
            case, _execution(evidence, path), None)
        if not _unavailable(reasons):
            failures.append(
                "a read failure still certified the retained constraint "
                f"evidence: {reasons}")
        if not any("program output collection failed: read" in reason
                   for reason in reasons):
            failures.append(
                f"the read failure is not named in the constraint reason: {reasons}")

        # A retained-log sink failure is a collection failure as well.
        class _BrokenSink:
            """A retained-log sink whose writes fail from the first line on."""

            def write(self, data):
                raise OSError("sink boom")

            def close(self):
                return None

        evidence = module.consume_corpus_stdout(
            [compile_marker, graph_start, vertex, graph_end], case,
            ["compiler"], _BrokenSink())
        if evidence.program_output_error is None:
            failures.append("the retained-log sink failure is not named")
        reasons = module.corpus_log_constraint_reasons(
            case, _execution(evidence, os.path.join(tmp, "sink.compiler.log")), None)
        if not _unavailable(reasons):
            failures.append(
                "a retained-log sink failure still certified the retained "
                f"constraint evidence: {reasons}")

        # The candidate cap is a sticky flag on a complete collection, so it
        # must not make a complete retained log unavailable.
        path = os.path.join(tmp, "candidate.compiler.log")
        evidence = _collect(
            [compile_marker, graph_start, vertex, graph_end, "program output\n"],
            path)
        evidence.record_candidate_capped = True
        reasons = module.corpus_log_constraint_reasons(
            case, _execution(evidence, path), None)
        if _unavailable(reasons):
            failures.append(
                "the independent candidate cap made the complete retained log "
                f"unavailable: {reasons}")
        if reasons:
            failures.append(
                "the candidate cap changed the evaluated constraints: "
                f"{reasons}")
    return failures


# ---------------------------------------------------------------------------
# The shared filter chain, the exact streaming oracle and the evidence
# validity contract (design `arksteed-corpus-filter-and-consumer-integration`,
# S1-S5). Everything below is VM-free: no build, no `ark_js_vm` and no real
# corpus.
# ---------------------------------------------------------------------------

FILTER_ANNOTATIONS = (
    "",
    "//! GC_COUNT_GE * 1\n",
    "//! DEOPT_COUNT *Deoptimize* 0\n",
    "//! GC_COUNT_GE * 1\n//! DEOPT_COUNT *Deoptimize* 0\n",
    "//! DEOPT_COUNT_GE *Deoptimize* 0\n//! GC_COUNT_GE * 1\n",
)

FILTER_COMPONENTS = (
    [],
    ["compiler"],
    ["gc", "compiler"],
    ["compiler", "gc"],
    ["compiler", "compiler"],
)

# The mandatory counterexamples of the design (S2/F2/F3). The expected value is
# the exact sequential-substitution result, not a line count or a verdict.
MANDATORY_FILTER_COUNTEREXAMPLES = (
    ("", ["compiler", "gc"], "[g[compiler] diagnostic\nc] diagnostic\n", ""),
    ("//! DEOPT_COUNT *Deoptimize* 0\n", ["compiler"],
     "[compiler] Deoptimize at first\n[compiler] Deoptimize at second\n"
     "    at frame\n\nOK\n", "OK\n"),
    ("//! DEOPT_COUNT *Deoptimize* 0\n", ["compiler"],
     "[compiler] Deoptimize\n at first\n    at frame\n\nOK\n", "OK\n"),
    ("", ["compiler"], "head [compiler] logged\ntail\n", "head tail\n"),
)

FILTER_EDGE_TEXTS = (
    "start\n[gc] headline GCReason: reason\nfirst verbose block\n"
    "second verbose block\n[gc] HandleStorage: 1\nend\n",
    "[gc] headline GCReason: reason\nno closer at all\n",
    "[gc] headline GCReason: reason\n[gc] HandleStorage: 1",
    "[compiler] Deoptimize    at a (x.js:1:1)\n\n",
    "[compiler] Lazy Deoptimize\tat b (x.js:2:1)\n    at c (x.js:3:1)\n\n",
    "[compiler] Deoptimize\n\n",
    # The opener and the frame literal both carry a mandatory trailing space;
    # a boundary that does not follow it is not a record.
    "[compiler] Deoptimize atX\n\nOK\n",
    "[compiler] Deoptimize at first\n    atX\n\nOK\n",
    "[compiler] Deoptimize at\n at first\n    at frame\n\nOK\n",
    "[compiler] Deoptimize at first\n    at frame\n    atX\n\nOK\n",
    "[compiler] Deoptimize at first\n    at frame\n    at \n\nOK\n",
    "[compiler] Deoptimize\ratX\n\nOK\n",
    "[compiler] Deoptimize\r at d (x.js:4:1)\n\n",
    "[compiler] Deoptimize\u00a0at e (x.js:5:1)\n\n",
    "[compiler] Eager deopt disables GC; skip GC-capable trace.\n",
    "[g[compiler] x\nc] y\n",
    "pre [compiler] a\npost [compiler] b\n",
    "[compiler] ======== ArkSteedCompilerTask: Starts compiling: m ========\n",
    "[compiler] >>> Running method: rec.m\n",
    "no markers here\nat all\n",
    "[gc] HandleStorage: 1\n[gc] headline GCReason: r\n[gc] HandleStorage: 2\n",
    "repeat [gc] not a record\n",
    "",
)

FILTER_MIXTURE_PIECES = (
    "[compiler] ", "[gc] ", "GCReason: 1\n", "[gc] HandleStorage: 3\n",
    "Deoptimize    at f (a.js:1:1)\n", "Lazy Deoptimize  at g (a.js:2:1)\n",
    "    at frame (a.js:3:1)\n", "    at x\n", "\n", "OK\n", "x", "yy\n",
    "[g", "c]", "compiler] ", "Eager deopt disables GC; skip\n", "at ", "Deop",
    "Deoptimize\n", " at first\n", "\r\n", "\t", "\u00a0at ", "  ", "\n\n",
    "[compiler] >>> Running method: a.b\n",
    "[compiler] ======== ArkSteedCompilerTask: Starts compiling: nm ========\n",
)


def _filter_case(module, annotations: str, rel: str):
    """A synthetic case whose only annotations are the given evidence lines."""
    return _make_synthetic_case(module, annotations + "print(1);\n", rel=rel)


def _oracle_filter(module, case, components, text: str) -> str:
    """Sequential empty `sub` of every pattern of the canonical chain."""
    filtered = text
    for pattern in module.case_filter_chain(case, components):
        filtered = pattern.sub("", filtered)
    return filtered


def _chunk_partitions(text: str):
    """Several exact partitions of the same decoded text (S2/S5)."""
    yield [text]
    yield list(text)
    yield [text[index:index + 3] for index in range(0, len(text), 3)]
    yield [text[index:index + 7] for index in range(0, len(text), 7)]
    pieces: List[str] = []
    index = 0
    step = 1
    while index < len(text):
        pieces.append(text[index:index + step])
        index += step
        step = step % 4 + 1
    yield pieces
    yield [""] + [text[offset:offset + 2] for offset in range(0, len(text), 2)] + [""]


def _consume(module, case, components, chunks, **kwargs):
    with tempfile.NamedTemporaryFile("wb") as log_file:
        return module.consume_corpus_stdout(chunks, case, components, log_file,
                                            **kwargs)


def check_corpus_filter_chain_contract(module) -> List[str]:
    """(S1) The exact ordered chain, its purity and the named config error."""
    failures: List[str] = []
    for annotations in FILTER_ANNOTATIONS:
        for components in FILTER_COMPONENTS:
            case = _filter_case(module, annotations, "self_test/chain")
            chain = module.case_filter_chain(case, components)
            expected = []
            if module._case_has_evidence(case, module.GcCountGeConstraint):
                expected.append(module.GC_TRACE_LOG_RE)
            if module._needs_deopt_trace(case):
                expected.append(module.DEOPT_TRACE_LOG_RE)
            for name in list(components) + module._implied_log_components(case):
                expected.append(
                    re.compile(r"\[%s\] [^\n]*\n?" % re.escape(name)))
            if [p.pattern for p in chain] != [p.pattern for p in expected]:
                failures.append(
                    f"chain order for {annotations!r}/{components!r}: "
                    f"{[p.pattern for p in chain]}")
            if [p.flags for p in chain] != [p.flags for p in expected]:
                failures.append(
                    f"chain flags for {annotations!r}/{components!r}")
            # Purity: equal inputs give equivalent lists and the inputs are not
            # mutated.
            before_annotations = list(case.evidence_constraints)
            before_components = list(components)
            again = module.case_filter_chain(case, components)
            if [p.pattern for p in again] != [p.pattern for p in chain] \
                    or [p.flags for p in again] != [p.flags for p in chain]:
                failures.append("two equal calls gave different chains")
            if list(case.evidence_constraints) != before_annotations \
                    or list(components) != before_components:
                failures.append("case_filter_chain mutated its inputs")

    # A valid case without any stage yields the exact empty chain and an
    # identical comparison; that is not a configuration error (C12).
    plain = _filter_case(module, "", "self_test/chain_plain")
    if module.case_filter_chain(plain, []) != []:
        failures.append("a valid case without stages did not give an empty chain")
    if _oracle_filter(module, plain, [], "unchanged\n") != "unchanged\n":
        failures.append("the empty chain changed the text")
    if module.compare_output("OK\n", plain.expected, plain, []) is not None:
        failures.append("the empty chain comparison is not identity")
    if module.compare_output("other\n", plain.expected, plain, []) is None:
        failures.append("the empty chain comparison accepted different text")

    class _NoEvidence:
        rel = "self_test/no-evidence"

    for bad in (None, 5, [None], [""], [3]):
        try:
            module.case_filter_chain(plain, bad)
        except module.FilterConfigurationError as error:
            if not str(error).startswith("filter configuration error: "):
                failures.append(f"the named config error is {str(error)!r}")
        except Exception as error:  # noqa: BLE001
            failures.append(f"components={bad!r} raised {type(error).__name__}")
        else:
            failures.append(f"components={bad!r} was accepted")
    try:
        module.case_filter_chain(_NoEvidence(), [])
    except module.FilterConfigurationError as error:
        if "filter configuration error: " not in str(error):
            failures.append(f"the missing-evidence error is {str(error)!r}")
    else:
        failures.append("a case without an evidence collection was accepted")

    # `compare_output` reports the configuration error as a diagnostic and
    # never as a match, even with an empty expectation.
    diff = module.compare_output("", plain.expected, _NoEvidence(), [])
    if diff is None or "filter configuration error: " not in diff:
        failures.append(f"compare_output did not name the config error: {diff!r}")
    return failures


def check_corpus_component_input_snapshot(module) -> List[str]:
    """(S1/review) The collector materializes the ordered explicit components.

    `consume_corpus_stdout` receives the canonical input, an ordered sequence
    of explicit component names, and that sequence may be a one-shot iterable.
    It materializes and validates the input exactly once, so chain
    construction, component-name pairing and raw observation all see the same
    stable ordered snapshot with its repetitions preserved, instead of the
    exhausted iterable giving a false `filter configuration error`.
    """
    failures: List[str] = []
    case = _filter_case(module, "//! GC_COUNT_GE * 1\n",
                        "self_test/component_input")
    text = "[compiler] diag\n[gc] diag\nOK\n"
    for label, components in (
            ("one-shot iterable", ["compiler"]),
            ("one-shot repetitions", ["compiler", "compiler"]),
            ("explicit plus implied repetition", ["gc", "compiler"])):
        expected = _oracle_filter(module, case, list(components), text)
        evidence = _consume(module, case, iter(components), [text])
        if evidence.program_output != expected:
            failures.append(
                f"the collector with the {label} gave "
                f"{evidence.program_output!r}, expected {expected!r}")
        if not module.corpus_program_output_usable(evidence):
            failures.append(
                f"the collector with the {label} produced unusable evidence: "
                f"{evidence!r}")
        names = module.case_filter_component_names(case, list(components))
        if names != list(components) + module._implied_log_components(case):
            failures.append(
                f"the {label} name pairing is {names!r}")
    # An unsupported input keeps the named configuration error instead of
    # becoming an empty chain or an empty evidence object.
    for bad in (None, 5):
        try:
            _consume(module, case, bad, [text])
        except module.FilterConfigurationError as error:
            if not str(error).startswith("filter configuration error: "):
                failures.append(f"the named input error is {str(error)!r}")
        except Exception as error:  # noqa: BLE001
            failures.append(f"components={bad!r} raised {type(error).__name__}")
        else:
            failures.append(f"components={bad!r} was accepted")
    return failures


def check_corpus_filter_exact_oracle(module) -> List[str]:
    """(S2/S3) Exact streaming equality with the sequential substitutions."""
    failures: List[str] = []
    texts = list(FILTER_EDGE_TEXTS)
    for annotations in FILTER_ANNOTATIONS:
        for components in FILTER_COMPONENTS:
            case = _filter_case(module, annotations, "self_test/oracle")
            for text in texts:
                expected = _oracle_filter(module, case, components, text)
                for chunks in _chunk_partitions(text):
                    evidence = _consume(module, case, components, chunks)
                    if evidence.program_output != expected:
                        failures.append(
                            f"oracle mismatch for {annotations!r}/{components!r} "
                            f"text={text!r} chunks={chunks[:4]!r}:\n"
                            f"  got {evidence.program_output!r}\n"
                            f"  exp {expected!r}")
                        break
                    if not module.corpus_program_output_usable(evidence):
                        failures.append(
                            f"a below-limit stream became invalid: {evidence!r}")
                        break

    # The mandatory counterexamples, with their stated exact results.
    for annotations, components, text, expected in MANDATORY_FILTER_COUNTEREXAMPLES:
        case = _filter_case(module, annotations, "self_test/counterexample")
        oracle = _oracle_filter(module, case, components, text)
        evidence = _consume(module, case, components, [text])
        if oracle != expected or evidence.program_output != expected:
            failures.append(
                f"counterexample {text!r}: oracle={oracle!r} "
                f"stream={evidence.program_output!r} expected={expected!r}")

    # Deterministic generated mixtures against the direct oracle, chunked.
    random.seed(20261001)
    for trial in range(120):
        annotations = FILTER_ANNOTATIONS[trial % len(FILTER_ANNOTATIONS)]
        components = FILTER_COMPONENTS[trial % len(FILTER_COMPONENTS)]
        case = _filter_case(module, annotations, "self_test/mixture")
        text = "".join(random.choice(FILTER_MIXTURE_PIECES)
                       for _ in range(random.randint(0, 9)))
        expected = _oracle_filter(module, case, components, text)
        chunks: List[str] = []
        index = 0
        while index < len(text):
            size = random.randint(1, 5)
            chunks.append(text[index:index + size])
            index += size
        evidence = _consume(module, case, components, chunks)
        if evidence.program_output != expected:
            failures.append(
                f"mixture {trial} mismatch: {text!r} -> "
                f"{evidence.program_output!r} != {expected!r}")

    # Strength of the check: the oracle rejects the shortcuts the design names.
    shortcuts = (
        ("prefix-line component filter", _shortcut_prefix_line_filter),
        ("combined component stage", _shortcut_combined_component_stage),
        ("rejected-window forwarding record stage",
         _shortcut_rejected_window_forwarding),
        ("space-only deopt opener", _shortcut_space_only_deopt_opener),
    )
    for label, shortcut in shortcuts:
        caught = False
        for annotations, components, text, expected in MANDATORY_FILTER_COUNTEREXAMPLES:
            case = _filter_case(module, annotations, "self_test/shortcut")
            try:
                produced = shortcut(module, case, components, text)
            except Exception:  # noqa: BLE001 - a crashing shortcut is not equal
                caught = True
                break
            if produced != expected:
                caught = True
                break
        if not caught:
            failures.append(f"the oracle does not catch the shortcut: {label}")
    return failures


def _shortcut_prefix_line_filter(module, case, components, text: str) -> str:
    """The former corpus filter: drop line-prefixed component lines only."""
    prefixes = [f"[{name}] " for name in
                list(components) + module._implied_log_components(case)]
    kept = []
    for line in text.splitlines(keepends=True):
        body = line[:-1] if line.endswith("\n") else line
        if any(body.startswith(prefix) for prefix in prefixes):
            continue
        kept.append(line)
    return "".join(kept)


def _shortcut_combined_component_stage(module, case, components, text: str) -> str:
    """One combined matcher for every component pattern."""
    names = list(components) + module._implied_log_components(case)
    filtered = text
    if names:
        combined = re.compile(
            r"\[(?:%s)\] [^\n]*\n?" % "|".join(re.escape(name) for name in names))
        filtered = combined.sub("", filtered)
    if module._case_has_evidence(case, module.GcCountGeConstraint):
        filtered = module.GC_TRACE_LOG_RE.sub("", filtered)
    if module._needs_deopt_trace(case):
        filtered = module.DEOPT_TRACE_LOG_RE.sub("", filtered)
    return filtered


def _shortcut_rejected_window_forwarding(module, case, components, text: str) -> str:
    """A record stage that forwards a rejected window to the next stage."""
    filtered = text
    if module._case_has_evidence(case, module.GcCountGeConstraint):
        filtered = module.GC_TRACE_LOG_RE.sub("", filtered)
    if module._needs_deopt_trace(case):
        filtered = re.sub(r"(?m)^\[compiler\] (?:Lazy )?Deoptimize[^\n]*\n",
                          "", filtered)
    for name in list(components) + module._implied_log_components(case):
        filtered = re.compile(
            r"\[%s\] [^\n]*\n?" % re.escape(name)).sub("", filtered)
    return filtered


def _shortcut_space_only_deopt_opener(module, case, components, text: str) -> str:
    """A deopt opener that requires a literal space before `at`."""
    filtered = text
    if module._case_has_evidence(case, module.GcCountGeConstraint):
        filtered = module.GC_TRACE_LOG_RE.sub("", filtered)
    if module._needs_deopt_trace(case):
        filtered = re.sub(
            r"(?m)^\[compiler\] (?:Lazy )?Deoptimize at [^\n]*\n"
            r"(?:    at [^\n]*\n)*\n", "", filtered)
    for name in list(components) + module._implied_log_components(case):
        filtered = re.compile(
            r"\[%s\] [^\n]*\n?" % re.escape(name)).sub("", filtered)
    return filtered


def check_corpus_filter_bounds(module) -> List[str]:
    """(S3/S5) Independent limits, sticky flags and valid empty output."""
    failures: List[str] = []
    gc_case = _filter_case(module, "//! GC_COUNT_GE * 1\n", "self_test/bounds_gc")
    deopt_case = _filter_case(module, "//! DEOPT_COUNT *Deoptimize* 0\n",
                              "self_test/bounds_deopt")
    plain = _filter_case(module, "", "self_test/bounds_plain")

    # The final-output cap: exactly the cap fits, the cap+1 boundary truncates,
    # multibyte text is measured in UTF-8 bytes and the prefix is the longest
    # complete-character prefix that fits.
    for text, cap, expected, truncated in (
            ("abcde", 5, "abcde", False),
            ("abcde", 4, "abcd", True),
            ("\u00e9\u00e9", 2, "\u00e9", True),
            ("\u00e9\u00e9", 4, "\u00e9\u00e9", False),
            ("", 0, "", False)):
        evidence = _consume(module, plain, [], [text], program_cap=cap)
        if evidence.program_output != expected \
                or evidence.program_output_truncated != truncated \
                or evidence.program_output_bytes != len(expected.encode("utf-8")):
            failures.append(
                f"program cap {cap} on {text!r}: {evidence.program_output!r} "
                f"truncated={evidence.program_output_truncated} "
                f"bytes={evidence.program_output_bytes}")

    # Removable candidates are not charged to the final-output cap, and a valid
    # empty result after successful deletion stays valid and comparable.
    removable = ("[gc] headline GCReason: reason\n" + "filler line\n" * 200
                 + "[gc] HandleStorage: 1\n")
    evidence = _consume(module, gc_case, [], [removable], program_cap=8)
    if evidence.program_output != "" or evidence.program_output_truncated:
        failures.append(
            f"a removable record was charged to the output cap: {evidence!r}")
    if not module.corpus_program_output_usable(evidence):
        failures.append("a valid empty result after deletion is not usable")

    # The per-record-stage candidate cap: a genuinely unresolved candidate
    # (one whose examined window ends exactly at the end of the input) is
    # charged up to its whole window, so `cap == size` is below the limit and
    # `cap == size - 1` is over it, identically under every chunk partition
    # (S3). A complete match is resolved and deleted, so its final LF leaves
    # nothing unresolved: the same record is not capped with `cap == size - 1`
    # even though the whole record is longer than that.
    unresolved_gc = ("[gc] headline GCReason: reason\n" + "z" * 20
                     + "\n[gc] HandleStorage")
    unresolved_deopt = "[compiler] Deoptimize    at a (x.js:1:1)" + "z" * 20
    complete_gc = ("[gc] headline GCReason: reason\n" + "z" * 12 + "\n"
                   + "[gc] HandleStorage\n")
    complete_deopt = ("[compiler] Deoptimize    at a (x.js:1:1)\n"
                      "    at f (x.js:2:1)\n\n")
    partitions = (
        ("whole chunk", lambda payload: [payload]),
        ("single characters", lambda payload: list(payload)),
        ("two-character chunks", lambda payload: [payload[i:i + 2]
                                                  for i in range(0, len(payload), 2)]),
        ("seven-character chunks", lambda payload: [payload[i:i + 7]
                                                    for i in range(0, len(payload), 7)]),
    )
    for label, candidate, case in (("gc", unresolved_gc, gc_case),
                                   ("deopt", unresolved_deopt, deopt_case)):
        size = len(candidate.encode("utf-8"))
        for cap, expected_capped in ((size, False), (size - 1, True)):
            for partition_label, partition in partitions:
                evidence = _consume(module, case, [], partition(candidate),
                                    record_candidate_cap=cap,
                                    program_cap=1024 * 1024)
                if evidence.record_candidate_capped != expected_capped:
                    failures.append(
                        f"the unresolved {label} candidate cap {cap}/{size} "
                        f"under {partition_label} gave capped="
                        f"{evidence.record_candidate_capped}, expected "
                        f"{expected_capped}")
    for label, record, case in (("gc", complete_gc, gc_case),
                                ("deopt", complete_deopt, deopt_case)):
        size = len(record.encode("utf-8"))
        # The record is resolved and deleted by its final LF, so the largest
        # examined window is exactly `size - 1`; both sides of that boundary
        # must hold under every partition.
        for cap, expected_capped in ((size - 1, False), (size - 2, True)):
            for partition_label, partition in partitions:
                evidence = _consume(module, case, [], partition(record),
                                    record_candidate_cap=cap,
                                    program_cap=1024 * 1024)
                if evidence.record_candidate_capped != expected_capped:
                    failures.append(
                        f"the complete {label} record {size} with cap {cap} "
                        f"under {partition_label} gave capped="
                        f"{evidence.record_candidate_capped}, expected "
                        f"{expected_capped}")
        # The default limit leaves a small record below the limit.
        evidence = _consume(module, case, [], [record])
        if evidence.record_candidate_capped or evidence.program_output_truncated:
            failures.append(
                f"a small {label} record was capped by the default limits: "
                f"{evidence!r}")

    # A record whose closer line carries a payload stays unresolved until its
    # mandatory LF arrives: the recognized closer literal alone does not settle
    # the deletion, so the closer suffix is charged and the largest examined
    # window is exactly `padded_size - 1` under every partition. The record is
    # still resolved and deleted while the window stayed within the limit.
    padded_gc = ("[gc] headline GCReason: r\n" + "z" * 12
                 + "\n[gc] HandleStorage" + "P" * 40 + "\n")
    padded_size = len(padded_gc.encode("utf-8"))
    for partition_label, partition in partitions:
        for cap, expected_capped in ((padded_size - 1, False),
                                     (padded_size - 2, True)):
            evidence = _consume(module, gc_case, [], partition(padded_gc),
                                record_candidate_cap=cap,
                                program_cap=1024 * 1024)
            if evidence.record_candidate_capped != expected_capped:
                failures.append(
                    f"the closer-line payload boundary {cap}/{padded_size} "
                    f"under {partition_label} gave capped="
                    f"{evidence.record_candidate_capped}, expected "
                    f"{expected_capped}")
        evidence = _consume(module, gc_case, [], partition(padded_gc),
                            record_candidate_cap=padded_size - 1,
                            program_cap=1024 * 1024)
        if evidence.program_output != "":
            failures.append(
                f"a complete record with closer-line payload was not deleted "
                f"under {partition_label}")
        # An over-limit window freezes the (empty) committed prefix instead of
        # deleting the record and certifying the result.
        evidence = _consume(module, gc_case, [], partition(padded_gc),
                            record_candidate_cap=padded_size - 20,
                            program_cap=1024 * 1024)
        if not evidence.record_candidate_capped:
            failures.append(
                f"an over-limit closer-line payload under {partition_label} "
                f"was not charged to the cap")
        if module.corpus_program_output_usable(evidence):
            failures.append(
                f"an over-limit closer-line payload under {partition_label} "
                f"certified its output")

    # The examined window is charged before the transition that releases it:
    # an oversized GC headline line that turns out not to be a record (no
    # `GCReason:`) is charged while its reason marker is undecided, identically
    # under every partition.
    non_record = "[gc] " + "x" * 100 + "\nOK\n"
    for partition_label, partition in partitions:
        evidence = _consume(module, gc_case, [], partition(non_record),
                            record_candidate_cap=20,
                            program_cap=1024 * 1024)
        if not evidence.record_candidate_capped:
            failures.append(
                f"the oversized non-record GC headline under "
                f"{partition_label} was not capped")

    # An unterminated closer keeps its whole examined suffix charged: without
    # the mandatory LF the record is still unresolved.
    unterminated = "[gc] GCReason: x\n[gc] HandleStorage"
    for total in (len(unterminated), len(unterminated) + 1):
        text = unterminated + "P" * (total - len(unterminated))
        for partition_label, partition in partitions:
            evidence = _consume(module, gc_case, [], partition(text),
                                record_candidate_cap=len(unterminated),
                                program_cap=1024 * 1024)
            expected_capped = total > len(unterminated)
            if evidence.record_candidate_capped != expected_capped:
                failures.append(
                    f"the unterminated closer {total} bytes under "
                    f"{partition_label} gave capped="
                    f"{evidence.record_candidate_capped}, expected "
                    f"{expected_capped}")
        # An unterminated closer that cannot be resolved at EOF still leaves
        # its diagnostic prefix bounded and is never certified as output.
        evidence = _consume(module, gc_case, [], [text],
                            record_candidate_cap=len(unterminated),
                            program_cap=1024 * 1024)
        if module.corpus_program_output_usable(evidence) \
                != (total <= len(unterminated)):
            failures.append(
                f"the unterminated closer {total} bytes certified the wrong "
                f"usability: {evidence!r}")

    # Partial record openers are unresolved too: `[gc` at a two-byte limit and
    # `[compiler` at an eight-byte limit already exceed it.
    partial_gc = _consume(module, gc_case, [], ["[gc"],
                          record_candidate_cap=2, program_cap=1024 * 1024)
    if not partial_gc.record_candidate_capped:
        failures.append("the partial `[gc` opener was not charged to the cap")
    partial_deopt = _consume(module, deopt_case, [], ["[compiler"],
                             record_candidate_cap=8, program_cap=1024 * 1024)
    if not partial_deopt.record_candidate_capped:
        failures.append("the partial `[compiler` opener was not charged to the cap")

    # Review: a supplied chunk must be charged exactly as its symbols would be,
    # so an intermediate partial literal that a later symbol of the same chunk
    # breaks is charged while it existed. The cap verdict is therefore identical
    # under whole-chunk, single-character and fixed-size partitions.
    for label, case, candidate, over_cap, fit_cap in (
            ("gc partial opener", gc_case, "[gcX", 2, 3),
            ("deopt partial head", deopt_case, "[compileX", 7, 8)):
        for cap, expected_capped in ((over_cap, True), (fit_cap, False)):
            for partition_label, partition in partitions:
                evidence = _consume(module, case, [], partition(candidate),
                                    record_candidate_cap=cap,
                                    program_cap=1024 * 1024)
                if evidence.record_candidate_capped != expected_capped:
                    failures.append(
                        f"the broken {label} {candidate!r} at cap {cap} under "
                        f"{partition_label} gave capped="
                        f"{evidence.record_candidate_capped}, expected "
                        f"{expected_capped}")

    # A mismatching candidate name is charged up to its matched prefix: the
    # whole candidate is unresolved when the mismatch character is examined.
    for partition_label, partition in partitions:
        evidence = _consume(module, deopt_case, [],
                            partition("[compiler] DeoptimizX"),
                            record_candidate_cap=15,
                            program_cap=1024 * 1024)
        if not evidence.record_candidate_capped:
            failures.append(
                f"the mismatching candidate name under {partition_label} was "
                f"not charged to its matched prefix")

    # A retained partial frame literal is unresolved until its mandatory space
    # arrives, so the whole candidate is charged and the cap boundary excludes
    # the one character that would settle deletion.
    frame_partial = "[compiler] Deoptimize at x\n    at"
    for cap, expected_capped in ((32, True), (33, False)):
        for partition_label, partition in partitions:
            evidence = _consume(module, deopt_case, [], partition(frame_partial),
                                record_candidate_cap=cap,
                                program_cap=1024 * 1024)
            if evidence.record_candidate_capped != expected_capped:
                failures.append(
                    f"the partial deopt frame at cap {cap} under "
                    f"{partition_label} gave capped="
                    f"{evidence.record_candidate_capped}, expected "
                    f"{expected_capped}")

    # The same boundary at the default candidate limit: the retained frame
    # prefix keeps the whole 4 MiB+4 byte candidate unresolved, so the default
    # limit must cap it under every partition instead of certifying it.
    frame_head = "[compiler] Deoptimize at "
    default_candidate_cap = module.CORPUS_RECORD_CANDIDATE_CAP
    huge_frame = (frame_head
                  + "x" * (default_candidate_cap - len(frame_head) - 3)
                  + "\n    at")
    if len(huge_frame.encode("utf-8")) <= default_candidate_cap:
        failures.append("the default-limit frame fixture is not over the cap")
    for partition_label, partition in (
            ("whole chunk", lambda payload: [payload]),
            ("64 KiB chunks", lambda payload: [payload[index:index + 65536]
                                               for index in range(0, len(payload),
                                                                  65536)])):
        evidence = _consume(module, deopt_case, ["compiler"],
                            partition(huge_frame), program_cap=1024 * 1024)
        if not evidence.record_candidate_capped:
            failures.append(
                f"the default-limit frame candidate under {partition_label} "
                f"was not capped")
        if module.corpus_program_output_usable(evidence):
            failures.append(
                f"the default-limit frame candidate under {partition_label} "
                f"certified its output")

    # Review: the first candidate excess freezes the already committed output
    # prefix at the symbol transition that produced it, so the retained text,
    # its byte count and every invalidity field - not only `candidate_capped` -
    # are identical under every read partition. A later symbol of a larger
    # supplied chunk must not be promoted into a committed diagnostic prefix,
    # and a same-chunk rejection must not release text that a character-wise
    # feed still held unresolved.
    def _freeze_tuple(source_evidence):
        return (source_evidence.program_output, source_evidence.program_output_bytes,
                source_evidence.program_output_truncated,
                source_evidence.record_candidate_capped,
                source_evidence.program_output_error)

    # A broken GC opener at a line start: the excess is reached while only the
    # partial opener exists and nothing has been committed yet, so no partition
    # may emit or truncate anything.
    broken_gc = "[gcX\nOK\n"
    for partition_label, partition in partitions:
        evidence = _consume(module, gc_case, [], partition(broken_gc),
                            record_candidate_cap=2, program_cap=1)
        value = _freeze_tuple(evidence)
        if value != ("", 0, False, True, None):
            failures.append(
                f"the broken gc opener under {partition_label} froze "
                f"{value!r}, expected the empty committed prefix")

    # A broken partial at a line start wins over a complete opener that starts
    # later in the same supplied chunk: a character-wise feed reaches the
    # excess first, so neither partition may emit the line before the opener.
    for label, partial_case, candidate, cap in (
            ("broken gc partial", gc_case, "[gcX\n[gc] ", 2),
            ("broken deopt partial", deopt_case, "[compileX\n[compiler] ", 7)):
        for partition_label, partition in partitions:
            evidence = _consume(module, partial_case, [], partition(candidate),
                                record_candidate_cap=cap,
                                program_cap=1024 * 1024)
            value = _freeze_tuple(evidence)
            if value != ("", 0, False, True, None):
                failures.append(
                    f"the {label} under {partition_label} froze {value!r}, "
                    f"expected the empty committed prefix")

    # The default-limit deopt candidate whose frame prefix mismatches instead
    # of completing: the whole 4 MiB candidate is still unresolved when the
    # excess is reached, so one chunk must not commit (and truncate) a 1 MiB
    # prefix that a 64 KiB partition keeps unresolved.
    mismatch_frame = (frame_head
                      + "x" * (default_candidate_cap - len(frame_head) - 3)
                      + "\n    atX\nOK\n")
    for partition_label, partition in (
            ("whole chunk", lambda payload: [payload]),
            ("64 KiB chunks", lambda payload: [payload[index:index + 65536]
                                               for index in range(0, len(payload),
                                                                  65536)])):
        evidence = _consume(module, deopt_case, [], partition(mismatch_frame),
                            record_candidate_cap=default_candidate_cap,
                            program_cap=1024 * 1024)
        value = _freeze_tuple(evidence)
        if value != ("", 0, False, True, None):
            failures.append(
                f"the mismatching default-limit deopt frame under "
                f"{partition_label} froze {value!r}, expected the empty "
                f"committed prefix")

    # Text that is safely emitted, and the unread tail of a caller-supplied
    # chunk, are never charged to the candidate limit: a long run of plain text
    # stays below a tiny cap under every partition.
    safe = "OK\n" * 2000
    for partition_label, partition in partitions:
        evidence = _consume(module, gc_case, [], partition(safe),
                            record_candidate_cap=20,
                            program_cap=1024 * 1024)
        if evidence.record_candidate_capped:
            failures.append(
                f"large safe text under {partition_label} was charged to the "
                f"candidate limit")
        if evidence.program_output != safe:
            failures.append(
                f"large safe text under {partition_label} was not emitted "
                f"unchanged")
    # A below-cap record followed by removable compiler diagnostics stays below
    # the limit: only the recognized record window is unresolved, while the
    # removable tail is emitted downstream and deleted by the component stage.
    mixed = complete_gc + (COMPILER + "dump line\n") * 200
    mixed_cap = len(complete_gc.encode("utf-8")) - 1
    for partition_label, partition in partitions:
        evidence = _consume(module, gc_case, ["compiler"], partition(mixed),
                            record_candidate_cap=mixed_cap,
                            program_cap=1024 * 1024)
        if evidence.record_candidate_capped:
            failures.append(
                f"a below-cap record followed by removable diagnostics was "
                f"charged to the candidate limit under {partition_label}")
        if evidence.program_output != "":
            failures.append(
                f"a below-cap record followed by removable diagnostics was "
                f"not filtered under {partition_label}: "
                f"{evidence.program_output[:40]!r}")

    # An earlier stage's limit is not reduced by the later stage's retention:
    # one enabled record stage holds a candidate while the other stays empty.
    both = _filter_case(module, "//! GC_COUNT_GE * 1\n//! DEOPT_COUNT *Deoptimize* 0\n",
                        "self_test/bounds_both")
    evidence = _consume(module, both, [], ["[gc] headline GCReason: r\n"
                                           + "z" * 40],
                        record_candidate_cap=5, program_cap=1024 * 1024)
    if not evidence.record_candidate_capped:
        failures.append("the gc candidate with the deopt stage enabled was not capped")

    # Regression (review): the same stream must give the same verdict under
    # every partition, and the unresolved window that grew past the limit is
    # charged even when the closer that deletes it arrives later in the stream.
    over = "[gc] GCReason: x\n" + "z" * 30 + "\n[gc] HandleStorage\nOK\n"
    for partition_label, chunks in (
            ("whole chunk", [over]), ("single characters", list(over)),
            ("two-character chunks", [over[i:i + 2]
                                      for i in range(0, len(over), 2)]),
            ("seven-character chunks", [over[i:i + 7]
                                        for i in range(0, len(over), 7)])):
        evidence = _consume(module, gc_case, [], chunks, record_candidate_cap=20)
        if not evidence.record_candidate_capped:
            failures.append(
                f"the over-limit candidate under {partition_label} was not "
                f"capped: {evidence!r}")
        if module.corpus_program_output_usable(evidence):
            failures.append(
                f"the over-limit candidate under {partition_label} certified "
                f"its output")

    # Stickiness: a later small chunk cannot restore validity, and an invalid
    # result with an empty retained prefix is still invalid.
    sticky = _consume(module, gc_case, [],
                      ["[gc] headline GCReason: reason\n"],
                      record_candidate_cap=4, program_cap=1024 * 1024)
    if not sticky.record_candidate_capped or sticky.program_output != "":
        failures.append(f"the sticky candidate flag with an empty prefix is {sticky!r}")
    sticky_output = _consume(module, plain, [],
                             ["0123456789", "ab"],
                             program_cap=5)
    if not sticky_output.program_output_truncated \
            or sticky_output.program_output != "01234":
        failures.append(
            f"a later small chunk changed the frozen prefix: {sticky_output!r}")

    # Independent log limit and the program cap do not interact.
    large_log = [COMPILER + f"dump line {index}\n" for index in range(200)]
    evidence = _consume(module, plain, ["compiler"], large_log,
                        log_cap=50, program_cap=1024)
    if not evidence.compiler_log_capped or evidence.program_output_truncated:
        failures.append(f"the log limit leaked into the output cap: {evidence!r}")
    return failures


def check_corpus_filter_errors(module) -> List[str]:
    """(S5) Read, filter and log-sink failures are named, never clean EOF."""
    failures: List[str] = []
    case = _filter_case(module, "", "self_test/errors")

    def failing_chunks():
        yield "[compiler] a\n"
        raise OSError("read boom")

    evidence = _consume(module, case, ["compiler"], failing_chunks())
    if evidence.program_output_error is None \
            or "program output collection failed: read:" not in \
            evidence.program_output_error:
        failures.append(f"a read failure is not named: {evidence.program_output_error!r}")
    if module.corpus_program_output_usable(evidence):
        failures.append("a read failure certified the output as usable")

    evidence = _consume(module, case, ["compiler"], [b"bytes\n"])
    if evidence.program_output_error is None \
            or "program output collection failed: filter:" not in \
            evidence.program_output_error:
        failures.append(f"a filter failure is not named: {evidence.program_output_error!r}")

    class _BrokenSink:
        """A retained-log sink whose writes fail from the first line on."""

        def write(self, data):
            raise OSError("sink boom")

        def close(self):
            return None

    class _DelayedBrokenSink(_BrokenSink):
        """A sink that accepts the first write and fails afterwards."""

        def __init__(self):
            self.writes = 0

        def write(self, data):
            self.writes += 1
            if self.writes > 1:
                raise OSError("sink boom later")

    evidence = module.consume_corpus_stdout(["[compiler] dump\n"], case,
                                            ["compiler"], _BrokenSink())
    if evidence.program_output_error is None \
            or "program output collection failed: retained log:" not in \
            evidence.program_output_error:
        failures.append(f"a log-sink failure is not named: {evidence.program_output_error!r}")
    if module.corpus_program_output_usable(evidence):
        failures.append("a log-sink failure certified the output as usable")

    # Only the sink failed: observation continues within the failing chunk and
    # in later chunks, so the readable markers still count (S5). The already
    # committed output prefix is frozen and the named error is preserved.
    marked = (COMPILER + "dump line\n"
              + COMPILER + ">>> Running method: rec.a\n"
              + COMPILER
              + "======== ArkSteedCompilerTask: Starts compiling: m ========\n")
    for label, chunks, sink in (
            ("one chunk", [marked], _BrokenSink()),
            ("later chunks", marked.splitlines(keepends=True), _BrokenSink()),
            ("delayed failure", marked.splitlines(keepends=True),
             _DelayedBrokenSink())):
        evidence = module.consume_corpus_stdout(chunks, case, ["compiler"], sink)
        if evidence.program_output_error is None \
                or "retained log" not in evidence.program_output_error:
            failures.append(
                f"the {label} sink failure is not named: "
                f"{evidence.program_output_error!r}")
        if (evidence.compile_markers, evidence.exec_markers) != (1, 1):
            failures.append(
                f"the {label} sink failure lost later markers: "
                f"compile={evidence.compile_markers} "
                f"exec={evidence.exec_markers}")
        if evidence.program_output != "":
            failures.append(
                f"the {label} sink failure did not freeze the committed "
                f"prefix: {evidence.program_output!r}")

    # The first named error is preserved; a later default cannot overwrite it.
    evidence = _consume(module, case, ["compiler"], failing_chunks())
    first = evidence.program_output_error
    _record = module._record_corpus_collection_error
    _record(evidence, "observe", ValueError("later"))
    if evidence.program_output_error != first:
        failures.append("a later collection error overwrote the first one")

    # Invalid configured input is a named configuration error, not evidence.
    try:
        module.consume_corpus_stdout(5, case, ["compiler"], io.BytesIO())
    except module.FilterConfigurationError as error:
        if "filter configuration error: " not in str(error):
            failures.append(f"the non-iterable chunks error is {str(error)!r}")
    else:
        failures.append("a non-iterable chunks argument was accepted")

    # A failure while *initializing* the iterator is a read failure of the
    # collector, exactly like a failure of `next` below: it becomes named, red
    # evidence and never an escaped exception or a clean EOF (S5). A genuinely
    # non-iterable input keeps its named configuration error above.
    class _BrokenInit:
        def __init__(self, init_error):
            self._error = init_error

        def __iter__(self):
            raise self._error

    for init_error in (OSError("iterator boom"),
                       ValueError("iterator setup failed")):
        evidence = _consume(module, case, ["compiler"], _BrokenInit(init_error))
        if evidence.program_output_error is None \
                or "program output collection failed: read:" not in \
                evidence.program_output_error:
            failures.append(
                f"an iterator-initialization {type(init_error).__name__} is not "
                f"named: {evidence.program_output_error!r}")
        if module.corpus_program_output_usable(evidence):
            failures.append(
                f"an iterator-initialization {type(init_error).__name__} "
                f"certified the output as usable")
    return failures


def check_corpus_log_failure_ordering(module) -> List[str]:
    """(S3/S5) A retained-log failure freezes the prefix it reached.

    Observation writes one line at a time, so the transformation branch must
    receive exactly the text that precedes the failing line: the retained
    text, its byte count and every invalidity field then stay identical under
    every read partition, while observation continues and the raw markers of
    the failing chunk and of later chunks still count. The named error is
    preserved and never turns the result into certified success.
    """
    failures: List[str] = []
    case = _filter_case(module, "", "self_test/log_order")

    class _BrokenSink:
        """A retained-log sink whose writes fail from the first line on."""

        def write(self, data):
            raise OSError("broken")

        def close(self):
            return None

    class _DelayedSink(_BrokenSink):
        """A sink that accepts `fail_at - 1` writes and fails afterwards."""

        def __init__(self, fail_at):
            self.fail_at = fail_at
            self.writes = 0

        def write(self, data):
            self.writes += 1
            if self.writes >= self.fail_at:
                raise OSError("broken later")

    def partitions(text):
        yield ("whole chunk", [text])
        yield ("per line", text.splitlines(keepends=True))
        yield ("single characters", list(text))
        yield ("four-character chunks", [text[index:index + 4]
                                         for index in range(0, len(text), 4)])
        yield ("empty pieces", [""] + [text[index:index + 2]
                                       for index in range(0, len(text), 2)])

    # The program line after the failing retained line is not committed by any
    # partition: only the text that precedes the failing write was observed
    # when the sink failed.
    marked = ("abcdefgh\n"
              + COMPILER + "diag\n"
              + "ijklmnop\n"
              + COMPILER + ">>> Running method: r.m\n"
              + COMPILER
              + "======== ArkSteedCompilerTask: Starts compiling: m ========\n")
    for program_cap, expected_prefix, expected_truncated in (
            (2, "ab", True),
            (100, "abcdefgh\n", False)):
        for label, chunks in partitions(marked):
            evidence = module.consume_corpus_stdout(
                chunks, case, ["compiler"], _BrokenSink(),
                program_cap=program_cap)
            if evidence.program_output_error is None \
                    or "retained log" not in evidence.program_output_error:
                failures.append(
                    f"the {label} log failure is not named: "
                    f"{evidence.program_output_error!r}")
            if module.corpus_program_output_usable(evidence):
                failures.append(
                    f"the {label} log failure certified its output")
            if evidence.program_output != expected_prefix \
                    or evidence.program_output_bytes != len(
                        expected_prefix.encode("utf-8")) \
                    or evidence.program_output_truncated != expected_truncated \
                    or evidence.record_candidate_capped:
                failures.append(
                    f"the {label} log failure at program cap {program_cap} "
                    f"froze {evidence.program_output!r}/"
                    f"{evidence.program_output_bytes}/"
                    f"truncated={evidence.program_output_truncated}, expected "
                    f"{expected_prefix!r}/{len(expected_prefix.encode('utf-8'))}/"
                    f"truncated={expected_truncated}")
            # Only the sink failed: every later marker is still readable and
            # must still be counted.
            if (evidence.compile_markers, evidence.exec_markers) != (1, 1):
                failures.append(
                    f"the {label} log failure at program cap {program_cap} "
                    f"lost later markers: compile={evidence.compile_markers} "
                    f"exec={evidence.exec_markers}")

    # A delayed failure names the later write and freezes exactly the prefix
    # that preceded the failing line: the program lines before it are kept,
    # the program line after it is not, under every partition.
    delayed = ("abcdefgh\n" + COMPILER + "a\n" + "ijklmnop\n"
               + COMPILER + "b\n" + "qrstuv\n"
               + COMPILER + ">>> Running method: r.m\n")
    for label, chunks in partitions(delayed):
        evidence = module.consume_corpus_stdout(
            chunks, case, ["compiler"], _DelayedSink(2), program_cap=100)
        if evidence.program_output != "abcdefgh\nijklmnop\n" \
                or evidence.program_output_bytes != 18 \
                or evidence.program_output_truncated \
                or evidence.program_output_error is None:
            failures.append(
                f"the delayed {label} log failure froze "
                f"{evidence.program_output!r}/"
                f"{evidence.program_output_bytes}/"
                f"truncated={evidence.program_output_truncated}/"
                f"error={evidence.program_output_error!r}")
        if evidence.exec_markers != 1:
            failures.append(
                f"the delayed {label} log failure lost a later marker: "
                f"exec={evidence.exec_markers}")

    # The same ordering holds when the failure is only flushed at EOF: the
    # text observed before the unterminated final line is still committed and
    # the failure stays named under every partition.
    eof_text = "abcdefgh\n" + COMPILER + "a\nijklmnop"
    for label, chunks in partitions(eof_text):
        evidence = module.consume_corpus_stdout(
            chunks, case, ["compiler"], _BrokenSink(), program_cap=100)
        if evidence.program_output != "abcdefgh\n" \
                or evidence.program_output_bytes != 9 \
                or evidence.program_output_error is None:
            failures.append(
                f"the EOF {label} log failure froze "
                f"{evidence.program_output!r} with "
                f"error={evidence.program_output_error!r}")

    # Review (cycle 2): the failing sink write runs after the whole failing
    # line (including its LF) has been observed, so the transformation branch
    # must receive that line too. Otherwise a candidate overflow of the
    # already observed line is lost under a coarse partition: the same stream
    # reports `record_candidate_capped=True` character by character and
    # `False` as one chunk. The frozen text, its byte count and every
    # invalidity field must stay identical under every read partition.
    gc_case = _filter_case(module, "//! GC_COUNT_GE * 1\n",
                           "self_test/log_order_gc")
    overflow = GC + "GCReason: " + "x" * 12 + "\nOK\n"
    for label, chunks in partitions(overflow):
        evidence = module.consume_corpus_stdout(
            chunks, gc_case, [], _BrokenSink(), record_candidate_cap=20,
            program_cap=1024 * 1024)
        if not evidence.record_candidate_capped:
            failures.append(
                f"the {label} log failure lost the candidate overflow of the "
                f"already observed failing line")
        if evidence.program_output != "" \
                or evidence.program_output_bytes != 0 \
                or evidence.program_output_truncated \
                or evidence.program_output_error is None \
                or "retained log" not in evidence.program_output_error \
                or module.corpus_program_output_usable(evidence):
            failures.append(
                f"the {label} log failure over a candidate overflow froze "
                f"{evidence.program_output!r}/"
                f"{evidence.program_output_bytes}/"
                f"truncated={evidence.program_output_truncated}/"
                f"error={evidence.program_output_error!r}")

    # The same boundary at the production candidate limit: the failing GC line
    # carries more than `CORPUS_RECORD_CANDIDATE_CAP` unresolved bytes, so a
    # whole chunk and a fixed 64 KiB partition must both cap it once the sink
    # write of that line fails.
    big = (GC + "GCReason: "
           + "x" * (module.CORPUS_RECORD_CANDIDATE_CAP + 65536) + "\nOK\n")
    for label, chunks in (("whole chunk", [big]),
                          ("64 KiB chunks",
                           [big[index:index + 65536]
                            for index in range(0, len(big), 65536)])):
        evidence = module.consume_corpus_stdout(
            chunks, gc_case, [], _BrokenSink())
        if not evidence.record_candidate_capped \
                or module.corpus_program_output_usable(evidence):
            failures.append(
                f"the default-limit {label} log failure gave capped="
                f"{evidence.record_candidate_capped}/"
                f"usable={module.corpus_program_output_usable(evidence)}")
    return failures


def check_corpus_observation_failure_freeze(module) -> List[str]:
    """(S3/S5) A raw observation failure freezes the committed prefix.

    The observation branch is the only branch that may raise an unexpected
    exception. It invalidates the collection: the transformation pipeline must
    be disabled at that point, so neither the failing chunk nor any later
    readable chunk is transformed, and the already committed bounded prefix,
    its byte count and every invalidity field stay those of the state at the
    failure - including whether the final-output cap had already truncated it.
    Later chunks are still drained, and the failure stays named and red.
    """
    failures: List[str] = []
    case = _filter_case(module, "", "self_test/observe_failure")
    real_observer = module._CorpusObserver

    class _FailingObserver(real_observer):
        """An observer whose second chunk raises after a committed prefix."""

        calls = 0

        def feed(self, text):
            type(self).calls += 1
            if type(self).calls == 2:
                raise ValueError("observe boom")
            return super().feed(text)

    for program_cap, expected, expected_truncated in (
            (100, "before\n", False),
            (7, "before\n", False),
            (4, "befo", True)):
        module._CorpusObserver = _FailingObserver
        try:
            _FailingObserver.calls = 0
            evidence = module.consume_corpus_stdout(
                ["before\n", "later\n", "after\n"], case, [], io.BytesIO(),
                program_cap=program_cap)
        finally:
            module._CorpusObserver = real_observer
        expected_bytes = len(expected.encode("utf-8"))
        if evidence.program_output != expected \
                or evidence.program_output_bytes != expected_bytes \
                or evidence.program_output_truncated != expected_truncated \
                or evidence.record_candidate_capped \
                or evidence.program_output_error is None \
                or "observe" not in evidence.program_output_error \
                or module.corpus_program_output_usable(evidence):
            failures.append(
                f"the observation failure at program cap {program_cap} froze "
                f"{evidence.program_output!r}/"
                f"{evidence.program_output_bytes}/"
                f"truncated={evidence.program_output_truncated}/"
                f"capped={evidence.record_candidate_capped}/"
                f"error={evidence.program_output_error!r}")

    # The failing chunk's own text is never transformed, and neither is a
    # component line of a later chunk: the prefix is frozen at the failure.
    module._CorpusObserver = _FailingObserver
    try:
        _FailingObserver.calls = 0
        evidence = module.consume_corpus_stdout(
            ["before\n", "later\n" + COMPILER + "diag\n", "after\n"],
            case, [], io.BytesIO(), program_cap=100)
    finally:
        module._CorpusObserver = real_observer
    if evidence.program_output != "before\n" \
            or evidence.program_output_bytes != 7:
        failures.append(
            f"the observation failure transformed a later chunk: "
            f"{evidence.program_output!r}/{evidence.program_output_bytes}")

    # Later chunks stay readable: the collector finishes draining the stream
    # after the observation failure instead of breaking out of the loop.
    drained = {"count": 0}

    def failing_chunks():
        for chunk in ("before\n", "later\n", "after\n"):
            drained["count"] += 1
            yield chunk

    module._CorpusObserver = _FailingObserver
    try:
        _FailingObserver.calls = 0
        module.consume_corpus_stdout(
            failing_chunks(), case, [], io.BytesIO(), program_cap=100)
    finally:
        module._CorpusObserver = real_observer
    if drained["count"] != 3:
        failures.append(
            f"the observation failure stopped draining after "
            f"{drained['count']} chunk(s)")
    return failures


def check_corpus_observation_equivalence(module) -> List[str]:
    """(S5) Raw counts, named patterns and the retained log stay exact."""
    failures: List[str] = []
    case = _corpus_stream_case(module, "//! EXEC_COUNT_GE *foo* 0\n")
    stream = (
        COMPILER + "======== ArkSteedCompilerTask: Starts compiling: foo ========\n"
        + COMPILER + ">>> Running method: rec.foo\n"
        + COMPILER + ">>> Running method: rec.bar\n"
        + COMPILER + ">>> Running method: rec.foo\n"
        + COMPILER + "Deoptimize    at a (x.js:1:1)\n"
        + COMPILER + "Eager deopt disables GC; skip trace.\n"
        + COMPILER + "Lazy Deoptimize    at b (x.js:2:1)\n"
        + GC + "highlight\n"
        + "program\n"
    )
    with tempfile.NamedTemporaryFile("wb", delete=False) as log_file:
        log_path = Path(log_file.name)
        evidence = module.consume_corpus_stdout([stream], case, ["compiler"],
                                                log_file)
    reference = module.collect_evidence(stream)
    if evidence.compile_markers != len(reference["compile"]):
        failures.append(
            f"compile markers {evidence.compile_markers} != {len(reference['compile'])}")
    if evidence.exec_markers != len(reference["exec"]):
        failures.append(f"exec markers {evidence.exec_markers} != {len(reference['exec'])}")
    if evidence.deopt_markers != len(reference["deopt"]):
        failures.append(f"deopt markers {evidence.deopt_markers} != {len(reference['deopt'])}")
    for constraint in case.evidence_constraints:
        expected = sum(
            1 for candidate in reference[constraint.EVIDENCE_KEY]
            if any(fnmatchcase(spelling, constraint.pattern)
                   for spelling in constraint.spellings(candidate)))
        actual = module.corpus_evidence_actual(constraint, evidence)
        if actual != expected:
            failures.append(
                f"{constraint.label()}: {actual} != {expected}")
    retained = log_path.read_text(encoding="utf-8")
    if ">>> Running method" in retained:
        failures.append("the retained log kept an execution marker payload")
    log_path.unlink(missing_ok=True)

    # A marker inside a complete or incomplete record candidate is still
    # counted, and raw counts survive a candidate overflow.
    inside = ("[gc] headline GCReason: r\n"
              + COMPILER + "======== ArkSteedCompilerTask: Starts compiling: in ========\n"
              + "[gc] HandleStorage: 1\n")
    inside_case = _corpus_stream_case(module)
    with tempfile.NamedTemporaryFile("wb") as log_file:
        evidence = module.consume_corpus_stdout([inside], inside_case,
                                                ["compiler"], log_file)
    if evidence.compile_markers != 1:
        failures.append(
            f"a marker inside a record candidate counted {evidence.compile_markers}")
    capped_case = _corpus_stream_case(module, "//! GC_COUNT_GE * 1\n")
    with tempfile.NamedTemporaryFile("wb") as log_file:
        evidence = module.consume_corpus_stdout(
            ["[gc] headline GCReason: r\n" + COMPILER
             + "======== ArkSteedCompilerTask: Starts compiling: in ========\n"],
            capped_case, ["compiler"], log_file, record_candidate_cap=4)
    if not evidence.record_candidate_capped or evidence.compile_markers != 1:
        failures.append(
            f"the counts after a candidate overflow are {evidence!r}")

    # A long named payload is matched, not truncated: the streaming matcher
    # accepts a payload `fnmatchcase` accepts regardless of its length.
    long_case = _corpus_stream_case(module, "//! EXEC_COUNT_GE *suffix* 0\n")
    payload = "p" * 20000 + "suffix"
    with tempfile.NamedTemporaryFile("wb") as log_file:
        evidence = module.consume_corpus_stdout(
            [COMPILER + f">>> Running method: {payload}\n"], long_case,
            ["compiler"], log_file)
    constraint = next(c for c in long_case.evidence_constraints
                      if c.pattern == "*suffix*")
    if module.corpus_evidence_actual(constraint, evidence) != 1:
        failures.append("a long named payload lost its named evidence")
    with tempfile.NamedTemporaryFile("wb") as log_file:
        evidence = module.consume_corpus_stdout(
            [COMPILER + f">>> Running method: {'p' * 20000}\n"], long_case,
            ["compiler"], log_file)
    if module.corpus_evidence_actual(constraint, evidence) != 0:
        failures.append("a long non-matching payload counted as named evidence")

    # Marker identities are recognized independently of the substitution and
    # retention component list (S5): an empty or unrelated explicit component
    # list is valid and must still count the compile and execution markers and
    # their named payloads, under every chunk partition, exactly as
    # `collect_evidence` does. A component that is not observed is counted but
    # not retained.
    markers_case = _corpus_stream_case(
        module, "//! COMPILE_COUNT m 1\n//! EXEC_COUNT r.m 1\n")
    marker_stream = (
        COMPILER + "======== ArkSteedCompilerTask: Starts compiling: m ========\n"
        + COMPILER + ">>> Running method: r.m\n")
    marker_reference = module.collect_evidence(marker_stream)
    for components in ([], ["gc"], ["interpreter"], ["gc", "gc"]):
        for label, chunks in (
                ("one chunk", [marker_stream]),
                ("per line", marker_stream.splitlines(keepends=True)),
                ("per character", list(marker_stream)),
                ("two-character chunks",
                 [marker_stream[i:i + 2]
                  for i in range(0, len(marker_stream), 2)])):
            with tempfile.NamedTemporaryFile("wb") as log_file:
                evidence = module.consume_corpus_stdout(
                    chunks, markers_case, components, log_file)
            if evidence.program_output_error is not None:
                failures.append(
                    f"components={components!r} under {label} failed: "
                    f"{evidence.program_output_error!r}")
            if (evidence.compile_markers, evidence.exec_markers) != (
                    len(marker_reference["compile"]),
                    len(marker_reference["exec"])):
                failures.append(
                    f"components={components!r} under {label} counted "
                    f"compile={evidence.compile_markers} "
                    f"exec={evidence.exec_markers}")
            for constraint in markers_case.evidence_constraints:
                candidates = marker_reference[constraint.EVIDENCE_KEY]
                expected = sum(
                    1 for candidate in candidates
                    if any(fnmatchcase(spelling, constraint.pattern)
                           for spelling in constraint.spellings(candidate)))
                actual = module.corpus_evidence_actual(constraint, evidence)
                if actual != expected:
                    failures.append(
                        f"components={components!r} under {label}: "
                        f"{constraint.label()} counted {actual}, expected "
                        f"{expected}")
    # A component that is not in the effective observation list is counted but
    # never retained: with an empty component list the retained log stays empty.
    with tempfile.NamedTemporaryFile("wb", delete=False) as log_file:
        empty_log = Path(log_file.name)
        module.consume_corpus_stdout([marker_stream], markers_case, [], log_file)
    if empty_log.read_text(encoding="utf-8") != "":
        failures.append("an unobserved component was retained in the log")
    empty_log.unlink(missing_ok=True)

    # A supplied component name that extends a fixed raw-marker identity must
    # not shadow it (S5): the fixed `[compiler] `/`[gc] ` identities have
    # priority, so the totals, the named counts and the retained-log decision
    # of the same raw stream are the same under every chunk partition and
    # match `collect_evidence`. An execution-marker line is never retained,
    # whatever supplied prefix overlaps it.
    def partitions(text: str):
        return (
            ("one chunk", [text]),
            ("per character", list(text)),
            ("two-character chunks",
             [text[index:index + 2] for index in range(0, len(text), 2)]),
            ("seven-character chunks",
             [text[index:index + 7] for index in range(0, len(text), 7)]),
        )

    def overlap_check(probe_case, probe_components, probe_stream, expected_log):
        reference_evidence = module.collect_evidence(probe_stream)
        for partition_label, partition_chunks in partitions(probe_stream):
            with tempfile.NamedTemporaryFile("wb", delete=False) as log_handle:
                log_file_path = Path(log_handle.name)
                run_evidence = module.consume_corpus_stdout(
                    partition_chunks, probe_case, probe_components, log_handle)
            retained_text = log_file_path.read_text(encoding="utf-8")
            log_file_path.unlink(missing_ok=True)
            if run_evidence.program_output_error is not None:
                failures.append(
                    f"the overlapping prefix {probe_components!r} under {partition_label} "
                    f"failed: {run_evidence.program_output_error!r}")
            if run_evidence.gc_markers != len(reference_evidence["gc"]):
                failures.append(
                    f"the overlapping prefix {probe_components!r} under {partition_label} "
                    f"counted gc={run_evidence.gc_markers}, expected "
                    f"{len(reference_evidence['gc'])}")
            if run_evidence.exec_markers != len(reference_evidence["exec"]):
                failures.append(
                    f"the overlapping prefix {probe_components!r} under {partition_label} "
                    f"counted exec={run_evidence.exec_markers}, expected "
                    f"{len(reference_evidence['exec'])}")
            for evidence_constraint in probe_case.evidence_constraints:
                expected_count = sum(
                    1 for candidate in reference_evidence[evidence_constraint.EVIDENCE_KEY]
                    if any(fnmatchcase(spelling, evidence_constraint.pattern)
                           for spelling in evidence_constraint.spellings(candidate)))
                actual_count = module.corpus_evidence_actual(evidence_constraint, run_evidence)
                if actual_count != expected_count:
                    failures.append(
                        f"the overlapping prefix {probe_components!r} under {partition_label}: "
                        f"{evidence_constraint.label()} counted {actual_count}, expected "
                        f"{expected_count}")
            if retained_text != expected_log:
                failures.append(
                    f"the overlapping prefix {probe_components!r} under {partition_label} "
                    f"retained {retained_text!r}, expected {expected_log!r}")

    gc_overlap = _corpus_stream_case(module, "//! GC_COUNT_GE *hi* 1\n")
    overlap_check(gc_overlap, ["compiler", "gc] x"], "[gc] x] hi\n",
                  "[gc] x] hi\n")
    exec_overlap = _corpus_stream_case(module, "//! EXEC_COUNT_GE *r* 1\n")
    overlap_check(exec_overlap,
                  ["compiler", "compiler] >>> Running method: r"],
                  COMPILER + ">>> Running method: r] hi\n", "")
    # Two supplied prefixes that overlap in the other direction (a longer name
    # before a shorter one) must also commit to the same name under every
    # partition: a later, shorter match is never chosen while an earlier,
    # longer one can still grow into a complete match.
    supplied_overlap = _corpus_stream_case(module)
    overlap_check(supplied_overlap, ["a] b", "a"], "[a] b] hi\n",
                  "[a] b] hi\n")
    # At a line end no candidate can grow further, so the complete shorter
    # match is still the component of that line.
    overlap_check(supplied_overlap, ["a] b", "a"], "[a] \n", "[a] \n")
    # The prefix matcher itself must be a pure, partition-independent function
    # (S5): a later, shorter candidate is committed only once every
    # higher-priority candidate is ruled out, so a longer name delivered in one
    # chunk and character by character recognises the same component.
    overlap_candidates = ["[a] b] ", "[a] "]
    if module._match_prefix_candidates("[a] ", overlap_candidates) \
            != (None, False):
        failures.append(
            "the prefix matcher committed to a later candidate while a "
            "higher-priority one could still match")
    if module._match_prefix_candidates("[a] ", overlap_candidates, True) \
            != (1, True):
        failures.append(
            "the prefix matcher lost a complete shorter match at a line end")
    if module._match_prefix_candidates("[a] b] x", overlap_candidates) \
            != (0, True):
        failures.append("the prefix matcher missed the longer candidate")
    if module._match_prefix_candidates("[a] x", overlap_candidates) \
            != (1, True):
        failures.append(
            "the prefix matcher did not fall back to the shorter candidate")
    return failures


def check_corpus_evidence_validity(module) -> List[str]:
    """(S5) The usability predicate and the three named verdict reasons."""
    failures: List[str] = []
    clean = module.CorpusStreamEvidence()
    if not module.corpus_program_output_usable(clean):
        failures.append("a clean evidence object is not usable")
    for name, value in (("program_output_truncated", True),
                        ("record_candidate_capped", True)):
        evidence = module.CorpusStreamEvidence(**{name: value})
        if module.corpus_program_output_usable(evidence):
            failures.append(f"{name} does not make the output unusable")
    evidence = module.CorpusStreamEvidence(
        program_output_error="program output collection failed: read: OSError: x")
    if module.corpus_program_output_usable(evidence):
        failures.append("a program_output_error does not make the output unusable")

    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    case = _corpus_case_with_invariant(module, loaded, CORPUS_CASES[0])
    injections = (
        ({"program_output_truncated": True}, "program output exceeds cap"),
        ({"record_candidate_capped": True}, "record candidate exceeds cap"),
        ({"program_output_error": "program output collection failed: read: OSError: x"},
         "program output collection failed: read: OSError: x"),
    )
    for overrides, reason in injections:
        for retained in ("", "visible output\n"):
            execution = _corpus_class_execution(
                module, program_output=retained, **overrides)
            verdict = module.evaluate_corpus_class_run(
                case, loaded, compile_ok=True, compile_message="",
                compile_timed_out=False, execution=execution,
                reference_verdict="PASS", valid_opcodes=None)
            if verdict.passed:
                failures.append(f"{reason} with retained {retained!r} is green")
            if reason not in verdict.reasons:
                failures.append(
                    f"{reason} is not named in {verdict.reasons}")
            # Base-verdict parity is independent and stays `matched`.
            if verdict.parity != module.CORPUS_PARITY_MATCHED:
                failures.append(
                    f"the base-verdict parity became {verdict.parity!r}")
    # A valid empty output stays eligible: no invalidity reason is produced.
    verdict = module.evaluate_corpus_class_run(
        case, loaded, compile_ok=True, compile_message="", compile_timed_out=False,
        execution=_corpus_class_execution(module, program_output=""),
        reference_verdict="PASS", valid_opcodes=None)
    if verdict.reasons:
        failures.append(f"a valid empty output is red: {verdict.reasons}")
    return failures


class _SubprocessShim:
    """Module-level `subprocess` stand-in that replaces only `Popen`."""

    def __init__(self, popen):
        self.Popen = popen

    def __getattr__(self, name):
        return getattr(real_subprocess, name)


class _FakeProc:
    """A fake `ark_js_vm` process with bounded in-memory pipes."""

    def __init__(self, stdout_data: bytes = b"", stderr_data: bytes = b"",
                 returncode: int = 0, read_error: Optional[BaseException] = None):
        self.stdout = _FakeStream(stdout_data, read_error)
        self.stderr = _FakeStream(stderr_data)
        self.returncode = returncode
        self.killed = False

    def wait(self) -> int:
        return self.returncode

    def kill(self) -> None:
        self.killed = True


class _FakeStream:
    def __init__(self, data: bytes, read_error: Optional[BaseException] = None):
        self._data = data
        self._position = 0
        self._read_error = read_error

    def read(self, size: int = -1) -> bytes:
        if self._read_error is not None and self._position:
            raise self._read_error
        if size is None or size < 0:
            size = len(self._data) - self._position
        chunk = self._data[self._position:self._position + size]
        self._position += len(chunk)
        return chunk

    def close(self) -> None:
        return None


class _ThreadShim:
    """`threading` stand-in that records the executor's drain thread.

    It lets a fake pipe wait until the parallel stderr drain has finished, so
    the failure-boundary checks are deterministic instead of depending on
    thread scheduling.
    """

    def __init__(self):
        self.threads: List[threading.Thread] = []

    def Thread(self, *args, **kwargs):
        thread = threading.Thread(*args, **kwargs)
        self.threads.append(thread)
        return thread

    def __getattr__(self, name):
        return getattr(threading, name)


class _ImmediateFailStream:
    """A fake pipe whose every read raises the given error."""

    def __init__(self, error: BaseException):
        self._error = error

    def read(self, size: int = -1) -> bytes:
        raise self._error

    def close(self) -> None:
        return None


class _SyncedStdout:
    """Stdout that is only delivered after the stderr drain has failed."""

    def __init__(self, data: bytes, shim: _ThreadShim,
                 read_error: Optional[BaseException] = None):
        self._data = data
        self._shim = shim
        self._read_error = read_error
        self._position = 0
        self._synced = False

    def _sync(self) -> None:
        if self._synced:
            return
        for _ in range(500):
            if self._shim.threads:
                break
            time.sleep(0.01)
        for thread in list(self._shim.threads):
            thread.join(5.0)
        self._synced = True

    def read(self, size: int = -1) -> bytes:
        self._sync()
        if self._read_error is not None and self._position:
            raise self._read_error
        if size is None or size < 0:
            size = len(self._data) - self._position
        chunk = self._data[self._position:self._position + size]
        self._position += len(chunk)
        return chunk

    def close(self) -> None:
        return None


class _StderrFailProc:
    """A fake VM whose stderr fails before any stdout data is delivered."""

    def __init__(self, stdout_data: bytes, shim: _ThreadShim,
                 stdout_error: Optional[BaseException] = None):
        self.stdout = _SyncedStdout(stdout_data, shim, stdout_error)
        self.stderr = _ImmediateFailStream(OSError("FIRST stderr failure"))
        self.returncode = 0
        self.killed = False

    def wait(self) -> int:
        return self.returncode

    def kill(self) -> None:
        self.killed = True


class _BlockingStream:
    """A fake pipe whose read blocks until the executor kills the child."""

    def __init__(self, released: threading.Event):
        self._released = released

    def read(self, size: int = -1) -> bytes:
        self._released.wait(5.0)
        return b""

    def close(self) -> None:
        return None


class _BlockingProc:
    """A fake VM that never reaches EOF, so the executor timer must fire."""

    def __init__(self):
        self.released = threading.Event()
        self.stdout = _BlockingStream(self.released)
        self.stderr = _BlockingStream(self.released)
        self.returncode = 0
        self.killed = False

    def wait(self) -> int:
        return self.returncode

    def kill(self) -> None:
        self.killed = True
        self.released.set()


class _FailingLogFile:
    """A retained-log file whose write or close fails as requested."""

    def __init__(self, fail_on: str = "write"):
        self._fail_on = fail_on

    def write(self, data) -> None:
        if self._fail_on == "write":
            raise OSError("log write boom")

    def close(self) -> None:
        if self._fail_on == "close":
            raise OSError("log close boom")


def check_corpus_class_executor_errors(module) -> List[str]:
    """(C4/S5) The class executor survives failures with named evidence."""
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    case = _corpus_case_with_invariant(module, loaded, CORPUS_CASES[0])
    cfg = module.BuildConfig("debug", "x64")
    context = module.CorpusContext(
        module=loaded, root=root, revision=None, build_config=None,
        reference_args=None, class_defaults=None, valid_opcodes=None)
    case_artifacts = _temp_dir("arksteed-self-test-artifacts-") / "case"
    case_artifacts.mkdir(parents=True, exist_ok=True)

    def run(proc, budget: int = 30):
        with _patched(module.subprocess, Popen=lambda *a, **k: proc):
            return module.run_corpus_class_execution(
                case, case_artifacts / "case.abc", cfg, CORPUS_CLASS_ARGS, context,
                budget, case_artifacts, [])

    # A normal stream reaches the bounded artifacts.
    execution = run(_FakeProc(b"[compiler] >>> Running method: a.b\nprogram\n"))
    if not execution.started or execution.returncode != 0 \
            or execution.evidence.program_output != "program\n":
        failures.append(f"a normal fake process gave {execution!r}")
    # A read failure of stdout is named and never a clean empty result; the
    # child is terminated so the executor cannot block on a full pipe.
    failing = _FakeProc(b"[compiler] >>> Running method: a.b\n",
                        read_error=OSError("pipe boom"))
    execution = run(failing)
    if execution.evidence.program_output_error is None \
            or "read" not in execution.evidence.program_output_error:
        failures.append(
            f"an executor read failure is not named: {execution.evidence!r}")
    if module.corpus_program_output_usable(execution.evidence):
        failures.append("an executor read failure certified the output")
    if not failing.killed:
        failures.append("an incomplete stdout drain did not terminate the child")
    # A start failure is independent and red.

    def _fail_start(*args, **kwargs):
        raise OSError("no vm")

    with _patched(module.subprocess, Popen=_fail_start):
        execution = module.run_corpus_class_execution(
            case, case_artifacts / "case.abc", cfg, CORPUS_CLASS_ARGS, context, 30,
            case_artifacts, [])
    if execution.started or execution.start_error is None:
        failures.append(f"a start failure is {execution!r}")
    verdict = module.evaluate_corpus_class_run(
        case, loaded, compile_ok=True, compile_message="", compile_timed_out=False,
        execution=execution, reference_verdict="PASS", valid_opcodes=None)
    if verdict.passed:
        failures.append("a start failure is green")

    # The actual executor timer: a child that never reaches EOF is timed out by
    # the executor itself (not by a result fixture), killed and reported red.
    blocking = _BlockingProc()
    execution = run(blocking, budget=0.05)
    if not execution.timed_out:
        failures.append("the executor timer did not time out the fake process")
    if not blocking.killed:
        failures.append("a timed-out class execution did not kill the child")
    verdict = module.evaluate_corpus_class_run(
        case, loaded, compile_ok=True, compile_message="", compile_timed_out=False,
        execution=execution, reference_verdict="TIMEOUT", valid_opcodes=None)
    if verdict.passed:
        failures.append("a timed-out class execution is green")

    # The actual program-output cap through the executor: the retained prefix is
    # exactly the cap, the flag is sticky and the output is unusable.
    cap_data = b"x" * (module.CORPUS_PROGRAM_OUTPUT_CAP + 4096)
    execution = run(_FakeProc(cap_data))
    if not execution.evidence.program_output_truncated:
        failures.append("the executor did not report the program-output cap")
    if execution.evidence.program_output_bytes != module.CORPUS_PROGRAM_OUTPUT_CAP:
        failures.append(
            f"the executor retained {execution.evidence.program_output_bytes} "
            f"bytes, expected {module.CORPUS_PROGRAM_OUTPUT_CAP}")
    if module.corpus_program_output_usable(execution.evidence):
        failures.append("the executor certified an over-cap program output")

    # A retained-log write failure and a delayed close failure stay named while
    # observation continues; both are recoverable for the raw counts (S5).
    marked = (b"[compiler] dump line\n"
              b"[compiler] >>> Running method: rec.a\n"
              b"[compiler] ======== ArkSteedCompilerTask: Starts compiling: "
              b"m ========\n")

    def run_with_log(log_file, proc):
        with _patched(module.subprocess, Popen=lambda *a, **k: proc), \
                _patched_module_attr(module, "open", lambda *a, **k: log_file):
            return module.run_corpus_class_execution(
                case, case_artifacts / "case.abc", cfg, CORPUS_CLASS_ARGS, context,
                30, case_artifacts, [])

    for fail_on in ("write", "close"):
        execution = run_with_log(_FailingLogFile(fail_on), _FakeProc(marked))
        if execution.evidence.program_output_error is None \
                or "program output collection failed: retained log:" \
                not in execution.evidence.program_output_error:
            failures.append(
                f"the executor log-{fail_on} failure is not named: "
                f"{execution.evidence.program_output_error!r}")
        if (execution.evidence.compile_markers,
                execution.evidence.exec_markers) != (1, 1):
            failures.append(
                f"the executor log-{fail_on} failure lost later markers: "
                f"{execution.evidence!r}")

    # An unopenable retained log uses the discard sink and keeps observing.
    blocked = (case_artifacts
               / f"{module.corpus_case_stem(case)}{module.CORPUS_LIVE_LOG_SUFFIX}")
    if blocked.is_file() or blocked.is_symlink():
        blocked.unlink()
    blocked.mkdir(exist_ok=True)
    execution = run(_FakeProc(marked))
    if execution.evidence.program_output_error is None \
            or "program output collection failed: retained log:" \
            not in execution.evidence.program_output_error:
        failures.append(
            f"the executor log-open failure is not named: "
            f"{execution.evidence.program_output_error!r}")
    if (execution.evidence.compile_markers,
            execution.evidence.exec_markers) != (1, 1):
        failures.append(
            f"the executor log-open failure lost later markers: "
            f"{execution.evidence!r}")
    blocked.rmdir()

    # The retained-log *open* failure is handed to the collector before any
    # stdout is consumed: the committed program prefix is frozen at the failure
    # boundary (empty, no output cap) and this first named error survives both a
    # later read failure and a later readable over-cap stream (S3/S5).
    def bad_open(*args, **kwargs):
        raise OSError("FIRST open failure")

    def run_with_bad_open(proc):
        with _patched(module.subprocess, Popen=lambda *a, **k: proc), \
                _patched_module_attr(module, "open", bad_open):
            return module.run_corpus_class_execution(
                case, case_artifacts / "case.abc", cfg, CORPUS_CLASS_ARGS, context,
                30, case_artifacts, [])

    over_cap = marked + b"x" * (module.CORPUS_PROGRAM_OUTPUT_CAP + 4096)
    for label, proc in (
            ("a later read failure",
             _FakeProc(marked, read_error=OSError("SECOND read failure"))),
            ("a later readable over-cap stream", _FakeProc(over_cap))):
        execution = run_with_bad_open(proc)
        error = execution.evidence.program_output_error or ""
        if "FIRST open failure" not in error:
            failures.append(
                f"the log-open failure before {label} is not the first named "
                f"error: {error!r}")
        if "SECOND read failure" in error:
            failures.append(
                f"{label} overwrote the earlier log-open failure: {error!r}")
        if (execution.evidence.compile_markers,
                execution.evidence.exec_markers) != (1, 1):
            failures.append(
                f"the log-open failure before {label} lost markers: "
                f"{execution.evidence!r}")
        if execution.evidence.program_output != "" \
                or execution.evidence.program_output_bytes != 0:
            failures.append(
                f"the log-open failure before {label} retained "
                f"{execution.evidence.program_output!r} / "
                f"{execution.evidence.program_output_bytes} bytes")
        if execution.evidence.program_output_truncated:
            failures.append(
                f"the log-open failure before {label} set the output cap")
        if module.corpus_program_output_usable(execution.evidence):
            failures.append(
                f"the log-open failure before {label} certified the output")

    # A stderr drain failure is propagated into the per-execution collection
    # state at the failure boundary (S3/S5): the committed program prefix of
    # the same execution is frozen before any later readable stdout text can be
    # transformed or charged against the output cap, and the stderr failure
    # stays the first named error even when a later stdout read also fails.
    # The fake pipes synchronize on the drain thread, so the order is exact.
    for label, stdout_data, stdout_error, expected_exec in (
            ("a later readable over-cap stream",
             b"x" * (module.CORPUS_PROGRAM_OUTPUT_CAP + 4096), None, 0),
            ("a later stdout read failure",
             COMPILER.encode() + b">>> Running method: a.b\n",
             OSError("SECOND stdout failure"), 1)):
        shim = _ThreadShim()
        proc = _StderrFailProc(stdout_data, shim, stdout_error)
        with _patched(module, threading=shim,
                      subprocess=_SubprocessShim(lambda *a, **k: proc)):
            execution = module.run_corpus_class_execution(
                case, case_artifacts / "case.abc", cfg, CORPUS_CLASS_ARGS, context,
                30, case_artifacts, [])
        error = execution.evidence.program_output_error or ""
        if "FIRST stderr failure" not in error:
            failures.append(
                f"the stderr failure before {label} is not named: {error!r}")
        if "SECOND stdout failure" in error:
            failures.append(
                f"{label} overwrote the earlier stderr failure: {error!r}")
        if execution.evidence.program_output != "" \
                or execution.evidence.program_output_bytes != 0:
            failures.append(
                f"the stderr failure before {label} retained "
                f"{execution.evidence.program_output!r} / "
                f"{execution.evidence.program_output_bytes} bytes")
        if execution.evidence.program_output_truncated:
            failures.append(
                f"the stderr failure before {label} set the output cap")
        if execution.evidence.record_candidate_capped:
            failures.append(
                f"the stderr failure before {label} capped a record candidate")
        if execution.evidence.exec_markers != expected_exec:
            failures.append(
                f"the stderr failure before {label} observed "
                f"{execution.evidence.exec_markers} execution markers, "
                f"expected {expected_exec}")
        if module.corpus_program_output_usable(execution.evidence):
            failures.append(
                f"the stderr failure before {label} certified the output")

    # The direct executor boundary reports an unsupported filter construction as
    # its named configuration failure instead of escaping as a raw TypeError
    # from a corpus command predicate (S1/S4).
    broken_case = _corpus_case_with_invariant(module, loaded, CORPUS_CASES[0])
    broken_case.evidence_constraints = None
    execution = module.run_corpus_class_execution(
        broken_case, case_artifacts / "case.abc", cfg, CORPUS_CLASS_ARGS, context,
        30, case_artifacts, [])
    if "filter configuration error: " not in (execution.start_error or ""):
        failures.append(
            f"the executor did not name the missing evidence collection: "
            f"{execution.start_error!r}")
    if execution.started:
        failures.append("the executor started a case with no evidence collection")
    if module.corpus_program_output_usable(execution.evidence):
        failures.append(
            "the executor certified a case with no evidence collection")

    # The executor supplies the ordered *explicit* component names to the
    # collector: a GC-annotated case with the explicit ['compiler'] gives the
    # canonical GC -> compiler -> implied-gc result and one implied 'gc'
    # occurrence only.
    workspace = _temp_dir("arksteed-self-test-executor-descriptors-") / "test"
    workspace.mkdir(parents=True)
    chain_stream = "[g[gc] diag\nc] diag\n"
    with _patched(module, TEST_DIR=workspace):
        _write_descriptor(workspace, CORPUS_CASES[0], "//! GC_COUNT_GE * 1\n")
        gc_case = _corpus_case_with_invariant(module, loaded, CORPUS_CASES[0])
        module.parse_corpus_descriptor(
            gc_case, None, set(module.corpus_class_defaults(loaded)))
        expected = _oracle_filter(module, gc_case, ["compiler"], chain_stream)
        if expected != "[gc] diag\n":
            failures.append(f"the explicit-component oracle is {expected!r}")
        if len(module.case_filter_chain(gc_case, ["compiler"])) != 3:
            failures.append("the gc case chain has an unexpected stage count")
        with _patched(module.subprocess,
                      Popen=lambda *a, **k: _FakeProc(chain_stream.encode())):
            execution = module.run_corpus_class_execution(
                gc_case, case_artifacts / "gc.abc", cfg, CORPUS_CLASS_ARGS, context,
                30, case_artifacts, [])
        if execution.evidence.program_output != expected:
            failures.append(
                f"the executor chain result is "
                f"{execution.evidence.program_output!r}, expected {expected!r}")
    return failures


def check_corpus_main_invalidity_orchestration(module) -> List[str]:
    """(S4) `main --corpus` carries every invalidity to verdicts/artifacts."""
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    ark_root = _fake_ark_root()
    env_name = module.CORPUS_ROOT_ENV_VAR
    rel = CORPUS_CASES[0]
    injections = (
        ({"record_candidate_capped": True}, "record candidate exceeds cap"),
        ({"program_output_truncated": True}, "program output exceeds cap"),
        ({"program_output_error":
          "program output collection failed: read: OSError: boom"},
         "program output collection failed: read: OSError: boom"),
    )

    def comparator_gate(evidence):
        """A stand-in for an absent differential comparator (S4).

        It mirrors the required consumer gate only: invalid output must never be
        certified, and a valid output may be `matched`. It never mutates the
        evidence it is given, so the injection stays the single cause of a red
        result.
        """
        if not module.corpus_program_output_usable(evidence):
            raise AssertionError("invalid output reached the comparator")
        return "matched"

    for overrides, reason in injections:
        if len(overrides) != 1:
            failures.append(f"the injection {overrides!r} is not a single field")
        for retained in ("", "visible output\n"):
            artifacts = _temp_dir("arksteed-self-test-artifacts-")
            collected: List[module.CorpusStreamEvidence] = []

            def fake_compile(*args, **kwargs):
                return True, "", False, []

            def fake_execute(*args, **kwargs):
                execution = _corpus_class_execution(
                    module, program_output=retained, **overrides)
                collected.append(execution.evidence)
                return execution

            with _patched(module, ARK_ROOT=ark_root, build_ark=lambda cfg, skip: True,
                          make_artifacts_root=lambda *a: artifacts,
                          load_corpus_module=lambda r: (loaded, None),
                          compile_corpus_case=fake_compile,
                          run_corpus_class_execution=fake_execute):
                rc, output = _call_main(
                    module, ["-p", "x64", "-m", "debug", "--corpus",
                             "-I", f"{module.CORPUS_NAMESPACE}/{rel}"],
                    env={env_name: str(root)})
            if rc != 1:
                failures.append(
                    f"{reason} with retained {retained!r} returned {rc}")
            if reason not in output:
                failures.append(
                    f"{reason} is not named in the run output: {output[-400:]!r}")
            if "skipped-empty-program-output" in output:
                failures.append(
                    f"an invalid output was reported as empty: {output[-400:]!r}")
            program_artifacts = sorted(artifacts.rglob(
                f"*{module.CORPUS_PROGRAM_OUTPUT_SUFFIX}"))
            if not program_artifacts:
                failures.append(f"{reason}: no program artifact was written")
            elif not program_artifacts[0].read_text(
                    encoding="utf-8").startswith("# incomplete program output prefix"):
                failures.append(
                    f"{reason}: the program artifact is not labelled incomplete")
            # The comparator gate is asserted against the evidence the real
            # run collected, independently of the verdict: the injection alone
            # must make that evidence unusable and the gate must refuse it.
            if not collected:
                failures.append(f"{reason}: no class execution was collected")
                continue
            evidence = collected[0]
            if module.corpus_program_output_usable(evidence):
                failures.append(
                    f"{reason}: the collected evidence certifies {retained!r}")
            refused = False
            try:
                comparator_gate(evidence)
            except AssertionError:
                refused = True
            if not refused:
                failures.append(
                    f"{reason}: the comparator certified invalid output")

    # Control arm: the same orchestration with a valid evidence object is
    # green and the gate accepts it, so the red results above are caused by the
    # injected invalidity alone.
    artifacts = _temp_dir("arksteed-self-test-artifacts-")
    valid_evidence: List[module.CorpusStreamEvidence] = []

    def valid_compile(*args, **kwargs):
        return True, "", False, []

    def valid_execute(*args, **kwargs):
        execution = _corpus_class_execution(module, program_output="visible\n")
        valid_evidence.append(execution.evidence)
        return execution

    with _patched(module, ARK_ROOT=ark_root, build_ark=lambda cfg, skip: True,
                  make_artifacts_root=lambda *a: artifacts,
                  load_corpus_module=lambda r: (loaded, None),
                  compile_corpus_case=valid_compile,
                  run_corpus_class_execution=valid_execute):
        rc, output = _call_main(
            module, ["-p", "x64", "-m", "debug", "--corpus",
                     "-I", f"{module.CORPUS_NAMESPACE}/{rel}"],
            env={env_name: str(root)})
    if rc != 0:
        failures.append(f"a valid substituted corpus run returned {rc}: {output[-300:]!r}")
    if not valid_evidence:
        failures.append("the control run collected no evidence")
    elif comparator_gate(valid_evidence[0]) != "matched":
        failures.append("the comparator rejected a valid evidence object")
    return failures


def check_corpus_config_error_orchestration(module) -> List[str]:
    """(S1/S4) A missing evidence collection is a named red case, not a crash.

    `main --corpus` must reach the ordinary verdict/report path with the named
    `filter configuration error`, never escape as a raw `TypeError` from a
    corpus command predicate.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    ark_root = _fake_ark_root()
    env_name = module.CORPUS_ROOT_ENV_VAR
    rel = CORPUS_CASES[0]
    artifacts = _temp_dir("arksteed-self-test-artifacts-")

    real_inject = module.inject_corpus_invariant

    def inject_then_break(case):
        # The case is otherwise complete: the class-level requirements are
        # injected first, then the evidence collection is removed, which is the
        # unsupported construction the canonical chain must name.
        real_inject(case)
        case.evidence_constraints = None

    with _patched(module, ARK_ROOT=ark_root, build_ark=lambda cfg, skip: True,
                  make_artifacts_root=lambda *a: artifacts,
                  load_corpus_module=lambda r: (loaded, None),
                  inject_corpus_invariant=inject_then_break):
        rc, output = _call_main(
            module, ["-p", "x64", "-m", "debug", "--corpus",
                     "-I", f"{module.CORPUS_NAMESPACE}/{rel}"],
            env={env_name: str(root)})
    if rc != 1:
        failures.append(
            f"a missing evidence collection returned {rc}: {output[-400:]!r}")
    if "filter configuration error: " not in output:
        failures.append(
            f"a missing evidence collection is not named: {output[-400:]!r}")
    if "Traceback" in output or "TypeError" in output:
        failures.append(
            f"a missing evidence collection escaped as an exception: "
            f"{output[-400:]!r}")
    return failures


def check_corpus_decoder_and_bounded_retention(module) -> List[str]:
    """(S5) Decoder equality, whitespace class and bounded retention."""
    failures: List[str] = []
    random.seed(20261002)
    for _ in range(120):
        data = bytes(random.getrandbits(8) for _ in range(random.randint(0, 300)))
        for size in (1, 2, 3, 9, 64, 1024):
            decoded = "".join(module._iter_decoded_chunks(io.BytesIO(data), size))
            if decoded != data.decode("utf-8", errors="replace"):
                failures.append(f"decoder mismatch at chunk size {size}")
                break
    text = "h\u00e9llo \U0001F600 \u4f60\u597d"
    for size in (1, 2, 3, 5):
        if "".join(module._iter_decoded_chunks(
                io.BytesIO(text.encode("utf-8")), size)) != text:
            failures.append(f"the decoder split a UTF-8 character at size {size}")

    # The deopt whitespace class is Python's `\s`, not the ASCII space.
    for char in ("\t", "\n", "\r", "\v", "\f", "\x1c", "\x1f", "\x85", "\u00a0",
                 "\u2000", "\u2028", "\u3000", " "):
        if bool(re.match(r"\s", char)) != char.isspace():
            failures.append(f"the whitespace predicate disagrees about {char!r}")

    # A long named payload, a long deleted fragment and a long unresolved
    # candidate are consumed without growing the retention with the stream.
    def measure(chunks_factory, case, **kwargs):
        tracemalloc.start()
        try:
            with tempfile.NamedTemporaryFile("wb") as log_file:
                result = module.consume_corpus_stdout(
                    chunks_factory(), case, ["compiler"], log_file, **kwargs)
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        return result, peak

    # A long named payload is consumed incrementally: doubling the stream does
    # not grow the retained memory.
    exec_case = _corpus_stream_case(module, "//! EXEC_COUNT_GE *suffix* 0\n")
    constraint = next(c for c in exec_case.evidence_constraints
                      if c.pattern == "*suffix*")

    def long_exec_stream(blocks: int):
        def stream():
            yield COMPILER + ">>> Running method: "
            for _ in range(blocks):
                yield "p" * 5000
            yield "suffix\n"
        return stream

    evidence, small_peak = measure(long_exec_stream(400), exec_case)
    _, large_peak = measure(long_exec_stream(1600), exec_case)
    if evidence.program_output_error is not None:
        failures.append(f"the long stream failed: {evidence.program_output_error!r}")
    if module.corpus_evidence_actual(constraint, evidence) != 1:
        failures.append("the long stream lost its named evidence")
    if large_peak > small_peak + 128 * 1024:
        failures.append(
            f"the retained memory grows with the named payload: "
            f"{small_peak} -> {large_peak}")

    # A long unresolved candidate is capped, its window is freed, and the
    # retained memory does not grow with the stream.
    candidate_case = _corpus_stream_case(module, "//! GC_COUNT_GE * 1\n")

    def candidate_stream(blocks: int):
        def stream():
            yield "[gc] headline GCReason: r\n"
            for _ in range(blocks):
                yield "z" * 5000
        return stream

    capped, capped_small = measure(candidate_stream(400), candidate_case,
                                   record_candidate_cap=64 * 1024)
    _, capped_large = measure(candidate_stream(1600), candidate_case,
                              record_candidate_cap=64 * 1024)
    if not capped.record_candidate_capped:
        failures.append("a long unresolved candidate was not capped")
    if capped_large > capped_small + 128 * 1024:
        failures.append(
            f"the capped candidate retained memory that grows with the stream: "
            f"{capped_small} -> {capped_large}")
    return failures


def check_corpus_main_real_chain_orchestration(module) -> List[str]:
    """(S4) `main --corpus` runs the real chain and writes its exact result.

    The class executor is not substituted here: only the VM process is a fake,
    so `main --corpus` exercises the real `case_filter_chain` (GC -> deopt ->
    explicit -> implied), the incremental transducers and the bounded sinks.
    The case carries GC and deopt descriptor annotations, so the observed
    stream exercises all four stages in order, and the case program artifact
    must hold exactly the sequential-substitution result of that stream. An
    injected invalidity on the same real path is reported red with its named
    reason and never as an empty or comparable result.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    ark_root = _fake_ark_root()
    env_name = module.CORPUS_ROOT_ENV_VAR
    rel = CORPUS_CASES[1]
    annotations = "//! GC_COUNT_GE * 1\n//! DEOPT_COUNT_GE *Deoptimize* 0\n"
    stdout = (
        "[compiler] ======== ArkSteedCompilerTask: Starts compiling: m ========\n"
        "[compiler] >>> Running method: rec.m\n"
        "[gc] headline GCReason: reason\n"
        "verbose continuation line\n"
        "[gc] HandleStorage: 1\n"
        "[compiler] Deoptimize at first (a.js:1:1)\n"
        "[compiler] Deoptimize at second (a.js:2:1)\n"
        "    at frame (a.js:3:1)\n"
        "\n"
        "[compiler] a component dump line\n"
        "[g[compiler] diagnostic\nc] diagnostic\n"
        "program line 1\n"
        "program line 2\n"
    )
    workspace = _temp_dir("arksteed-self-test-chain-descriptors-") / "test"
    workspace.mkdir(parents=True)

    def fake_popen(*args, **kwargs):
        return _FakeProc(stdout.encode("utf-8"), b"")

    def fake_compile(*args, **kwargs):
        # Only the VM process is faked: compilation is not part of the filter
        # chain, while the executor, the chain and the artifacts are real.
        return True, "", False, []

    def run_chain(artifacts):
        with _patched(module, ARK_ROOT=ark_root, build_ark=lambda cfg, skip: True,
                      make_artifacts_root=lambda *a: artifacts,
                      compile_corpus_case=fake_compile,
                      subprocess=_SubprocessShim(fake_popen),
                      TEST_DIR=workspace):
            return _call_main(
                    module, ["-p", "x64", "-m", "debug", "--corpus",
                             "-I", f"{module.CORPUS_NAMESPACE}/{rel}"],
                    env={env_name: str(root)})

    with _patched(module, TEST_DIR=workspace):
        _write_descriptor(workspace, rel, annotations)
        case = _corpus_case_with_invariant(module, loaded, rel)
        module.parse_corpus_descriptor(
            case, None, set(module.corpus_class_defaults(loaded)))
        chain = module.case_filter_chain(case, ["compiler"])
        if [p.pattern for p in chain] != [
                module.GC_TRACE_LOG_RE.pattern, module.DEOPT_TRACE_LOG_RE.pattern,
                module._component_pattern("compiler").pattern,
                module._component_pattern("gc").pattern]:
            failures.append(
                f"the descriptor annotations gave the chain "
                f"{[p.pattern for p in chain]}")
        expected = _oracle_filter(module, case, ["compiler"], stdout)
        if expected != "program line 1\nprogram line 2\n":
            failures.append(f"the real-chain oracle is {expected!r}")
        # The check is sensitive to a dropped record stage: removing either
        # record substitution changes the exact result, so an orchestration
        # check that silently loses a stage must fail.
        for dropped in (module.GC_TRACE_LOG_RE, module.DEOPT_TRACE_LOG_RE):
            filtered = stdout
            for pattern in chain:
                if pattern is dropped:
                    continue
                filtered = pattern.sub("", filtered)
            if filtered == expected:
                failures.append(
                    f"dropping {dropped.pattern!r} does not change the result")

    artifacts = _temp_dir("arksteed-self-test-artifacts-")
    rc, output = run_chain(artifacts)
    if rc != 0:
        failures.append(f"the real-chain corpus run returned {rc}: {output[-400:]!r}")
    program_artifacts = sorted(artifacts.rglob(
        f"*{module.CORPUS_PROGRAM_OUTPUT_SUFFIX}"))
    if not program_artifacts:
        failures.append("the real-chain corpus run wrote no program artifact")
    else:
        written = program_artifacts[0].read_text(encoding="utf-8")
        if written != expected:
            failures.append(
                f"the program artifact is not the exact chain result:\n"
                f"  got {written!r}\n  exp {expected!r}")
    summary = artifacts / "summary.md"
    if summary.is_file():
        text = summary.read_text(encoding="utf-8")
        if "Passed: 1  Failed: 0" not in text:
            failures.append(f"the run summary is not green: {text[:200]!r}")

    # The same real path with an injected invalidity is red with the named
    # reason and never certifies the collected output as empty/comparable.
    invalid_artifacts = _temp_dir("arksteed-self-test-artifacts-")
    real_execute = module.run_corpus_class_execution

    def poisoned_execute(*args, **kwargs):
        execution = real_execute(*args, **kwargs)
        execution.evidence.record_candidate_capped = True
        return execution

    with _patched(module, ARK_ROOT=ark_root, build_ark=lambda cfg, skip: True,
                  make_artifacts_root=lambda *a: invalid_artifacts,
                  compile_corpus_case=fake_compile,
                  run_corpus_class_execution=poisoned_execute,
                  subprocess=_SubprocessShim(fake_popen),
                  TEST_DIR=workspace):
        invalid_rc, invalid_output = _call_main(
            module, ["-p", "x64", "-m", "debug", "--corpus",
                     "-I", f"{module.CORPUS_NAMESPACE}/{rel}"],
            env={env_name: str(root)})
    if invalid_rc != 1:
        failures.append(
            f"the real-chain run with an injected invalidity returned "
            f"{invalid_rc}: {invalid_output[-400:]!r}")
    if "record candidate exceeds cap" not in invalid_output:
        failures.append(
            f"the injected invalidity is not named on the real chain: "
            f"{invalid_output[-400:]!r}")
    if "skipped-empty-program-output" in invalid_output:
        failures.append(
            f"the real chain reported an invalid output as empty: "
            f"{invalid_output[-400:]!r}")
    return failures


def check_corpus_streaming_provenance(module) -> List[str]:
    """(S1) The streaming pipeline is built from the canonical chain.

    `consume_corpus_stdout` must obtain its substitutions from
    `case_filter_chain` and build one stage per chain element in order: the GC
    and deopt record stages, then one component stage for every explicit name
    and every implied name, repetitions preserved. The canonical constructor
    is the only source, so a collector that reimplemented the chain or used the
    command's deduplicated observation list cannot pass.
    """
    failures: List[str] = []
    case = _corpus_stream_case(
        module, "//! DEOPT_COUNT *Deoptimize* 0\n//! GC_COUNT_GE * 1\n")
    calls: List[List[str]] = []
    real_chain = module.case_filter_chain

    def spy_chain(case_arg, components):
        chain = real_chain(case_arg, components)
        calls.append([pattern.pattern for pattern in chain])
        return chain

    captured: List[object] = []
    real_pipeline = module._CorpusFilterPipeline

    class _SpyPipeline(real_pipeline):
        def __init__(self, entries, program_cap, record_candidate_cap):
            super().__init__(entries, program_cap, record_candidate_cap)
            captured.append(self)

    with _patched(module, case_filter_chain=spy_chain,
                  _CorpusFilterPipeline=_SpyPipeline):
        _consume(module, case, ["compiler", "compiler"], ["OK\n"])
    if not calls:
        failures.append("consume_corpus_stdout did not call case_filter_chain")
    else:
        expected_chain = [pattern.pattern
                          for pattern in real_chain(case, ["compiler", "compiler"])]
        if calls[0] != expected_chain:
            failures.append(
                f"the streaming chain is {calls[0]}, canonical is "
                f"{expected_chain}")
    if not captured:
        failures.append("consume_corpus_stdout did not build the pipeline")
    else:
        expected_kinds = ["gc", "deopt", "component:compiler",
                          "component:compiler", "component:gc"]
        if captured[0].stage_kinds != expected_kinds:
            failures.append(
                f"the streaming stage sequence is "
                f"{captured[0].stage_kinds}, expected {expected_kinds}")
    return failures


def check_corpus_artifact_failure_orchestration(module) -> List[str]:
    """(S5/Failure) An unwritable required case artifact is a named red reason.

    The bounded program prefix and the reproducing command are the auditable
    evidence of a delivered corpus case, so an artifact that cannot be written
    has to be named at the case boundary and reach the verdict, summary and
    console; it must not be silently discarded while the case advertises a
    successful result. The check runs the real `main --corpus` path (only the
    VM process is faked) with a directory occupying each artifact destination,
    and once with an independently injected invalidity whose reason must
    survive the artifact failure unchanged.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    ark_root = _fake_ark_root()
    env_name = module.CORPUS_ROOT_ENV_VAR
    rel = CORPUS_CASES[1]
    annotations = "//! GC_COUNT_GE * 1\n//! DEOPT_COUNT_GE *Deoptimize* 0\n"
    stdout = (
        "[compiler] ======== ArkSteedCompilerTask: Starts compiling: m ========\n"
        "[compiler] >>> Running method: rec.m\n"
        "[gc] headline GCReason: reason\n"
        "[gc] HandleStorage: 1\n"
        "[compiler] Deoptimize at first (a.js:1:1)\n"
        "\n"
        "program line 1\n"
    )
    workspace = _temp_dir("arksteed-self-test-artifact-descriptors-") / "test"
    workspace.mkdir(parents=True)

    def fake_popen(*args, **kwargs):
        return _FakeProc(stdout.encode("utf-8"), b"")

    def fake_compile(*args, **kwargs):
        return True, "", False, []

    with _patched(module, TEST_DIR=workspace):
        _write_descriptor(workspace, rel, annotations)
        case = _corpus_case_with_invariant(module, loaded, rel)
        module.parse_corpus_descriptor(
            case, None, set(module.corpus_class_defaults(loaded)))
    stem = module.corpus_case_stem(case)
    case_rel = case.rel
    suffixes = (module.CORPUS_PROGRAM_OUTPUT_SUFFIX,
                module.CORPUS_CLASS_COMMAND_SUFFIX)

    def run(artifacts, block=(), poison=False):
        for suffix in block:
            (artifacts / case_rel / f"{stem}{suffix}").mkdir(
                parents=True, exist_ok=True)
        patches = dict(
            ARK_ROOT=ark_root, build_ark=lambda cfg, skip: True,
            make_artifacts_root=lambda *a: artifacts,
            compile_corpus_case=fake_compile,
            subprocess=_SubprocessShim(fake_popen), TEST_DIR=workspace)
        if poison:
            real_execute = module.run_corpus_class_execution

            def poisoned_execute(*args, **kwargs):
                execution = real_execute(*args, **kwargs)
                execution.evidence.record_candidate_capped = True
                return execution

            patches["run_corpus_class_execution"] = poisoned_execute
        with _patched(module, **patches):
            return _call_main(
                module, ["-p", "x64", "-m", "debug", "--corpus",
                         "-I", f"{module.CORPUS_NAMESPACE}/{rel}"],
                env={env_name: str(root)})

    # Control arm: the same orchestration without a blocked destination is
    # green and writes both required artifacts, so the red results below are
    # caused by the artifact failure alone.
    control = _temp_dir("arksteed-self-test-artifacts-")
    rc, output = run(control)
    if rc != 0:
        failures.append(f"the artifact control run returned {rc}: {output[-300:]!r}")
    for suffix in suffixes:
        if not (control / case_rel / f"{stem}{suffix}").is_file():
            failures.append(f"the control run wrote no {suffix} artifact")

    for suffix in suffixes:
        artifacts = _temp_dir("arksteed-self-test-artifacts-")
        rc, output = run(artifacts, block=(suffix,))
        if rc != 1:
            failures.append(
                f"a blocked {suffix} artifact returned {rc}: {output[-300:]!r}")
        if f"cannot write case artifact {stem}{suffix}" not in output:
            failures.append(
                f"a blocked {suffix} artifact is not named: {output[-300:]!r}")

    # An invalid retained prefix and a blocked artifact are separate reasons:
    # the execution invalidity must survive the artifact failure and stay
    # named.
    artifacts = _temp_dir("arksteed-self-test-artifacts-")
    rc, output = run(artifacts, block=(module.CORPUS_PROGRAM_OUTPUT_SUFFIX,),
                     poison=True)
    if rc != 1:
        failures.append(f"the invalid-prefix artifact run returned {rc}")
    for reason in ("record candidate exceeds cap",
                   f"cannot write case artifact {stem}"
                   f"{module.CORPUS_PROGRAM_OUTPUT_SUFFIX}"):
        if reason not in output:
            failures.append(
                f"the {reason!r} reason is missing: {output[-400:]!r}")
    return failures


def check_corpus_streaming_scaling(module) -> List[str]:
    """(S2/S3) A growing candidate costs linear work and bounded retention.

    The unresolved window appends without copying its stored text and answers
    its cursor searches without rescanning the whole window, so feeding a
    below-cap candidate one character at a time costs work proportional to the
    stream: quadrupling the candidate must not multiply the work by sixteen,
    which is what a growing immutable string does. The same volume in normal
    read chunks stays fast and its peak retention stays bounded by the window.
    """
    failures: List[str] = []
    case = _corpus_stream_case(module, "//! GC_COUNT_GE * 1\n")

    def stream(blocks: int, chunk: int):
        yield "[gc] headline GCReason: r\n"
        for _ in range(blocks):
            yield "z" * chunk

    def measure(blocks: int, chunk: int):
        tracemalloc.start()
        try:
            # CPU time, not wall time: host contention on a shared build machine
            # must not turn a linear run into a false failure.
            start = time.process_time()
            with tempfile.NamedTemporaryFile("wb") as log_file:
                evidence = module.consume_corpus_stdout(
                    stream(blocks, chunk), case, [], log_file,
                    record_candidate_cap=64 * 1024 * 1024,
                    program_cap=64 * 1024 * 1024)
            elapsed = time.process_time() - start
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        if evidence.record_candidate_capped or evidence.program_output_error:
            failures.append(f"the below-cap candidate was invalid: {evidence!r}")
        return elapsed, peak

    small, _ = measure(50000, 1)
    large, _ = measure(200000, 1)
    if small > 0.02 and large > small * 8:
        failures.append(
            f"the single-character candidate partition is not linear: "
            f"{small:.3f}s for 50000 vs {large:.3f}s for 200000")
    chunk_elapsed, chunk_peak = measure(64, 65536)
    if chunk_elapsed > 5.0:
        failures.append(
            f"the 64 KiB candidate partition took {chunk_elapsed:.3f}s")
    if chunk_peak > 32 * 1024 * 1024:
        failures.append(
            f"the 64 KiB candidate retained {chunk_peak} bytes of window")

    # An unterminated closer keeps its examined suffix charged. With a small
    # candidate limit the first excess disables and frees the pipeline, so the
    # retained window stays bounded by the limit instead of growing with the
    # stream length.
    tracemalloc.start()
    try:
        with tempfile.NamedTemporaryFile("wb") as log_file:
            evidence = module.consume_corpus_stdout(
                stream(2000, 4096), case, [], log_file,
                record_candidate_cap=1024 * 1024,
                program_cap=64 * 1024 * 1024)
        _, unterminated_peak = tracemalloc.get_traced_memory()
    finally:
        tracemalloc.stop()
    if not evidence.record_candidate_capped:
        failures.append("the unterminated closer was not capped")
    if unterminated_peak > 16 * 1024 * 1024:
        failures.append(
            f"the unterminated closer retained {unterminated_peak} bytes of "
            f"window")
    return failures


def check_corpus_filter_long_literal(module) -> List[str]:
    """(S2/review) A literal longer than a search batch is still exact.

    The segmented window answers literal searches in bounded batches. A needle
    longer than a batch must not skip a possible start, so component literals
    of 512 characters and more give exactly the canonical sequential `sub`
    oracle result under whole, single-character and short-chunk partitions.
    """
    failures: List[str] = []
    case = _filter_case(module, "", "self_test/long_literal")
    for length in (512, 513, 520, 660):
        component = "a" * length
        text = "[" + component + "] diagnostic\nOK\n"
        expected = _oracle_filter(module, case, [component], text)
        if expected != "OK\n":
            failures.append(
                f"the oracle for a {length}-character literal returned "
                f"{expected!r}")
        for label, chunks in (
                ("whole chunk", [text]),
                ("single characters", list(text)),
                ("16-character chunks",
                 [text[index:index + 16]
                  for index in range(0, len(text), 16)]),
                ("512-character chunks",
                 [text[index:index + 512]
                  for index in range(0, len(text), 512)])):
            evidence = _consume(module, case, [component], chunks,
                                program_cap=1024 * 1024)
            if evidence.program_output != expected:
                failures.append(
                    f"the {length}-character literal under {label} gave "
                    f"{evidence.program_output[:48]!r}, expected {expected!r}")
            if not module.corpus_program_output_usable(evidence):
                failures.append(
                    f"the {length}-character literal under {label} was not "
                    f"usable: {evidence!r}")
    # The literal may also start later in the stream, so its span crosses the
    # segmented-window boundaries rather than starting at the window head.
    component = "a" * 600
    for offset in (510, 511, 512, 513):
        text = "f" * offset + "[" + component + "] payload\nOK\n"
        expected = _oracle_filter(module, case, [component], text)
        for label, chunks in (
                ("whole chunk", [text]),
                ("single characters", list(text)),
                ("16-character chunks",
                 [text[index:index + 16]
                  for index in range(0, len(text), 16)])):
            evidence = _consume(module, case, [component], chunks,
                                program_cap=1024 * 1024)
            if evidence.program_output != expected:
                failures.append(
                    f"the offset-{offset} literal under {label} gave "
                    f"{evidence.program_output[:48]!r}, expected {expected!r}")
    return failures


# ---------------------------------------------------------------------------
# Corpus differential twins (ISSUE-0094 / TW1, TW2, TW6): the twin command is
# the actual class command with its single JIT token turned off, two fresh
# sequential interpreter processes run in the per-case class budget through the
# shared streaming entry point, and every execution fact - both separate limit
# flags, the collection error, the raw marker counts and the duration - survives
# into the twin result. The eligibility gate decides before any process exists.
# ---------------------------------------------------------------------------


def _twin_class_command(module) -> List[str]:
    """A class command with exactly one explicit `--compiler-enable-jit`."""
    return ["ark_js_vm", f"{module.CORPUS_JIT_OPTION}=true",
            "--log-level=info", "--log-components=compiler",
            f"{module.CORPUS_EXEC_MARKER_OPTION}=true",
            "--entry-point=air", "case.abc"]


def _independent_twin_command(module, class_command: List[str]) -> List[str]:
    """Expected interpreter twin, derived without the runner's producer.

    The class command with the value of its single explicit
    `--compiler-enable-jit` token replaced by `false`, computed here rather
    than by `module.derive_corpus_twin_command`. An expectation produced by the
    same producer as the executed command would move together with any
    producer regression, so a changed abc, entry point or logging observation
    could never fail the comparison.
    """
    on = f"{module.CORPUS_JIT_OPTION}=true"
    off = f"{module.CORPUS_JIT_OPTION}={module.CORPUS_JIT_OFF_VALUE}"
    return [off if token == on else token for token in class_command]


def check_corpus_twin_command_identity(module) -> List[str]:
    """(TW1) The twin command is the actual class command, one token off."""
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    case = _corpus_case_with_invariant(module, loaded, CORPUS_CASES[0])
    cfg = module.BuildConfig("debug", "x64")
    command, _ = module.build_corpus_vm_command(
        case, Path("/tmp/arksteed-self-test.abc"), cfg, CORPUS_CLASS_ARGS, loaded)
    jit = [index for index, token in enumerate(command)
           if module._option_name(token) == module.CORPUS_JIT_OPTION]
    if len(jit) != 1:
        return [f"the class command has {len(jit)} JIT tokens: {' '.join(command)}"]
    # The neighbouring lazy-deopt option must never be mistaken for the switch.
    lazy = f"{module.CORPUS_JIT_OPTION}-lazy-deopt=true"
    if module._option_name(lazy) == module.CORPUS_JIT_OPTION:
        failures.append("_option_name confuses the JIT switch with its neighbour")
    if not any(module._option_name(token) == f"{module.CORPUS_JIT_OPTION}-lazy-deopt"
               for token in command):
        failures.append(f"the class command misses the lazy-deopt neighbour: {command}")

    twin = module.derive_corpus_twin_command(command)
    if len(twin) != len(command):
        failures.append("the twin command changed the token count")
    differing = [index for index in range(min(len(twin), len(command)))
                 if twin[index] != command[index]]
    expected = f"{module.CORPUS_JIT_OPTION}={module.CORPUS_JIT_OFF_VALUE}"
    if len(differing) != 1:
        failures.append(
            f"the twin command differs in {len(differing)} token(s): {differing}")
    elif twin[differing[0]] != expected:
        failures.append(
            f"the twin JIT token is {twin[differing[0]]!r}, expected {expected!r}")
    for index in range(min(len(twin), len(command))):
        if differing and index == differing[0]:
            continue
        if twin[index] != command[index]:
            failures.append(
                f"the twin command changed unrelated token {index}: "
                f"{command[index]!r} -> {twin[index]!r}")
    if twin[-1] != command[-1]:
        failures.append(f"the twin command changed the abc: {twin[-1]!r}")
    if f"{module.CORPUS_EXEC_MARKER_OPTION}=true" not in twin:
        failures.append("the twin command dropped the class observation")

    # The space-separated value is replaced in place; a bare flag (which means
    # `true`) becomes an explicit `false`.
    spaced = module.derive_corpus_twin_command(
        ["vm", module.CORPUS_JIT_OPTION, "true", "case.abc"])
    if spaced != ["vm", module.CORPUS_JIT_OPTION, module.CORPUS_JIT_OFF_VALUE,
                  "case.abc"]:
        failures.append(f"the space-separated twin is {spaced!r}")
    bare = module.derive_corpus_twin_command(["vm", module.CORPUS_JIT_OPTION])
    if bare != ["vm", expected]:
        failures.append(f"the bare-flag twin is {bare!r}")

    # Absence and duplication are named usage errors that never yield a twin.
    without = [token for token in command
               if module._option_name(token) != module.CORPUS_JIT_OPTION]
    for label, broken in (("absent", without),
                          ("duplicated",
                           command + [f"{module.CORPUS_JIT_OPTION}=true"])):
        message = module.corpus_twin_command_usage_error(broken)
        if message is None or module.CORPUS_JIT_OPTION not in message:
            failures.append(f"the {label} JIT token is not named: {message!r}")
        raised = False
        try:
            module.derive_corpus_twin_command(broken)
        except module.CorpusTwinCommandError:
            raised = True
        if not raised:
            failures.append(f"deriving a twin from a {label} JIT token did not fail")

    # The run-level check turns the same condition into usage code 2 before any
    # execution: a corpus module whose class defaults carry no JIT token.
    broken_source = _CORPUS_MODULE_SOURCE.replace(
        '"--compiler-enable-jit=true"', '"--asm-interpreter=true"')
    broken_root = _write_corpus_root(
        cases=CORPUS_CASES, name="no-jit", module_source=broken_source)
    broken_loaded, load_error = module.load_corpus_module(broken_root)
    if load_error is not None:
        return failures + [f"the broken synthetic module did not load: {load_error}"]
    calls: List[object] = []

    def never_popen(*args, **kwargs):
        calls.append(args)
        raise AssertionError("no execution may happen without a JIT token")

    with _patched(module, ARK_ROOT=_fake_ark_root(),
                  build_ark=lambda build_cfg, skip: True,
                  make_artifacts_root=lambda *a: _temp_dir(
                      "arksteed-self-test-artifacts-"),
                  load_corpus_module=lambda r: (broken_loaded, None),
                  subprocess=_SubprocessShim(never_popen)):
        rc, output = _call_main(
            module, ["-p", "x64", "-m", "debug", "--corpus"],
            env={module.CORPUS_ROOT_ENV_VAR: str(broken_root)})
    if rc != 2:
        failures.append(f"an absent JIT token returned {rc}: {output[-300:]!r}")
    if module.CORPUS_JIT_OPTION not in output:
        failures.append(f"the absent JIT token is not named: {output[-300:]!r}")
    if calls:
        failures.append("an absent JIT token reached the executor")
    return failures


def check_corpus_twin_command_preflight(module) -> List[str]:
    """(TW1) The *actual* per-case class command is validated before execution.

    Validating the derived defaults is not sufficient: the command the executor
    launches can differ from them. `main` therefore constructs the actual
    class command of every selected case from the same per-case inputs the case
    loop uses, rejects an absent or duplicated `--compiler-enable-jit` token
    with a named usage error (code 2) before any class or twin process exists,
    and keeps the validated command so the case loop executes exactly it. The
    class executor refuses to launch such a command on the direct entry point
    as well. Only the builder's returned command is changed here; the synthetic
    module's defaults stay valid, so a defaults-only check cannot pass these
    probes.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    ark_root = _fake_ark_root()
    env_name = module.CORPUS_ROOT_ENV_VAR
    rel = CORPUS_CASES[0]
    default_error = module.corpus_twin_command_usage_error(
        module.corpus_class_command_options(loaded))
    if default_error is not None:
        return [f"the synthetic module defaults are not a valid JIT base: "
                f"{default_error}"]

    real_build = module.build_corpus_vm_command

    def without_jit(command):
        return [token for token in command
                if module._option_name(token) != module.CORPUS_JIT_OPTION]

    def duplicated_jit(command):
        return list(command) + [f"{module.CORPUS_JIT_OPTION}=true"]

    for label, mutate in (("absent", without_jit),
                          ("duplicated", duplicated_jit)):
        def broken_build(*args, mutate_fn=mutate, **kwargs):
            command, components = real_build(*args, **kwargs)
            return mutate_fn(list(command)), list(components)

        calls: List[object] = []

        def fake_popen(cmd, **kwargs):
            calls.append(list(cmd))
            return _FakeProc(b"program\n")

        artifacts = _temp_dir("arksteed-self-test-artifacts-")
        with _patched(module, ARK_ROOT=ark_root, build_ark=lambda cfg, skip: True,
                      make_artifacts_root=lambda *a: artifacts,
                      load_corpus_module=lambda r: (loaded, None),
                      build_corpus_vm_command=broken_build,
                      subprocess=_SubprocessShim(fake_popen)):
            rc, output = _call_main(
                module, ["-p", "x64", "-m", "debug", "--corpus",
                         "-I", f"{module.CORPUS_NAMESPACE}/{rel}"],
                env={env_name: str(root)})
        if rc != 2:
            failures.append(
                f"a {label} JIT token in the actual command returned {rc}: "
                f"{output[-300:]!r}")
        if module.CORPUS_JIT_OPTION not in output:
            failures.append(
                f"a {label} JIT token in the actual command is not named: "
                f"{output[-300:]!r}")
        if calls:
            failures.append(
                f"a {label} JIT token in the actual command reached the "
                f"executor: {calls}")

    # The class executor itself never launches a command whose JIT token is
    # invalid: a direct entry point cannot report a green run either.
    cfg = module.BuildConfig("debug", "x64")
    case = _corpus_case_with_invariant(module, loaded, rel)
    context = module.CorpusContext(
        module=loaded, root=root, revision=None,
        build_config=module.build_reference_config(loaded, cfg),
        reference_args=module.build_reference_namespace(loaded, cfg),
        class_defaults=module.corpus_class_defaults(loaded), valid_opcodes=None)
    case_artifacts = _temp_dir("arksteed-self-test-twin-preflight-") / "case"
    case_artifacts.mkdir(parents=True, exist_ok=True)
    for label, mutate in (("absent", without_jit),
                          ("duplicated", duplicated_jit)):
        def broken_build(*args, mutate_fn=mutate, **kwargs):
            command, components = real_build(*args, **kwargs)
            return mutate_fn(list(command)), list(components)

        spawned: List[object] = []

        def fake_popen(cmd, **kwargs):
            spawned.append(list(cmd))
            return _FakeProc(b"program\n")

        with _patched(module, ARK_ROOT=ark_root,
                      build_corpus_vm_command=broken_build,
                      subprocess=_SubprocessShim(fake_popen)):
            execution = module.run_corpus_class_execution(
                case, Path("/tmp/arksteed-self-test.abc"), cfg,
                CORPUS_CLASS_ARGS, context, 30, case_artifacts, [])
        if execution.started:
            failures.append(
                f"the executor launched a {label} JIT command")
        if execution.command_error is None or module.CORPUS_JIT_OPTION \
                not in execution.command_error:
            failures.append(
                f"the executor did not name the {label} JIT token: "
                f"{execution.command_error!r}")
        if spawned:
            failures.append(
                f"the executor launched a {label} JIT command: {spawned}")

    # The validated pre-flight command is the executed one: the class execution
    # receives exactly the command the pre-flight built and accepted, and no
    # second construction can drift from it.
    built_commands: List[List[str]] = []
    executed_commands: List[List[str]] = []

    def recording_build(*args, **kwargs):
        command, components = real_build(*args, **kwargs)
        built_commands.append(list(command))
        return list(command), list(components)

    def recording_stream(command, components, case_, explicit_components,
                         budget, env, log_path):
        executed_commands.append(list(command))
        return _corpus_class_execution(module, command=list(command),
                                       program_output="program\n")

    artifacts = _temp_dir("arksteed-self-test-artifacts-")
    with _patched(module, ARK_ROOT=ark_root, build_ark=lambda build_cfg, skip: True,
                  make_artifacts_root=lambda *a: artifacts,
                  compile_corpus_case=lambda *a, **k: (True, "", False, []),
                  load_corpus_module=lambda r: (loaded, None),
                  build_corpus_vm_command=recording_build,
                  run_corpus_stream_execution=recording_stream):
        rc, output = _call_main(
            module, ["-p", "x64", "-m", "debug", "--corpus",
                     "-I", f"{module.CORPUS_NAMESPACE}/{rel}"],
            env={env_name: str(root)})
    if rc != 0:
        failures.append(
            f"the prepared-command run returned {rc}: {output[-300:]!r}")
    if len(built_commands) != 1:
        failures.append(
            f"the pre-flight built {len(built_commands)} command(s), expected 1")
        return failures
    if len(executed_commands) != 3:
        failures.append(
            f"the prepared run executed {len(executed_commands)} command(s), "
            "expected 3 (class + two twins)")
    elif executed_commands[0] != built_commands[0]:
        failures.append(
            "the executed class command is not the validated pre-flight "
            f"command:\n  built {built_commands[0]}\n  exec  "
            f"{executed_commands[0]}")
    else:
        expected_twin = module.derive_corpus_twin_command(built_commands[0])
        if executed_commands[1] != expected_twin \
                or executed_commands[2] != expected_twin:
            failures.append(
                f"the executed twin commands are {executed_commands[1:]}, "
                f"expected {expected_twin}")
    return failures


def check_corpus_config_error_command_preflight(module) -> List[str]:
    """(TW1/C5) A descriptor config error is not replaced by a usage error.

    The actual-command pre-flight validates the commands of *executable* cases
    only. A case whose descriptor gave a config error has no executions at all
    (C5) and its required per-case diagnostic is the config error itself; it
    cannot run a class or a twin, so it is outside the twin pre-flight. A
    pre-flight that validates such a case anyway replaces both diagnostics
    with a run-level usage error (code 2), aborts before ordinary case
    reporting and prevents the other selected cases from running. A duplicated
    forbidden class-managed `PARAMS` option is exactly such a case: the
    descriptor cannot set a class default (C5). This check selects it next to
    an executable companion and requires exit 1, both configuration
    diagnostics, ordinary reporting, the companion run and zero executions for
    the config-error case.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    ark_root = _fake_ark_root()
    workspace = _temp_dir("arksteed-self-test-descriptors-") / "test"
    broken_rel = CORPUS_CASES[0]
    companion_rel = CORPUS_CASES[1]
    broken_id = f"{module.CORPUS_NAMESPACE}/{broken_rel}"
    companion_id = f"{module.CORPUS_NAMESPACE}/{companion_rel}"
    _write_descriptor(
        workspace, broken_rel,
        f"//! PARAMS {module.CORPUS_JIT_OPTION}=true "
        f"{module.CORPUS_JIT_OPTION}=false\n")
    artifacts = _temp_dir("arksteed-self-test-artifacts-")
    env_name = module.CORPUS_ROOT_ENV_VAR
    loaded.RUN_CALLS.clear()
    class_cases: List[str] = []
    class_execution = _corpus_class_execution(
        module, command=_twin_class_command(module), program_output="program\n")

    def fake_class_execution(case, *args, **kwargs):
        class_cases.append(case.rel)
        return class_execution

    def fake_stream_execution(command, *args, **kwargs):
        return _corpus_class_execution(
            module, command=list(command), program_output="program\n")

    with _patched(module, ARK_ROOT=ark_root, TEST_DIR=workspace,
                  build_ark=lambda cfg, skip: True,
                  make_artifacts_root=lambda *a: artifacts,
                  load_corpus_module=lambda r: (loaded, None),
                  compile_corpus_case=lambda *a, **k: (True, "", False, []),
                  run_corpus_class_execution=fake_class_execution,
                  run_corpus_stream_execution=fake_stream_execution):
        rc, output = _call_main(
            module, ["-p", "x64", "-m", "debug", "--corpus",
                     "-I", f"{module.CORPUS_NAMESPACE}/{broken_rel}",
                     "-I", f"{module.CORPUS_NAMESPACE}/{companion_rel}"],
            env={env_name: str(root)})
    if rc != 1:
        failures.append(
            f"a config-error descriptor next to an executable case returned "
            f"{rc}, expected 1: {output[-400:]!r}")
    diagnostic = (f"'{module.CORPUS_JIT_OPTION}' is a "
                  f"{module.CASE_CLASS_CORPUS} class default (class-managed) "
                  "and cannot be set via PARAMS")
    if output.count(diagnostic) != 2:
        failures.append(
            f"the two class-managed config diagnostics are not both reported "
            f"({output.count(diagnostic)} of 2): {output[-400:]!r}")
    if f"[FAIL] {broken_id}" not in output:
        failures.append(
            f"the config-error case is not reported as an ordinary red case: "
            f"{output[-400:]!r}")
    if f"parity: {module.CORPUS_PARITY_NOT_APPLICABLE}" not in output:
        failures.append(
            f"the not-applicable parity of the config-error case is missing: "
            f"{output[-400:]!r}")
    if broken_id in class_cases:
        failures.append(
            f"the config-error case executed a class process: {class_cases}")
    if companion_id not in class_cases:
        failures.append(
            f"the executable companion did not run: {class_cases}")
    if not any(call[0] == companion_rel for call in loaded.RUN_CALLS):
        failures.append(
            f"the executable companion executed no reference: "
            f"{loaded.RUN_CALLS}")
    return failures


def check_corpus_twin_execution(module) -> List[str]:
    """(TW2/TW6) Eligibility, two sequential runs and surviving facts."""
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    rel = CORPUS_CASES[0]
    case = _corpus_case_with_invariant(module, loaded, rel)
    cfg = module.BuildConfig("debug", "x64")
    case_artifacts = _temp_dir("arksteed-self-test-twin-artifacts-") / "case"
    case_artifacts.mkdir(parents=True, exist_ok=True)
    stem = module.corpus_case_stem(case)

    # The execution result shape must keep every invalidity flag and every
    # count: a narrowing conversion would not expose it to the classifier.
    evidence_fields = {field.name
                       for field in dataclasses.fields(module.CorpusStreamEvidence)}
    for name in ("program_output", "program_output_bytes",
                 "program_output_truncated", "record_candidate_capped",
                 "program_output_error", "compile_markers", "exec_markers",
                 "compiler_log_bytes", "compiler_log_capped", "named_counts"):
        if name not in evidence_fields:
            failures.append(f"CorpusStreamEvidence lost the field '{name}'")
    execution_fields = {field.name
                        for field in dataclasses.fields(module.CorpusClassExecution)}
    for name in ("command", "components", "returncode", "timed_out", "started",
                 "start_error", "evidence", "compiler_log_path", "duration"):
        if name not in execution_fields:
            failures.append(f"CorpusClassExecution lost the field '{name}'")

    # Ineligibility: no process is created for an absent, unstarted, unusable or
    # empty class output, and the reason is named. An invalid empty retained
    # output is never a skip.
    empty_usable = module.CorpusClassExecution(
        started=True, evidence=module.CorpusStreamEvidence(program_output=""))
    ineligible = (
        ("absent", None, module.CORPUS_DIFFERENTIAL_NOT_COMPARABLE),
        ("unstarted",
         module.CorpusClassExecution(started=False, start_error="no vm"),
         module.CORPUS_DIFFERENTIAL_NOT_COMPARABLE),
        ("truncated",
         module.CorpusClassExecution(
             started=True,
             evidence=module.CorpusStreamEvidence(
                 program_output="prefix\n", program_output_truncated=True)),
         module.CORPUS_DIFFERENTIAL_NOT_COMPARABLE),
        ("candidate-capped",
         module.CorpusClassExecution(
             started=True,
             evidence=module.CorpusStreamEvidence(
                 program_output="", record_candidate_capped=True)),
         module.CORPUS_DIFFERENTIAL_NOT_COMPARABLE),
        ("collection-error",
         module.CorpusClassExecution(
             started=True,
             evidence=module.CorpusStreamEvidence(
                 program_output="", program_output_error="boom")),
         module.CORPUS_DIFFERENTIAL_NOT_COMPARABLE),
        ("empty", empty_usable, module.CORPUS_DIFFERENTIAL_SKIPPED_EMPTY),
    )
    spawned: List[object] = []

    def recording_popen(*args, **kwargs):
        spawned.append(args)
        return _FakeProc(b"")

    for label, class_execution, expected_status in ineligible:
        with _patched(module.subprocess, Popen=recording_popen):
            run = module.run_corpus_twin_execution(
                case, cfg, CORPUS_CLASS_ARGS, 30, class_execution, case_artifacts, [])
        if run.status != expected_status:
            failures.append(
                f"the {label} class output gave status {run.status!r}, "
                f"expected {expected_status!r}")
        if run.executions:
            failures.append(f"the {label} class output ran twin executions")
        if not run.reasons:
            failures.append(f"the {label} class output has no named reason")
    if spawned:
        failures.append("an ineligible class output reached the executor")

    # A usable nonempty class output runs exactly two fresh sequential
    # processes with the derived command, the class environment, the abc and
    # the whole per-case class budget.
    class_execution = _corpus_class_execution(
        module, command=_twin_class_command(module), program_output="program\n")
    invocations: List[Dict[str, object]] = []
    budgets: List[object] = []
    order: List[str] = []
    real_stream = module.run_corpus_stream_execution

    def recording_stream(*args, **kwargs):
        budgets.append(args[4])
        return real_stream(*args, **kwargs)

    def fake_popen(cmd, stdout=None, stderr=None, env=None):
        order.append("popen")
        invocations.append({"cmd": list(cmd), "env": dict(env or {})})
        proc = _FakeProc(
            b"[compiler] ======== ArkSteedCompilerTask: Starts compiling: m "
            b"========\n[compiler] >>> Running method: rec.m\nprogram\n")
        original_wait = proc.wait

        def wait():
            order.append("wait")
            return original_wait()

        proc.wait = wait
        return proc

    with _patched(module, run_corpus_stream_execution=recording_stream), \
            _patched(module.subprocess, Popen=fake_popen):
        run = module.run_corpus_twin_execution(
            case, cfg, CORPUS_CLASS_ARGS, 30, class_execution, case_artifacts, [])
    expected_twin = module.derive_corpus_twin_command(class_execution.command)
    if run.status is not None:
        failures.append(f"an eligible class output gave status {run.status!r}")
    if len(run.executions) != 2 or len(invocations) != 2:
        failures.append(
            f"the eligible run made {len(invocations)} invocation(s) and "
            f"{len(run.executions)} result(s), expected 2 and 2")
    if budgets != [30, 30]:
        failures.append(
            f"the twins received budgets {budgets}, expected [30, 30]")
    if order != ["popen", "wait", "popen", "wait"]:
        failures.append(f"the twins did not run sequentially: {order}")
    for index, invocation in enumerate(invocations):
        if invocation["cmd"] != expected_twin:
            failures.append(
                f"twin {index + 1} command is not the derived twin: "
                f"{invocation['cmd']}")
        if invocation["env"] != module.corpus_execution_environment(cfg):
            failures.append(f"twin {index + 1} environment differs from the class")
    for execution in run.executions:
        if not execution.started or execution.returncode != 0:
            failures.append(f"a twin did not run cleanly: {execution!r}")
        if execution.evidence.program_output != "program\n":
            failures.append(
                f"a twin filtered output is {execution.evidence.program_output!r}")
        if execution.evidence.compile_markers != 1 \
                or execution.evidence.exec_markers != 1:
            failures.append(f"a twin lost its raw counts: {execution.evidence!r}")
        if execution.duration < 0:
            failures.append("a twin duration is negative")
    for suffix in module.CORPUS_TWIN_LOG_SUFFIXES:
        if (case_artifacts / f"{stem}{suffix}").exists():
            failures.append(f"the temporary twin log {suffix} was not removed")

    # A failure of the first twin does not suppress the second: both commands
    # and both outcomes stay observable.
    attempts: List[int] = []

    def failing_first(cmd, **kwargs):
        attempts.append(len(attempts) + 1)
        if len(attempts) == 1:
            raise OSError("twin 1 could not start")
        return _FakeProc(b"program\n")

    with _patched(module.subprocess, Popen=failing_first):
        run = module.run_corpus_twin_execution(
            case, cfg, CORPUS_CLASS_ARGS, 30, class_execution, case_artifacts, [])
    if len(attempts) != 2 or len(run.executions) != 2:
        failures.append(
            f"twin 1 failure gave {len(attempts)} attempt(s), "
            f"{len(run.executions)} result(s)")
    if run.executions and (run.executions[0].started
                           or run.executions[0].start_error is None):
        failures.append("a twin start failure is not recorded")
    if len(run.executions) > 1 and not run.executions[1].started:
        failures.append("twin 2 was suppressed by the twin 1 start failure")
    if [run_execution.command for run_execution in run.executions] != [
            expected_twin, expected_twin]:
        failures.append("the failed attempt did not retain its command")

    # A crash or an ordinary nonzero exit of twin 1 is only a fact here; twin 2
    # is still attempted.
    codes = [(-6), (3)]

    def twin_codes(cmd, **kwargs):
        return _FakeProc(b"program\n", returncode=codes.pop(0))

    with _patched(module.subprocess, Popen=twin_codes):
        run = module.run_corpus_twin_execution(
            case, cfg, CORPUS_CLASS_ARGS, 30, class_execution, case_artifacts, [])
    if len(run.executions) != 2 or [run_execution.returncode
                                    for run_execution in run.executions] != [-6, 3]:
        failures.append(
            f"twin return codes are {[e.returncode for e in run.executions]}")

    # The executor's own timer times out and kills each twin in turn.
    blocking: List[_BlockingProc] = []

    def blocking_popen(cmd, **kwargs):
        proc = _BlockingProc()
        blocking.append(proc)
        return proc

    with _patched(module.subprocess, Popen=blocking_popen):
        run = module.run_corpus_twin_execution(
            case, cfg, CORPUS_CLASS_ARGS, 0.05, class_execution, case_artifacts, [])
    if len(blocking) != 2:
        failures.append(f"the blocking run created {len(blocking)} process(es)")
    for proc in blocking:
        if not proc.killed:
            failures.append("a timed-out twin was not killed")
    if not all(run_execution.timed_out for run_execution in run.executions):
        failures.append(
            f"a twin timeout is not recorded: "
            f"{[e.timed_out for e in run.executions]}")

    # Everything the shared entry point collects survives into the twin
    # result: the program-output cap, the collection error and the record
    # candidate cap, each with its retained prefix and counts.
    def over_cap_popen(cmd, **kwargs):
        return _FakeProc(b"x" * (module.CORPUS_PROGRAM_OUTPUT_CAP + 4096))

    with _patched(module.subprocess, Popen=over_cap_popen):
        run = module.run_corpus_twin_execution(
            case, cfg, CORPUS_CLASS_ARGS, 30, class_execution, case_artifacts, [])
    for execution in run.executions:
        evidence = execution.evidence
        if not evidence.program_output_truncated:
            failures.append("the twin program-output cap did not survive")
        if evidence.program_output_bytes != module.CORPUS_PROGRAM_OUTPUT_CAP:
            failures.append(
                f"the twin retained {evidence.program_output_bytes} bytes, "
                f"expected {module.CORPUS_PROGRAM_OUTPUT_CAP}")

    def read_failure_popen(cmd, **kwargs):
        return _FakeProc(b"[compiler] >>> Running method: a.b\n",
                         read_error=OSError("twin pipe boom"))

    with _patched(module.subprocess, Popen=read_failure_popen):
        run = module.run_corpus_twin_execution(
            case, cfg, CORPUS_CLASS_ARGS, 30, class_execution, case_artifacts, [])
    for execution in run.executions:
        if "read" not in (execution.evidence.program_output_error or ""):
            failures.append(
                f"the twin collection error did not survive: {execution.evidence!r}")

    # The record candidate cap needs its own case (the gc record stage) and a
    # genuinely unresolved candidate above the class limit.
    gc_case = _filter_case(module, "//! GC_COUNT_GE * 1\n", "self_test/twin_gc")
    unresolved = ("[gc] headline GCReason: reason\n"
                  + "z" * (module.CORPUS_RECORD_CANDIDATE_CAP + 16) + "\n")

    def candidate_popen(cmd, **kwargs):
        return _FakeProc(unresolved.encode("utf-8"))

    with _patched(module.subprocess, Popen=candidate_popen):
        run = module.run_corpus_twin_execution(
            gc_case, cfg, CORPUS_CLASS_ARGS, 30, class_execution, case_artifacts, [])
    for execution in run.executions:
        if not execution.evidence.record_candidate_capped:
            failures.append(
                "the twin record-candidate cap did not survive: "
                f"{execution.evidence!r}")

    # The descriptor keeps its existing vocabulary: no mask, opt-out, status
    # override or numeric tolerance key is added (TW8).
    if tuple(module.CORPUS_DESCRIPTOR_KEYWORDS) != ("TIMEOUT", "NO_EXEC_MARKER"):
        failures.append(
            "the descriptor gained a differential key: "
            f"{module.CORPUS_DESCRIPTOR_KEYWORDS}")

    # Isolation (TW8): a run without `--corpus` never reaches the twin entry
    # point, the twin command derivation or the corpus streaming executor.
    touched: List[str] = []

    def corpus_only(target_name):
        def _fail(*args, **kwargs):
            touched.append(target_name)
            raise AssertionError(f"{target_name} ran without --corpus")
        return _fail

    with _patched(module, ARK_ROOT=_fake_ark_root(),
                  TEST_DIR=_fake_embedded_tree(),
                  make_artifacts_root=lambda *a: _temp_dir(
                      "arksteed-self-test-artifacts-"),
                  build_ark=lambda build_cfg, skip: True,
                  run_corpus_twin_execution=corpus_only("twin"),
                  run_corpus_stream_execution=corpus_only("stream"),
                  build_corpus_vm_command=corpus_only("command")):
        rc, output = _call_main(module, ["-p", "x64", "-m", "debug", "-F"])
    if rc not in (0, 1):
        failures.append(
            f"a default run reached the twin path: {rc} {output[-200:]!r}")
    if touched:
        failures.append(f"a default run called {touched}")
    return failures


def check_corpus_twin_combined_consumer(module) -> List[str]:
    """(combined T1) The twin path uses the exact shared filter chain.

    A synthetic twin stream carrying a multi-line GC record, a deopt record and
    an embedded component fragment is run through the real twin execution path
    and the real streaming consumer. The filtered text and the raw counts must
    equal the sequential-substitution oracle: a consumer that filters only
    prefixed component lines, loses joined lines or counts markers on the
    filtered text cannot pass.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    rel = CORPUS_CASES[1]
    annotations = "//! GC_COUNT_GE * 1\n//! DEOPT_COUNT_GE *Deoptimize* 0\n"
    stream = (
        "[compiler] ======== ArkSteedCompilerTask: Starts compiling: m ========\n"
        "[compiler] >>> Running method: rec.m\n"
        "[gc] headline GCReason: reason\n"
        "verbose continuation line\n"
        "[gc] HandleStorage: 1\n"
        "[compiler] Deoptimize at first (a.js:1:1)\n"
        "[compiler] Deoptimize at second (a.js:2:1)\n"
        "    at frame (a.js:3:1)\n"
        "\n"
        "[g[compiler] diagnostic\nc] diagnostic\n"
        "program line 1\n"
        "program line 2\n"
    )
    workspace = _temp_dir("arksteed-self-test-twin-descriptors-") / "test"
    workspace.mkdir(parents=True)
    with _patched(module, TEST_DIR=workspace):
        _write_descriptor(workspace, rel, annotations)
        case = _corpus_case_with_invariant(module, loaded, rel)
        module.parse_corpus_descriptor(
            case, None, set(module.corpus_class_defaults(loaded)))
        expected = _oracle_filter(module, case, ["compiler"], stream)
        if expected != "program line 1\nprogram line 2\n":
            failures.append(f"the twin-path oracle is {expected!r}")
        # A prefix-only filter, the rejected alternative, would keep the record
        # continuation and the fragment lines: the oracle comparison above is
        # therefore sensitive to that regression.
        naive = "".join(line for line in stream.splitlines(True)
                        if not line.startswith("[compiler] "))
        if naive == expected:
            failures.append("the prefix-only filter is not distinguishable")
    cfg = module.BuildConfig("debug", "x64")
    case_artifacts = _temp_dir("arksteed-self-test-twin-chain-") / "case"
    case_artifacts.mkdir(parents=True, exist_ok=True)
    class_execution = _corpus_class_execution(
        module, command=_twin_class_command(module), program_output="program\n")

    def fake_popen(cmd, **kwargs):
        return _FakeProc(stream.encode("utf-8"))

    with _patched(module.subprocess, Popen=fake_popen):
        run = module.run_corpus_twin_execution(
            case, cfg, CORPUS_CLASS_ARGS, 30, class_execution, case_artifacts, [])
    if len(run.executions) != 2:
        failures.append(f"the combined twin run made {len(run.executions)} attempt(s)")
    for index, execution in enumerate(run.executions):
        evidence = execution.evidence
        if evidence.program_output != expected:
            failures.append(
                f"twin {index + 1} output is {evidence.program_output!r}, "
                f"expected {expected!r}")
        if evidence.compile_markers != 1 or evidence.exec_markers != 1:
            failures.append(
                f"twin {index + 1} raw counts are "
                f"{evidence.compile_markers}/{evidence.exec_markers}, expected 1/1")
        if ">>> Running method" in evidence.program_output:
            failures.append(f"twin {index + 1} kept an execution marker")
    return failures


def check_corpus_twin_orchestration(module) -> List[str]:
    """(TW2/TW8) `run_corpus_case` runs the twins of an eligible corpus case.

    The corpus case path is the only caller: an eligible class output runs
    exactly two JIT-off twin processes and keeps the run on the result, an
    unusable or empty class output launches none, and a case whose descriptor
    gave a config error has no executions and no twin run at all.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    rel = CORPUS_CASES[0]
    cfg = module.BuildConfig("debug", "x64")
    ark_root = _fake_ark_root()

    def context():
        with _patched(module, ARK_ROOT=ark_root):
            return module.CorpusContext(
                module=loaded, root=root, revision=None,
                build_config=module.build_reference_config(loaded, cfg),
                reference_args=module.build_reference_namespace(loaded, cfg),
                class_defaults=module.corpus_class_defaults(loaded),
                valid_opcodes=None)

    reference_context = context()

    def run_one(class_execution, stage, budget_override=None):
        calls: List[List[str]] = []
        budgets: List[object] = []
        artifacts = _temp_dir("arksteed-self-test-artifacts-")

        def fake_compile(*args, **kwargs):
            return True, "", False, []

        def fake_execute(*args, **kwargs):
            return class_execution

        def fake_popen(cmd, **kwargs):
            calls.append(list(cmd))
            return _FakeProc(b"program\n")

        real_stream = module.run_corpus_stream_execution

        def recording_stream(*args, **kwargs):
            budgets.append(args[4])
            return real_stream(*args, **kwargs)

        case = _corpus_case_with_invariant(module, loaded, rel)
        if stage == "config-error":
            case.config_errors.append("synthetic descriptor error")
        patches = dict(
            ARK_ROOT=ark_root, compile_corpus_case=fake_compile,
            run_corpus_class_execution=fake_execute,
            run_corpus_stream_execution=recording_stream,
            subprocess=_SubprocessShim(fake_popen))
        if budget_override is not None:
            patches["corpus_class_budget"] = lambda m, c, f: (budget_override, None)
        with _patched(module, **patches):
            result = module.run_corpus_case(
                case, cfg, CORPUS_CLASS_ARGS, artifacts, reference_context)
        return result, calls, budgets

    valid_command = _twin_class_command(module)
    # eligible: two JIT-off twins, the run is kept, the whole class budget is
    # forwarded to both and the case stays green
    result, calls, budgets = run_one(
        _corpus_class_execution(module, command=valid_command,
                                program_output="program\n"), "eligible",
        budget_override=123)
    run = result.corpus_twins
    if run is None:
        failures.append("an eligible corpus case has no twin run")
    else:
        if run.status is not None:
            failures.append(f"an eligible corpus case has status {run.status!r}")
        if len(run.executions) != 2:
            failures.append(
                f"an eligible corpus case made {len(run.executions)} twin(s)")
    if len(calls) != 2:
        failures.append(f"an eligible corpus case made {len(calls)} twin call(s)")
    if budgets != [123, 123]:
        failures.append(
            f"the corpus twins received budgets {budgets}, expected [123, 123]")
    if any(not any(token == f"{module.CORPUS_JIT_OPTION}="
                   f"{module.CORPUS_JIT_OFF_VALUE}" for token in cmd)
           for cmd in calls):
        failures.append("an orchestrated twin command does not turn the JIT off")
    if not result.passed:
        failures.append(f"an eligible twin run made the case red: {result.reasons}")

    # unusable and empty class outputs launch no process
    for label, class_execution, expected in (
            ("unusable",
             _corpus_class_execution(
                 module, command=valid_command, program_output="prefix\n",
                 program_output_truncated=True),
             module.CORPUS_DIFFERENTIAL_NOT_COMPARABLE),
            ("empty",
             _corpus_class_execution(module, command=valid_command,
                                     program_output=""),
             module.CORPUS_DIFFERENTIAL_SKIPPED_EMPTY)):
        result, calls, _ = run_one(class_execution, label)
        run = result.corpus_twins
        if run is None or run.status != expected:
            failures.append(
                f"the {label} class output gave twin status "
                f"{None if run is None else run.status!r}, expected {expected!r}")
        if calls:
            failures.append(f"the {label} class output launched {len(calls)} twin(s)")

    # a descriptor config error has no executions and no twin run
    result, calls, _ = run_one(None, "config-error")
    if result.corpus_twins is not None:
        failures.append("a config-error corpus case has a twin run")
    if calls:
        failures.append("a config-error corpus case launched a twin")
    return failures


# ---------------------------------------------------------------------------
# Twin-only reproducibility mask (TW4): the numeric-field grammar, the pure
# inference, the absence of a mask channel and the combined T1+T2 behaviour.
# Everything below is VM-free: it calls the pure functions and the real twin
# execution path with synthetic streams, without a build, a VM or the real
# corpus.
# ---------------------------------------------------------------------------

# The numeric-field grammar of the mask (TW4). Each entry is
# `(label, line, expected numeric spellings, expected exact segments)`; the
# segments count is always one more than the field count.
MASK_GRAMMAR_CASES = (
    ("plain integer", "42", ("42",), ("", "")),
    ("negative integer", "-7", ("-7",), ("", "")),
    ("explicit plus", "+7", ("+7",), ("", "")),
    ("fraction", "3.14", ("3.14",), ("", "")),
    ("leading dot", ".5", (".5",), ("", "")),
    ("trailing dot", "5.", ("5.",), ("", "")),
    ("exponent", "1e5", ("1e5",), ("", "")),
    ("signed exponent", "-2.5E-3", ("-2.5E-3",), ("", "")),
    ("leading-dot exponent", ".5e+2", (".5e+2",), ("", "")),
    ("labelled value", "value=12 ms", ("12",), ("value=", " ms")),
    # Word neighbours and non-decimal spellings stay exact text.
    ("digits inside an identifier", "abc1def", (), ("abc1def",)),
    ("digit followed by a word", "1abc", (), ("1abc",)),
    ("identifier ending in a digit", "abc1", (), ("abc1",)),
    ("hexadecimal spelling", "0x1F", (), ("0x1F",)),
    ("NaN", "NaN", (), ("NaN",)),
    ("Infinity", "Infinity", (), ("Infinity",)),
    ("cyrillic word neighbour", "1\u0430", (), ("1\u0430",)),
    ("cyrillic leading word", "\u04301", (), ("\u04301",)),
    ("dots on both sides", "1.2.3", (), ("1.2.3",)),
    # Ordering, signs and exact (untrimmed) segments.
    ("two fields", "a 1 b 2 c", ("1", "2"), ("a ", " b ", " c")),
    ("signed and exponential fields", "x=-1;y=+2.5e10", ("-1", "+2.5e10"),
     ("x=", ";y=", "")),
    ("spaces and tab preserved", "  spaced  1.5\t", ("1.5",),
     ("  spaced  ", "\t")),
    ("comma separated", "1,2", ("1", "2"), ("", ",", "")),
    ("adjacent minus is a separator", "1-2", ("1", "2"), ("", "-", "")),
    ("run of spaces preserved", "1  2", ("1", "2"), ("", "  ", "")),
    ("empty line", "", (), ("",)),
)


def check_corpus_numeric_field_grammar(module) -> List[str]:
    """(TW4) `split_numeric_fields` and the exact numeric-field grammar.

    The spellings and the exact separating segments must match the design's
    grammar for every admitted form and must leave the rejected forms (digits
    inside identifiers, hex, NaN/Infinity, non-ASCII word neighbours) as exact
    text. `rstrip` and whitespace normalisation must not happen: the segments
    keep the original spaces and tabs.
    """
    failures: List[str] = []
    for label, line, expected_fields, expected_segments in MASK_GRAMMAR_CASES:
        fields, segments = module.split_numeric_fields(line)
        if fields != expected_fields:
            failures.append(
                f"the {label} fields are {fields!r}, expected {expected_fields!r}")
        if segments != expected_segments:
            failures.append(
                f"the {label} segments are {segments!r}, "
                f"expected {expected_segments!r}")
        if len(segments) != len(fields) + 1:
            failures.append(
                f"the {label} segments do not surround the fields: "
                f"{len(segments)} segment(s), {len(fields)} field(s)")
        if "".join(
                value for pair in zip(segments, fields + ("",))
                for value in pair) != line:
            failures.append(f"the {label} split lost or reordered characters")
    # The grammar is the design's own pattern: a dot or a word character on
    # either side is rejected, and the boundaries are Python's Unicode `\w`.
    if module.NUMERIC_FIELD_RE.pattern != (
            r"(?<![\w.])[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)"
            r"(?:[eE][+-]?[0-9]+)?(?![\w.])"):
        failures.append(
            f"the numeric-field pattern changed: {module.NUMERIC_FIELD_RE.pattern!r}")
    return failures


MASK_GENERATOR_SEGMENTS = ("alpha", "beta", "gamma", "delta", "epsilon",
                           "zeta", "eta", "theta")
MASK_GENERATOR_FIELDS = ("0", "-0", "1", "+1", "-1", "0.5", ".5", "5.",
                         "1e3", "1E-3", "+2.5e+2", "-.25", "100", "3.14159",
                         "-7.0E0", "0007")


def _mask_generator_oracle(count: int, seed: int):
    """Deterministic generated mixtures with a by-construction oracle (TW4).

    Each line is built from non-numeric word segments and numeric spellings, so
    the scanner must find exactly the constructed fields; the expected mask is
    the set of positions whose two spellings differ by construction.
    """
    generator = random.Random(seed)
    lines1: List[str] = []
    lines2: List[str] = []
    expected: List[tuple] = []
    for line_number in range(1, count + 1):
        field_count = generator.randint(1, 3)
        segments = [generator.choice(MASK_GENERATOR_SEGMENTS)
                    for _ in range(field_count + 1)]
        fields1 = [generator.choice(MASK_GENERATOR_FIELDS)
                   for _ in range(field_count)]
        fields2 = [generator.choice(MASK_GENERATOR_FIELDS)
                   for _ in range(field_count)]
        # Keep a stable field in every line so the mask must not include it.
        if field_count >= 2:
            fields2[0] = fields1[0]
        parts: List[str] = []
        for index in range(field_count):
            parts.append(segments[index])
            parts.append(fields1[index])
        parts.append(segments[-1])
        lines1.append(" ".join(parts))
        parts = []
        for index in range(field_count):
            parts.append(segments[index])
            parts.append(fields2[index])
        parts.append(segments[-1])
        lines2.append(" ".join(parts))
        for index in range(field_count):
            if fields1[index] != fields2[index]:
                expected.append((line_number, index + 1,
                                 fields1[index], fields2[index]))
    return "\n".join(lines1) + "\n", "\n".join(lines2) + "\n", expected


def check_corpus_reproducibility_mask(module) -> List[str]:
    """(TW4) The pure twin-only mask inference and its alignment failures."""
    failures: List[str] = []

    def mask(first, second):
        return module.derive_corpus_reproducibility_mask(first, second)

    # Identical twins: a valid, available mask with zero positions.
    identical = mask("alpha 1\nbeta 2\n", "alpha 1\nbeta 2\n")
    if not identical.available or identical.count != 0 or identical.positions:
        failures.append(f"identical twins gave {identical!r}")

    # Aligned numeric spellings: exact 1-based positions and both spellings.
    result = mask("a 1 b\nc 2\n", "a 3 b\nc 2\n")
    if not result.available or result.count != 1:
        failures.append(f"one volatile field gave {result!r}")
    elif [(p.line, p.field, p.twin1, p.twin2) for p in result.positions] != [
            (1, 1, "1", "3")]:
        failures.append(f"the volatile position is wrong: {result.positions!r}")
    elif result.positions[0].label() != "line 1 field 1: '1' != '3'":
        failures.append(f"the position label is wrong: {result.positions[0].label()!r}")

    # Several positions across lines and fields, ordered by position.
    result = mask("x 1 2\ny 3 4\n", "x 1 5\ny 7 4\n")
    if [(p.line, p.field) for p in result.positions] != [(1, 2), (2, 1)]:
        failures.append(f"the ordered positions are wrong: {result.positions!r}")

    # Textually different spellings of the same value are still volatile: no
    # float conversion and no tolerance.
    for label, first, second in (
            ("decimal equality", "1.0", "1.00"),
            ("exponent equality", "1e2", "100"),
            ("sign equality", "+1", "1"),
            ("zero spelling", "0", "-0"),
            ("fraction spelling", "0.5", ".5"),
            ("trailing-dot spelling", "5", "5.")):
        result = mask(first, second)
        if not result.available or result.count != 1:
            failures.append(f"the {label} pair gave {result!r}")
        elif (result.positions[0].twin1, result.positions[0].twin2) != (first, second):
            failures.append(
                f"the {label} pair kept {result.positions[0]!r}")

    # Alignment failures: different line counts, field counts or segments are
    # divergences, never additional masked fields, and never a partial mask.
    for label, first, second, needle in (
            ("line count", "a 1\nb 2\n", "a 1\n", "line count"),
            ("field count", "a 1 2\n", "a 1\n", "numeric field"),
            ("extra space", "a 1 b\n", "a  1 b\n", "skeleton"),
            ("trailing space", "a 1 \n", "a 1\n", "skeleton"),
            ("tab instead of space", "a\t1\n", "a 1\n", "skeleton"),
            ("changed text", "a 1 b\n", "a 1 c\n", "skeleton"),
            ("lost number", "a 1 b\n", "a b\n", "numeric field"),
            ("extra number", "a b\n", "a 1 b\n", "numeric field"),
            ("trailing text", "a 1 b\n", "a 1 b c\n", "skeleton")):
        result = mask(first, second)
        if result.available or result.positions:
            failures.append(
                f"the {label} mismatch produced a partial mask: {result!r}")
        elif needle not in (result.reason or ""):
            failures.append(
                f"the {label} reason does not name the cause: {result.reason!r}")

    # LF, EOF and empty lines: `splitlines` interpretation, a final newline is
    # not a line, and an empty line keeps its position.
    result = mask("1\n\n2", "9\n\n2")
    if [(p.line, p.field) for p in result.positions] != [(1, 1)]:
        failures.append(f"the empty-line pair gave {result.positions!r}")
    result = mask("1", "2\n")
    if [(p.line, p.field) for p in result.positions] != [(1, 1)]:
        failures.append(f"the EOF pair gave {result.positions!r}")
    result = mask("1\n", "2\n")
    if [(p.line, p.field) for p in result.positions] != [(1, 1)]:
        failures.append(f"the LF pair gave {result.positions!r}")
    result = mask("\n", "\n")
    if not result.available or result.count != 0:
        failures.append(f"two empty lines gave {result!r}")

    # Determinism: equal inputs give the same result, and purity: the inputs
    # are not mutated and the result is immutable.
    first, second = "a 1\nb 2\n", "a 3\nb 2\n"
    first_copy, second_copy = first, second
    if mask(first, second) != mask(first, second):
        failures.append("the same inputs gave different masks")
    if (first, second) != (first_copy, second_copy):
        failures.append("the mask inference mutated its inputs")

    # A direct by-construction oracle on deterministic generated mixtures.
    for seed in (1, 7, 20260101):
        first_text, second_text, expected = _mask_generator_oracle(60, seed)
        first_fields = [module.split_numeric_fields(line)
                        for line in first_text.splitlines()]
        for line_number, (fields, segments) in enumerate(first_fields, start=1):
            if len(segments) != len(fields) + 1:
                failures.append(
                    f"generated line {line_number} lost its segments")
        result = mask(first_text, second_text)
        actual = [(p.line, p.field, p.twin1, p.twin2)
                  for p in result.positions]
        if not result.available or actual != expected:
            failures.append(
                f"the generated mixture seed {seed} gave {actual!r}, "
                f"expected {expected!r} ({result.reason!r})")

    # Bounded retention: an input at the program-output cap is processed
    # completely, and only the differing fields are retained.
    line_count = 40000
    first_lines = [f"row {index} value {index}" for index in range(line_count)]
    second_lines = [f"row {index} value {index + 1 if index % 1000 == 0 else index}"
                    for index in range(line_count)]
    first_text = "\n".join(first_lines) + "\n"
    second_text = "\n".join(second_lines) + "\n"
    result = mask(first_text, second_text)
    expected_positions = [(index + 1, 2) for index in range(0, line_count, 1000)]
    if not result.available \
            or [p.line for p in result.positions] != [p[0] for p in expected_positions]:
        failures.append(
            f"the bounded input gave {result.count} position(s), "
            f"expected {len(expected_positions)}")
    return failures


def check_corpus_mask_channel_isolation(module) -> List[str]:
    """(TW4/TW8) No class, descriptor or case input can extend the mask."""
    failures: List[str] = []
    signature = inspect.signature(module.derive_corpus_reproducibility_mask)
    parameters = list(signature.parameters.values())
    if [parameter.name for parameter in parameters] != ["twin1_text", "twin2_text"]:
        failures.append(
            f"the mask signature accepts {[p.name for p in parameters]}")
    if any(parameter.kind in (parameter.VAR_POSITIONAL, parameter.VAR_KEYWORD)
           for parameter in parameters):
        failures.append("the mask inference accepts arbitrary extra arguments")
    if any(parameter.default is not inspect.Parameter.empty
           for parameter in parameters):
        failures.append("the mask inference has a defaulted input")
    split_parameters = list(
        inspect.signature(module.split_numeric_fields).parameters)
    if split_parameters != ["line"]:
        failures.append(
            f"the numeric split accepts {split_parameters}, expected ['line']")
    for name in ("case", "corpus_case", "descriptor", "class_execution"):
        if name in (parameter.name for parameter in parameters):
            failures.append(f"the mask inference takes a '{name}' input")

    # Every descriptor keyword keeps its existing meaning: none adds a mask
    # position, because the inference has no descriptor channel at all.
    if tuple(module.CORPUS_DESCRIPTOR_KEYWORDS) != ("TIMEOUT", "NO_EXEC_MARKER"):
        failures.append(
            f"the descriptor vocabulary changed: {module.CORPUS_DESCRIPTOR_KEYWORDS}")
    before = module.derive_corpus_reproducibility_mask("a 1\n", "a 2\n")
    workspace = _temp_dir("arksteed-self-test-mask-descriptor-") / "test"
    workspace.mkdir(parents=True)
    with _patched(module, TEST_DIR=workspace):
        for keyword, text in (("TIMEOUT", "//! TIMEOUT 60 slow\n"),
                              ("NO_EXEC_MARKER", "//! NO_EXEC_MARKER single call\n")):
            rel = f"self_test/mask_{keyword.lower()}"
            case = _make_synthetic_case(module, "print(1);\n", rel=rel)
            _write_descriptor(workspace, rel, text)
            module.parse_corpus_descriptor(case, None, set())
            if case.config_errors:
                failures.append(
                    f"the {keyword} descriptor was rejected: {case.config_errors}")
            after = module.derive_corpus_reproducibility_mask("a 1\n", "a 2\n")
            if after != before:
                failures.append(f"the {keyword} descriptor changed the mask")
            for field in dataclasses.fields(case):
                if "mask" in field.name.lower() or "volatile" in field.name.lower():
                    failures.append(
                        f"the {keyword} descriptor opened a mask channel: {field.name}")
    return failures


def check_corpus_mask_combined_twin_filter(module) -> List[str]:
    """(combined T1+T2) The mask of two real twin executions of the class.

    Two synthetic twin processes run through the real twin execution path (T2)
    and the real streaming consumer (T1). Their filtered program output differs
    only in measured numeric fields, so the inferred mask must name exactly
    those positions. A filter that leaves a component line in the output
    changes the skeleton or adds differing numeric fields, so the exact
    expectation below is not vacuous.

    The command identity is asserted in full against an expectation built here
    by changing only the JIT value of the supplied class command, not by the
    runner's own producer. The real executor is re-run with a producer that
    changes the abc or the logging observation, and those launches must be
    rejected by the independent expectation.
    """
    failures: List[str] = []
    root = _write_corpus_root(cases=CORPUS_CASES)
    loaded, error = module.load_corpus_module(root)
    if error is not None:
        return [f"the synthetic corpus module did not load: {error}"]
    rel = CORPUS_CASES[0]
    case = _corpus_case_with_invariant(module, loaded, rel)
    cfg = module.BuildConfig("debug", "x64")
    case_artifacts = _temp_dir("arksteed-self-test-mask-artifacts-") / "case"
    case_artifacts.mkdir(parents=True, exist_ok=True)

    def component_and_program(counter: str, measured: str) -> str:
        # The component lines are the observation noise the shared filter must
        # remove; both carry a number that differs between the twins, so a leak
        # is visible either as a skeleton change or as an extra masked field.
        return (
            "[compiler] ======== ArkSteedCompilerTask: Starts compiling: m "
            "========\n"
            "[compiler] >>> Running method: rec.m\n"
            f"[compiler] counter {counter}\n"
            "result 547\n"
            f"timing {measured} us\n"
            "tail\n"
        )

    raw_first = component_and_program("1", "12.5")
    raw_second = component_and_program("2", "13.0")
    class_command = _twin_class_command(module)
    class_execution = _corpus_class_execution(
        module, command=class_command, program_output="program\n")
    invocations: List[List[str]] = []
    queue = [raw_first.encode("utf-8"), raw_second.encode("utf-8")]

    def fake_popen(cmd, stdout=None, stderr=None, env=None):
        invocations.append(list(cmd))
        return _FakeProc(queue.pop(0))

    with _patched(module.subprocess, Popen=fake_popen):
        run = module.run_corpus_twin_execution(
            case, cfg, CORPUS_CLASS_ARGS, 30, class_execution, case_artifacts, [])
    if len(run.executions) != 2:
        return failures + [
            f"the combined twin run made {len(run.executions)} attempt(s)"]

    # The expectation is built independently of the runner's producer: the
    # class command with its single explicit JIT value turned off. A producer
    # regression (a changed abc, entry point or logging observation) would move
    # an expectation derived from `module.derive_corpus_twin_command` together
    # with the executed command and stay invisible.
    expected_twin = _independent_twin_command(module, class_command)
    differing = [index for index, token in enumerate(expected_twin)
                 if index >= len(class_command) or class_command[index] != token]
    if len(expected_twin) != len(class_command) or len(differing) != 1:
        failures.append(
            "the independent twin expectation is not one token off the class "
            f"command: {expected_twin}")
    if invocations != [expected_twin, expected_twin]:
        failures.append(
            "a combined twin was not launched with the independent twin "
            f"expectation: {invocations} != {[expected_twin, expected_twin]}")
    for index, command in enumerate(invocations):
        if command != expected_twin:
            failures.append(
                f"combined twin {index + 1} command is {command!r}, expected "
                f"{expected_twin!r}")
        if f"{module.CORPUS_JIT_OPTION}={module.CORPUS_JIT_OFF_VALUE}" not in command:
            failures.append(f"combined twin {index + 1} does not turn the JIT off")
        if f"{module.CORPUS_EXEC_MARKER_OPTION}=true" not in command:
            failures.append(
                f"combined twin {index + 1} lost the class observation")
        if command == class_command:
            failures.append(f"combined twin {index + 1} is the class command")

    # Sensitivity of the independent identity to the token classes the fixed
    # fake streams cannot expose: a changed abc and a changed logging
    # observation. The real executor and the real streaming consumer run again
    # with a producer that mutates only that token. The resulting launches must
    # then differ from the independent expectation, while the producer-derived
    # expectation that this check used before moves with the mutation and
    # cannot reject it.
    original_derive = module.derive_corpus_twin_command

    def run_with_producer(producer):
        local_invocations: List[List[str]] = []
        local_queue = [raw_first.encode("utf-8"), raw_second.encode("utf-8")]

        def local_popen(cmd, stdout=None, stderr=None, env=None):
            local_invocations.append(list(cmd))
            return _FakeProc(local_queue.pop(0))

        with _patched(module, derive_corpus_twin_command=producer,
                      subprocess=_SubprocessShim(local_popen)):
            module.run_corpus_twin_execution(
                case, cfg, CORPUS_CLASS_ARGS, 30, class_execution, case_artifacts, [])
        return local_invocations

    def changed_abc(source_command):
        return [token.replace("case.abc", "different.abc")
                for token in original_derive(source_command)]

    def changed_logging_observation(source_command):
        return [token.replace("--log-level=info", "--log-level=error")
                for token in original_derive(source_command)]

    def dropped_compiler_component(source_command):
        return [token for token in original_derive(source_command)
                if token != "--log-components=compiler"]

    for label, producer in (
            ("abc", changed_abc),
            ("logging observation", changed_logging_observation),
            ("compiler log component", dropped_compiler_component)):
        mutated = run_with_producer(producer)
        producer_expectation = producer(class_command)
        if mutated != [producer_expectation, producer_expectation]:
            failures.append(
                f"the {label} mutation did not reach the executed command: "
                f"{mutated}")
        if mutated == [expected_twin, expected_twin]:
            failures.append(
                f"a twin with a changed {label} passes the independent "
                "expectation")
    for index, execution in enumerate(run.executions):
        if execution.returncode != 0 or not execution.started:
            failures.append(
                f"combined twin {index + 1} did not run cleanly: {execution!r}")
        if execution.evidence.compile_markers != 1 \
                or execution.evidence.exec_markers != 1:
            failures.append(
                f"combined twin {index + 1} lost its raw counts: "
                f"{execution.evidence!r}")

    outputs = [run_execution.evidence.program_output
               for run_execution in run.executions]
    expected_outputs = ["result 547\ntiming 12.5 us\ntail\n",
                        "result 547\ntiming 13.0 us\ntail\n"]
    for index, output in enumerate(outputs):
        if output != expected_outputs[index]:
            failures.append(
                f"combined twin {index + 1} filtered output is {output!r}, "
                f"expected {expected_outputs[index]!r}")
        if "[compiler]" in output or "counter" in output:
            failures.append(
                f"combined twin {index + 1} kept a component line: {output!r}")

    result = module.derive_corpus_reproducibility_mask(outputs[0], outputs[1])
    if not result.available:
        failures.append(f"the combined mask is unavailable: {result.reason!r}")
    elif [(p.line, p.field, p.twin1, p.twin2) for p in result.positions] != [
            (2, 1, "12.5", "13.0")]:
        failures.append(
            f"the combined mask is {result.positions!r}, expected the single "
            "measured field")

    # Sensitivity: a filter that leaked the component lines would change the
    # skeleton/positions, so the exact expectation above is not vacuous.
    leaked = module.derive_corpus_reproducibility_mask(raw_first, raw_second)
    if leaked.available and leaked.positions == result.positions:
        failures.append(
            "a leaked component line is not distinguishable from the filtered mask")
    return failures


CHECKS: List[CHECK_RESULTS] = [
    check_deopt_events_only,
    check_real_deopt_events_counted_once,
    check_conflicting_params_rejected,
    check_deopt_trace_options_implied,
    check_deopt_trace_message_filtered,
    check_control_evidence_collection,
    check_control_applicability,
    check_control_construction,
    check_control_failure_classification,
    check_control_live_range_violation,
    check_control_variant_isolation,
    check_control_default_path_invariance,
    check_control_jit_off_command,
    check_control_report_rendering,
    check_build_root_override,
    check_corpus_root_resolution,
    check_corpus_class_selection,
    check_corpus_root_immutability,
    check_corpus_stage_order,
    check_corpus_reference_execution,
    check_corpus_verdict_and_parity,
    check_corpus_descriptor_grammar,
    check_corpus_class_defaults,
    check_corpus_descriptor_config_error_red,
    check_corpus_descriptor_isolation,
    check_corpus_default_path_invariance,
    check_corpus_streaming_evidence,
    check_corpus_invariant,
    check_corpus_class_execution_verdict,
    check_corpus_constraint_evidence_availability,
    check_corpus_filter_chain_contract,
    check_corpus_component_input_snapshot,
    check_corpus_filter_exact_oracle,
    check_corpus_filter_bounds,
    check_corpus_filter_errors,
    check_corpus_log_failure_ordering,
    check_corpus_observation_failure_freeze,
    check_corpus_observation_equivalence,
    check_corpus_evidence_validity,
    check_corpus_class_executor_errors,
    check_corpus_main_invalidity_orchestration,
    check_corpus_config_error_orchestration,
    check_corpus_main_real_chain_orchestration,
    check_corpus_decoder_and_bounded_retention,
    check_corpus_streaming_provenance,
    check_corpus_artifact_failure_orchestration,
    check_corpus_streaming_scaling,
    check_corpus_filter_long_literal,
    check_corpus_twin_command_identity,
    check_corpus_twin_command_preflight,
    check_corpus_config_error_command_preflight,
    check_corpus_twin_execution,
    check_corpus_twin_combined_consumer,
    check_corpus_twin_orchestration,
    check_corpus_numeric_field_grammar,
    check_corpus_reproducibility_mask,
    check_corpus_mask_channel_isolation,
    check_corpus_mask_combined_twin_filter,
]


def main() -> int:
    module = load_runner()
    failed = 0
    for check in CHECKS:
        failures = check(module)
        status = "PASS" if not failures else "FAIL"
        print(f"[{status}] {check.__name__}")
        for failure in failures:
            print(f"        {failure}")
        failed += bool(failures)
    print(f"\n{len(CHECKS) - failed}/{len(CHECKS)} self-test check(s) passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
