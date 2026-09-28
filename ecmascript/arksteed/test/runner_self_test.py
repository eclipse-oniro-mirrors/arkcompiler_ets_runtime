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
    the design fixes it, and a run without the mode must not gain an option.

This script checks those mechanics directly, with no VM and no build required:

    python3 ./runner_self_test.py

Exit status is 0 iff every check passes.
"""

import argparse
import collections
import importlib.util
import json
import re
import sys
import tempfile
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
    plain_compiler_filter = re.compile(r"\[compiler\] [^\n]*\n?")
    filters = [module.DEOPT_TRACE_LOG_RE, plain_compiler_filter]
    trace = (
        "[compiler] Lazy Deoptimize    at level3 (/tmp/x/case.js:12:1)\n"
        "    at level2 (/tmp/x/case.js:11:1)\n"
        "    at func_main_0 (/tmp/x/case.js:13:1)\n"
        "\n"
    )
    case = _make_case(module, "print(1);\n", expected="1\n2\nOK\n")
    stdout = "1\n" + trace + "2\nOK\n"
    diff = module.compare_output(stdout, case.expected, filters)
    if diff is not None:
        failures.append(f"deopt trace message not fully filtered:\n{diff}")

    # A program line that merely looks like a stack-trace frame is untouched
    # when it does not continue a traced deopt event.
    case = _make_case(module, "print(1);\n", expected="    at program output\n42\n")
    stdout = "[compiler] >>> Running method: case.#*#f\n    at program output\n42\n"
    diff = module.compare_output(stdout, case.expected, filters)
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
        if instance.variant_id != (f"{'count' if instance.class_token == module.CONTROL_CLASS_COUNT else 'address'}-{instance.line}"):
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
    diff = module.compare_output("2\n", expected_case.expected, [])
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
        result.instance.assertion.key() for result in results
        if result.class_token == module.CONTROL_CLASS_ADDRESS
        and result.verdict == module.CONTROL_VERDICT_VALID
        and result.instance.assertion is not None}
    masked_index = next(
        (index for index, result in enumerate(results)
         if result.class_token == module.CONTROL_CLASS_COUNT
         and result.verdict == module.CONTROL_VERDICT_VALID
         and result.instance.assertion is not None
         and result.instance.assertion.key() in address_covered), None)
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
        if not any(class_token in reason and instance_id in reason
                   for reason in masked_result.reasons):
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
        if module.compare_output(SYNTHETIC_STDOUT, case.expected, []) is not None:
            failures.append(
                f"the synthetic stdout does not satisfy the original expectation: "
                f"{SYNTHETIC_STDOUT!r}")
        diff = module.compare_output(SYNTHETIC_STDOUT, variant.expected, [])
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
    if module.compare_output("", empty.expected, []) is not None:
        failures.append("an empty original expectation must skip the stdout comparison")
    empty_diff = module.compare_output("", empty_variant.expected, [])
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
        instance for instance in instances
        if instance.class_token == module.CONTROL_CLASS_COUNT)
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
    check_control_report_rendering,
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
