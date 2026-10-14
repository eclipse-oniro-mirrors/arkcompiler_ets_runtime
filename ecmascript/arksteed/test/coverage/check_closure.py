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

"""VM-free structural checker of the JS/TS cluster closure records.

The coverage matrix `coverage/matrix.md` is the single source of truth about
coverage completeness. This checker reads only local files and verifies the
machine-checkable part of the closure contract of the `JS/TS` cluster of
`ecmascript/arksteed/test/coverage/matrix.md`
(`arksteed-jsts-closure-records-and-checker` R1-R6, R9):

  * the cluster composition comes from the `JS/TS` row of the
    `Индекс кластеров` table and is compared against the fixed list of 46 IDs
    kept below; a repeated ID in that row, a substituted, missing or extra ID
    and a row length other than 46 are defects reporting the ID or the number
    and both sides of the discrepancy; every ID must have exactly one row in
    the group tables `## G1 —` ... `## G12 —` whose `Задача` cell is `JS/TS`;
  * the status and the priority of every row are read from its group-table row;
    the status dictionary is read from `### Статусы строки`;
  * a closure record is recognised only in the section
    `## Записи закрытия строк кластера JS/TS`, as a block
    `### <ID> — <название>` with fields `- **<Поле>:** <значение>`; all 12
    mandatory fields must be present exactly once and in the canonical order,
    and the `Строка`, `Профиль` and `Остаточный пробел` values must have the
    canonical sub-form (R3);
  * every `Кейсы` value is resolved against the cases root of the descriptor:
    the path is relative and free of `..`, the directory exists, is found by
    `discover_cases([], [])`, holds exactly one `.ts`/`.js`/`.cjs` source and
    exactly one `expected_output.txt`; a multi-source directory is rejected; a
    `corpus/<suite>/<case>` reference requires the descriptor
    `corpus/<suite>/<case>.annotations` and cannot be the only case (R4);
  * every `Аннотации свидетельств` citation is normalized (inline `#` comment
    dropped, whitespace collapsed) and parsed with the runner grammar against
    `arksteed_opcode_list.h`; an unknown keyword, an opcode outside the header,
    a `CallCommonStub`/`CallRuntime` citation without a stub name, a stub name
    with a shell wildcard or with a tail outside `common_stub_csigns.h` /
    `runtime_stub_list.h`, and a method-scoped constraint cited before
    `//! METHOD` are record defects; the normalized citation must appear
    verbatim in a `//!` line of a record case or of its corpus descriptor, and
    the record cases must parse without config errors; a P0 record whose
    annotations are empty or made only of `EXEC_COUNT*`, or whose
    `Контроль чувствительности` lacks a control-class token or a control-report
    reference, is a defect (R5, R3);
  * both command fields are parsed as runner argument vectors without being
    executed: `python3 ./run_arksteed_tests.py`, `-p x64` and an
    `-I`/`--include` filter are mandatory; the profile of a field is decided by
    the mode the runner itself resolves, because the checker asks the runner's
    own `parse_args` for it instead of re-implementing its grammar. Every
    spelling the runner accepts is therefore read exactly as the runner reads
    it - `-m value`, `-mvalue`, `-m=value`, `--mode value`, `--mode=value` and
    the unambiguous long-option abbreviations `--mod`/`--mo`/`--m` - the *last*
    occurrence of the non-accumulating `-m`/`--mode` option wins and the default
    is `debug`. A Release field must therefore resolve to `release` and a Debug
    field to `debug`, so a repeated mode option such as `-m release -m debug`
    can never be recorded as Release evidence; an argument vector the runner's
    own parser rejects (a mode value outside `debug`/`release`, a mode option
    without a value, an unknown option) is a defect of the field, because the
    runner would not execute the recorded command. The cases selected by the
    filters (by the runner's own `_pattern_matches`) must equal the built-in
    subset of `Кейсы` (R6);
  * when the stub inventory artifact `coverage/stub-inventory.md` is present,
    every position of it is checked (R8): a position is a `### <заголовок>`
    block carrying the identity fields `Перечисление`, `Имя в дампе`,
    `Локатор эмиссии` and the three obligations `Нормальный путь`,
    `Guard/deopt`, `Исключение`, each exactly once and with a value; the
    enumeration name must be `CommonStubID::X` or `RTSTUB_ID(X)` with `X` in
    `COMMON_STUB_LIST` (including the `*StwCopy` variants) or in
    `RUNTIME_STUB_LIST`; the dump name must be `COStub_X` or `RTStub_X` with a
    family consistent with the enumeration form and the same tail; the emission
    locator must resolve against the checkout root (`path:line` inside the file
    boundaries, `path#symbol` present in the file text); every obligation is
    exactly one of two forms - `закрыто — кейс `<путь>`, аннотация
    `HAS CallCommonStub|CallRuntime <точное имя>`` where the case and the
    verbatim annotation go through the same R4/R5 checks as a record case (the
    annotation must live in the built-in case source or in the
    `corpus/<suite>/<case>.annotations` descriptor of a corpus reference, the
    opcode family must match `Имя в дампе`, and the stub name must carry no
    shell wildcard and have the tail of `Имя в дампе`), or `not-applicable —
    причина из реализации: …; локатор проверки достижимости: …;
    подтверждение reviewer: …` with non-empty sub-fields and a reachability
    locator resolving to an existing file and an in-range line; the auxiliary
    sections of the artifact (the regeneration command, the list of classifying
    references, prose) are not positions and produce no defects;
  * a row without a block, a missing `coverage/stub-inventory.md` artifact and
    an artifact without positions are reported as the expected incomplete state
    of the epic, not as defects.

Modes: the default mode performs the checks above and prints its report to
stdout. `--control-set` (R7) prints to stdout only the case paths of the
structurally valid records of the rows whose `Приоритет` cell is `P0` - one
path per line, sorted, without duplicates, in exactly the spelling of the
`Кейсы` field and usable word-for-word as `-I` patterns of the runner - and
sends every diagnostic to stderr. An empty set with exit code 0 is the
expected incomplete state before the first P0 record exists; a present but
structurally invalid P0 record is a defect (exit code 1) whose cases are never
silently dropped, so the sensitivity-control consumer sees the defect instead
of an empty list. `--self-test` (R10) runs the production checks above on
synthetic fixtures in temporary directories and proves that every defect
fixture returns a non-zero code with a message naming the row and the field or
the position and the obligation, while reverting the mutation restores the
expected code; it never reads the production tree and never leaves a file
behind. The modes are mutually exclusive: `--control-set --self-test` and
`--self-test <other>` are usage errors (exit code 2).

Exit codes (R9):

  * 0 - every cluster row carries a complete and valid record and the stub
    inventory artifact is present and complete (every position carries three
    closed or justified obligations);
  * 1 - structural defects (incomplete or invalid content of records present,
    composition drift of the cluster rows, invalid or incomplete inventory
    positions present);
  * 2 - usage error (unknown argument, unreadable matrix, unreadable opcode or
    enumeration-name header, unreadable inventory artifact, unloadable
    runner);
  * 3 - pure incompleteness (cluster row without a record, absent or
    positionless stub inventory artifact) with no structural defect; 1 outranks
    3.

One message per defect in the form `<ID|позиция>: <поле|обязательство>:
<сообщение>`, plus a summary line with the numbers of records, inventory
positions and defects. A row without a record is reported as `<ID>: нет записи
закрытия (статус: <статус>)`; the missing inventory artifact as
`ecmascript/arksteed/test/coverage/stub-inventory.md: артефакт отсутствует (ожидаемое незавершённое состояние E-STUB)`;
an artifact without positions as `<артефакт>: позиции: артефакт не содержит ни
одной позиции (ожидаемое незавершённое состояние E-STUB)`.

The checker never writes to the source tree, never builds, never runs the VM
and never starts an external process, so a repeated run over unchanged
artifacts prints the same stdout and returns the same code.

Internal API (R10): `RootDescriptor` carries every path the production checks
read; the production functions never rely on global paths, and `main()` builds
the descriptor from the location of this script. The descriptor is the seam
used by the VM-free self-test (ISSUE-0055) and by the fixture checks of the
task that owns this skeleton.

Scope (sequencing of `ISSUE-0040`): the composition of the 46 cluster rows, the
mandatory fields of a record, the content checks of a record - case directories
and `discover_cases` (R4), the grammar and presence of the cited annotations,
the opcode and stub names and the sufficiency of the P0 evidence (R5), the
commands of both profiles (R6), the schema of a stub-inventory position and the
machine check of its triple coverage (R8), the `--control-set` mode (R7), the
exit codes and the reporting are implemented here. The content of the inventory
positions, its regeneration command and its list of classifying references
belong to E-STUB (ISSUE-0046); the `--self-test` mode (R10) is implemented
here as well (ISSUE-0055).
"""

import contextlib
import importlib.util
import io
import os
import re
import shlex
import sys
import tempfile
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Set, Tuple

# Never leave bytecode caches in the source tree (this module is imported by
# the VM-free fixtures of the follow-up self-test).
sys.dont_write_bytecode = True

# All default paths are resolved from the location of this script
# (<checkout>/ecmascript/arksteed/test/coverage/check_closure.py), following
# the precedent of run_arksteed_tests.py.
SCRIPT_DIR = Path(__file__).resolve().parent
TEST_DIR = SCRIPT_DIR.parent
CHECKOUT_ROOT = TEST_DIR.parents[2]

MATRIX_RELATIVE_PATH = "ecmascript/arksteed/test/coverage/matrix.md"
INVENTORY_RELATIVE_PATH = "ecmascript/arksteed/test/coverage/stub-inventory.md"
# The opcode list header sits next to the arksteed compiler sources; the two
# enum lists are read by the stub-inventory checks of ISSUE-0053.
OPCODE_LIST_HEADER = TEST_DIR.parent / "arksteed_opcode_list.h"
COMMON_STUB_HEADER = TEST_DIR.parents[1] / "compiler" / "common_stub_csigns.h"
RUNTIME_STUB_HEADER = TEST_DIR.parents[1] / "stubs" / "runtime_stub_list.h"
# The runner is the canonical grammar and traversal implementation; the record
# checks load it with importlib instead of re-implementing its rules (R5).
RUNNER_PATH = TEST_DIR / "run_arksteed_tests.py"
RUNNER_MODULE_NAME = "arksteed_runner_contract"

# Fixed composition of the JS/TS cluster (R1). The authoritative source is the
# `JS/TS` row of the `Индекс кластеров` table; this list exists to catch drift
# of that row (a substituted, missing or extra ID).
JS_TS_CLUSTER_IDS: Tuple[str, ...] = (
    "G1.1", "G1.2", "G1.3", "G1.4", "G1.6",
    "G2.3j", "G2.4",
    "G3.1", "G3.2", "G3.3", "G3.4",
    "G4.1j", "G4.2", "G4.3j", "G4.5",
    "G5.1", "G5.2", "G5.3", "G5.5",
    "G6.2j", "G6.3", "G6.4",
    "G7.1j", "G7.2", "G7.3", "G7.4j",
    "G8.2j", "G8.3", "G8.4",
    "G9.1", "G9.2", "G9.4",
    "G10.1", "G10.2", "G10.3", "G10.4", "G10.5",
    "G11.1", "G11.2", "G11.3", "G11.4", "G11.5", "G11.6", "G11.7",
    "G12.1", "G12.2",
)
JS_TS_CLUSTER_ID_SET = frozenset(JS_TS_CLUSTER_IDS)

JS_TS_CLUSTER_CELL = "JS/TS"
RECORDS_SECTION_TITLE = "Записи закрытия строк кластера JS/TS"
INDEX_SECTION_TITLE = "Индекс кластеров"
STATUS_SECTION_TITLE = "Статусы строки"

# Mandatory fields of a closure record, in canonical order (R3).
RECORD_FIELDS: Tuple[str, ...] = (
    "Строка",
    "Обязательство",
    "Кейсы",
    "Команда (x64 Debug)",
    "Команда (x64 Release)",
    "Oracle",
    "Аннотации свидетельств",
    "Стимул и отличимый эффект",
    "Профиль",
    "Контроль чувствительности",
    "Остаточный пробел",
    "Решение дизайна",
)
RECORD_FIELD_INDEX = {name: index for index, name in enumerate(RECORD_FIELDS)}

# Used only when the `### Статусы строки` dictionary cannot be read; the values
# are the canonical vocabulary of the matrix (`Статусы строки`).
FALLBACK_STATUS_VOCABULARY = frozenset(
    ("covered-existing", "covered-new", "open", "not-applicable", "known-defect")
)

EXIT_OK = 0
EXIT_DEFECT = 1
EXIT_USAGE = 2
EXIT_INCOMPLETE = 3

# Entry-point modes (R7, R10, R9). `--control-set` is the machine-readable
# control-set mode of this task; `--self-test` is reserved by ISSUE-0055 and is
# rejected here as an unknown argument, while the pair is rejected as a usage
# error because the modes are mutually exclusive.
MODE_DEFAULT = "default"
MODE_HELP = "help"
MODE_CONTROL_SET = "control-set"
MODE_SELF_TEST = "self-test"
CONTROL_SET_FLAG = "--control-set"
SELF_TEST_FLAG = "--self-test"

GROUP_SECTION_RE = re.compile(r"^## G(\d+)\b")
SECTION_HEADING_RE = re.compile(r"^##\s")
ANY_HEADING_RE = re.compile(r"^#{1,6}\s")
ID_PATTERN = r"G\d+(?:\.\d+)?[a-z]?"
ID_RE = re.compile(r"^%s$" % ID_PATTERN)
# A per-row diagnostic of the records section starts with the block ID
# (`<ID>: <поле>: <сообщение>`); a section-level message does not name a block.
MESSAGE_ID_RE = re.compile(r"^(?P<id>%s): " % ID_PATTERN)
ID_HEADING_RE = re.compile(r"^###\s+(%s)\b.*$" % ID_PATTERN)
CANONICAL_BLOCK_HEADING_RE = re.compile(r"^###\s+(%s)\s+—\s+.+$" % ID_PATTERN)
FIELD_LINE_RE = re.compile(r"^-\s+\*\*(?P<name>[^*]+?):\*\*[ \t]*(?P<value>.*)$")
ROW_FIELD_RE = re.compile(
    r"^`?(?P<id>%s)\s*\(\s*(?P<priority>P0|P1)\s*,\s*кластер\s*`JS/TS`\s*\)`?\.?$"
    % ID_PATTERN
)
SEPARATOR_CELL_RE = re.compile(r"^:?-{2,}:?$")
STATUS_TOKEN_RE = re.compile(r"[`\s]*([A-Za-z][A-Za-z0-9-]*)")
RESIDUAL_GAP_TOKEN_RE = re.compile(r"(?<![A-Za-zА-Яа-яЁё])нет(?![A-Za-zА-Яа-яЁё])")

PROFILE_TOKENS = ("x64", "Debug", "Release")

# Mode of the runner (R6). The checker never re-implements the `argparse`
# grammar of `-m`/`--mode`; it asks the runner's own parser (ISSUE-0060), so the
# accepted spellings, the last-occurrence policy of the non-accumulating option
# and the `debug` default are exactly those of the runner.

# Fields of a stub-inventory position (R8): the three identity fields and the
# three obligations of the triple coverage. A position is the block
# `### <заголовок>` and carries each of these fields exactly once. A block
# carrying none of the field names is an auxiliary section of the artifact (or
# prose) and is not a position, so it cannot produce a defect.
INVENTORY_POSITION_FIELDS: Tuple[str, ...] = (
    "Перечисление",
    "Имя в дампе",
    "Локатор эмиссии",
)
INVENTORY_OBLIGATION_FIELDS: Tuple[str, ...] = (
    "Нормальный путь",
    "Guard/deopt",
    "Исключение",
)
INVENTORY_FIELDS: Tuple[str, ...] = INVENTORY_POSITION_FIELDS + INVENTORY_OBLIGATION_FIELDS
INVENTORY_FIELD_SET = frozenset(INVENTORY_FIELDS)

# Schema of one position (R8): the enumeration form, the dump-name form, the
# emission locator and the two obligation forms. The literal spellings are the
# single schema shared with the artifact of E-STUB/ISSUE-0046; alternative
# spellings are reported as a format discrepancy.
INVENTORY_HEADING_RE = re.compile(r"^###\s+(?P<title>\S.*)$")
# A block whose heading names an enumeration entry is a position even when every
# field line is missing, so a mangled position cannot be silently skipped as an
# auxiliary section.
ENUM_IN_HEADING_RE = re.compile(r"\b(?:CommonStubID::\w+|RTSTUB_ID\(\w+\))")
COMMON_ENUM_RE = re.compile(r"^CommonStubID::(?P<name>[A-Za-z_][A-Za-z0-9_]*)$")
RUNTIME_ENUM_RE = re.compile(r"^RTSTUB_ID\((?P<name>[A-Za-z_][A-Za-z0-9_]*)\)$")
DUMP_NAME_RE = re.compile(r"^(?P<prefix>COStub_|RTStub_)(?P<tail>.+)$")
LOCATOR_LINE_RE = re.compile(r"^(?P<path>.+):(?P<line>\d+)$")
CLOSED_OBLIGATION_RE = re.compile(
    r"^закрыто\s*—\s*кейс\s*`(?P<case>[^`]+)`\s*,\s*аннотация\s*`(?P<annotation>[^`]+)`$"
)
NOT_APPLICABLE_OBLIGATION_RE = re.compile(
    r"^not-applicable\s*—\s*причина из реализации:\s*(?P<reason>.*?)\s*;\s*"
    r"локатор проверки достижимости:\s*(?P<locator>.*?)\s*;\s*"
    r"подтверждение reviewer:\s*(?P<review>.*)$"
)


@dataclass(frozen=True)
class RootDescriptor:
    """Every path the production checks read.

    `checkout_root` is used only to render diagnostic paths relative to the
    checkout; no check consults a path outside this descriptor.
    """

    checkout_root: Path
    matrix_path: Path
    cases_dir: Path
    opcode_list_header: Path
    common_stub_header: Path
    runtime_stub_header: Path
    inventory_path: Path

    def display_path(self, path: Path) -> str:
        """Render `path` relative to the checkout root when it lives inside."""
        try:
            return path.relative_to(self.checkout_root).as_posix()
        except ValueError:
            return path.as_posix()


def default_descriptor() -> RootDescriptor:
    """Descriptor of the production tree, resolved from this script."""
    return RootDescriptor(
        checkout_root=CHECKOUT_ROOT,
        matrix_path=CHECKOUT_ROOT / MATRIX_RELATIVE_PATH,
        cases_dir=TEST_DIR,
        opcode_list_header=OPCODE_LIST_HEADER,
        common_stub_header=COMMON_STUB_HEADER,
        runtime_stub_header=RUNTIME_STUB_HEADER,
        inventory_path=CHECKOUT_ROOT / INVENTORY_RELATIVE_PATH,
    )


@dataclass(frozen=True)
class MatrixRow:
    """One row of a group table (`## G1 —` ... `## G12 —`)."""

    row_id: str
    priority: str
    task: str
    status: str
    status_raw: str
    line: int


@dataclass(frozen=True)
class FieldEntry:
    """One `- **<Поле>:** <значение>` line of a record block."""

    name: str
    value: str
    line: int


@dataclass(frozen=True)
class RecordBlock:
    """One `### <ID> — <название>` block of the records section."""

    record_id: str
    line: int
    fields: Tuple[FieldEntry, ...]


@dataclass
class _BlockBuilder:
    """Mutable accumulator of one record block while the section is scanned."""

    record_id: str
    line: int
    fields: List[FieldEntry]


@dataclass(frozen=True)
class InventoryBlock:
    """One `### <заголовок>` position of the stub inventory artifact (R8)."""

    title: str
    line: int
    fields: Tuple[FieldEntry, ...]


@dataclass
class _InventoryBuilder:
    """Mutable accumulator of one inventory position while the artifact is scanned."""

    title: str
    line: int
    fields: List[FieldEntry]


@dataclass
class CheckReport:
    """Result of `check_closure`."""

    exit_code: int
    defects: List[str]
    incomplete: List[str]
    record_count: int
    summary: str
    usage_error: Optional[str] = None

    @property
    def messages(self) -> List[str]:
        """Messages for stdout: structural defects first, then incompleteness."""
        return list(self.defects) + list(self.incomplete)


@dataclass
class ControlSetReport:
    """Result of `check_control_set` (R7).

    `paths` is the machine-readable stdout of the mode (sorted, duplicate-free
    case paths); `diagnostics` is the stderr side and names the row and the
    field of every defect.
    """

    exit_code: int
    paths: List[str]
    diagnostics: List[str]
    usage_error: Optional[str] = None


def _table_cells(line: str) -> Optional[List[str]]:
    """Split a markdown table row into stripped cells, or return None."""
    stripped = line.strip()
    if not stripped.startswith("|"):
        return None
    stripped = stripped.strip("|")
    return [cell.strip() for cell in stripped.split("|")]


def _is_table_header_or_separator(cells: Sequence[str]) -> bool:
    if not cells:
        return True
    if cells[0].strip("`").lower() == "id":
        return True
    return all(SEPARATOR_CELL_RE.match(cell) for cell in cells if cell != "")


def _find_section(lines: Sequence[str], title: str) -> Optional[Tuple[int, int]]:
    """Return the half-open body range of the `## <title>` section."""
    heading = "## %s" % title
    for index, line in enumerate(lines):
        if line.strip() == heading:
            end = len(lines)
            for probe in range(index + 1, len(lines)):
                if SECTION_HEADING_RE.match(lines[probe]):
                    end = probe
                    break
            return index + 1, end
    return None


def _find_heading(lines: Sequence[str], pattern: re.Pattern) -> Optional[int]:
    """Return the index of the first heading matching `pattern`."""
    for index, line in enumerate(lines):
        if pattern.match(line):
            return index
    return None


def parse_index_ids(lines: Sequence[str]) -> Tuple[Optional[List[str]], List[str]]:
    """Read the cluster composition from the `JS/TS` row of the index table.

    Returns the ID occurrences in row order (None when the index or its `JS/TS`
    row cannot be read) plus the structural defects found while reading it. The
    occurrences are kept as a list, so that a repeated ID of that row stays
    detectable: collapsing them into a set would silently accept an index row
    that lists one ID more than once (R1).
    """
    defects: List[str] = []
    bounds = _find_section(lines, INDEX_SECTION_TITLE)
    if bounds is None:
        defects.append(
            "%s: Раздел: раздел «%s» отсутствует в матрице покрытия — состав "
            "46 строк кластера не может быть прочитан"
            % (INDEX_SECTION_TITLE, INDEX_SECTION_TITLE)
        )
        return None, defects
    start, end = bounds
    for index in range(start, end):
        cells = _table_cells(lines[index])
        if cells is None or _is_table_header_or_separator(cells):
            continue
        if JS_TS_CLUSTER_CELL not in cells[0]:
            continue
        if len(cells) < 2:
            defects.append(
                "%s: Строка `%s`: строка индекса не содержит списка ID"
                % (INDEX_SECTION_TITLE, JS_TS_CLUSTER_CELL)
            )
            return None, defects
        return list(re.findall(ID_PATTERN, cells[1])), defects
    defects.append(
        "%s: Строка `%s`: строка кластера `%s` отсутствует в таблице «%s»"
        % (INDEX_SECTION_TITLE, JS_TS_CLUSTER_CELL, JS_TS_CLUSTER_CELL, INDEX_SECTION_TITLE)
    )
    return None, defects


def parse_group_rows(lines: Sequence[str]) -> Tuple[Dict[str, List[MatrixRow]], List[str]]:
    """Read every row of the group tables, keyed by row ID."""
    rows: Dict[str, List[MatrixRow]] = {}
    defects: List[str] = []
    in_group_section = False
    for index, line in enumerate(lines):
        if GROUP_SECTION_RE.match(line):
            in_group_section = True
            continue
        if SECTION_HEADING_RE.match(line):
            in_group_section = False
            continue
        if not in_group_section:
            continue
        cells = _table_cells(line)
        if cells is None or _is_table_header_or_separator(cells):
            continue
        row_id = cells[0].strip("`")
        if len(cells) != 14:
            defects.append(
                "%s: Таблицы групп: строка таблицы содержит %d ячеек, ожидается 14"
                % (row_id, len(cells))
            )
            continue
        if not ID_RE.match(row_id):
            continue
        rows.setdefault(row_id, []).append(
            MatrixRow(
                row_id=row_id,
                priority=cells[11],
                task=cells[12].strip("`"),
                status=_normalize_status(cells[13]),
                status_raw=cells[13],
                line=index + 1,
            )
        )
    return rows, defects


def _normalize_status(raw: str) -> str:
    """Extract the leading status token of a `Статус` cell."""
    match = STATUS_TOKEN_RE.match(raw)
    return match.group(1) if match else ""


def parse_status_vocabulary(lines: Sequence[str]) -> Set[str]:
    """Read the status vocabulary from `### Статусы строки`.

    Falls back to the canonical vocabulary when the dictionary cannot be read,
    so a documentation edit cannot silently turn every row into a defect.
    """
    heading = _find_heading(lines, re.compile(r"^###\s+%s\b" % STATUS_SECTION_TITLE))
    if heading is None:
        return set(FALLBACK_STATUS_VOCABULARY)
    vocabulary: Set[str] = set()
    for index in range(heading + 1, len(lines)):
        if ANY_HEADING_RE.match(lines[index]):
            break
        cells = _table_cells(lines[index])
        if cells is None or _is_table_header_or_separator(cells):
            continue
        vocabulary.add(cells[0].strip("`"))
    return vocabulary or set(FALLBACK_STATUS_VOCABULARY)


def parse_records_section(
    lines: Sequence[str],
) -> Tuple[Optional[Dict[str, List[RecordBlock]]], List[str]]:
    """Read the closure record blocks of the JS/TS records section.

    Returns the blocks keyed by block ID (None when the section is absent) plus
    the structural defects of the section and its block headings.
    """
    defects: List[str] = []
    bounds = _find_section(lines, RECORDS_SECTION_TITLE)
    if bounds is None:
        defects.append(
            "%s: Раздел: раздел записей отсутствует в матрице покрытия"
            % RECORDS_SECTION_TITLE
        )
        return None, defects
    start, end = bounds
    blocks: Dict[str, List[RecordBlock]] = {}
    builder: Optional[_BlockBuilder] = None

    def flush(pending: Optional[_BlockBuilder]) -> None:
        if pending is None:
            return
        blocks.setdefault(pending.record_id, []).append(
            RecordBlock(
                record_id=pending.record_id,
                line=pending.line,
                fields=tuple(pending.fields),
            )
        )

    for index in range(start, end):
        line = lines[index]
        if ANY_HEADING_RE.match(line):
            flush(builder)
            builder = None
            canonical = CANONICAL_BLOCK_HEADING_RE.match(line)
            identifier = ID_HEADING_RE.match(line)
            if canonical is not None:
                builder = _BlockBuilder(
                    record_id=canonical.group(1),
                    line=index + 1,
                    fields=[],
                )
            elif identifier is not None:
                defects.append(
                    "%s: Блок записи: заголовок не соответствует форме "
                    "`### <ID> — <название>`" % identifier.group(1)
                )
                builder = _BlockBuilder(
                    record_id=identifier.group(1),
                    line=index + 1,
                    fields=[],
                )
            continue
        if builder is None:
            continue
        field = FIELD_LINE_RE.match(line)
        if field is not None:
            builder.fields.append(
                FieldEntry(
                    name=field.group("name").strip(),
                    value=field.group("value").strip(),
                    line=index + 1,
                )
            )
            continue
        if line.strip() and builder.fields:
            # Continuation of a wrapped markdown value.
            last = builder.fields[-1]
            builder.fields[-1] = FieldEntry(
                name=last.name,
                value=("%s %s" % (last.value, line.strip())).strip(),
                line=last.line,
            )
    flush(builder)
    return blocks, defects


def parse_inventory_blocks(text: str) -> List[InventoryBlock]:
    """Read the position blocks of the stub inventory artifact (R8).

    Every `### <заголовок>` block is scanned the same way as a record block:
    fields are `- **<Поле>:** <значение>` lines and a wrapped value continues on
    the following line. A block is a position when it carries at least one
    inventory field name or when its heading names an enumeration entry; the
    auxiliary sections of the artifact (the regeneration command, the list of
    classifying references, prose) do neither and are ignored, so they cannot
    produce a defect.
    """
    lines = text.splitlines()
    blocks: List[InventoryBlock] = []
    builder: Optional[_InventoryBuilder] = None

    def flush(pending: Optional[_InventoryBuilder]) -> None:
        if pending is None:
            return
        if (
            not any(field.name in INVENTORY_FIELD_SET for field in pending.fields)
            and ENUM_IN_HEADING_RE.search(pending.title) is None
        ):
            return
        blocks.append(
            InventoryBlock(
                title=pending.title,
                line=pending.line,
                fields=tuple(pending.fields),
            )
        )

    for index, line in enumerate(lines):
        if ANY_HEADING_RE.match(line):
            flush(builder)
            builder = None
            heading = INVENTORY_HEADING_RE.match(line)
            if heading is not None:
                builder = _InventoryBuilder(
                    title=heading.group("title").strip(),
                    line=index + 1,
                    fields=[],
                )
            continue
        if builder is None:
            continue
        field = FIELD_LINE_RE.match(line)
        if field is not None:
            builder.fields.append(
                FieldEntry(
                    name=field.group("name").strip(),
                    value=field.group("value").strip(),
                    line=index + 1,
                )
            )
            continue
        if line.strip() and builder.fields:
            # Continuation of a wrapped markdown value.
            last = builder.fields[-1]
            builder.fields[-1] = FieldEntry(
                name=last.name,
                value=("%s %s" % (last.value, line.strip())).strip(),
                line=last.line,
            )
    flush(builder)
    return blocks


def check_cluster_composition(
    index_occurrences: Optional[Sequence[str]],
    rows: Dict[str, List[MatrixRow]],
) -> List[str]:
    """Compare the index, the fixed list and the group tables (R1).

    The index row is compared both as an occurrence list and as a set: the
    occurrence list catches a repeated ID (the row lists more entries than the
    fixed list, while the distinct set may still be complete), the set catches a
    substituted, missing or extra ID.
    """
    defects: List[str] = []
    fixed = set(JS_TS_CLUSTER_IDS)
    expected_count = len(JS_TS_CLUSTER_IDS)

    if index_occurrences is not None:
        index_ids = set(index_occurrences)
        occurrence_counts = Counter(index_occurrences)
        for identifier in sorted(
            name for name, count in occurrence_counts.items() if count > 1
        ):
            defects.append(
                "%s: Состав: ID `%s` повторён в строке `%s` индекса %d раза "
                "(индекс — %d вхождений, фиксированный список — 1 вхождение)"
                % (
                    INDEX_SECTION_TITLE,
                    identifier,
                    JS_TS_CLUSTER_CELL,
                    occurrence_counts[identifier],
                    occurrence_counts[identifier],
                )
            )
        if len(index_ids) != expected_count:
            defects.append(
                "%s: Состав: число ID кластера `%s` = %d, фиксированный список = %d"
                % (INDEX_SECTION_TITLE, JS_TS_CLUSTER_CELL, len(index_ids), expected_count)
            )
        if len(index_occurrences) != expected_count:
            defects.append(
                "%s: Состав: число вхождений ID в строке `%s` индекса = %d, "
                "фиксированный список = %d"
                % (
                    INDEX_SECTION_TITLE,
                    JS_TS_CLUSTER_CELL,
                    len(index_occurrences),
                    expected_count,
                )
            )
        for missing in sorted(fixed - index_ids):
            defects.append(
                "%s: %s: ID фиксированного списка %d строк отсутствует в строке `%s` "
                "индекса (фиксированный список — присутствует, индекс — отсутствует)"
                % (missing, INDEX_SECTION_TITLE, expected_count, JS_TS_CLUSTER_CELL)
            )
        for extra in sorted(index_ids - fixed):
            defects.append(
                "%s: %s: ID присутствует в строке `%s` индекса и отсутствует в "
                "фиксированном списке %d строк (индекс — присутствует, фиксированный "
                "список — отсутствует)"
                % (extra, INDEX_SECTION_TITLE, JS_TS_CLUSTER_CELL, expected_count)
            )

    for row_id in JS_TS_CLUSTER_IDS:
        matched = rows.get(row_id, [])
        if not matched:
            defects.append(
                "%s: Таблицы групп: строка отсутствует (индекс и фиксированный список — "
                "строка есть, таблицы групп `## G1 —` … `## G12 —` — строки нет)" % row_id
            )
            continue
        if len(matched) > 1:
            lines = ", ".join(str(row.line) for row in matched)
            defects.append(
                "%s: Таблицы групп: строка повторена %d раза (строки матрицы %s; "
                "индекс — один ID, таблицы групп — %d строки)"
                % (row_id, len(matched), lines, len(matched))
            )
            continue
        row = matched[0]
        if row.task != JS_TS_CLUSTER_CELL:
            defects.append(
                "%s: Таблицы групп: `Задача` = `%s`, ожидается `%s` (индекс и фиксированный "
                "список — `%s`, таблица групп — `%s`)"
                % (row_id, row.task, JS_TS_CLUSTER_CELL, JS_TS_CLUSTER_CELL, row.task)
            )

    for row_id in sorted(rows):
        if row_id in fixed:
            continue
        for row in rows[row_id]:
            if row.task == JS_TS_CLUSTER_CELL:
                defects.append(
                    "%s: Таблицы групп: строка с `Задача` = `%s` отсутствует в фиксированном "
                    "списке %d строк (таблица групп — присутствует, фиксированный список — "
                    "отсутствует)" % (row_id, JS_TS_CLUSTER_CELL, expected_count)
                )
    return defects


def check_record_block(
    block: RecordBlock,
    row_id: str,
    priority: Optional[str],
) -> List[str]:
    """Check the mandatory fields and sub-forms of one record block (R2, R3)."""
    defects: List[str] = []
    counts: Dict[str, int] = {}
    first_seen: List[str] = []
    for entry in block.fields:
        if entry.name not in counts:
            first_seen.append(entry.name)
        counts[entry.name] = counts.get(entry.name, 0) + 1

    for name in RECORD_FIELDS:
        if name not in counts:
            defects.append(
                "%s: %s: отсутствует обязательное поле записи закрытия" % (row_id, name)
            )
    for name in first_seen:
        if name in RECORD_FIELD_INDEX and counts[name] > 1:
            lines = ", ".join(
                str(entry.line) for entry in block.fields if entry.name == name
            )
            defects.append(
                "%s: %s: повторённое обязательное поле записи закрытия (%d раза; "
                "строки матрицы %s)" % (row_id, name, counts[name], lines)
            )

    previous_index = -1
    previous_name: Optional[str] = None
    for entry in block.fields:
        index = RECORD_FIELD_INDEX.get(entry.name)
        if index is None:
            continue
        if index < previous_index:
            expected = (
                RECORD_FIELDS[previous_index + 1]
                if previous_index + 1 < len(RECORD_FIELDS)
                else RECORD_FIELDS[previous_index]
            )
            defects.append(
                "%s: %s: нарушен порядок обязательных полей записи закрытия "
                "(после `%s` ожидалось `%s`)" % (row_id, entry.name, previous_name, expected)
            )
        else:
            previous_index = index
            previous_name = entry.name

    for entry in block.fields:
        if entry.name in RECORD_FIELD_INDEX and not entry.value:
            defects.append(
                "%s: %s: пустое значение обязательного поля записи закрытия"
                % (row_id, entry.name)
            )

    values = {entry.name: entry.value for entry in block.fields}

    if counts.get("Строка") == 1 and values["Строка"]:
        value = values["Строка"]
        match = ROW_FIELD_RE.match(value)
        if match is None:
            defects.append(
                "%s: Строка: значение `%s` не соответствует канонической форме "
                "`<ID> (<P0|P1>, кластер `JS/TS`)`" % (row_id, value)
            )
        else:
            if match.group("id") != row_id:
                defects.append(
                    "%s: Строка: ID `%s` не совпадает с ID блока `%s`"
                    % (row_id, match.group("id"), row_id)
                )
            if priority is not None and match.group("priority") != priority:
                defects.append(
                    "%s: Строка: приоритет `%s` не совпадает с ячейкой `Приоритет` "
                    "строки матрицы (`%s`)" % (row_id, match.group("priority"), priority)
                )

    if counts.get("Профиль") == 1 and values["Профиль"]:
        for token in PROFILE_TOKENS:
            if token not in values["Профиль"]:
                defects.append(
                    "%s: Профиль: отсутствует обязательный токен `%s` (значение: `%s`)"
                    % (row_id, token, values["Профиль"])
                )

    if counts.get("Остаточный пробел") == 1 and values["Остаточный пробел"]:
        if RESIDUAL_GAP_TOKEN_RE.search(values["Остаточный пробел"]) is None:
            defects.append(
                "%s: Остаточный пробел: отсутствует канонический токен `нет` (значение: `%s`)"
                % (row_id, values["Остаточный пробел"])
            )
    return defects


# ---------------------------------------------------------------------------
# Runner contract, case directories, commands and annotations (R4-R6)
# ---------------------------------------------------------------------------

# The runner is the only sanctioned source of the annotation grammar and of the
# test-tree traversal. This module loads it and reuses its public rules; it
# never re-implements a grammar that could drift from the runner (R5).
V_ENTRY_RE = re.compile(r"\bV\((\w+)\)")
BACKTICK_SPAN_RE = re.compile(r"`([^`]*)`")
CORPUS_PREFIX = "corpus/"
CORPUS_DESCRIPTOR_SUFFIX = ".annotations"

# Annotation kinds recognised in the `Аннотации свидетельств` field. The set is
# the runner's constraint vocabulary (IR constraints and evidence constraints)
# plus the codegen and bytecode counts the runner parses in the same method
# scope; every other keyword is a record defect (R5).
MECHANISM_ANNOTATIONS = frozenset(("HAS", "HAS_NOT", "CODEGEN_COUNT"))
COUNTER_ANNOTATIONS = frozenset(
    (
        "COMPILE_COUNT",
        "COMPILE_COUNT_GE",
        "DEOPT_COUNT",
        "DEOPT_COUNT_GE",
        "GC_COUNT_GE",
    )
)
EXEC_ONLY_ANNOTATIONS = frozenset(("EXEC_COUNT", "EXEC_COUNT_GE"))
# Annotations the runner only accepts after a `//! METHOD` line (R5).
METHOD_SCOPED_ANNOTATIONS = frozenset(
    (
        "HAS",
        "HAS_NOT",
        "COUNT",
        "COUNT_GE",
        "COUNT_LE",
        "CODEGEN_COUNT",
        "BYTECODE_COUNT",
    )
)

# Canonical control-class tokens and the shape of a reference to the control
# report (R3); the report artifacts belong to ISSUE-0041.
CONTROL_CLASS_TOKENS = (
    "JIT-off",
    "подмена ожидания",
    "count-perturbation",
    "address-substitution",
)
CONTROL_REPORT_RE = re.compile(
    r"\]\([^)]+\)"
    r"|https?://\S+"
    r"|\b[A-Za-z0-9_.-]+\.(?:md|json|tsv|txt)\b"
    r"|`[^`]*/[^`]*`"
    r"|[A-Za-z0-9_.-]+#[A-Z][0-9]+"
)

# A design reference is `<page>#<key>` with the key `[A-Z][0-9]+`; the value
# must also carry a phrase naming the mechanism and the verification method.
DESIGN_REFERENCE_RE = re.compile(r"[A-Za-z0-9_.-]+#[A-Z][0-9]+")
PHRASE_RE = re.compile(r"[A-Za-zА-Яа-яЁё]{3,}")
SHELL_WILDCARD_CHARS = "*?[]"


class ContractUsageError(Exception):
    """A contract input cannot be read; mapped to exit code 2 (R9)."""


@dataclass(frozen=True)
class RunnerContract:
    """The runner module plus the name lists the record checks read."""

    module: object
    valid_opcodes: Set[str]
    common_stub_names: Set[str]
    runtime_stub_names: Set[str]


@dataclass
class CaseInfo:
    """One discovered built-in leaf case of a record."""

    rel: str
    directory: Path
    source: Path
    expected: Path


@dataclass
class ContentContext:
    """Caches shared by the content checks of every record."""

    descriptor: RootDescriptor
    contract: RunnerContract
    bodies: Dict[Path, Tuple[Set[str], Set[str]]] = field(default_factory=dict)
    _discovered: Optional[List[str]] = None
    _discovered_set: Optional[Set[str]] = None

    def discovered_paths(self) -> List[str]:
        """Relative paths of every case discoverable under the cases root."""
        if self._discovered is None:
            self._discovered = discover_case_paths(self.descriptor, self.contract)
        return self._discovered

    def discovered_set(self) -> Set[str]:
        """The discoverable case paths as a set (cached for repeated lookups)."""
        if self._discovered_set is None:
            self._discovered_set = set(self.discovered_paths())
        return self._discovered_set


@dataclass(frozen=True)
class ParsedAnnotation:
    """One successfully parsed citation of the `Аннотации свидетельств` field."""

    raw: str
    normalized: str
    keyword: str

    @property
    def method_scoped(self) -> bool:
        return self.keyword in METHOD_SCOPED_ANNOTATIONS


def _same_path(left: Path, right: Path) -> bool:
    """Whether two paths resolve to the same location."""
    try:
        return Path(left).resolve() == Path(right).resolve()
    except OSError:
        return False


def load_runner_module() -> object:
    """Load `run_arksteed_tests.py` through importlib, without bytecode caches."""
    spec = importlib.util.spec_from_file_location(RUNNER_MODULE_NAME, RUNNER_PATH)
    if spec is None or spec.loader is None:
        raise ContractUsageError("не удаётся загрузить раннер %s" % RUNNER_PATH)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    except Exception as error:  # pragma: no cover - defensive, exercised on damage
        raise ContractUsageError("не удаётся загрузить раннер %s: %s" % (RUNNER_PATH, error))
    return module


def _read_text(path: Path, what: str) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ContractUsageError("нечитаемый %s %s: %s" % (what, path, error))


def _read_valid_opcodes(descriptor: RootDescriptor, runner: object) -> Set[str]:
    """Read `arksteed_opcode_list.h`; unreadable input is a usage error (R5)."""
    if _same_path(descriptor.opcode_list_header, runner.OPCODE_LIST_HEADER):
        names = runner.load_opcode_names()
        if not names:
            raise ContractUsageError(
                "нечитаемый заголовок списка опкодов %s" % descriptor.opcode_list_header
            )
        return set(names)
    text = _read_text(descriptor.opcode_list_header, "заголовок списка опкодов")
    names = set(V_ENTRY_RE.findall(text))
    if not names:
        raise ContractUsageError(
            "заголовок списка опкодов %s не содержит ни одного имени опкода"
            % descriptor.opcode_list_header
        )
    return names


def _extract_common_stub_names(text: str) -> Set[str]:
    """Names of `COMMON_STUB_LIST`, including the `XStwCopy` variants (R5)."""
    names: Set[str] = set()
    inside = False
    for line in text.splitlines():
        if not inside:
            if line.startswith("#define COMMON_STUB_LIST(V)"):
                inside = True
            continue
        names.update(V_ENTRY_RE.findall(line))
        if not line.rstrip().endswith("\\"):
            break
    return names | {name + "StwCopy" for name in names}


def _extract_runtime_stub_names(text: str) -> Set[str]:
    """Names of `RUNTIME_STUB_LIST` as written in `runtime_stub_list.h` (R5)."""
    return set(V_ENTRY_RE.findall(text))


def load_runner_contract(descriptor: RootDescriptor) -> RunnerContract:
    """Load the runner and every header the content checks read."""
    runner = load_runner_module()
    valid_opcodes = _read_valid_opcodes(descriptor, runner)
    common_text = _read_text(descriptor.common_stub_header, "заголовок списка имён common stub")
    runtime_text = _read_text(
        descriptor.runtime_stub_header, "заголовок списка имён runtime stub"
    )
    common_names = _extract_common_stub_names(common_text)
    runtime_names = _extract_runtime_stub_names(runtime_text)
    if not common_names:
        raise ContractUsageError(
            "заголовок списка имён common stub %s не содержит ни одного имени"
            % descriptor.common_stub_header
        )
    if not runtime_names:
        raise ContractUsageError(
            "заголовок списка имён runtime stub %s не содержит ни одного имени"
            % descriptor.runtime_stub_header
        )
    return RunnerContract(runner, valid_opcodes, common_names, runtime_names)


def _walk_case_rels(cases_dir: Path, runner: object) -> List[str]:
    """Case paths discoverable under a non-runner cases root.

    Mirrors the traversal of `discover_cases` (entry convention, one expected
    output file, the skipped directory names) so a fake root is checked by the
    very rules the runner applies to the real tree.
    """
    rels: List[str] = []
    if not cases_dir.is_dir():
        return rels
    for dirpath, dirnames, filenames in os.walk(cases_dir):
        dirnames[:] = sorted(d for d in dirnames if d not in ("__pycache__", "excluded"))
        sources = sorted(f for f in filenames if f.endswith(runner.SOURCE_SUFFIXES))
        if not sources and runner.EXPECTED_OUTPUT_NAME not in filenames:
            continue
        case_dir = Path(dirpath)
        rel = case_dir.relative_to(cases_dir).as_posix()
        if rel == ".":
            continue
        if not sources:
            continue
        if len(sources) > 1:
            matches = [s for s in sources if Path(s).stem == case_dir.name]
            if len(matches) != 1:
                continue
        if runner.EXPECTED_OUTPUT_NAME not in filenames:
            continue
        rels.append(rel)
    return sorted(rels)


def discover_case_paths(descriptor: RootDescriptor, contract: RunnerContract) -> List[str]:
    """Relative paths of the cases discoverable under the cases root."""
    runner = contract.module
    if _same_path(descriptor.cases_dir, runner.TEST_DIR):
        return sorted(case.rel for case in runner.discover_cases([], []))
    return _walk_case_rels(descriptor.cases_dir, runner)


def _backtick_spans(value: str) -> List[str]:
    """Non-empty backtick-quoted spans of a field value, in order."""
    return [span.strip() for span in BACKTICK_SPAN_RE.findall(value) if span.strip()]


def _list_entries(value: str) -> List[str]:
    """Values of a `;`-separated list whose items are backtick-quoted."""
    entries: List[str] = []
    for span in BACKTICK_SPAN_RE.findall(value):
        for part in span.split(";"):
            part = part.strip()
            if part:
                entries.append(part)
    return entries


def _normalize_annotation(text: str) -> str:
    """Drop the inline `#` comment and collapse whitespace (R5)."""
    comment_at = text.find("#")
    if comment_at != -1:
        text = text[:comment_at]
    return " ".join(text.split())


def _annotation_bodies(path: Path, ctx: ContentContext) -> Tuple[Set[str], Set[str]]:
    """Normalized `//!` bodies of a case source or descriptor.

    Returns the bodies seen after a `//! METHOD` line and all bodies, so that a
    present-but-method-unscoped IR constraint can be told apart from an absent
    citation.
    """
    cached = ctx.bodies.get(path)
    if cached is not None:
        return cached
    scoped: Set[str] = set()
    unscoped: Set[str] = set()
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        lines = []
    method_seen = False
    for raw in lines:
        stripped = raw.strip()
        if not stripped.startswith("//!"):
            continue
        body = _normalize_annotation(stripped[3:])
        if not body:
            continue
        if body.split()[0] == "METHOD":
            method_seen = True
            continue
        unscoped.add(body)
        if method_seen:
            scoped.add(body)
    result = (scoped, unscoped)
    ctx.bodies[path] = result
    return result


def _find_annotation(
    normalized: str, method_scoped: bool, locations: Sequence[Path], ctx: ContentContext
) -> Tuple[bool, bool]:
    """Locate a normalized citation; return (found, found_out_of_scope_only)."""
    scoped_found = False
    unscoped_found = False
    for path in locations:
        scoped, unscoped = _annotation_bodies(path, ctx)
        if normalized in scoped:
            scoped_found = True
            break
        if normalized in unscoped:
            unscoped_found = True
    if method_scoped:
        return scoped_found, unscoped_found and not scoped_found
    return (scoped_found or unscoped_found), False


def _parse_citation(
    raw: str, row_id: str, ctx: ContentContext
) -> Tuple[Optional[ParsedAnnotation], List[str]]:
    """Parse one citation with the runner grammar and validate stub names (R5)."""
    defects: List[str] = []
    normalized = _normalize_annotation(raw)
    if not normalized:
        return None, [
            "%s: Аннотации свидетельств: пустая цитата аннотации" % row_id
        ]
    tokens = normalized.split()
    keyword = tokens[0]
    runner = ctx.contract.module
    constraint = None
    try:
        if keyword in runner.CONSTRAINT_BY_KEYWORD:
            constraint = runner.CONSTRAINT_BY_KEYWORD[keyword].from_tokens(
                tokens, ctx.contract.valid_opcodes
            )
        elif keyword in runner.EVIDENCE_CONSTRAINT_BY_KEYWORD:
            runner.EVIDENCE_CONSTRAINT_BY_KEYWORD[keyword].from_tokens(tokens)
        elif keyword == "CODEGEN_COUNT":
            runner.CodegenCountConstraint.from_tokens(tokens)
        elif keyword == "BYTECODE_COUNT":
            runner.BytecodeCountConstraint.from_tokens(tokens)
        else:
            return None, [
                "%s: Аннотации свидетельств: неизвестный ключ `%s` в цитате `%s`"
                % (row_id, keyword, raw)
            ]
    except runner.ConstraintError as error:
        return None, [
            "%s: Аннотации свидетельств: цитата `%s` не разбирается грамматикой раннера: %s"
            % (row_id, raw, error)
        ]
    if constraint is not None and constraint.opcode in runner.STUB_CALL_OPCODES:
        defects.extend(_check_stub_name(row_id, constraint, ctx))
    return ParsedAnnotation(raw, normalized, keyword), defects


def _check_stub_name(row_id: str, constraint: object, ctx: ContentContext) -> List[str]:
    """Validate the stub name of a `CallCommonStub`/`CallRuntime` citation (R5)."""
    stub = constraint.stub or ""
    opcode = constraint.opcode
    if any(char in stub for char in SHELL_WILDCARD_CHARS):
        return [
            "%s: Аннотации свидетельств: имя stub `%s` в аннотации `%s` содержит shell-шаблон"
            % (row_id, stub, constraint.label())
        ]
    common_family = opcode == "CallCommonStub"
    own_prefix = "COStub_" if common_family else "RTStub_"
    other_prefix = "RTStub_" if common_family else "COStub_"
    if stub.startswith(other_prefix):
        return [
            "%s: Аннотации свидетельств: имя stub `%s` в аннотации `%s` принадлежит семейству "
            "`%s`, ожидается `%s`"
            % (row_id, stub, constraint.label(), other_prefix, own_prefix)
        ]
    tail = stub[len(own_prefix):] if stub.startswith(own_prefix) else stub
    names = ctx.contract.common_stub_names if common_family else ctx.contract.runtime_stub_names
    if tail not in names:
        source = (
            ctx.descriptor.display_path(ctx.descriptor.common_stub_header)
            if common_family
            else ctx.descriptor.display_path(ctx.descriptor.runtime_stub_header)
        )
        return [
            "%s: Аннотации свидетельств: хвост `%s` имени stub в аннотации `%s` отсутствует в "
            "списке имён перечисления (%s)" % (row_id, tail, constraint.label(), source)
        ]
    return []


def _resolve_corpus_reference(
    prefix: str, entry: str, ctx: ContentContext
) -> Tuple[Optional[Path], List[str]]:
    """Resolve `corpus/<suite>/<case>` to its `.annotations` descriptor (R4).

    `prefix` renders the message place (`<ID>: Кейсы` for a record field,
    `<позиция>: <обязательство>` for an inventory obligation).
    """
    defects: List[str] = []
    parts = entry.split("/")
    if len(parts) != 3 or not parts[1] or not parts[2]:
        defects.append(
            "%s: корпусная ссылка `%s` не соответствует форме `corpus/<suite>/<case>`"
            % (prefix, entry)
        )
        return None, defects
    descriptor_path = (
        ctx.descriptor.cases_dir / "corpus" / parts[1] / (parts[2] + CORPUS_DESCRIPTOR_SUFFIX)
    )
    if not descriptor_path.is_file():
        defects.append(
            "%s: дескриптор `%s` корпусной ссылки `%s` отсутствует"
            % (prefix, ctx.descriptor.display_path(descriptor_path), entry)
        )
        return None, defects
    return descriptor_path, defects


def _resolve_builtin_case(
    prefix: str, entry: str, ctx: ContentContext
) -> Tuple[Optional[CaseInfo], List[str]]:
    """Resolve one built-in leaf case of a `Кейсы`-style value (R4).

    The path must be relative and free of `..`, the directory must exist, be
    found by `discover_cases([], [])`, hold exactly one `.ts`/`.js`/`.cjs`
    source and exactly one `expected_output.txt`; a multi-source directory is
    rejected even though the runner supports sibling modules.
    """
    defects: List[str] = []
    runner = ctx.contract.module
    if entry.startswith("/") or ".." in entry.split("/"):
        defects.append(
            "%s: путь `%s` не является относительным путём без `..`" % (prefix, entry)
        )
        return None, defects
    rel = entry.strip("/")
    directory = ctx.descriptor.cases_dir / rel
    if not directory.is_dir():
        defects.append(
            "%s: каталог кейса `%s` не существует (%s)"
            % (prefix, entry, ctx.descriptor.display_path(directory))
        )
        return None, defects
    try:
        names = os.listdir(directory)
    except OSError as error:
        defects.append("%s: каталог кейса `%s` нечитаем: %s" % (prefix, entry, error))
        return None, defects
    sources = sorted(name for name in names if name.endswith(runner.SOURCE_SUFFIXES))
    if len(sources) != 1:
        defects.append(
            "%s: каталог `%s` содержит %d JS/TS-источника, ожидается ровно один"
            % (prefix, entry, len(sources))
        )
        return None, defects
    if runner.EXPECTED_OUTPUT_NAME not in names:
        defects.append(
            "%s: каталог `%s` не содержит `%s`"
            % (prefix, entry, runner.EXPECTED_OUTPUT_NAME)
        )
        return None, defects
    if rel not in ctx.discovered_set():
        defects.append(
            "%s: каталог `%s` не обнаруживается discover_cases([], [])" % (prefix, entry)
        )
        return None, defects
    return (
        CaseInfo(
            rel=rel,
            directory=directory,
            source=directory / sources[0],
            expected=directory / runner.EXPECTED_OUTPUT_NAME,
        ),
        defects,
    )


def _check_cases_field(
    row_id: str, value: str, ctx: ContentContext
) -> Tuple[List[str], List[CaseInfo], Set[str], List[Path]]:
    """Check every value of the `Кейсы` field (R4)."""
    defects: List[str] = []
    prefix = "%s: Кейсы" % row_id
    entries = _list_entries(value)
    built_in: List[CaseInfo] = []
    built_in_rels: Set[str] = set()
    corpus_paths: List[Path] = []
    built_in_entries = 0
    for entry in entries:
        if entry.startswith(CORPUS_PREFIX):
            descriptor_path, entry_defects = _resolve_corpus_reference(prefix, entry, ctx)
            defects.extend(entry_defects)
            if descriptor_path is not None:
                corpus_paths.append(descriptor_path)
            continue
        built_in_entries += 1
        info, entry_defects = _resolve_builtin_case(prefix, entry, ctx)
        defects.extend(entry_defects)
        if info is None:
            continue
        built_in.append(info)
        built_in_rels.add(info.rel)
    if not entries:
        defects.append(
            "%s: Кейсы: поле не содержит ни одного значения в обратных кавычках" % row_id
        )
    elif not built_in_entries:
        defects.append(
            "%s: Кейсы: встроенный каталог кейса обязателен — корпусная ссылка не может быть "
            "единственным подтверждением записи" % row_id
        )
    return defects, built_in, built_in_rels, corpus_paths


def _check_case_config_errors(
    prefix: str, cases: Sequence[CaseInfo], ctx: ContentContext
) -> List[str]:
    """The cases of a record or of an obligation must parse without config errors (R5)."""
    defects: List[str] = []
    for info in cases:
        case = ctx.contract.module.TestCase(info.rel, info.source, info.expected)
        ctx.contract.module.parse_annotations(case, ctx.contract.valid_opcodes)
        for error in case.config_errors:
            defects.append("%s: кейс `%s`: %s" % (prefix, info.rel, error))
    return defects


def _option_values(tokens: Sequence[str], names: Sequence[str]) -> List[str]:
    """Values of `-x value` and `--long=value` options in a command line."""
    values: List[str] = []
    for index, token in enumerate(tokens):
        for name in names:
            if token == name and index + 1 < len(tokens):
                values.append(tokens[index + 1])
            elif token.startswith(name + "="):
                values.append(token[len(name) + 1:])
    return values


def _runner_arguments(tokens: Sequence[str]) -> Optional[List[str]]:
    """The argument vector a recorded command line hands to the runner.

    The runner entry point is the token naming `run_arksteed_tests.py` (with or
    without a leading `./` or an interpreter); every token after it is an
    argument of the runner. `None` when the command does not invoke the runner
    at all, which `_check_command` reports as its own defect.
    """
    for index, token in enumerate(tokens):
        if Path(token).name == RUNNER_PATH.name:
            return list(tokens[index + 1:])
    return None


def _parser_rejection(text: str, code: object) -> str:
    """A one-line reason for an argument vector the runner's parser rejected."""
    if code == 0:
        return "раннер печатает справку вместо запуска кейсов"
    for line in reversed(text.splitlines()):
        stripped = line.strip()
        if not stripped:
            continue
        if "error: " in stripped:
            stripped = stripped.split("error: ", 1)[1]
        return "раннер отклоняет команду: %s" % stripped
    return "раннер отклоняет команду (код argparse %s)" % code


def _resolve_runner_mode(
    runner: object, arguments: Sequence[str]
) -> Tuple[Optional[str], Optional[str]]:
    """The mode the runner itself resolves for a recorded argument vector (R6).

    The checker must not re-implement the `argparse` grammar of the runner, so
    it calls the runner's own `parse_args` on the argument vector and takes the
    resulting `mode`. That makes every spelling the runner accepts - `-m value`,
    `-mvalue`, `-m=value`, `--mode value`, `--mode=value` and the unambiguous
    long-option abbreviations such as `--mod`, `--mo`, `--m` - read exactly as
    the runner reads it, keeps the last-occurrence policy of the
    non-accumulating `-m`/`--mode` option with `debug` as the default, and makes
    a value outside `debug`/`release` a rejection of the field, because the
    runner would not execute the recorded command at all. Nothing is run: the
    parser only builds a `Namespace`. argparse prints usage and errors to
    stdout/stderr and raises `SystemExit`; both streams are captured and the
    exit is turned into the second value, which is `None` iff the vector is
    accepted. VM-free and side-effect free (ISSUE-0060).
    """
    output = io.StringIO()
    with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
        try:
            namespace = runner.parse_args(arguments)
        except SystemExit as error:  # argparse reports usage errors this way
            return None, _parser_rejection(output.getvalue(), error.code)
        except Exception as error:  # pragma: no cover - defensive, exercised on damage
            return None, "раннер не разбирает команду: %s" % error
    mode = getattr(namespace, "mode", None)
    if mode is None:
        return None, "раннер не разрешает режим команды"
    return str(mode), None


def _check_command(
    row_id: str,
    field_name: str,
    value: str,
    mode: str,
    built_in_rels: Set[str],
    ctx: ContentContext,
) -> List[str]:
    """Check one command field and its coverage of the record cases (R6)."""
    defects: List[str] = []
    spans = _backtick_spans(value)
    if len(spans) != 1:
        defects.append(
            "%s: %s: поле должно содержать ровно одну команду в обратных кавычках "
            "(найдено %d)" % (row_id, field_name, len(spans))
        )
        return defects
    command = spans[0]
    if "python3 ./run_arksteed_tests.py" not in command:
        defects.append(
            "%s: %s: команда должна содержать `python3 ./run_arksteed_tests.py`"
            % (row_id, field_name)
        )
    try:
        tokens = shlex.split(command)
    except ValueError as error:
        defects.append("%s: %s: команда не разбирается: %s" % (row_id, field_name, error))
        return defects
    if _option_values(tokens, ("-p", "--platform")) != ["x64"]:
        defects.append(
            "%s: %s: команда должна содержать ровно один `-p x64`" % (row_id, field_name)
        )
    includes = _option_values(tokens, ("-I", "--include", "--filter"))
    excludes = _option_values(tokens, ("-X", "--exclude"))
    if not includes:
        defects.append(
            "%s: %s: команда должна содержать фильтр `-I`/`--include`" % (row_id, field_name)
        )
    else:
        runner = ctx.contract.module
        selected = {
            rel
            for rel in ctx.discovered_paths()
            if runner._pattern_matches(rel, includes)
            and not (excludes and runner._pattern_matches(rel, excludes))
        }
        if selected != built_in_rels:
            missing = sorted(built_in_rels - selected)
            extra = sorted(selected - built_in_rels)
            defects.append(
                "%s: %s: фильтры команды выбирают другой набор кейсов (не выбраны: %s; лишние: %s)"
                % (
                    row_id,
                    field_name,
                    ", ".join(missing) or "нет",
                    ", ".join(extra) or "нет",
                )
            )
    runner_arguments = _runner_arguments(tokens)
    if runner_arguments is not None:
        effective_mode, parser_reason = _resolve_runner_mode(
            ctx.contract.module, runner_arguments
        )
        if parser_reason is not None:
            defects.append("%s: %s: %s" % (row_id, field_name, parser_reason))
        elif mode == "release" and effective_mode != "release":
            defects.append(
                "%s: %s: эффективный режим раннера `%s`, а Release-поле требует `-m release` "
                "(режим разрешает сам раннер: последняя опция `-m`/`--mode`, по умолчанию "
                "`debug`)" % (row_id, field_name, effective_mode)
            )
        elif mode == "debug" and effective_mode != "debug":
            defects.append(
                "%s: %s: эффективный режим раннера `%s` запрещён в Debug-поле "
                "(режим разрешает сам раннер: последняя опция `-m`/`--mode`)"
                % (row_id, field_name, effective_mode)
            )
    return defects


def _check_p0_evidence(row_id: str, keywords: Set[str], control: str) -> List[str]:
    """Check the P0 evidence and the sensitivity-control result (R5, R3).

    The target-path evidence is one of (1) a mechanism annotation (`HAS`,
    `HAS_NOT` or `CODEGEN_COUNT`), (2) a target-event counter (`COMPILE_COUNT*`,
    `DEOPT_COUNT*`, `GC_COUNT_GE`) or (3) a distinguishable effect named by the
    mandatory `Стимул и отличимый эффект` field (whose wording is confirmed by
    the reviewer, as the design requires). A list that is empty or made only of
    `EXEC_COUNT*` satisfies none of them and is rejected.
    """
    defects: List[str] = []
    mechanism = bool(keywords & MECHANISM_ANNOTATIONS)  # way (1)
    counter = bool(keywords & COUNTER_ANNOTATIONS)  # way (2)
    beyond_exec = bool(keywords - EXEC_ONLY_ANNOTATIONS)  # way (3) candidate
    if not (mechanism or counter or beyond_exec):
        defects.append(
            "%s: Аннотации свидетельств: P0-запись не называет свидетельство целевого пути — "
            "список аннотаций пуст или сводится к аннотациям `EXEC_COUNT*`" % row_id
        )
    if not any(token in control for token in CONTROL_CLASS_TOKENS):
        defects.append(
            "%s: Контроль чувствительности: P0-запись не называет ни одного канонического "
            "токена класса контроля (`JIT-off`, `подмена ожидания`, `count-perturbation`, "
            "`address-substitution`)" % row_id
        )
    if CONTROL_REPORT_RE.search(control) is None:
        defects.append(
            "%s: Контроль чувствительности: P0-запись не ссылается на отчёт о контролях"
            % row_id
        )
    return defects


def _check_design_decision(row_id: str, value: str) -> List[str]:
    """A design reference plus a phrase naming the mechanism and its check."""
    if DESIGN_REFERENCE_RE.search(value) is None:
        return [
            "%s: Решение дизайна: отсутствует ссылка вида `<страница>#<ключ>` (ключ `[A-Z][0-9]+`)"
            % row_id
        ]
    phrase = DESIGN_REFERENCE_RE.sub(" ", value)
    if PHRASE_RE.search(phrase) is None:
        return [
            "%s: Решение дизайна: отсутствует фраза о механизме и способе проверки" % row_id
        ]
    return []


def _record_values(block: RecordBlock) -> Dict[str, str]:
    """The field values of a record block, keyed by field name."""
    return {entry.name: entry.value for entry in block.fields}


def record_is_complete(block: RecordBlock) -> bool:
    """Whether every mandatory field is present exactly once with a value."""
    counts = Counter(entry.name for entry in block.fields)
    for name in RECORD_FIELDS:
        if counts.get(name) != 1:
            return False
    values = {entry.name: entry.value for entry in block.fields}
    return all(values[name] for name in RECORD_FIELDS)


def check_record_content(
    row_id: str,
    values: Dict[str, str],
    priority: Optional[str],
    ctx: ContentContext,
) -> List[str]:
    """Check the content of one complete record (R4-R6)."""
    defects: List[str] = []
    case_defects, built_in, built_in_rels, corpus_paths = _check_cases_field(
        row_id, values.get("Кейсы", ""), ctx
    )
    defects.extend(case_defects)
    defects.extend(_check_case_config_errors("%s: Кейсы" % row_id, built_in, ctx))

    locations: List[Path] = [info.source for info in built_in] + list(corpus_paths)
    citations = _list_entries(values.get("Аннотации свидетельств", ""))
    keywords: Set[str] = set()
    if not citations:
        defects.append(
            "%s: Аннотации свидетельств: поле не содержит ни одной цитаты в обратных кавычках"
            % row_id
        )
    for raw in citations:
        parsed, citation_defects = _parse_citation(raw, row_id, ctx)
        defects.extend(citation_defects)
        if parsed is None:
            continue
        keywords.add(parsed.keyword)
        if not locations:
            defects.append(
                "%s: Аннотации свидетельств: цитата `%s` не проверяема — у записи нет ни одного "
                "кейса или дескриптора" % (row_id, parsed.normalized)
            )
            continue
        found, out_of_scope = _find_annotation(
            parsed.normalized, parsed.method_scoped, locations, ctx
        )
        if found:
            continue
        if out_of_scope:
            defects.append(
                "%s: Аннотации свидетельств: цитата `%s` присутствует в кейсе до аннотации "
                "`//! METHOD` — IR-ограничение должно идти после неё"
                % (row_id, parsed.normalized)
            )
        else:
            defects.append(
                "%s: Аннотации свидетельств: цитата `%s` отсутствует в исходнике кейса и в "
                "дескрипторе корпусной ссылки" % (row_id, parsed.normalized)
            )

    defects.extend(
        _check_command(
            row_id,
            "Команда (x64 Debug)",
            values.get("Команда (x64 Debug)", ""),
            "debug",
            built_in_rels,
            ctx,
        )
    )
    defects.extend(
        _check_command(
            row_id,
            "Команда (x64 Release)",
            values.get("Команда (x64 Release)", ""),
            "release",
            built_in_rels,
            ctx,
        )
    )

    control = values.get("Контроль чувствительности", "")
    if priority == "P0":
        defects.extend(_check_p0_evidence(row_id, keywords, control))
    elif priority == "P1":
        if "не требуется" not in control or "P1" not in control:
            defects.append(
                "%s: Контроль чувствительности: для P1-строки поле должно содержать "
                "`не требуется` и `P1` (значение: `%s`)" % (row_id, control)
            )

    defects.extend(_check_design_decision(row_id, values.get("Решение дизайна", "")))
    return defects


# ---------------------------------------------------------------------------
# Stub inventory: positions, identity and the triple coverage (R8)
# ---------------------------------------------------------------------------


def _strip_code_span(value: str) -> str:
    """A field value without one optional surrounding backtick pair."""
    text = value.strip()
    if len(text) >= 2 and text.startswith("`") and text.endswith("`") and "`" not in text[1:-1]:
        return text[1:-1].strip()
    return text


def _resolve_locator(prefix: str, value: str, ctx: ContentContext) -> List[str]:
    """Resolve a `path:line` / `path#symbol` locator against the checkout root (R8).

    The path is relative to the descriptor's checkout root; the `path:line` form
    must name a line inside the file boundaries and the `path#symbol` form must
    find the symbol in the file text. `prefix` renders the message place
    (`<позиция>: Локатор эмиссии` or `<позиция>: <обязательство>: локатор
    проверки достижимости`).
    """
    defects: List[str] = []
    text = _strip_code_span(value)
    path_text: str
    symbol: Optional[str] = None
    line_number: Optional[int] = None
    if "#" in text:
        path_text, _, raw_symbol = text.partition("#")
        path_text = path_text.strip()
        symbol = raw_symbol.strip()
        if not path_text or not symbol:
            defects.append(
                "%s: значение `%s` не соответствует форме `path:line` или `path#symbol`"
                % (prefix, text)
            )
            return defects
    else:
        match = LOCATOR_LINE_RE.match(text)
        if match is None:
            defects.append(
                "%s: значение `%s` не соответствует форме `path:line` или `path#symbol`"
                % (prefix, text)
            )
            return defects
        path_text = match.group("path").strip()
        line_number = int(match.group("line"))
    if not path_text or path_text.startswith("/") or ".." in path_text.split("/"):
        defects.append(
            "%s: путь `%s` не является относительным путём от корня checkout"
            % (prefix, path_text)
        )
        return defects
    resolved = ctx.descriptor.checkout_root / path_text.strip("/")
    display = ctx.descriptor.display_path(resolved)
    if not resolved.is_file():
        defects.append("%s: файл `%s` не существует" % (prefix, display))
        return defects
    try:
        content = resolved.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        defects.append("%s: файл `%s` нечитаем: %s" % (prefix, display, error))
        return defects
    if line_number is not None:
        count = len(content.splitlines())
        if line_number < 1 or line_number > count:
            defects.append(
                "%s: строка %d вне границ файла `%s` (в файле %d строк)"
                % (prefix, line_number, display, count)
            )
    elif symbol not in content:
        defects.append("%s: символ `%s` отсутствует в файле `%s`" % (prefix, symbol, display))
    return defects


def _check_inventory_identity(
    position: str,
    enum_value: Optional[str],
    dump_value: Optional[str],
    ctx: ContentContext,
) -> Tuple[Optional[str], Optional[str], List[str]]:
    """Check `Перечисление` and `Имя в дампе` against the enum name lists (R8).

    `enum_value` / `dump_value` are None when the corresponding field is absent,
    repeated or empty (that defect is reported by the field check of the
    position), so this function reports only the content of present values.
    Returns (dump family, dump tail, defects).
    """
    defects: List[str] = []
    family: Optional[str] = None
    name: Optional[str] = None
    enum_text = ""
    if enum_value is not None:
        enum_text = _strip_code_span(enum_value)
        match = COMMON_ENUM_RE.match(enum_text)
        if match is not None:
            family, name = "common", match.group("name")
        else:
            match = RUNTIME_ENUM_RE.match(enum_text)
            if match is not None:
                family, name = "runtime", match.group("name")
        if family is None:
            defects.append(
                "%s: Перечисление: значение `%s` не соответствует форме `CommonStubID::X` "
                "или `RTSTUB_ID(X)`" % (position, enum_text)
            )
        else:
            names = (
                ctx.contract.common_stub_names
                if family == "common"
                else ctx.contract.runtime_stub_names
            )
            header = (
                ctx.descriptor.common_stub_header
                if family == "common"
                else ctx.descriptor.runtime_stub_header
            )
            if name not in names:
                defects.append(
                    "%s: Перечисление: имя `%s` отсутствует в списке имён перечисления (%s)"
                    % (position, name, ctx.descriptor.display_path(header))
                )
                name = None

    dump_family: Optional[str] = None
    dump_tail: Optional[str] = None
    dump_text = ""
    if dump_value is not None:
        dump_text = _strip_code_span(dump_value)
        if any(char in dump_text for char in SHELL_WILDCARD_CHARS):
            defects.append(
                "%s: Имя в дампе: значение `%s` содержит shell-шаблон" % (position, dump_text)
            )
        else:
            dump_match = DUMP_NAME_RE.match(dump_text)
            if dump_match is None:
                defects.append(
                    "%s: Имя в дампе: значение `%s` не соответствует форме `COStub_X` или "
                    "`RTStub_X`" % (position, dump_text)
                )
            else:
                dump_family = "common" if dump_match.group("prefix") == "COStub_" else "runtime"
                dump_tail = dump_match.group("tail")

    if family is not None and dump_family is not None:
        if family != dump_family:
            expected_opcode = "CallCommonStub" if family == "common" else "CallRuntime"
            defects.append(
                "%s: Имя в дампе: префикс `%s` не согласован с формой имени перечисления "
                "`%s` (семейству %s соответствует `%s`)"
                % (
                    position,
                    dump_text,
                    enum_text,
                    "`CommonStubID::X`" if family == "common" else "`RTSTUB_ID(X)`",
                    expected_opcode,
                )
            )
        elif name is not None and dump_tail is not None and name != dump_tail:
            defects.append(
                "%s: Имя в дампе: хвост `%s` не совпадает с именем перечисления `%s`"
                % (position, dump_tail, name)
            )
    return dump_family, dump_tail, defects


def _check_address_annotation(
    prefix: str,
    annotation: str,
    dump_family: Optional[str],
    dump_tail: Optional[str],
    ctx: ContentContext,
) -> List[str]:
    """Check the `HAS CallCommonStub|CallRuntime <точное имя>` form (R8, R5)."""
    defects: List[str] = []
    normalized = _normalize_annotation(annotation)
    tokens = normalized.split()
    if not tokens or tokens[0] != "HAS":
        defects.append(
            "%s: аннотация `%s` должна иметь форму `HAS CallCommonStub <точное имя>` или "
            "`HAS CallRuntime <точное имя>`" % (prefix, normalized or annotation)
        )
        return defects
    runner = ctx.contract.module
    try:
        constraint = runner.CONSTRAINT_BY_KEYWORD["HAS"].from_tokens(
            tokens, ctx.contract.valid_opcodes
        )
    except runner.ConstraintError as error:
        defects.append(
            "%s: аннотация `%s` не разбирается грамматикой раннера: %s"
            % (prefix, normalized, error)
        )
        return defects
    if constraint.opcode not in runner.STUB_CALL_OPCODES:
        defects.append(
            "%s: аннотация `%s` должна называть `CallCommonStub` или `CallRuntime`"
            % (prefix, normalized)
        )
        return defects
    stub = constraint.stub or ""
    if any(char in stub for char in SHELL_WILDCARD_CHARS):
        defects.append(
            "%s: имя stub `%s` в аннотации `%s` содержит shell-шаблон — требуется точное имя"
            % (prefix, stub, normalized)
        )
        return defects
    family = "common" if constraint.opcode == "CallCommonStub" else "runtime"
    if dump_family is not None and family != dump_family:
        expected_opcode = "CallCommonStub" if dump_family == "common" else "CallRuntime"
        defects.append(
            "%s: имя опкода `%s` не соответствует семейству формы в дампе (ожидается `%s`)"
            % (prefix, constraint.opcode, expected_opcode)
        )
    own_prefix = "COStub_" if family == "common" else "RTStub_"
    tail = stub[len(own_prefix):] if stub.startswith(own_prefix) else stub
    if dump_tail is not None and tail != dump_tail:
        defects.append(
            "%s: хвост `%s` имени stub в аннотации `%s` не равен хвосту `%s` поля "
            "`Имя в дампе`" % (prefix, tail, normalized, dump_tail)
        )
    return defects


def _check_closed_obligation(
    prefix: str,
    case_entry: str,
    annotation: str,
    dump_family: Optional[str],
    dump_tail: Optional[str],
    ctx: ContentContext,
) -> List[str]:
    """Check the `закрыто` obligation form (R8).

    The obligation must read `закрыто — кейс `<путь>`, аннотация
    `HAS CallCommonStub|CallRuntime <точное имя>``; the case goes through the
    R4 checks and the annotation through the R5 ones, and the cited annotation
    must be present verbatim in the built-in case source or in the descriptor of
    the corpus reference.
    """
    defects: List[str] = []
    defects.extend(
        _check_address_annotation(prefix, annotation, dump_family, dump_tail, ctx)
    )
    locations: List[Path] = []
    if case_entry.startswith(CORPUS_PREFIX):
        descriptor_path, case_defects = _resolve_corpus_reference(prefix, case_entry, ctx)
        defects.extend(case_defects)
        if descriptor_path is not None:
            locations.append(descriptor_path)
    else:
        info, case_defects = _resolve_builtin_case(prefix, case_entry, ctx)
        defects.extend(case_defects)
        if info is not None:
            defects.extend(_check_case_config_errors(prefix, [info], ctx))
            locations.append(info.source)
    if not locations:
        return defects
    normalized = _normalize_annotation(annotation)
    found, out_of_scope = _find_annotation(normalized, True, locations, ctx)
    if found:
        return defects
    place = ctx.descriptor.display_path(locations[0])
    if out_of_scope:
        defects.append(
            "%s: аннотация `%s` присутствует до аннотации `//! METHOD` — адресная аннотация "
            "должна идти после неё" % (prefix, normalized)
        )
    else:
        defects.append("%s: аннотация `%s` отсутствует в `%s`" % (prefix, normalized, place))
    return defects


def _check_justified_obligation(
    prefix: str, match: "re.Match", ctx: ContentContext
) -> List[str]:
    """Check the `not-applicable — …` form of an obligation (R8)."""
    defects: List[str] = []
    reason = match.group("reason").strip()
    locator = match.group("locator").strip()
    review = match.group("review").strip()
    if not reason:
        defects.append("%s: причина из реализации: пустое значение" % prefix)
    if not locator:
        defects.append("%s: локатор проверки достижимости: пустое значение" % prefix)
    else:
        defects.extend(
            _resolve_locator("%s: локатор проверки достижимости" % prefix, locator, ctx)
        )
    if not review:
        defects.append("%s: подтверждение reviewer: пустое значение" % prefix)
    return defects


OBLIGATION_FORMS = (
    "`закрыто — кейс `<путь>`, аннотация `HAS CallCommonStub|CallRuntime <точное имя>`` "
    "или `not-applicable — причина из реализации: <…>; локатор проверки достижимости: "
    "<…>; подтверждение reviewer: <…>`"
)


def _check_inventory_obligation(
    position: str,
    field: str,
    value: str,
    dump_family: Optional[str],
    dump_tail: Optional[str],
    ctx: ContentContext,
) -> List[str]:
    """Check one obligation of an inventory position (R8)."""
    prefix = "%s: %s" % (position, field)
    text = value.strip()
    if not text:
        # The empty value is reported by the field check of the position.
        return []
    closed = CLOSED_OBLIGATION_RE.match(text)
    if closed is not None:
        return _check_closed_obligation(
            prefix,
            closed.group("case").strip(),
            closed.group("annotation").strip(),
            dump_family,
            dump_tail,
            ctx,
        )
    justified = NOT_APPLICABLE_OBLIGATION_RE.match(text)
    if justified is not None:
        return _check_justified_obligation(prefix, justified, ctx)
    return ["%s: значение не соответствует форме %s" % (prefix, OBLIGATION_FORMS)]


def _check_inventory_position(block: InventoryBlock, ctx: ContentContext) -> List[str]:
    """Check one inventory position: fields, identity, locator, obligations (R8)."""
    position = block.title
    defects: List[str] = []
    counts = Counter(entry.name for entry in block.fields)
    for name in INVENTORY_FIELDS:
        if counts.get(name, 0) == 0:
            defects.append(
                "%s: %s: отсутствует обязательное поле позиции инвентаря" % (position, name)
            )
        elif counts[name] > 1:
            lines = ", ".join(
                str(entry.line) for entry in block.fields if entry.name == name
            )
            defects.append(
                "%s: %s: повторённое обязательное поле позиции инвентаря (%d раза; строки "
                "артефакта %s)" % (position, name, counts[name], lines)
            )

    values: Dict[str, str] = {}
    for entry in block.fields:
        if entry.name in INVENTORY_FIELD_SET and entry.name not in values:
            values[entry.name] = entry.value
    for name in INVENTORY_FIELDS:
        if counts.get(name) == 1 and not values.get(name, "").strip():
            defects.append(
                "%s: %s: пустое значение обязательного поля позиции инвентаря"
                % (position, name)
            )

    def single_value(name: str) -> Optional[str]:
        """The value of a field present exactly once and non-empty, else None."""
        if counts.get(name) == 1 and values.get(name, "").strip():
            return values[name]
        return None

    dump_family, dump_tail, identity_defects = _check_inventory_identity(
        position,
        single_value("Перечисление"),
        single_value("Имя в дампе"),
        ctx,
    )
    defects.extend(identity_defects)

    locator = single_value("Локатор эмиссии")
    if locator is not None:
        defects.extend(_resolve_locator("%s: Локатор эмиссии" % position, locator, ctx))

    for name in INVENTORY_OBLIGATION_FIELDS:
        if counts.get(name) == 1:
            defects.extend(
                _check_inventory_obligation(
                    position, name, values.get(name, ""), dump_family, dump_tail, ctx
                )
            )
    return defects


def check_stub_inventory(
    descriptor: RootDescriptor, ctx: ContentContext
) -> Tuple[List[str], List[str], int]:
    """Check the stub inventory artifact (R8).

    Returns (structural defects, incompleteness messages, number of positions).
    A missing artifact and an artifact without positions are the expected
    incomplete state of the epic (E-STUB/ISSUE-0046), not defects; a present
    but unreadable artifact is a usage error (R9).
    """
    inventory = descriptor.inventory_path
    display = descriptor.display_path(inventory)
    if not inventory.is_file():
        return [], [
            "%s: артефакт отсутствует (ожидаемое незавершённое состояние E-STUB)" % display
        ], 0
    text = _read_text(inventory, "артефакт инвентаря stub")
    blocks = parse_inventory_blocks(text)
    if not blocks:
        return [], [
            "%s: позиции: артефакт не содержит ни одной позиции (ожидаемое незавершённое "
            "состояние E-STUB)" % display
        ], 0
    defects: List[str] = []
    for block in blocks:
        defects.extend(_check_inventory_position(block, ctx))
    return defects, [], len(blocks)


def _read_matrix(descriptor: RootDescriptor) -> Tuple[Optional[List[str]], Optional[str]]:
    """Read the matrix; return (lines, usage_error)."""
    try:
        text = descriptor.matrix_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        return None, "нечитаемая матрица %s: %s" % (
            descriptor.display_path(descriptor.matrix_path),
            error,
        )
    return text.splitlines(), None


def check_closure(descriptor: RootDescriptor) -> CheckReport:
    """Run every production check of the closure contract (R1-R6, R8, R9)."""
    lines, usage_error = _read_matrix(descriptor)
    if lines is None:
        return CheckReport(
            exit_code=EXIT_USAGE,
            defects=[],
            incomplete=[],
            record_count=0,
            summary="",
            usage_error=usage_error,
        )
    try:
        contract = load_runner_contract(descriptor)
    except ContractUsageError as error:
        return CheckReport(
            exit_code=EXIT_USAGE,
            defects=[],
            incomplete=[],
            record_count=0,
            summary="",
            usage_error=str(error),
        )
    ctx = ContentContext(descriptor, contract)

    defects: List[str] = []
    incomplete: List[str] = []

    index_occurrences, index_defects = parse_index_ids(lines)
    rows, group_defects = parse_group_rows(lines)
    vocabulary = parse_status_vocabulary(lines)
    records, records_defects = parse_records_section(lines)

    defects.extend(index_defects)
    defects.extend(group_defects)
    defects.extend(check_cluster_composition(index_occurrences, rows))
    defects.extend(records_defects)

    if records is not None:
        for block_id in records:
            if block_id not in JS_TS_CLUSTER_ID_SET:
                defects.append(
                    "%s: Блок записи: ID отсутствует в фиксированном списке %d строк кластера"
                    % (block_id, len(JS_TS_CLUSTER_IDS))
                )

    record_count = 0
    for row_id in JS_TS_CLUSTER_IDS:
        matched = rows.get(row_id, [])
        priority = matched[0].priority if len(matched) == 1 else None
        blocks = (records or {}).get(row_id, [])
        if blocks:
            record_count += 1
        if len(matched) == 1:
            row = matched[0]
            if row.status not in vocabulary:
                defects.append(
                    "%s: Статус: значение `%s` отсутствует в словаре «%s»"
                    % (row_id, row.status_raw, STATUS_SECTION_TITLE)
                )
            if blocks:
                if row.status == "open":
                    defects.append("%s: Статус: строка в статусе open" % row_id)
                elif row.status != "covered-new":
                    defects.append(
                        "%s: Статус: запись закрытия при статусе `%s` (запись и статус "
                        "`covered-new` вносятся одним изменением)" % (row_id, row.status)
                    )
        if not blocks:
            if len(matched) == 1:
                incomplete.append(
                    "%s: нет записи закрытия (статус: %s)" % (row_id, matched[0].status)
                )
            continue
        if len(blocks) > 1:
            block_lines = ", ".join(str(block.line) for block in blocks)
            defects.append(
                "%s: Блок записи: повтор блока записи закрытия (%d блока; строки матрицы %s)"
                % (row_id, len(blocks), block_lines)
            )
        for block in blocks:
            defects.extend(check_record_block(block, row_id, priority))
            if record_is_complete(block):
                defects.extend(
                    check_record_content(row_id, _record_values(block), priority, ctx)
                )

    try:
        inventory_defects, inventory_messages, inventory_positions = check_stub_inventory(
            descriptor, ctx
        )
    except ContractUsageError as error:
        return CheckReport(
            exit_code=EXIT_USAGE,
            defects=[],
            incomplete=[],
            record_count=0,
            summary="",
            usage_error=str(error),
        )
    defects.extend(inventory_defects)
    incomplete.extend(inventory_messages)

    if defects:
        exit_code = EXIT_DEFECT
    elif incomplete or record_count != len(JS_TS_CLUSTER_IDS) or inventory_positions == 0:
        exit_code = EXIT_INCOMPLETE
    else:
        exit_code = EXIT_OK

    summary = (
        "Сводка: строк кластера %d; записей закрытия %d; позиций инвентаря %d; структурных "
        "дефектов %d; сообщений о неполноте %d; код возврата %d"
        % (
            len(JS_TS_CLUSTER_IDS),
            record_count,
            inventory_positions,
            len(defects),
            len(incomplete),
            exit_code,
        )
    )
    return CheckReport(
        exit_code=exit_code,
        defects=defects,
        incomplete=incomplete,
        record_count=record_count,
        summary=summary,
    )


def _message_row_id(message: str) -> Optional[str]:
    """The block ID a per-row diagnostic starts with, or None (R9 form)."""
    match = MESSAGE_ID_RE.match(message)
    return match.group("id") if match else None


def _pattern_selects_exactly(ctx: ContentContext, rel: str) -> bool:
    """Whether `rel` used verbatim as an `-I` pattern selects exactly itself.

    Uses the runner's own filter semantics (`_pattern_matches`) over the case
    paths the runner discovers, so a printed control-set line is a usable `-I`
    template by construction rather than by convention (R7).
    """
    runner = ctx.contract.module
    selected = [
        path for path in ctx.discovered_paths() if runner._pattern_matches(path, [rel])
    ]
    return selected == [rel]


def _record_case_rels(
    row_id: str, values: Dict[str, str], ctx: ContentContext
) -> List[str]:
    """Built-in case paths of a record (corpus references are not `-I` cases)."""
    _, built_in, _, _ = _check_cases_field(row_id, values.get("Кейсы", ""), ctx)
    return [info.rel for info in built_in]


def check_control_set(descriptor: RootDescriptor) -> ControlSetReport:
    """Compute the P0 control set: cases of the valid P0 records (R7).

    The list comes from the records of exactly those cluster rows whose
    `Приоритет` cell is `P0`; only their built-in cases are printed (a corpus
    reference is not selectable by `-I`). A row without a record contributes
    nothing - an empty list with exit code 0 is the expected incomplete state -
    but a present and structurally invalid P0 record (a missing mandatory field,
    a failing R4/R5/R6 check, an unreadable priority of a recorded row, a
    repeated block or a malformed block heading) is a defect: exit code 1, a
    diagnostic naming the row and the field on stderr, and no case of that row
    on stdout, so the defect cannot be mistaken for an empty control set.
    Every path is verified to be usable word-for-word as an `-I` pattern.
    """
    lines, usage_error = _read_matrix(descriptor)
    if lines is None:
        return ControlSetReport(
            exit_code=EXIT_USAGE, paths=[], diagnostics=[], usage_error=usage_error
        )
    try:
        contract = load_runner_contract(descriptor)
    except ContractUsageError as error:
        return ControlSetReport(
            exit_code=EXIT_USAGE, paths=[], diagnostics=[], usage_error=str(error)
        )
    ctx = ContentContext(descriptor, contract)
    rows, _group_defects = parse_group_rows(lines)
    records, records_defects = parse_records_section(lines)

    diagnostics: List[str] = []
    accepted: List[str] = []
    priorities: Dict[str, Optional[str]] = {}
    for row_id in JS_TS_CLUSTER_IDS:
        blocks = (records or {}).get(row_id, [])
        if not blocks:
            continue
        matched = rows.get(row_id, [])
        priority = matched[0].priority if len(matched) == 1 else None
        priorities[row_id] = priority
        if priority is None:
            diagnostics.append(
                "%s: Приоритет: ячейка `Приоритет` строки матрицы не читается однозначно "
                "(%d строк таблиц групп с этим ID) — запись не может быть отнесена ни к P0, "
                "ни к P1 и не может быть молча пропущена" % (row_id, len(matched))
            )
            continue
        if priority != "P0":
            continue
        defects: List[str] = []
        if len(blocks) > 1:
            defects.append(
                "%s: Блок записи: повтор блока записи закрытия (%d блока; строки матрицы %s)"
                % (row_id, len(blocks), ", ".join(str(block.line) for block in blocks))
            )
        for block in blocks:
            defects.extend(check_record_block(block, row_id, priority))
            if record_is_complete(block):
                defects.extend(
                    check_record_content(row_id, _record_values(block), priority, ctx)
                )
        if defects:
            diagnostics.extend(defects)
            continue
        accepted.append(row_id)

    for message in records_defects:
        # A structural defect of the section that names a block makes that
        # block's record invalid, so its cases are not printed; the priority of
        # the named row must be P0 for the defect to affect the control set (an
        # unreadable priority already produced a diagnostic above).
        row_id = _message_row_id(message)
        if priorities.get(row_id) != "P0":
            continue
        diagnostics.append(message)
        if row_id in accepted:
            accepted.remove(row_id)

    paths: Set[str] = set()
    for row_id in accepted:
        row_paths: Set[str] = set()
        row_valid = True
        for block in (records or {}).get(row_id, []):
            for rel in _record_case_rels(row_id, _record_values(block), ctx):
                if _pattern_selects_exactly(ctx, rel):
                    row_paths.add(rel)
                else:
                    row_valid = False
                    diagnostics.append(
                        "%s: Кейсы: путь `%s` не пригоден дословно как шаблон `-I` — фильтр "
                        "выбирает не только этот кейс записи" % (row_id, rel)
                    )
        if row_valid:
            paths |= row_paths

    exit_code = EXIT_DEFECT if diagnostics else EXIT_OK
    return ControlSetReport(exit_code=exit_code, paths=sorted(paths), diagnostics=diagnostics)


# ---------------------------------------------------------------------------
# VM-free self-test on synthetic fixtures (R10, ISSUE-0055)
# ---------------------------------------------------------------------------
#
# The self-test never reads the production tree: every artifact path
# (matrix, cases root, checkout root, opcode header, enumeration-name headers,
# inventory artifact) comes from the descriptor of a fresh temporary fixture
# root. Only the runner module itself is loaded from the test directory, exactly
# as the production mode does (R5); every path a check consults comes from the
# fixture descriptor. The fixtures are written and removed through
# `tempfile.TemporaryDirectory`, so no source file is touched and no bytecode
# cache is created (`sys.dont_write_bytecode` is set above).

_SELF_TEST_TEMPS: List["tempfile.TemporaryDirectory"] = []

SELF_TEST_CASE_REL = "fixture/basic"
SELF_TEST_PLAIN_REL = "fixture/plain"
SELF_TEST_CORPUS_REF = "corpus/suite/one"
SELF_TEST_EMPTY_CORPUS_REF = "corpus/suite/empty"
SELF_TEST_EMISSION_LOCATOR = "ecmascript/arksteed/fixture_emission.cpp:2"
SELF_TEST_REACHABILITY_LOCATOR = "ecmascript/arksteed/fixture_emission.cpp#EmitFixtureStub"
SELF_TEST_INVENTORY_POSITION = "CommonStubID::PrintMethodName"
SELF_TEST_EXPECTED_OUTPUT = "expected_output.txt"

# Priorities of the fixture rows mirror the delivery matrix (35 P0 / 11 P1), so
# both the P0 and the P1 branches of the record contract are exercised.
SELF_TEST_P1_ROWS = frozenset(
    ("G1.2", "G1.3", "G1.4", "G1.6", "G3.3", "G3.4", "G4.5", "G7.4j",
     "G9.2", "G9.4", "G11.5")
)

SELF_TEST_OPCODE_HEADER = (
    "#ifndef ARKSTEED_OPCODE_LIST_H\n"
    "#define ARKSTEED_OPCODE_LIST_H\n"
    "#define ARKSTEED_OPCODE_LIST(V) \\\n"
    "    V(LoadTaggedField)          \\\n"
    "    V(F64Add)                   \\\n"
    "    V(CallCommonStub)           \\\n"
    "    V(CallRuntime)\n"
    "#endif\n"
)
SELF_TEST_COMMON_STUB_HEADER = (
    "#ifndef COMMON_STUB_CSIGNS_H\n"
    "#define COMMON_STUB_CSIGNS_H\n"
    "#define COMMON_STUB_LIST(V) \\\n"
    "    V(PrintMethodName)      \\\n"
    "    V(Add)\n"
    "#endif\n"
)
SELF_TEST_RUNTIME_STUB_HEADER = (
    "#ifndef RUNTIME_STUB_LIST_H\n"
    "#define RUNTIME_STUB_LIST_H\n"
    "#define RUNTIME_STUB_LIST(V) \\\n"
    "    V(PrintMethodName)          \\\n"
    "    V(ToPropertyKey)\n"
    "#endif\n"
)
SELF_TEST_CASE_SOURCE = (
    "//! METHOD target\n"
    "//! HAS LoadTaggedField\n"
    "//! HAS CallCommonStub PrintMethodName\n"
    "//! COMPILE_COUNT target 1\n"
    "//! EXEC_COUNT *target* 1\n"
    "//! DEOPT_COUNT *Deoptimize* 0\n"
    "function target(): number { return 1; }\n"
    "print(target());\n"
)
SELF_TEST_PLAIN_SOURCE = "print(1);\n"
SELF_TEST_CORPUS_DESCRIPTOR = (
    "//! METHOD target\n"
    "//! HAS CallCommonStub PrintMethodName\n"
)
SELF_TEST_EMPTY_CORPUS_DESCRIPTOR = "//! METHOD target\n"
SELF_TEST_EMISSION_SOURCE = (
    "// Fixture emission locator of the stub-inventory checks.\n"
    "void EmitFixtureStub() {}\n"
    "// end of fixture emission locator\n"
)


def _self_test_priority(row_id: str) -> str:
    """Priority of a fixture row (mirrors the delivery matrix split)."""
    return "P1" if row_id in SELF_TEST_P1_ROWS else "P0"


def _self_test_group_row(row_id: str, status: str) -> str:
    """One 14-cell group-table row of the fixture matrix."""
    cells = (
        row_id, "fixture", "fixture", "fixture", "fixture", "fixture", "fixture",
        "fixture", "fixture", "fixture", "x64 Debug/Release",
        _self_test_priority(row_id), JS_TS_CLUSTER_CELL, status,
    )
    return "| " + " | ".join(cells) + " |"


def _self_test_index_row(ids: Sequence[str]) -> str:
    """The `JS/TS` row of the fixture `Индекс кластеров` table."""
    return "| `%s` (ISSUE-0009) | %s |" % (JS_TS_CLUSTER_CELL, ", ".join(ids))


def _self_test_record_block(row_id: str) -> str:
    """A complete canonical record block of the fixture matrix (R3)."""
    priority = _self_test_priority(row_id)
    if priority == "P0":
        control = "JIT-off; отчёт `control-report.md`"
    else:
        control = "не требуется (P1)"
    lines = [
        "### %s — Возможность %s" % (row_id, row_id),
        "",
        "- **Строка:** %s (%s, кластер `JS/TS`)" % (row_id, priority),
        "- **Обязательство:** целевой метод компилируется и исполняется",
        "- **Кейсы:** `%s`" % SELF_TEST_CASE_REL,
        "- **Команда (x64 Debug):** `python3 ./run_arksteed_tests.py -p x64 -I %s`"
        % SELF_TEST_CASE_REL,
        "- **Команда (x64 Release):** `python3 ./run_arksteed_tests.py -p x64 -m release -I %s`"
        % SELF_TEST_CASE_REL,
        "- **Oracle:** специфицированная семантика метода",
        "- **Аннотации свидетельств:** `HAS LoadTaggedField`; `COMPILE_COUNT target 1`",
        "- **Стимул и отличимый эффект:** прогретый вызов метода даёт компиляцию "
        "и точный результат",
        "- **Профиль:** x64 Debug/Release",
        "- **Контроль чувствительности:** %s" % control,
        "- **Остаточный пробел:** нет",
        "- **Решение дизайна:** `arksteed-jsts-behavioral-case-suite#A4` — механизм "
        "и способ проверки названы",
    ]
    return "\n".join(lines)


def _self_test_matrix_text() -> str:
    """Synthetic matrix with the 46 cluster rows and their complete records."""
    out: List[str] = []
    out.append("### %s" % STATUS_SECTION_TITLE)
    out.append("")
    out.append("| Статус | Смысл |")
    out.append("|---|---|")
    for status in sorted(FALLBACK_STATUS_VOCABULARY):
        out.append("| `%s` | fixture |" % status)
    out.append("")
    out.append("## %s" % INDEX_SECTION_TITLE)
    out.append("")
    out.append("| Кластер | Строки |")
    out.append("|---|---|")
    out.append(_self_test_index_row(JS_TS_CLUSTER_IDS))
    out.append("")
    current_group: Optional[str] = None
    for row_id in JS_TS_CLUSTER_IDS:
        group = row_id.split(".")[0]
        if group != current_group:
            current_group = group
            out.append("## %s — Fixture group" % group)
            out.append("")
            out.append(
                "| ID | Возможность | Участок | Существующие тесты | Аналог | Пробел | "
                "Тип | Входы | Oracle | JIT | Профиль | Приоритет | Задача | Статус |"
            )
            out.append("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
        out.append(_self_test_group_row(row_id, "covered-new"))
    out.append("")
    out.append("## %s" % RECORDS_SECTION_TITLE)
    out.append("")
    out.append("Форма записи — fixture (`arksteed-jsts-closure-records-and-checker` R3).")
    out.append("")
    for row_id in JS_TS_CLUSTER_IDS:
        out.append(_self_test_record_block(row_id))
        out.append("")
    return "\n".join(out) + "\n"


def _self_test_inventory_text() -> str:
    """Synthetic stub inventory with one position and three obligations."""
    return (
        "# Инвентарь stub (fixture)\n"
        "\n"
        "## Перевывод\n"
        "\n"
        "Команда перевывода — принадлежит E-STUB и чекером не проверяется.\n"
        "\n"
        "### %s\n\n"
        "- **Перечисление:** `CommonStubID::PrintMethodName`\n"
        "- **Имя в дампе:** `COStub_PrintMethodName`\n"
        "- **Локатор эмиссии:** `%s`\n"
        "- **Нормальный путь:** закрыто — кейс `%s`, аннотация `HAS CallCommonStub "
        "PrintMethodName`\n"
        "- **Guard/deopt:** not-applicable — причина из реализации: stub не имеет "
        "guard-перехода; локатор проверки достижимости: `%s`; подтверждение "
        "reviewer: ISSUE-0046\n"
        "- **Исключение:** закрыто — кейс `%s`, аннотация `HAS CallCommonStub "
        "PrintMethodName`\n"
        % (
            SELF_TEST_INVENTORY_POSITION,
            SELF_TEST_EMISSION_LOCATOR,
            SELF_TEST_CASE_REL,
            SELF_TEST_REACHABILITY_LOCATOR,
            SELF_TEST_CORPUS_REF,
        )
    )


@dataclass
class SelfTestFixture:
    """A temporary fixture root and the paths its descriptor exposes."""

    root: Path
    cases_dir: Path

    @property
    def matrix_path(self) -> Path:
        return self.root / "matrix.md"

    @property
    def inventory_path(self) -> Path:
        return self.root / "stub-inventory.md"

    @property
    def descriptor(self) -> RootDescriptor:
        return RootDescriptor(
            checkout_root=self.root,
            matrix_path=self.matrix_path,
            cases_dir=self.cases_dir,
            opcode_list_header=self.root / "arksteed_opcode_list.h",
            common_stub_header=self.root / "common_stub_csigns.h",
            runtime_stub_header=self.root / "runtime_stub_list.h",
            inventory_path=self.inventory_path,
        )

    def read_matrix(self) -> str:
        return self.matrix_path.read_text(encoding="utf-8")

    def write_matrix(self, text: str) -> None:
        self.matrix_path.write_text(text, encoding="utf-8")

    def read_inventory(self) -> str:
        return self.inventory_path.read_text(encoding="utf-8")

    def write_inventory(self, text: str) -> None:
        self.inventory_path.write_text(text, encoding="utf-8")


def _write_self_test_fixture(fixture: SelfTestFixture) -> None:
    """Write the synthetic baseline: headers, cases and complete artifacts."""
    cases_dir = fixture.cases_dir
    for rel in (SELF_TEST_CASE_REL, SELF_TEST_PLAIN_REL, "corpus/suite"):
        (cases_dir / rel).mkdir(parents=True, exist_ok=True)
    (fixture.root / "ecmascript/arksteed").mkdir(parents=True, exist_ok=True)
    (cases_dir / SELF_TEST_CASE_REL / "case.js").write_text(
        SELF_TEST_CASE_SOURCE, encoding="utf-8"
    )
    (cases_dir / SELF_TEST_CASE_REL / SELF_TEST_EXPECTED_OUTPUT).write_text(
        "1\n", encoding="utf-8"
    )
    (cases_dir / SELF_TEST_PLAIN_REL / "plain.js").write_text(
        SELF_TEST_PLAIN_SOURCE, encoding="utf-8"
    )
    (cases_dir / SELF_TEST_PLAIN_REL / SELF_TEST_EXPECTED_OUTPUT).write_text(
        "1\n", encoding="utf-8"
    )
    (cases_dir / "corpus/suite/one.annotations").write_text(
        SELF_TEST_CORPUS_DESCRIPTOR, encoding="utf-8"
    )
    (cases_dir / "corpus/suite/empty.annotations").write_text(
        SELF_TEST_EMPTY_CORPUS_DESCRIPTOR, encoding="utf-8"
    )
    (fixture.root / "ecmascript/arksteed/fixture_emission.cpp").write_text(
        SELF_TEST_EMISSION_SOURCE, encoding="utf-8"
    )
    fixture.write_matrix(_self_test_matrix_text())
    fixture.write_inventory(_self_test_inventory_text())
    (fixture.root / "arksteed_opcode_list.h").write_text(
        SELF_TEST_OPCODE_HEADER, encoding="utf-8"
    )
    (fixture.root / "common_stub_csigns.h").write_text(
        SELF_TEST_COMMON_STUB_HEADER, encoding="utf-8"
    )
    (fixture.root / "runtime_stub_list.h").write_text(
        SELF_TEST_RUNTIME_STUB_HEADER, encoding="utf-8"
    )


def _new_self_test_fixture() -> SelfTestFixture:
    """A fresh temporary fixture root with the complete baseline state."""
    temp = tempfile.TemporaryDirectory(prefix="arksteed-check-closure-selftest-")
    _SELF_TEST_TEMPS.append(temp)
    root = Path(temp.name)
    fixture = SelfTestFixture(root=root, cases_dir=root / "cases")
    _write_self_test_fixture(fixture)
    return fixture


def _record_block_bounds(lines: Sequence[str], row_id: str) -> Optional[Tuple[int, int]]:
    """Half-open line range of one `### <ID> —` block of the fixture matrix."""
    prefix = "### %s —" % row_id
    start = None
    for index, line in enumerate(lines):
        if line.startswith(prefix):
            start = index
            break
    if start is None:
        return None
    end = len(lines)
    for index in range(start + 1, len(lines)):
        if lines[index].startswith("### ") or lines[index].startswith("## "):
            end = index
            break
    return start, end


def _mutate_record(fixture: SelfTestFixture, row_id: str,
                   transform: Callable[[List[str]], List[str]]) -> None:
    """Rewrite one record block of the fixture matrix through `transform`."""
    lines = fixture.read_matrix().splitlines()
    bounds = _record_block_bounds(lines, row_id)
    if bounds is None:
        raise AssertionError("self-test fixture: no block for %s" % row_id)
    start, end = bounds
    block = transform(list(lines[start:end]))
    fixture.write_matrix("\n".join(lines[:start] + block + lines[end:]) + "\n")


def _remove_record_field(fixture: SelfTestFixture, row_id: str, field: str) -> None:
    prefix = "- **%s:**" % field
    _mutate_record(
        fixture, row_id,
        lambda block: [line for line in block if not line.startswith(prefix)],
    )


def _set_record_field(fixture: SelfTestFixture, row_id: str, field: str,
                      value: str) -> None:
    prefix = "- **%s:**" % field

    def transform(block: List[str]) -> List[str]:
        out: List[str] = []
        for line in block:
            if line.startswith(prefix):
                out.append("%s %s" % (prefix, value))
            else:
                out.append(line)
        return out

    _mutate_record(fixture, row_id, transform)


def _replace_matrix(fixture: SelfTestFixture, old: str, new: str) -> None:
    text = fixture.read_matrix()
    if old not in text:
        raise AssertionError("self-test fixture: matrix text not found: %r" % old)
    fixture.write_matrix(text.replace(old, new, 1))


def _set_inventory_field(fixture: SelfTestFixture, field: str, value: str) -> None:
    prefix = "- **%s:**" % field
    line = "%s %s" % (prefix, value) if value else prefix
    lines = fixture.read_inventory().splitlines()
    if not any(item.startswith(prefix) for item in lines):
        raise AssertionError("self-test fixture: inventory field not found: %s" % field)
    out = [line if item.startswith(prefix) else item for item in lines]
    fixture.write_inventory("\n".join(out) + "\n")


def _remove_inventory_field(fixture: SelfTestFixture, field: str) -> None:
    prefix = "- **%s:**" % field
    lines = fixture.read_inventory().splitlines()
    fixture.write_inventory(
        "\n".join(item for item in lines if not item.startswith(prefix)) + "\n"
    )


def _remove_records_section(fixture: SelfTestFixture) -> None:
    text = fixture.read_matrix()
    marker = "## %s" % RECORDS_SECTION_TITLE
    if marker not in text:
        raise AssertionError("self-test fixture: records section not found")
    fixture.write_matrix(text[: text.index(marker)])


def _remove_record_blocks(fixture: SelfTestFixture) -> None:
    """Keep the records section but drop every block (no P0 record exists)."""
    text = fixture.read_matrix()
    marker = "## %s" % RECORDS_SECTION_TITLE
    if marker not in text:
        raise AssertionError("self-test fixture: records section not found")
    index = text.index(marker)
    heading_end = text.index("\n", index) + 1
    fixture.write_matrix(text[:heading_end] + "\n")


def _self_test_report_failures(name: str, report: CheckReport, expected_code: int,
                               markers: Sequence[str]) -> List[str]:
    """Non-hollow assertion: expected code and the place named by a message."""
    failures: List[str] = []
    if report.exit_code != expected_code:
        failures.append(
            "%s: ожидался код %d, получен %d (usage_error: %s)"
            % (name, expected_code, report.exit_code, report.usage_error)
        )
    text = "\n".join(list(report.messages) + [report.summary or ""])
    for marker in markers:
        if marker not in text:
            failures.append(
                "%s: ни одно сообщение не называет `%s`; вывод: %s"
                % (name, marker, list(report.messages) or report.summary)
            )
    return failures


def _self_test_scenario_pair(name: str, mutate: Callable[[SelfTestFixture], None],
                               expected_code: int,
                               markers: Sequence[str]) -> List[Tuple[str, Callable[[], List[str]]]]:
    """One `mutation -> defect` check and its `revert -> expected code` pair."""

    def mutation_check() -> List[str]:
        fixture = _new_self_test_fixture()
        mutate(fixture)
        report = check_closure(fixture.descriptor)
        return _self_test_report_failures(name, report, expected_code, markers)

    def revert_check() -> List[str]:
        fixture = _new_self_test_fixture()
        report = check_closure(fixture.descriptor)
        return _self_test_report_failures(
            "%s: возврат мутации" % name, report, EXIT_OK, ()
        )

    return [
        ("%s: мутация → код %d и указание места" % (name, expected_code), mutation_check),
        ("%s: возврат мутации → код 0" % name, revert_check),
    ]


def _self_test_control_set_failures(name: str, result: ControlSetReport,
                                    expected_code: int, markers: Sequence[str],
                                    expected_paths: Optional[Sequence[str]] = None
                                    ) -> List[str]:
    failures: List[str] = []
    if result.exit_code != expected_code:
        failures.append(
            "%s: ожидался код %d, получен %d (usage_error: %s)"
            % (name, expected_code, result.exit_code, result.usage_error)
        )
    text = "\n".join(result.diagnostics)
    for marker in markers:
        if marker not in text:
            failures.append(
                "%s: диагностика не называет `%s`; диагностика: %s"
                % (name, marker, result.diagnostics)
            )
    if expected_paths is not None and result.paths != list(expected_paths):
        failures.append(
            "%s: ожидался перечень %s, получен %s"
            % (name, list(expected_paths), result.paths)
        )
    return failures


def check_self_test_positive_fixture() -> List[str]:
    """The complete synthetic state passes with code 0 (R10)."""
    fixture = _new_self_test_fixture()
    report = check_closure(fixture.descriptor)
    failures = _self_test_report_failures(
        "полное синтетическое состояние", report, EXIT_OK, ()
    )
    if report.defects or report.incomplete:
        failures.append(
            "полное синтетическое состояние: неожиданные сообщения: %s"
            % (list(report.defects) + list(report.incomplete))
        )
    if report.record_count != len(JS_TS_CLUSTER_IDS):
        failures.append(
            "полное синтетическое состояние: записей %d, ожидалось %d"
            % (report.record_count, len(JS_TS_CLUSTER_IDS))
        )
    return failures


def check_self_test_control_set_positive() -> List[str]:
    """`--control-set` with valid P0 records prints their cases (R7)."""
    fixture = _new_self_test_fixture()
    result = check_control_set(fixture.descriptor)
    return _self_test_control_set_failures(
        "--control-set с P0-записями", result, EXIT_OK, (), [SELF_TEST_CASE_REL]
    )


def check_self_test_control_set_invalid_p0() -> List[str]:
    """A structurally invalid P0 record is a defect, not an empty set (R7)."""
    fixture = _new_self_test_fixture()
    _remove_record_field(fixture, "G1.1", "Обязательство")
    result = check_control_set(fixture.descriptor)
    return _self_test_control_set_failures(
        "--control-set, недействительная P0-запись", result, EXIT_DEFECT,
        ("G1.1", "Обязательство"),
    )


def _check_self_test_control_set_invalid_p0_revert() -> List[str]:
    fixture = _new_self_test_fixture()
    result = check_control_set(fixture.descriptor)
    return _self_test_control_set_failures(
        "--control-set, возврат мутации", result, EXIT_OK, (), [SELF_TEST_CASE_REL]
    )


def check_self_test_control_set_empty() -> List[str]:
    """No P0 record: empty stdout, no diagnostics and code 0 (R7, criterion 6)."""
    fixture = _new_self_test_fixture()
    _remove_record_blocks(fixture)
    result = check_control_set(fixture.descriptor)
    failures = _self_test_control_set_failures(
        "--control-set без P0-записей", result, EXIT_OK, (), []
    )
    if result.diagnostics:
        failures.append(
            "--control-set без P0-записей: неожиданная диагностика: %s"
            % result.diagnostics
        )
    return failures


def check_self_test_usage_errors() -> List[str]:
    """Unreadable inputs and bad argument vectors are usage errors (R9, R10)."""
    failures: List[str] = []
    fixture = _new_self_test_fixture()
    fixture.matrix_path.unlink()
    report = check_closure(fixture.descriptor)
    if report.exit_code != EXIT_USAGE or not report.usage_error:
        failures.append(
            "основной режим с нечитаемой матрицей: код %d, usage_error %r"
            % (report.exit_code, report.usage_error)
        )
    fixture = _new_self_test_fixture()
    fixture.matrix_path.unlink()
    result = check_control_set(fixture.descriptor)
    if result.exit_code != EXIT_USAGE or not result.usage_error:
        failures.append(
            "--control-set с нечитаемой матрицей: код %d" % result.exit_code
        )
    fixture = _new_self_test_fixture()
    (fixture.root / "arksteed_opcode_list.h").write_text(
        "no opcode entries here\n", encoding="utf-8"
    )
    report = check_closure(fixture.descriptor)
    if report.exit_code != EXIT_USAGE or not report.usage_error:
        failures.append(
            "основной режим с нечитаемым заголовком опкодов: код %d, usage_error %r"
            % (report.exit_code, report.usage_error)
        )
    mode, error = _select_mode([SELF_TEST_FLAG])
    if mode != MODE_SELF_TEST or error is not None:
        failures.append(
            "_select_mode([--self-test]): режим %r, ошибка %r" % (mode, error)
        )
    for arguments in (
        [SELF_TEST_FLAG, CONTROL_SET_FLAG],
        [SELF_TEST_FLAG, "--bogus"],
        [CONTROL_SET_FLAG, "--bogus"],
        ["--bogus"],
    ):
        mode, error = _select_mode(arguments)
        if mode is not None or error is None:
            failures.append(
                "_select_mode(%s): ожидалась usage-ошибка, получено режим %r ошибка %r"
                % (arguments, mode, error)
            )
            continue
        with contextlib.redirect_stderr(io.StringIO()):
            code = main(list(arguments))
        if code != EXIT_USAGE:
            failures.append("main(%s): код %d, ожидался 2" % (arguments, code))
    return failures


def _self_test_scenario_checks() -> List[Tuple[str, Callable[[], List[str]]]]:
    """Every R10 defect fixture and its non-hollow revert pair."""
    checks: List[Tuple[str, Callable[[], List[str]]]] = []
    for field_name in RECORD_FIELDS:
        checks.extend(
            _self_test_scenario_pair(
                "запись G1.1 без обязательного поля `%s`" % field_name,
                lambda fixture, field_name=field_name: _remove_record_field(
                    fixture, "G1.1", field_name
                ),
                EXIT_DEFECT,
                ("G1.1", field_name),
            )
        )
    checks.extend(
        _self_test_scenario_pair(
            "строка G1.1 в статусе `open` при наличии записи",
            lambda fixture: _replace_matrix(
                fixture,
                _self_test_group_row("G1.1", "covered-new"),
                _self_test_group_row("G1.1", "open"),
            ),
            EXIT_DEFECT,
            ("G1.1", "open"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "каталог кейса отсутствует",
            lambda fixture: _set_record_field(fixture, "G1.1", "Кейсы", "`fixture/missing`"),
            EXIT_DEFECT,
            ("G1.1", "Кейсы", "не существует"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "каталог кейса без expected_output.txt",
            lambda fixture: (
                fixture.cases_dir / SELF_TEST_CASE_REL / SELF_TEST_EXPECTED_OUTPUT
            ).unlink(),
            EXIT_DEFECT,
            ("G1.1", "Кейсы", SELF_TEST_EXPECTED_OUTPUT),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "каталог кейса с двумя JS/TS-источниками",
            lambda fixture: (
                fixture.cases_dir / SELF_TEST_CASE_REL / "extra.js"
            ).write_text("print(2);\n", encoding="utf-8"),
            EXIT_DEFECT,
            ("G1.1", "Кейсы", "ожидается ровно один"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "аннотация записи отсутствует в кейсе и в дескрипторе",
            lambda fixture: _set_record_field(
                fixture, "G1.1", "Аннотации свидетельств", "`HAS F64Add`"
            ),
            EXIT_DEFECT,
            ("G1.1", "Аннотации свидетельств", "отсутствует"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "имя опкода вне arksteed_opcode_list.h",
            lambda fixture: _set_record_field(
                fixture, "G1.1", "Аннотации свидетельств", "`HAS NotAnOpcode`"
            ),
            EXIT_DEFECT,
            ("G1.1", "Аннотации свидетельств", "unknown IR opcode"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "хвост имени stub вне списка имён перечисления",
            lambda fixture: _set_record_field(
                fixture, "G1.1", "Аннотации свидетельств",
                "`HAS CallCommonStub NotAStub`",
            ),
            EXIT_DEFECT,
            ("G1.1", "Аннотации свидетельств", "отсутствует в списке имён перечисления"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "инвентарь: пустое обязательство",
            lambda fixture: _set_inventory_field(fixture, "Нормальный путь", ""),
            EXIT_DEFECT,
            (SELF_TEST_INVENTORY_POSITION, "Нормальный путь", "пустое значение"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "инвентарь: позиция без локатора эмиссии",
            lambda fixture: _remove_inventory_field(fixture, "Локатор эмиссии"),
            EXIT_DEFECT,
            (SELF_TEST_INVENTORY_POSITION, "Локатор эмиссии", "отсутствует"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "инвентарь: ссылка на отсутствующую адресную аннотацию",
            lambda fixture: _set_inventory_field(
                fixture, "Нормальный путь",
                "закрыто — кейс `%s`, аннотация `HAS CallCommonStub PrintMethodName`"
                % SELF_TEST_PLAIN_REL,
            ),
            EXIT_DEFECT,
            (SELF_TEST_INVENTORY_POSITION, "Нормальный путь", "отсутствует"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "инвентарь: удалено третье обязательство",
            lambda fixture: _remove_inventory_field(fixture, "Исключение"),
            EXIT_DEFECT,
            (SELF_TEST_INVENTORY_POSITION, "Исключение", "отсутствует"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "инвентарь: корпусная ссылка без дескриптора",
            lambda fixture: _set_inventory_field(
                fixture, "Исключение",
                "закрыто — кейс `corpus/suite/missing`, аннотация "
                "`HAS CallCommonStub PrintMethodName`",
            ),
            EXIT_DEFECT,
            (SELF_TEST_INVENTORY_POSITION, "Исключение", "дескриптор"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "инвентарь: в дескрипторе корпусной ссылки нет требуемой аннотации",
            lambda fixture: _set_inventory_field(
                fixture, "Исключение",
                "закрыто — кейс `%s`, аннотация `HAS CallCommonStub PrintMethodName`"
                % SELF_TEST_EMPTY_CORPUS_REF,
            ),
            EXIT_DEFECT,
            (SELF_TEST_INVENTORY_POSITION, "Исключение", "отсутствует"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "инвентарь: имя перечисления вне списков имён",
            lambda fixture: _set_inventory_field(
                fixture, "Перечисление", "`CommonStubID::NotStub`"
            ),
            EXIT_DEFECT,
            (SELF_TEST_INVENTORY_POSITION, "Перечисление", "отсутствует в списке имён перечисления"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "инвентарь: `Перечисление` и `Имя в дампе` не согласованы",
            lambda fixture: _set_inventory_field(
                fixture, "Имя в дампе", "`RTStub_PrintMethodName`"
            ),
            EXIT_DEFECT,
            (SELF_TEST_INVENTORY_POSITION, "Имя в дампе", "не согласован"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "состав строк кластера расходится с фиксированным списком",
            lambda fixture: _replace_matrix(
                fixture,
                _self_test_index_row(JS_TS_CLUSTER_IDS),
                _self_test_index_row(JS_TS_CLUSTER_IDS[1:]),
            ),
            EXIT_DEFECT,
            ("Состав", "G1.1"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "раздел записей кластера JS/TS отсутствует",
            lambda fixture: _remove_records_section(fixture),
            EXIT_DEFECT,
            (RECORDS_SECTION_TITLE, "раздел записей отсутствует"),
        )
    )
    checks.extend(
        _self_test_scenario_pair(
            "артефакт инвентаря stub отсутствует",
            lambda fixture: fixture.inventory_path.unlink(),
            EXIT_INCOMPLETE,
            ("stub-inventory.md", "артефакт отсутствует"),
        )
    )
    return checks


def _self_test_check_list() -> List[Tuple[str, Callable[[], List[str]]]]:
    """The named checks of the `--self-test` mode, in report order."""
    checks: List[Tuple[str, Callable[[], List[str]]]] = [
        ("фикстура: полное состояние → код 0", check_self_test_positive_fixture),
        ("фикстура: --control-set с P0-записями → код 0 и кейс P0-записи",
         check_self_test_control_set_positive),
        ("фикстура: --control-set с недействительной P0-записью → код 1 и указание места",
         check_self_test_control_set_invalid_p0),
        ("фикстура: --control-set, возврат мутации недействительной P0-записи → код 0",
         _check_self_test_control_set_invalid_p0_revert),
        ("фикстура: --control-set без P0-записей → пустой stdout и код 0",
         check_self_test_control_set_empty),
        ("фикстура: usage-ошибки и взаимоисключающие режимы → код 2",
         check_self_test_usage_errors),
    ]
    checks.extend(_self_test_scenario_checks())
    return checks


def run_self_test() -> int:
    """Run the production checks on synthetic fixtures; 0 iff all pass (R10)."""
    checks = _self_test_check_list()
    failed = 0
    try:
        for name, check in checks:
            try:
                failures = check()
            except Exception as error:  # noqa: BLE001 - report, never mask as pass
                failures = [
                    "неожиданное исключение %s: %s" % (type(error).__name__, error)
                ]
            status = "PASS" if not failures else "FAIL"
            print("[%s] %s" % (status, name))
            for failure in failures:
                print("        %s" % failure)
            failed += bool(failures)
    finally:
        for temp in _SELF_TEST_TEMPS:
            temp.cleanup()
        _SELF_TEST_TEMPS.clear()
    print("")
    print(
        "check_closure --self-test: %d/%d проверок пройдено"
        % (len(checks) - failed, len(checks))
    )
    return 1 if failed else 0


USAGE = """usage: coverage/check_closure.py [-h] [--control-set] [--self-test]

VM-free structural checker of the JS/TS cluster closure records of
ecmascript/arksteed/test/coverage/matrix.md and of the stub inventory artifact
ecmascript/arksteed/test/coverage/stub-inventory.md.

  (no arguments)  check the matrix and the stub inventory against the closure
                  contract
  --control-set   print the case paths of the structurally valid P0 records to
                  stdout, one path per line, sorted and without duplicates, as
                  `-I` patterns of run_arksteed_tests.py; diagnostics go to
                  stderr
  --self-test     run the production checks on synthetic fixtures in temporary
                  directories (VM-free, no build) and report the verdict of
                  every check; the modes are mutually exclusive
  -h, --help      show this message

Exit codes: 0 - complete records and a complete stub inventory (or a computed
control set, including an empty one, or a green self-test); 1 - structural
defects; 2 - usage error; 3 - pure incompleteness."""


def _select_mode(arguments: Sequence[str]) -> Tuple[Optional[str], Optional[str]]:
    """Map the argument vector to a mode; the second value is a usage error."""
    if not arguments:
        return MODE_DEFAULT, None
    if list(arguments) in (["-h"], ["--help"]):
        return MODE_HELP, None
    if list(arguments) == [CONTROL_SET_FLAG]:
        return MODE_CONTROL_SET, None
    if list(arguments) == [SELF_TEST_FLAG]:
        return MODE_SELF_TEST, None
    if CONTROL_SET_FLAG in arguments and SELF_TEST_FLAG in arguments:
        return None, (
            "режимы `%s` и `%s` взаимоисключающие" % (CONTROL_SET_FLAG, SELF_TEST_FLAG)
        )
    if SELF_TEST_FLAG in arguments:
        return None, (
            "режим `%s` не принимает других аргументов: %s"
            % (SELF_TEST_FLAG, " ".join(arg for arg in arguments if arg != SELF_TEST_FLAG))
        )
    return None, "неизвестный аргумент: %s" % " ".join(arguments)


def main(argv: Optional[List[str]] = None) -> int:
    """Entry point; see the module docstring for the modes and exit codes."""
    arguments = list(sys.argv[1:] if argv is None else argv)
    mode, usage_error = _select_mode(arguments)
    if usage_error is not None:
        print("check_closure: %s" % usage_error, file=sys.stderr)
        print(USAGE, file=sys.stderr)
        return EXIT_USAGE
    if mode == MODE_HELP:
        print(USAGE)
        return EXIT_OK
    if mode == MODE_SELF_TEST:
        return run_self_test()
    if mode == MODE_CONTROL_SET:
        control_set = check_control_set(default_descriptor())
        if control_set.usage_error:
            print("check_closure: %s" % control_set.usage_error, file=sys.stderr)
            return control_set.exit_code
        for message in control_set.diagnostics:
            print(message, file=sys.stderr)
        for path in control_set.paths:
            print(path)
        return control_set.exit_code
    report = check_closure(default_descriptor())
    if report.usage_error:
        print("check_closure: %s" % report.usage_error, file=sys.stderr)
        return report.exit_code
    for message in report.messages:
        print(message)
    print(report.summary)
    return report.exit_code


if __name__ == "__main__":
    sys.exit(main())
