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

"""VM-free regression fixture for the branch metric of the coverage tool.

Regression for ISSUE-0059 (review cycle 3): `parse_intermediate` of
`measure_arksteed_coverage.py` matched only `branch:<line>,taken|nottaken`, so
every `notexec` record (a branch outcome that was instrumented but never
evaluated) was silently dropped from the denominator. The branch metric is the
gcov "taken at least once" metric, whose denominator is `taken + nottaken +
notexec`, so dropping `notexec` published a branch coverage roughly 2.5x too
high: `arksteed_task.cpp` was reported as 140/268 = 52.24% where
`llvm-cov gcov` prints `Taken at least once ... of 406` = 34.48%.

The fixture drives the production parser on `llvm-cov gcov -i` intermediate
text, without a build tree and without a VM:

  * all three branch states are counted and only `taken` counts as covered;
  * a real per-translation-unit sample (`arksteed_task.cpp`: 140 `taken`,
    128 `nottaken`, 138 `notexec`) yields 140/406;
  * a branch record of an unknown shape is returned as unrecognized and makes
    `main` fail with exit code 3 without writing a report, so the metric cannot
    silently change again.

Run:

    python3 ecmascript/arksteed/test/coverage/measure_arksteed_coverage_test.py
"""

import contextlib
import importlib.util
import io
import sys
import tempfile
from pathlib import Path
from typing import Callable, List, Tuple

# Never leave bytecode caches in the source tree (this script imports the tool
# from the coverage directory).
sys.dont_write_bytecode = True

SCRIPT_DIR = Path(__file__).resolve().parent
TOOL_PATH = SCRIPT_DIR / "measure_arksteed_coverage.py"

# (label, gcov branch records, expected (covered, total))
BRANCH_CASES: Tuple[Tuple[str, Tuple[str, ...], Tuple[int, int]], ...] = (
    ("taken only", ("branch:10,taken",), (1, 1)),
    ("nottaken only", ("branch:10,nottaken",), (0, 1)),
    ("notexec only", ("branch:10,notexec",), (0, 1)),
    ("evaluated pair", ("branch:10,taken", "branch:10,nottaken"), (1, 2)),
    (
        "never evaluated pair",
        ("branch:10,notexec", "branch:10,notexec"),
        (0, 2),
    ),
    (
        "mixed outcome",
        ("branch:10,taken", "branch:10,nottaken", "branch:10,notexec"),
        (1, 3),
    ),
    (
        "arksteed_task.cpp sample (llvm-cov gcov: taken at least once 140 of 406)",
        tuple(["branch:7,taken"] * 140
              + ["branch:7,nottaken"] * 128
              + ["branch:7,notexec"] * 138),
        (140, 406),
    ),
)

# Branch records of a shape the tool must not accept silently.
UNKNOWN_RECORDS: Tuple[str, ...] = (
    "branch:10,taken,taken",
    "branch:10,",
    "branch:x,taken",
    "branch:10,unknown",
)


def load_tool():
    spec = importlib.util.spec_from_file_location("measure_arksteed_coverage", TOOL_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _intermediate(records: Tuple[str, ...]) -> str:
    lines = ["file:../../arkcompiler/ets_runtime/ecmascript/arksteed/arksteed_task.cpp"]
    lines += ["function:1,1,main"] + list(records)
    lines += ["file:../../arkcompiler/ets_runtime/ecmascript/arksteed/arksteed_task.h"]
    lines += ["branch:3,notexec"]
    return "\n".join(lines) + "\n"


def check_branch_states(tool) -> List[str]:
    """All three branch states count; only `taken` counts as covered."""
    failures: List[str] = []
    for label, records, expected in BRANCH_CASES:
        order, sections, unrecognized = tool.parse_intermediate(_intermediate(records))
        if unrecognized:
            failures.append("%s: unknown records %r" % (label, unrecognized))
        if not order:
            failures.append("%s: no file section parsed" % label)
            continue
        counts = sections[order[0]]
        actual = (counts.branches_covered, counts.branches)
        if actual != expected:
            failures.append("%s: branches %r, expected %r" % (label, actual, expected))
    return failures


def check_header_sections_separate(tool) -> List[str]:
    """A header section is counted separately from the compiled source section."""
    failures: List[str] = []
    order, sections, unrecognized = tool.parse_intermediate(
        _intermediate(("branch:7,taken", "branch:7,notexec")))
    if unrecognized:
        failures.append("unexpected unknown records %r" % (unrecognized,))
    if len(order) != 2:
        failures.append("expected 2 sections, got %r" % (order,))
        return failures
    source = sections[order[0]]
    header = sections[order[1]]
    if (source.branches_covered, source.branches) != (1, 2):
        failures.append("source section branches %r, expected (1, 2)"
                        % ((source.branches_covered, source.branches),))
    if (header.branches_covered, header.branches) != (0, 1):
        failures.append("header section branches %r, expected (0, 1)"
                        % ((header.branches_covered, header.branches),))
    return failures


def check_unknown_records_reported(tool) -> List[str]:
    """An unrecognized branch record is reported, not dropped."""
    failures: List[str] = []
    for record in UNKNOWN_RECORDS:
        order, sections, unrecognized = tool.parse_intermediate(_intermediate((record,)))
        if unrecognized != [record]:
            failures.append("record %r: unrecognized %r, expected [%r]"
                            % (record, unrecognized, record))
        if tool.BRANCH_RE.match(record):
            failures.append("record %r matched BRANCH_RE %r"
                            % (record, tool.BRANCH_RE.pattern))
    # A well-formed record is not reported.
    order, sections, unrecognized = tool.parse_intermediate(_intermediate(("branch:7,taken",)))
    if unrecognized:
        failures.append("well-formed record reported as unrecognized: %r" % (unrecognized,))
    return failures


def check_main_rejects_unknown_records(tool) -> List[str]:
    """`main` fails with code 3 and writes no report on an unknown record."""
    failures: List[str] = []
    original_collect = tool.collect
    original_find = tool.find_llvm_cov
    try:
        tool.collect = lambda out_dir, scope, excludes, llvm_cov: (
            [], ["arksteed_task.gcno: branch:1,bogus"])
        tool.find_llvm_cov = lambda explicit, out_dir: sys.executable
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp) / "out"
            out_dir.mkdir()
            json_path = Path(tmp) / "report.json"
            markdown_path = Path(tmp) / "report.md"
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                code = tool.main(["--out", str(out_dir), "--llvm-cov", sys.executable,
                                  "--json", str(json_path),
                                  "--markdown", str(markdown_path)])
            if code != 3:
                failures.append("exit code %r, expected 3" % (code,))
            if json_path.exists() or markdown_path.exists():
                failures.append("a report was written despite the unknown record")
            if "unrecognized gcov branch record" not in stderr.getvalue():
                failures.append("no diagnostic on stderr: %r" % stderr.getvalue())
    finally:
        tool.collect = original_collect
        tool.find_llvm_cov = original_find
    return failures


CHECKS: Tuple[Tuple[str, Callable], ...] = (
    ("check_branch_states", check_branch_states),
    ("check_header_sections_separate", check_header_sections_separate),
    ("check_unknown_records_reported", check_unknown_records_reported),
    ("check_main_rejects_unknown_records", check_main_rejects_unknown_records),
)


def main() -> int:
    tool = load_tool()
    failed = 0
    for name, check in CHECKS:
        try:
            failures = check(tool)
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
