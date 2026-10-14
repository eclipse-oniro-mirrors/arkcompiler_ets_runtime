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

"""VM-free regression fixture for the Debug/Release mode check of the checker.

Regression for ISSUE-0060. `check_closure.py::_check_command` used to accept a
Release command field that mentioned `release` in *any* `-m`/`--mode` option, so
`... -m release -m debug` was recorded as Release evidence although the runner's
argparse resolves that line to `debug` (the last occurrence of the
non-accumulating option wins).

The check now derives the profile from the mode the runner itself resolves by
calling the runner's own `parse_args`, so no spelling the runner accepts can be
misread: `-m value`, `-mvalue`, `-m=value`, `--mode value`, `--mode=value` and
the unambiguous long-option abbreviations `--mod`, `--mo` and `--m` are read
exactly as the runner reads them, the last occurrence wins, the default is
`debug`, and an argument vector the runner's parser rejects (a value outside
`debug`/`release`, a mode option without a value, an unknown option) is a defect
of the field.

The fixture drives the production functions on the real, read-only test tree:

  * `check_record_content` at the record boundary (a complete record whose case
    is the existing `async/no_await_value`) with a valid positive control, with
    the invalid Release command of the report and with a Debug field whose
    effective mode is Release;
  * `_check_command` for the short, long, attached, `=`-value, abbreviated,
    repeated and invalid mode spellings of both fields;
  * a differential sweep: every single and every pair of the mode spellings
    accepted by the runner is compared against the runner's own `parse_args`, so
    the checker cannot drift from the parser of the runner it validates;
  * every listed expectation is cross-checked against the runner's own
    `parse_args` as well.

    cd ecmascript/arksteed/test && python3 coverage/check_closure_command_mode_test.py

Exit status is 0 iff every check passes. The fixture never writes to the source
tree, never builds, never runs the VM and never starts an external process.
"""

import contextlib
import importlib.util
import io
import shlex
import sys
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

# Never leave bytecode caches in the source tree (this script imports the
# checker from the coverage directory).
sys.dont_write_bytecode = True

SCRIPT_DIR = Path(__file__).resolve().parent
CHECKER_PATH = SCRIPT_DIR / "check_closure.py"

ROW_ID = "G1.1"
CASE_REL = "async/no_await_value"
BASE_COMMAND = "python3 ./run_arksteed_tests.py -p x64 -I %s" % CASE_REL
DEBUG_FIELD = "Команда (x64 Debug)"
RELEASE_FIELD = "Команда (x64 Release)"

# The mode values a command can carry: the two valid ones and the rejected ones
# (a value outside the choices of the runner and an empty attached value).
MODE_VALUES: Tuple[str, ...] = ("release", "debug", "invalid", "")

# Every spelling the runner's argparse accepts for the non-accumulating
# `-m`/`--mode` option, as a token sequence built from the mode value. A parser
# that re-implements this grammar instead of asking the runner misses at least
# one of these spellings (ISSUE-0060).
MODE_SPELLINGS: Tuple[Tuple[str, Callable[[str], Tuple[str, ...]]], ...] = (
    ("`-m value`", lambda value: ("-m", value)),
    ("`-mvalue`", lambda value: ("-m" + value,)),
    ("`-m=value`", lambda value: ("-m=" + value,)),
    ("`--mode value`", lambda value: ("--mode", value)),
    ("`--mode=value`", lambda value: ("--mode=" + value,)),
    ("`--mod value`", lambda value: ("--mod", value)),
    ("`--mo value`", lambda value: ("--mo", value)),
    ("`--m value`", lambda value: ("--m", value)),
    ("`--mod=value`", lambda value: ("--mod=" + value,)),
    ("`--m=value`", lambda value: ("--m=" + value,)),
)

# (label, field, required profile, mode option tokens, accepted)
MODE_CASES: Tuple[Tuple[str, str, str, Tuple[str, ...], bool], ...] = (
    (
        "release with repeated short options resolving to debug",
        RELEASE_FIELD, "release", ("-m", "release", "-m", "debug"), False,
    ),
    (
        "release with repeated attached long options resolving to debug",
        RELEASE_FIELD, "release", ("--mode=release", "--mode=debug"), False,
    ),
    (
        "release with mixed spellings resolving to debug",
        RELEASE_FIELD, "release", ("-m", "release", "--mode=debug"), False,
    ),
    (
        "release with an abbreviated long option resolving to debug",
        RELEASE_FIELD, "release", ("-m", "release", "--mod", "debug"), False,
    ),
    (
        "release with an abbreviated attached long option resolving to debug",
        RELEASE_FIELD, "release", ("--mode=release", "--mo=debug"), False,
    ),
    (
        "release with an `=`-valued short option resolving to debug",
        RELEASE_FIELD, "release", ("-m=release", "-m=debug"), False,
    ),
    (
        "release with an invalid mode value",
        RELEASE_FIELD, "release", ("-m", "release", "-m", "invalid"), False,
    ),
    (
        "release with an invalid attached short value",
        RELEASE_FIELD, "release", ("-minvalid",), False,
    ),
    (
        "release with a dangling mode option",
        RELEASE_FIELD, "release", ("-m",), False,
    ),
    ("valid short release option", RELEASE_FIELD, "release", ("-m", "release"), True),
    ("valid long release option", RELEASE_FIELD, "release", ("--mode", "release"), True),
    (
        "valid attached long release option",
        RELEASE_FIELD, "release", ("--mode=release",), True,
    ),
    (
        "valid attached short release option",
        RELEASE_FIELD, "release", ("-mrelease",), True,
    ),
    (
        "valid `=`-valued short release option",
        RELEASE_FIELD, "release", ("-m=release",), True,
    ),
    (
        "valid abbreviated long release option `--mod`",
        RELEASE_FIELD, "release", ("--mod", "release"), True,
    ),
    (
        "valid abbreviated long release option `--mo`",
        RELEASE_FIELD, "release", ("--mo", "release"), True,
    ),
    (
        "valid abbreviated long release option `--m`",
        RELEASE_FIELD, "release", ("--m", "release"), True,
    ),
    (
        "valid abbreviated attached long release option `--m=release`",
        RELEASE_FIELD, "release", ("--m=release",), True,
    ),
    (
        "valid repeated release option",
        RELEASE_FIELD, "release", ("-m", "release", "-m", "release"), True,
    ),
    ("debug default without a mode option", DEBUG_FIELD, "debug", (), True),
    ("debug with explicit mode", DEBUG_FIELD, "debug", ("-m", "debug"), True),
    (
        "debug whose effective mode is release",
        DEBUG_FIELD, "debug", ("-m", "debug", "-m", "release"), False,
    ),
    (
        "debug with repeated options resolving to release",
        DEBUG_FIELD, "debug", ("--mode=debug", "--mode=release"), False,
    ),
    (
        "debug with an abbreviated long option resolving to release",
        DEBUG_FIELD, "debug", ("--m", "release"), False,
    ),
    (
        "debug with a mixed `=`-valued pair resolving to release",
        DEBUG_FIELD, "debug", ("-m=debug", "--mod=release"), False,
    ),
    ("debug with an invalid mode value", DEBUG_FIELD, "debug", ("--mode=invalid",), False),
)


def load_checker():
    """Load the production checker module from the coverage directory."""
    spec = importlib.util.spec_from_file_location(
        "check_closure_command_mode_under_test", CHECKER_PATH
    )
    if spec is None or spec.loader is None:
        raise SystemExit("cannot load checker: %s" % CHECKER_PATH)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _command(mode_tokens: Tuple[str, ...]) -> str:
    return " ".join([BASE_COMMAND, *mode_tokens]).strip()


def _runner_mode(module, command: str) -> Tuple[Optional[str], Optional[str]]:
    """The mode the runner's own parser resolves, plus its error, if any.

    `argparse` prints its usage to stderr and raises `SystemExit` for an invalid
    mode value; both streams are captured so the fixture output stays clean.
    """
    argv = shlex.split(command)[2:]
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        try:
            return module.parse_args(argv).mode, None
        except SystemExit as error:
            return None, str(error.code)


def _record_values(release_command: str, debug_command: str) -> Dict[str, str]:
    """A complete record whose only variable parts are the two command fields."""
    return {
        "Кейсы": "`%s`" % CASE_REL,
        DEBUG_FIELD: "`%s`" % debug_command,
        RELEASE_FIELD: "`%s`" % release_command,
        "Аннотации свидетельств": "`HAS CallRuntime AsyncFunctionEnter`",
        "Контроль чувствительности": "JIT-off; отчёт controls.md",
        "Решение дизайна": (
            "arksteed-jsts-behavioral-case-suite#A4 — механизм и способ проверки"
        ),
    }


def _require_field_defect(
    failures: List[str], label: str, defects: List[str], field_name: str
) -> None:
    """A rejection must name both the closure row and the command field."""
    if not defects:
        failures.append("%s: the checker accepted the command" % label)
        return
    if not any(
        defect.startswith(ROW_ID + ": ") and field_name in defect for defect in defects
    ):
        failures.append(
            "%s: no diagnostic names row %s and field %s: %s"
            % (label, ROW_ID, field_name, defects)
        )


def check_mode_matrix(checker, contract, ctx) -> List[str]:
    """The command-level matrix: accepted/rejected per mode spelling (R6)."""
    failures: List[str] = []
    for label, field, required_mode, mode_tokens, accepted in MODE_CASES:
        command = _command(mode_tokens)
        runner_mode, runner_error = _runner_mode(contract.module, command)
        defects = checker._check_command(
            ROW_ID, field, "`%s`" % command, required_mode, {CASE_REL}, ctx
        )
        if accepted:
            if runner_error is not None or runner_mode != required_mode:
                failures.append(
                    "%s: fixture expectation broken - the runner resolves the command to "
                    "mode=%s error=%s instead of %s"
                    % (label, runner_mode, runner_error, required_mode)
                )
            if defects:
                failures.append("%s: the checker rejected a valid command: %s" % (label, defects))
            continue
        if runner_error is None and runner_mode == required_mode:
            failures.append(
                "%s: fixture expectation broken - the runner resolves the command to %s, "
                "so the case does not exercise a mode mismatch" % (label, required_mode)
            )
        _require_field_defect(failures, label, defects, field)
    return failures


# (label, mode option tokens) for every single spelling of every mode value.
def _mode_units() -> List[Tuple[str, Tuple[str, ...]]]:
    units: List[Tuple[str, Tuple[str, ...]]] = []
    for value in MODE_VALUES:
        for spelling, build in MODE_SPELLINGS:
            units.append(
                ("%s with value %s" % (spelling, repr(value)), build(value))
            )
    return units


def _mode_sequences() -> List[Tuple[str, Tuple[str, ...]]]:
    """The mode spellings the sweep drives: singles and opposing pairs.

    Every spelling of every mode value is checked alone, every spelling pair is
    checked in both orders with the two valid values (`release` first and then
    `debug`, and the other way round) and every spelling is checked followed by
    an invalid value and by a valid `-m release`. That is 251 argument vectors
    and covers every defect class of the report: a spelling the checker cannot
    see, an attached or `=`-valued form it mis-splits, and a repeated option
    whose effective mode must be the last one.
    """
    units = _mode_units()
    sequences: List[Tuple[str, Tuple[str, ...]]] = [("no mode option", ())]
    sequences.extend(units)
    for left_spelling, build_left in MODE_SPELLINGS:
        for right_spelling, build_right in MODE_SPELLINGS:
            sequences.append(
                (
                    "%s release then %s debug" % (left_spelling, right_spelling),
                    build_left("release") + build_right("debug"),
                )
            )
            sequences.append(
                (
                    "%s debug then %s release" % (left_spelling, right_spelling),
                    build_left("debug") + build_right("release"),
                )
            )
    for spelling, build in MODE_SPELLINGS:
        sequences.append(
            (
                "%s invalid then `-m release`" % spelling,
                build("invalid") + ("-m", "release"),
            )
        )
    return sequences


def check_parser_fidelity(checker, contract, ctx) -> List[str]:
    """Differential sweep: the checker vs the runner's own `parse_args` (R6).

    For every single and every pair of the mode spellings the runner accepts,
    the verdict of the production `_check_command` must equal the verdict
    implied by the runner's own parser: the field is accepted iff the parser
    accepts the vector and resolves it to the profile of the field. This is the
    direct non-hollow control of the ISSUE-0060 defect class - a re-implemented
    grammar that drops `-m=value` or an abbreviated long option disagrees with
    the parser here even when the hand-written matrix above lists no such case.
    """
    failures: List[str] = []
    checked = 0
    sequences = _mode_sequences()
    for label, tokens in sequences:
        command = _command(tokens)
        runner_mode, runner_error = _runner_mode(contract.module, command)
        for field, required_mode in ((DEBUG_FIELD, "debug"), (RELEASE_FIELD, "release")):
            defects = checker._check_command(
                ROW_ID, field, "`%s`" % command, required_mode, {CASE_REL}, ctx
            )
            expected_accept = runner_error is None and runner_mode == required_mode
            checked += 1
            if (not defects) == expected_accept:
                continue
            failures.append(
                "%s [%s]: the checker %s the command, the runner resolves mode=%s "
                "error=%s (defects: %s)"
                % (
                    label,
                    field,
                    "accepted" if not defects else "rejected",
                    runner_mode,
                    runner_error,
                    defects or "none",
                )
            )
            if len(failures) >= 10:
                failures.append("... further disagreements suppressed")
                return failures
    if checked != 2 * len(sequences):
        failures.append(
            "the differential sweep checked %d of %d commands"
            % (checked, 2 * len(sequences))
        )
    return failures


def check_record_boundary(checker, contract, ctx) -> List[str]:
    """The record boundary: a positive control and the reported invalid commands."""
    del contract  # the record boundary exercises the production record checker
    failures: List[str] = []
    if not checker._pattern_selects_exactly(ctx, CASE_REL):
        failures.append(
            "fixture environment broken: `%s` used as an -I pattern does not select exactly "
            "itself" % CASE_REL
        )
        return failures

    valid_debug = BASE_COMMAND
    valid_release = _command(("-m", "release"))
    valid_record = _record_values(valid_release, valid_debug)
    defects = checker.check_record_content(ROW_ID, valid_record, "P0", ctx)
    if defects:
        failures.append("the valid positive control record is rejected: %s" % defects)

    accepted_record = _record_values(_command(("-m=release",)), valid_debug)
    defects = checker.check_record_content(ROW_ID, accepted_record, "P0", ctx)
    if defects:
        failures.append(
            "the valid `-m=release` record is rejected: %s" % defects
        )

    invalid_record = _record_values(_command(("-m", "release", "-m", "debug")), valid_debug)
    defects = checker.check_record_content(ROW_ID, invalid_record, "P0", ctx)
    _require_field_defect(
        failures,
        "record with `-m release -m debug` in the Release field",
        defects,
        RELEASE_FIELD,
    )

    invalid_record = _record_values(_command(("-m", "release", "--mod", "debug")), valid_debug)
    defects = checker.check_record_content(ROW_ID, invalid_record, "P0", ctx)
    _require_field_defect(
        failures,
        "record with `-m release --mod debug` in the Release field",
        defects,
        RELEASE_FIELD,
    )

    invalid_record = _record_values(valid_release, _command(("-m", "debug", "-m", "release")))
    defects = checker.check_record_content(ROW_ID, invalid_record, "P0", ctx)
    _require_field_defect(
        failures,
        "record whose Debug field effectively runs Release",
        defects,
        DEBUG_FIELD,
    )

    invalid_record = _record_values(valid_release, _command(("--m", "release")))
    defects = checker.check_record_content(ROW_ID, invalid_record, "P0", ctx)
    _require_field_defect(
        failures,
        "record whose Debug field runs Release through an abbreviated long option",
        defects,
        DEBUG_FIELD,
    )

    invalid_record = _record_values(_command(("-m", "release", "-m", "invalid")), valid_debug)
    defects = checker.check_record_content(ROW_ID, invalid_record, "P0", ctx)
    _require_field_defect(
        failures,
        "record with the invalid mode value in the Release field",
        defects,
        RELEASE_FIELD,
    )
    return failures


CHECKS = (
    ("check_mode_matrix", check_mode_matrix),
    ("check_parser_fidelity", check_parser_fidelity),
    ("check_record_boundary", check_record_boundary),
)


def main() -> int:
    checker = load_checker()
    descriptor = checker.default_descriptor()
    contract = checker.load_runner_contract(descriptor)
    ctx = checker.ContentContext(descriptor, contract)
    failed = 0
    for name, check in CHECKS:
        try:
            failures = check(checker, contract, ctx)
        except Exception as error:  # a crash must not be reported as a pass
            failures = ["unexpected exception: %r" % error]
        status = "PASS" if not failures else "FAIL"
        print("[%s] %s" % (status, name))
        for failure in failures:
            print("        %s" % failure)
        failed += bool(failures)
    print("\n%d/%d regression check(s) passed" % (len(CHECKS) - failed, len(CHECKS)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
