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

"""Numeric C++ coverage of the ArkSteed JIT compiler from a gcov build tree.

The measurement build is produced with the GN argument `arksteed_coverage`
(see `js_runtime_config.gni` and `ecmascript/compiler/BUILD.gn`): the source
sets that compile the ArkSteed production sources are built with clang
`--coverage` and the targets that link them link the coverage runtime.
`libark_jsoptimizer_set_with_maple` carries the ArkSteed compiler sources
(`arksteed_jit_sources`) and `libark_jsruntime_set` carries the ArkSteed runtime
sources (`arksteed_deopt_helper.cpp`, `arksteed_safepoint_table.cpp`,
`arksteed_task.cpp`); together they cover every production translation unit of
`ecmascript/arksteed`. A build output directory of
such a build therefore contains one `.gcno` per instrumented translation unit,
and a process that ran against it writes the matching `.gcda` next to the
`.gcno` (the object paths in the build are relative to the out directory, so
the measured process has to run with the out directory as the working
directory).

This tool reads that output directory and reports functions, lines and
branches of the translation units inside the measurement scope. The branch
metric is the gcov "taken at least once" metric: the denominator is every
branch outcome `llvm-cov gcov -i` records (`taken`, `nottaken`, `notexec`) and
the numerator is the outcomes marked `taken`. A branch record of any other
shape is reported and fails the run instead of being dropped. It is a
measurement tool only: it never builds anything, never runs `ark_js_vm` and
never counts test cases. `gn gen` succeeding and the tests passing are not
coverage evidence, so the tool fails (non-zero exit) unless it finds at least
one instrumented translation unit inside the scope *and* at least one of them
has execution data with a non-zero counter.

Exit codes: 0 = report written; 1 = no instrumented in-scope translation unit,
or no in-scope line executed; 2 = missing build directory or `llvm-cov`; 3 = the
gcov data contained a branch record this tool does not understand.

Usage:

    measure_arksteed_coverage.py --out out/x64.debug \
        [--scope ecmascript/arksteed/] [--exclude PREFIX]... \
        [--llvm-cov PATH] [--json PATH] [--markdown PATH]

The default scope and exclusions are the ones fixed in
`ecmascript/arksteed/test/coverage/coverage-before.md`: production files of
`ecmascript/arksteed/**`, without the unit tests, the JS/TS cases and the
arm64 sources (not compiled on x64).
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple

DEFAULT_SCOPE = "ecmascript/arksteed/"
DEFAULT_EXCLUDES = (
    "ecmascript/arksteed/unittests/",
    "ecmascript/arksteed/test/",
    "ecmascript/arksteed/arch/arm64/",
)

INTERMEDIATE_SUFFIX = ".gcov"
FUNCTION_RE = re.compile(r"^function:(-?\d+),(-?\d+),(.*)$")
LCOUNT_RE = re.compile(r"^lcount:(-?\d+),(-?\d+)$")
BRANCH_PREFIX = "branch:"
BRANCH_RE = re.compile(r"^branch:(-?\d+),(taken|nottaken|notexec)$")
# `llvm-cov gcov -i` marks every branch outcome with one of three states:
# `taken` (evaluated and taken at least once), `nottaken` (evaluated, never
# taken) and `notexec` (never evaluated). All three are branch outcomes in the
# gcov denominator (`Taken at least once: <taken> of <taken+nottaken+notexec>`),
# so dropping `notexec` would understate the denominator. Only `taken` counts as
# covered.
FILE_RE = re.compile(r"^file:(.*)$")


class Counts:
    """Covered/total counters of one translation unit or of the whole report."""

    __slots__ = ("functions", "functions_covered", "lines", "lines_covered",
                 "branches", "branches_covered")

    def __init__(self) -> None:
        self.functions = 0
        self.functions_covered = 0
        self.lines = 0
        self.lines_covered = 0
        self.branches = 0
        self.branches_covered = 0

    def add(self, other: "Counts") -> None:
        self.functions += other.functions
        self.functions_covered += other.functions_covered
        self.lines += other.lines
        self.lines_covered += other.lines_covered
        self.branches += other.branches
        self.branches_covered += other.branches_covered

    def as_dict(self) -> Dict[str, int]:
        return {name: getattr(self, name) for name in self.__slots__}

    def percent(self, covered: int, total: int) -> str:
        return "n/a" if total == 0 else f"{100.0 * covered / total:.2f}%"


class FileCoverage:
    """Counters and execution state of one instrumented translation unit."""

    def __init__(self, source: str, gcno: Path, executed: bool) -> None:
        self.source = source
        self.gcno = gcno
        self.executed = executed
        self.counts = Counts()


def parse_intermediate(text: str) -> Tuple[List[str], Dict[str, Counts], List[str]]:
    """Split the `llvm-cov gcov -i` intermediate text into per-file sections.

    Every `file:` record opens a section; the records after it belong to that
    file until the next `file:` record. A translation unit therefore carries
    sections for the compiled source and for every header with instrumented
    inline code.

    Returns the section order, the per-file counters, and every branch record
    that does not match `BRANCH_RE`. Unknown records are reported instead of
    being silently ignored, so a format change cannot quietly change the
    published numbers again.
    """
    order: List[str] = []
    sections: Dict[str, Counts] = {}
    unrecognized: List[str] = []
    current: Optional[Counts] = None
    for line in text.splitlines():
        match = FILE_RE.match(line)
        if match:
            name = match.group(1)
            current = sections.get(name)
            if current is None:
                current = Counts()
                sections[name] = current
                order.append(name)
            continue
        if current is None:
            continue
        match = FUNCTION_RE.match(line)
        if match:
            current.functions += 1
            current.functions_covered += 1 if int(match.group(2)) > 0 else 0
            continue
        match = LCOUNT_RE.match(line)
        if match:
            current.lines += 1
            current.lines_covered += 1 if int(match.group(2)) > 0 else 0
            continue
        match = BRANCH_RE.match(line)
        if match:
            current.branches += 1
            current.branches_covered += 1 if match.group(2) == "taken" else 0
            continue
        if line.startswith(BRANCH_PREFIX):
            unrecognized.append(line)
    return order, sections, unrecognized


def select_translation_unit(gcno: Path, order: List[str], sections: Dict[str, Counts]
                            ) -> Tuple[Optional[str], Counts, str]:
    """Select the section of the compiled source file, not of an included header."""
    if not order:
        return None, Counts(), f"no file section in {gcno}"
    stem = gcno.stem
    source = order[0]
    if Path(source).stem != stem:
        matching = [name for name in order if Path(name).stem == stem]
        if not matching:
            return None, Counts(), (
                f"no section of the compiled source {stem} among {len(order)} sections "
                f"of {gcno}")
        source = matching[0]
    return source, sections[source], ""


def find_llvm_cov(explicit: Optional[str], out_dir: Path) -> Optional[str]:
    """Resolve llvm-cov from the option, the environment, the tree, then PATH."""
    if explicit:
        return explicit if Path(explicit).is_file() else shutil.which(explicit)
    env_value = os.environ.get("LLVM_COV")
    if env_value and Path(env_value).is_file():
        return env_value
    relative = Path("prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-cov")
    # The build context and the repository root are both worth trying: inside a
    # build context `prebuilts` is a symlink to the standalone dependency tree.
    here = Path(__file__).resolve()
    roots = [out_dir, *out_dir.parents, Path.cwd(), *Path.cwd().parents, here.parents[4]]
    for root in roots:
        candidate = root / relative
        if candidate.is_file():
            return str(candidate)
    return shutil.which("llvm-cov")


def read_unit(llvm_cov: str, gcno: Path, workdir: Path
              ) -> Tuple[Optional[str], Counts, str, List[str]]:
    """Run llvm-cov on one .gcno and return source, counters, diagnostics and
    unrecognized branch records."""
    result = subprocess.run(
        [llvm_cov, "gcov", "-i", "-b", "-c", str(gcno.resolve())],
        cwd=str(workdir), capture_output=True, text=True, check=False,
    )
    intermediate = workdir / (gcno.name + INTERMEDIATE_SUFFIX)
    text = intermediate.read_text(encoding="utf-8", errors="replace") if intermediate.is_file() else ""
    if intermediate.is_file():
        intermediate.unlink()
    if not text:
        return None, Counts(), (result.stderr or "") + f"no intermediate output for {gcno}\n", []
    order, sections, unrecognized = parse_intermediate(text)
    source, counts, diagnostics = select_translation_unit(gcno, order, sections)
    return source, counts, (result.stderr or "") + diagnostics, unrecognized


def in_scope(source: str, scope: str, excludes: List[str]) -> bool:
    normalized = source.replace(os.sep, "/")
    if scope not in normalized:
        return False
    index = normalized.index(scope)
    relative = normalized[index:]
    return not any(relative.startswith(prefix) for prefix in excludes)


def collect(out_dir: Path, scope: str, excludes: List[str], llvm_cov: str
            ) -> Tuple[List[FileCoverage], List[str]]:
    """Collect in-scope translation units of an instrumented build tree."""
    gcno_files = sorted(out_dir.rglob("*.gcno"))
    units: List[FileCoverage] = []
    problematic: List[str] = []
    unrecognized: List[str] = []
    with tempfile.TemporaryDirectory(prefix="arksteed-coverage-") as tmp:
        workdir = Path(tmp)
        for gcno in gcno_files:
            source, counts, diagnostics, unknown = read_unit(llvm_cov, gcno, workdir)
            if diagnostics.strip():
                problematic.append(diagnostics.strip())
                if os.environ.get("ARKSTEED_COVERAGE_VERBOSE"):
                    print(diagnostics.strip(), file=sys.stderr)
            unrecognized.extend(
                f"{gcno}: {record}" for record in unknown)
            if source is None or not in_scope(source, scope, excludes):
                continue
            unit = FileCoverage(source, gcno, gcno.with_suffix(".gcda").is_file()
                                and gcno.with_suffix(".gcda").stat().st_size > 0)
            unit.counts = counts
            units.append(unit)
        if problematic:
            print(f"warning: {len(problematic)} instrumented objects had no usable "
                  f"gcov section", file=sys.stderr)
    return units, unrecognized


def totals(units: List[FileCoverage]) -> Counts:
    total = Counts()
    for unit in units:
        total.add(unit.counts)
    return total


def short_name(source: str) -> str:
    normalized = source.replace(os.sep, "/")
    marker = "/ecmascript/arksteed/"
    index = normalized.find(marker)
    return ("ecmascript/arksteed/" + normalized[index + len(marker):]) if index >= 0 else normalized


def render_table(units: List[FileCoverage]) -> str:
    header = ("| Файл (TU) | Функции | Покрыто | Строки | Покрыто | Ветви | Покрыто | Исполнен |\n"
              "|---|---|---|---|---|---|---|---|\n")
    rows = []
    for unit in sorted(units, key=lambda item: item.source):
        counts = unit.counts
        rows.append("| `{}` | {} | {} | {} | {} | {} | {} | {} |".format(
            short_name(unit.source), counts.functions, counts.functions_covered,
            counts.lines, counts.lines_covered, counts.branches,
            counts.branches_covered, "да" if unit.executed else "нет",
        ))
    total = totals(units)
    rows.append("| **Итого** | {} | {} | {} | {} | {} | {} | {} |".format(
        total.functions, total.functions_covered, total.lines, total.lines_covered,
        total.branches, total.branches_covered,
        sum(1 for executed_unit in units if executed_unit.executed),
    ))
    return header + "\n".join(rows) + "\n"


def render_report(units: List[FileCoverage], scope: str, excludes: List[str],
                  llvm_cov: str, llvm_cov_version: str) -> str:
    total = totals(units)
    lines = [
        "# Численный C++ coverage ArkSteed (измерительный патч)",
        "",
        f"- Инструмент: `{llvm_cov}`",
        f"- Версия инструмента: `{llvm_cov_version}`",
        f"- Область измерения: `{scope}`",
        f"- Исключения: {', '.join('`' + item + '`' for item in excludes)}",
        f"- Инструментированных TU в области: **{len(units)}**",
        f"- Из них с записанным профилем (`.gcda`): **{sum(1 for unit in units if unit.executed)}**",
        "",
        "Покрытие считается по инструментированным точкам (функции/строки/ветви "
        "в терминологии gcov), а не по числу строк исходника.",
        "",
        "| Метрика | Покрыто | Всего | Доля |",
        "|---|---|---|---|",
        f"| Функции | {total.functions_covered} | "
        f"{total.functions} | {total.percent(total.functions_covered, total.functions)} |",
        f"| Строки | {total.lines_covered} | {total.lines} | {total.percent(total.lines_covered, total.lines)} |",
        f"| Ветви | {total.branches_covered} | "
        f"{total.branches} | {total.percent(total.branches_covered, total.branches)} |",
        "",
        render_table(units),
    ]
    return "\n".join(lines) + "\n"


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Report ArkSteed C++ coverage from an instrumented build tree.",
    )
    parser.add_argument("--out", required=True,
                        help="build output directory of the arksteed_coverage build")
    parser.add_argument("--scope", default=DEFAULT_SCOPE,
                        help=f"source path prefix of the measurement scope (default: {DEFAULT_SCOPE})")
    parser.add_argument("--exclude", action="append", default=None,
                        help="source path prefix excluded from the scope (repeatable)")
    parser.add_argument("--llvm-cov", default=None, help="path to llvm-cov")
    parser.add_argument("--json", default=None, help="write the numeric result as JSON")
    parser.add_argument("--markdown", default=None, help="write the report as Markdown")
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    out_dir = Path(args.out)
    if not out_dir.is_dir():
        print(f"error: build output directory is missing: {out_dir}", file=sys.stderr)
        return 2
    llvm_cov = find_llvm_cov(args.llvm_cov, out_dir.resolve())
    if llvm_cov is None:
        print("error: llvm-cov was not found; pass --llvm-cov", file=sys.stderr)
        return 2
    excludes = list(args.exclude) if args.exclude is not None else list(DEFAULT_EXCLUDES)

    units, unrecognized = collect(out_dir, args.scope, excludes, llvm_cov)
    if unrecognized:
        for record in unrecognized[:20]:
            print(f"error: unrecognized gcov branch record: {record}", file=sys.stderr)
        print(f"error: {len(unrecognized)} gcov branch records did not match "
              f"{BRANCH_RE.pattern}; branch totals would be wrong", file=sys.stderr)
        return 3
    total = totals(units)
    version = subprocess.run([llvm_cov, "--version"], capture_output=True, text=True,
                             check=False).stdout.strip()
    report = render_report(units, args.scope, excludes, llvm_cov, version)
    print(report)
    result = {
        "out": str(out_dir.resolve()),
        "scope": args.scope,
        "excludes": excludes,
        "llvm_cov": llvm_cov,
        "llvm_cov_version": version,
        "instrumented_units": len(units),
        "executed_units": sum(1 for unit in units if unit.executed),
        "totals": total.as_dict(),
        "files": [
            {"source": short_name(unit.source), "gcno": str(unit.gcno),
             "gcda": str(unit.gcno.with_suffix(".gcda")),
             "executed": unit.executed, **unit.counts.as_dict()}
            for unit in sorted(units, key=lambda item: item.source)
        ],
    }
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if args.markdown:
        Path(args.markdown).write_text(report, encoding="utf-8")

    if not units:
        print(f"error: no instrumented translation unit inside {args.scope}", file=sys.stderr)
        return 1
    if total.lines_covered == 0:
        print("error: instrumented translation units executed no instrumented line",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
