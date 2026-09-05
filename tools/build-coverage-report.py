#!/usr/bin/env python3
# SPDX-License-Identifier: MIT-0

"""Generate text coverage artifacts from `llvm-cov export` JSON.

Usage:
    build-coverage-report.py ROOT_DIR BUILD_DIR PROFDATA JSON SUMMARY UNCOVERED_QF UNUSED_SUPPRESSIONS_QF

Inputs:
    ROOT_DIR: repository root, used to filter coverage down to project files
    BUILD_DIR: build directory label written into the summary
    PROFDATA: merged `.profdata` path written into the summary
    JSON: output from `llvm-cov export`

Outputs:
    SUMMARY: per-file and total coverage percentages
    UNCOVERED_QF: Vim quickfix-style uncovered locations with source text
    UNUSED_SUPPRESSIONS_QF: suppression directives that matched no findings
"""

import json
import os
import re
import sys
from fnmatch import fnmatchcase
from typing import Any, Dict, List, NamedTuple, Optional, Sequence, Set, Tuple


IGNORE_FILE = "coverage-ignore"
SUPPRESSION_MARKER = "IMGNEKO_UNCOVERED_OK"
SUPPRESSION_START_MARKER = f"{SUPPRESSION_MARKER}_START"
SUPPRESSION_END_MARKER = f"{SUPPRESSION_MARKER}_END"
LLVM_EXPANSION_REGION_KIND = 1
SUPPRESSION_COUNT_RE = re.compile(
    rf"{SUPPRESSION_MARKER}\[(\d+) lines\]"
)


class BranchKey(NamedTuple):
    """One source branch site after merging header-inline instantiations.

    relpath: repo-relative source path
    lineno: 1-based source line
    column: 1-based source column for this branch edge
    text: normalized source line text used in quickfix output
    """

    relpath: str
    lineno: int
    column: int
    text: str


class FunctionKey(NamedTuple):
    """One source function site after merging translation-unit instantiations.

    relpath: repo-relative source path
    lineno: 1-based source line where the function starts
    display_name: canonical function name without the translation-unit prefix
    """

    relpath: str
    lineno: int
    display_name: str


class SourcePoint(NamedTuple):
    """A 1-based source coordinate used to match regions with segments."""

    line: int
    column: int


class SuppressionKey(NamedTuple):
    """The source location of an `IMGNEKO_UNCOVERED_OK` directive."""

    relpath: str
    line: int


class SuppressionInfo(NamedTuple):
    """Coverage suppression directives and the source lines they affect."""

    directive_lines: Set[int]
    directives_by_suppressed_line: Dict[int, Set[int]]


class LlvmRegion(NamedTuple):
    """The eight fields in an `llvm-cov export` coverage-region record."""

    start_line: int
    start_column: int
    end_line: int
    end_column: int
    execution_count: int
    file_id: int
    expanded_file_id: int
    kind: int

    @classmethod
    def from_json(cls, values: Sequence[Any]) -> "LlvmRegion":
        """Decode LLVM's fixed-width JSON array into named fields."""
        (
            start_line,
            start_column,
            end_line,
            end_column,
            execution_count,
            file_id,
            expanded_file_id,
            kind,
        ) = values
        return cls(
            int(start_line),
            int(start_column),
            int(end_line),
            int(end_column),
            int(execution_count),
            int(file_id),
            int(expanded_file_id),
            int(kind),
        )


class IgnoreRule(NamedTuple):
    """One repo-level coverage suppression rule.

    relpath_pattern: glob matched against the declaration/definition file path
    function_pattern: glob matched against the canonical function name
    whole_file: whether this rule came from a single-field `path-glob` entry
    """

    relpath_pattern: str
    function_pattern: str
    whole_file: bool


def rel_project_path(root_dir: str, filename: str) -> Optional[str]:
    """Return a repo-relative project path for covered source files.

    Coverage data also references generated headers and system files. Those are
    excluded so the report only contains locations under `src/` and `testing/`.
    """
    root_dir = os.path.normpath(root_dir)
    filename = os.path.normpath(filename)
    prefix = root_dir + os.sep
    if not filename.startswith(prefix):
        return None
    relpath = filename[len(prefix):]
    if relpath.startswith("src" + os.sep) or relpath.startswith("testing" + os.sep):
        return relpath.replace(os.sep, "/")
    return None


def read_source_lines(root_dir: str, relpath: str) -> List[str]:
    """Load a source file so uncovered entries can include the source text."""
    with open(os.path.join(root_dir, relpath), encoding="utf-8") as stream:
        return stream.read().splitlines()


def load_cached_source_lines(root_dir: str,
                             relpath: str,
                             source_cache: Dict[str, List[str]],
                             suppressions_by_path: Dict[str, SuppressionInfo]
                             ) -> Optional[List[str]]:
    """Return cached source lines for one project file.

    root_dir is the repository root used to resolve relpath on disk. relpath is
    the repo-relative path to load, such as `src/util/options.c`. source_cache
    memoizes previously read files so callers can reuse the same source text
    across line, branch, and function passes. `suppressions_by_path` stores the
    parsed `IMGNEKO_UNCOVERED_OK` directives for each cached file.

    Returns the file's physical lines, without trailing newlines, or None when
    the file cannot be read.
    """
    source_lines = source_cache.get(relpath)
    if source_lines is not None:
        return source_lines

    try:
        source_lines = read_source_lines(root_dir, relpath)
    except OSError:
        return None

    source_cache[relpath] = source_lines
    suppressions_by_path[relpath] = source_suppression_info(source_lines)
    return source_lines


def source_line_text(source_lines: Sequence[str], lineno: int) -> str:
    """Normalize a single source line for compact quickfix output."""
    if lineno < 1 or lineno > len(source_lines):
        return ""
    return " ".join(source_lines[lineno - 1].strip().split())


def source_span_text(source_lines: Sequence[str],
                     start_line: int,
                     start_column: int,
                     end_line: int,
                     end_column: int) -> str:
    """Normalize one source span for compact quickfix output.

    LLVM branch records carry both start and end columns. Use that span when it
    stays on one physical line so quickfix points at the specific subexpression
    that owns the branch, not the entire enclosing statement.
    """
    if start_line != end_line:
        return source_line_text(source_lines, start_line)
    if start_line < 1 or start_line > len(source_lines):
        return ""

    source_line = source_lines[start_line - 1]
    if start_column < 1 or end_column < start_column:
        return source_line_text(source_lines, start_line)

    snippet = source_line[start_column - 1:end_column - 1]
    snippet = " ".join(snippet.strip().split())
    if snippet:
        return snippet
    return source_line_text(source_lines, start_line)


def uncovered_entry_sort_key(entry: str) -> Tuple[str, int, int, str]:
    """Sort quickfix entries by source location, not lexicographic text.

    The quickfix format is `path:line:column: message`. Sorting those whole
    strings directly puts line `100` before line `20`, so parse the numeric
    fields explicitly and keep the full entry as a deterministic tie-breaker.
    """
    relpath, lineno, column, _message = entry.split(":", 3)
    return relpath, int(lineno), int(column), entry


def function_rel_project_path(root_dir: str,
                              function: Dict[str, Any]) -> Optional[str]:
    """Return the first project file referenced by one LLVM function record.

    root_dir is the repository root used to filter out system and generated
    files. function is one entry from `llvm-cov export`'s top-level `functions`
    list and may name multiple files when inline headers participate in the
    function body.

    Returns the first repo-relative path under `src/` or `testing/`, or None
    when the function record only references non-project files.
    """
    for filename in function.get("filenames", []):
        relpath = rel_project_path(root_dir, filename)
        if relpath is not None:
            return relpath
    return None


def indexed_rel_project_path(root_dir: str,
                             filenames: Sequence[str],
                             index: int) -> Optional[str]:
    """Resolve one LLVM filename-table index to a repo-relative project path.

    root_dir is the repository root used by rel_project_path(). filenames is
    the `function["filenames"]` table from one LLVM function record, and index
    is the zero-based filename id stored inside a branch tuple.

    Returns the matching repo-relative path under `src/` or `testing/`, or None
    when index is out of range or the referenced file is outside the project.
    """
    if index < 0 or index >= len(filenames):
        return None
    return rel_project_path(root_dir, filenames[index])


def function_display_name(function_name: str) -> str:
    """Drop the translation-unit prefix from LLVM's per-function names.

    For header-inline helpers, LLVM emits names such as `path.c:foo`. The source
    function site is `foo`, so use that canonical name when merging instantiations
    across translation units.
    """
    if ":" not in function_name:
        return function_name
    return function_name.split(":", 1)[1]


def load_ignore_rules(root_dir: str) -> List[IgnoreRule]:
    """Load gitignore-like coverage suppression rules from the repo root.

    Supported syntax is intentionally small:
    - blank lines are ignored
    - lines beginning with `#` are comments
    - `path-glob` ignores every uncovered line/branch/function in matching files
    - `path-glob function-glob` ignores matching functions in matching files
    """
    ignore_path = os.path.join(root_dir, IGNORE_FILE)
    if not os.path.exists(ignore_path):
        return []

    rules: List[IgnoreRule] = []
    with open(ignore_path, encoding="utf-8") as stream:
        for lineno, line in enumerate(stream, start=1):
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue

            fields = stripped.split()
            if len(fields) == 1:
                rules.append(IgnoreRule(fields[0], "*", True))
                continue

            if len(fields) == 2:
                rules.append(IgnoreRule(fields[0], fields[1], False))
                continue

            raise ValueError(
                f"{IGNORE_FILE}:{lineno}: expected 'path-glob' or "
                f"'path-glob function-glob', got: {stripped}"
            )

    return rules


def source_suppression_info(source_lines: Sequence[str]) -> SuppressionInfo:
    """Parse `IMGNEKO_UNCOVERED_OK` directives from source lines.

    A marker on the same line suppresses that exact line. When the marker
    appears on a standalone comment line, it also suppresses the next physical
    source line so callers can write:

        // IMGNEKO_UNCOVERED_OK: fatal error path
        if (bad_input)
            die("...");

    `IMGNEKO_UNCOVERED_OK_START` and `IMGNEKO_UNCOVERED_OK_END` suppress every
    physical line in the marked block, including the marker lines themselves.

    `IMGNEKO_UNCOVERED_OK[N lines]` suppresses the marker line and the next `N`
    physical lines. The word `lines` is mandatory.

    Returns every directive line, every suppressed source line, and the mapping
    needed to identify which directives suppressed a coverage finding.
    """
    directive_lines: Set[int] = set()
    directives_by_suppressed_line: Dict[int, Set[int]] = {}
    active_block_directives: List[int] = []

    def record_suppression(target_line: int, directive_line: int) -> None:
        """Associate a source line with the directive that suppresses it."""
        directives_by_suppressed_line.setdefault(target_line, set()).add(
            directive_line
        )

    for lineno, line in enumerate(source_lines, start=1):
        for directive_line in active_block_directives:
            record_suppression(lineno, directive_line)

        if SUPPRESSION_START_MARKER in line:
            directive_lines.add(lineno)
            active_block_directives.append(lineno)
            record_suppression(lineno, lineno)
            continue

        if SUPPRESSION_END_MARKER in line:
            if active_block_directives:
                active_block_directives.pop()
            continue

        count_match = SUPPRESSION_COUNT_RE.search(line)
        if count_match is not None:
            directive_lines.add(lineno)
            last_line = min(len(source_lines),
                            lineno + int(count_match.group(1)))
            for target_line in range(lineno, last_line + 1):
                record_suppression(target_line, lineno)
            continue

        if SUPPRESSION_MARKER not in line:
            continue

        directive_lines.add(lineno)
        record_suppression(lineno, lineno)
        leading = line.lstrip()
        if ((leading.startswith("//") or leading.startswith("/*")) and
                lineno < len(source_lines)):
            record_suppression(lineno + 1, lineno)

    return SuppressionInfo(directive_lines, directives_by_suppressed_line)


def suppression_keys_for_line(relpath: str,
                              lineno: int,
                              suppression_info: SuppressionInfo
                              ) -> Set[SuppressionKey]:
    """Return the directives that suppress a source line in a project file."""
    return {
        SuppressionKey(relpath, directive_line)
        for directive_line in
        suppression_info.directives_by_suppressed_line.get(lineno, set())
    }


def is_ignored_function(ignore_rules: Sequence[IgnoreRule],
                        relpath: str,
                        display_name: str) -> bool:
    """Report whether repo-level rules suppress this function site."""
    for rule in ignore_rules:
        if not fnmatchcase(relpath, rule.relpath_pattern):
            continue
        if fnmatchcase(display_name, rule.function_pattern):
            return True
    return False


def is_ignored_file(ignore_rules: Sequence[IgnoreRule], relpath: str) -> bool:
    """Report whether repo-level rules suppress every finding in this file."""
    for rule in ignore_rules:
        if not fnmatchcase(relpath, rule.relpath_pattern):
            continue
        if rule.whole_file:
            return True
    return False


def function_start_line(function: Dict[str, Any]) -> Optional[int]:
    """Return the function definition line for one `llvm-cov` function record."""
    # The first region starts at the function definition itself. Later regions may come
    # from macro expansions inside the body and can point at earlier lines in the
    # header, so using the minimum region line splits one function into fake separate
    # sites.
    regions = function.get("regions", [])
    if not regions:
        return None
    return int(regions[0][0])


def expansion_has_executed_descendant(
    target_regions: Sequence[LlvmRegion],
    expanded_file_id: int,
) -> bool:
    """Report whether an expanded region tree contains an executed region.

    target_regions contains the regions generated by a macro expansion.
    expanded_file_id identifies the virtual file (it's an llvm thing) containing its
    generated source. Nested macro expansions refer to additional virtual files, so
    traverse those files too.
    """
    regions_by_file_id: Dict[int, List[LlvmRegion]] = {}
    for region in target_regions:
        regions_by_file_id.setdefault(region.file_id, []).append(region)

    pending_file_ids = [expanded_file_id]
    visited_file_ids: Set[int] = set()

    while pending_file_ids:
        file_id = pending_file_ids.pop()
        if file_id in visited_file_ids:
            continue
        visited_file_ids.add(file_id)

        for region in regions_by_file_id.get(file_id, []):
            if region.execution_count > 0:
                return True
            if region.kind == LLVM_EXPANSION_REGION_KIND:
                pending_file_ids.append(region.expanded_file_id)

    return False


def find_executed_expansion_wrappers(
    expansions: Sequence[Dict[str, Any]],
) -> Set[SourcePoint]:
    """Return zero-count macro wrappers whose generated code was executed.

    expansions is one file's `expansions` list from `llvm-cov export`.

    LLVM can assign a zero count to an outer expansion region even though its
    generated descendants have nonzero counts. The flattened file segments do
    not retain the expansion kind, so preserve the wrappers' source coordinates
    here for add_uncovered_lines(). An expansion with no executed descendants
    is deliberately omitted because it represents genuinely uncovered code.
    """
    wrappers: Set[SourcePoint] = set()

    for expansion in expansions:
        source_region = LlvmRegion.from_json(expansion["source_region"])
        if (source_region.kind != LLVM_EXPANSION_REGION_KIND or
                source_region.execution_count != 0):
            continue

        # LLVM repeats large target-region lists for each expansion. Decode them
        # only for zero-count wrappers that need a descendant execution check.
        target_regions = [
            LlvmRegion.from_json(region)
            for region in expansion.get("target_regions", [])
        ]
        if not expansion_has_executed_descendant(
            target_regions, source_region.expanded_file_id
        ):
            continue
        wrappers.add(SourcePoint(source_region.start_line,
                                 source_region.start_column))

    return wrappers


def add_uncovered_lines(
    relpath: str,
    source_lines: Sequence[str],
    segments: Sequence[Sequence[Any]],
    suppression_info: SuppressionInfo,
    ignore_rules: Sequence[IgnoreRule],
    executed_expansion_wrappers: Set[SourcePoint],
    triggered_suppressions: Set[SuppressionKey],
    entries: Set[str],
) -> None:
    """Add entries for zero-count executable regions from `llvm-cov` segments.

    `llvm-cov export` reports line coverage as a sequence of source-coordinate
    segments. A zero-count segment starts an uncovered region, and the next
    segment marks where that region ends. Expand those ranges back into source
    lines so quickfix can jump directly to the uncovered code.

    `relpath` names the project file being processed. `source_lines` contains
    that file's physical lines. `segments` is the flattened LLVM segment list.
    `suppression_info` contains explicit source-level suppressions.
    `ignore_rules` contains repo-wide file exclusions.
    `executed_expansion_wrappers` contains zero-count segment starts proven to
    have executed generated descendants. `triggered_suppressions` records the
    directives that match findings. `entries` is updated with new quickfix
    records.
    """
    if is_ignored_file(ignore_rules, relpath):
        return

    for current, nxt in zip(segments, segments[1:]):
        line, column, count, has_count, _is_region_entry, is_gap = current[:6]
        next_line, next_column = nxt[0], nxt[1]
        if not has_count or is_gap or count != 0:
            continue
        if SourcePoint(int(line), int(column)) in executed_expansion_wrappers:
            continue

        end_line = next_line
        if next_column <= 1:
            end_line -= 1
        if end_line < line:
            continue

        for lineno in range(line, end_line + 1):
            text = source_line_text(source_lines, lineno)
            if not text or text == "}":
                continue
            suppression_keys = suppression_keys_for_line(
                relpath, lineno, suppression_info
            )
            if suppression_keys:
                triggered_suppressions.update(suppression_keys)
                continue
            entries.add(f"{relpath}:{lineno}:1: uncovered line: {text}")


def collect_function_branches(
    root_dir: str,
    function: Dict[str, Any],
    ignore_rules: Sequence[IgnoreRule],
    source_cache: Dict[str, List[str]],
    suppressions_by_path: Dict[str, SuppressionInfo],
    branch_totals: Dict[BranchKey, Dict[str, Any]],
) -> None:
    """Accumulate branch counts for one source branch site across functions.

    Header-only inline helpers are instantiated in multiple translation units,
    and `llvm-cov export` reports one branch record per instantiated function.
    Aggregate those records by `(path, line, column, source_text)` so quickfix
    shows one branch site, not one line per translation unit. A branch is only
    uncovered if the merged counts still miss one side.

    `root_dir` resolves per-branch filename ids back to project paths.
    `function` is the raw LLVM function record whose `branches` array is being
    merged. `ignore_rules` suppresses whole-file findings from coverage-ignore.
    `source_cache` and `suppressions_by_path` are shared caches populated for
    each branch source file. `branch_totals` is the mutable output accumulator
    keyed by the final quickfix source location.

    The function mutates branch_totals in place and returns None.
    """
    filenames = function.get("filenames", [])

    for branch in function.get("branches", []):
        if len(branch) < 6:
            continue

        # LLVM branch tuples carry the source file index for the branch site.
        # Respect it so inline macro/header branches are reported at the header
        # definition instead of the instantiating .c file.
        if len(branch) >= 7:
            relpath = indexed_rel_project_path(root_dir, filenames, int(branch[6]))
            if relpath is None:
                continue
        else:
            relpath = function_rel_project_path(root_dir, function)
            if relpath is None:
                continue

        if is_ignored_file(ignore_rules, relpath):
            continue

        source_lines = load_cached_source_lines(root_dir, relpath, source_cache,
                                                suppressions_by_path)
        if source_lines is None:
            continue

        lineno = int(branch[0])
        column = int(branch[1])
        end_line = int(branch[2]) if len(branch) >= 4 else lineno
        end_column = int(branch[3]) if len(branch) >= 4 else column
        true_count = int(branch[4])
        false_count = int(branch[5])
        text = source_span_text(source_lines, lineno, column, end_line,
                                end_column)
        if not text:
            continue
        key = BranchKey(relpath, lineno, column, text)
        totals = branch_totals.setdefault(
            key,
            {
                "true_count": 0,
                "false_count": 0,
                "instances": 0,
                "suppression_keys": set(),
            },
        )
        totals["true_count"] += true_count
        totals["false_count"] += false_count
        totals["instances"] += 1
        totals["suppression_keys"].update(suppression_keys_for_line(
            relpath, lineno, suppressions_by_path[relpath]
        ))


def add_merged_branches(
    branch_totals: Dict[BranchKey, Dict[str, Any]],
    triggered_suppressions: Set[SuppressionKey],
    entries: Set[str],
) -> None:
    """Emit unsuppressed branch findings and record triggered directives."""
    for key, totals in sorted(branch_totals.items()):
        relpath, lineno, column, text = key
        true_count = int(totals["true_count"])
        false_count = int(totals["false_count"])
        if true_count > 0 and false_count > 0:
            continue

        suppression_keys = totals["suppression_keys"]
        if suppression_keys:
            triggered_suppressions.update(suppression_keys)
            continue

        instance_count = int(totals["instances"])
        context = ""
        if instance_count > 1:
            context = f" across {instance_count} instantiations"
        entries.add(
            f"{relpath}:{lineno}:{column}: branch not fully covered{context} "
            f"[true={true_count} false={false_count}]: {text}"
        )


def collect_functions(
    root_dir: str,
    functions: Sequence[Dict[str, Any]],
    ignore_rules: Sequence[IgnoreRule],
    suppressions_by_path: Dict[str, SuppressionInfo],
    function_totals: Dict[FunctionKey, Dict[str, Any]],
) -> None:
    """Accumulate execution counts for each source function site across TUs.

    `root_dir` resolves each function's source path. `functions` contains raw
    LLVM function records. `ignore_rules` excludes configured function sites.
    `suppressions_by_path` provides directives for each source file.
    `function_totals` is the mutable output accumulator.
    """
    for function in functions:
        relpath = function_rel_project_path(root_dir, function)
        if relpath is None:
            continue

        lineno = function_start_line(function)
        if lineno is None:
            continue
        display_name = function_display_name(function.get("name", "<unknown>"))
        if is_ignored_function(ignore_rules, relpath, display_name):
            continue
        key = FunctionKey(relpath, lineno, display_name)
        totals = function_totals.setdefault(
            key,
            {"count": 0, "instances": 0, "suppression_keys": set()},
        )
        totals["count"] += int(function.get("count", 0))
        totals["instances"] += 1
        suppression_info = suppressions_by_path.get(relpath)
        if suppression_info is not None:
            totals["suppression_keys"].update(suppression_keys_for_line(
                relpath, lineno, suppression_info
            ))


def add_merged_functions(
    function_totals: Dict[FunctionKey, Dict[str, Any]],
    triggered_suppressions: Set[SuppressionKey],
    entries: Set[str],
) -> None:
    """Emit unsuppressed function findings and record triggered directives."""
    for key, totals in sorted(function_totals.items()):
        relpath, lineno, display_name = key
        if int(totals["count"]) != 0:
            continue

        suppression_keys = totals["suppression_keys"]
        if suppression_keys:
            triggered_suppressions.update(suppression_keys)
            continue

        instance_count = int(totals["instances"])
        if instance_count > 1:
            entries.add(
                f"{relpath}:{lineno}:1: function never executed across {instance_count} "
                f"instantiations: {display_name}"
            )
            continue

        entries.add(
            f"{relpath}:{lineno}:1: function never executed: {display_name}"
        )


def find_unused_suppression_entries(
    source_cache: Dict[str, List[str]],
    suppressions_by_path: Dict[str, SuppressionInfo],
    triggered_suppressions: Set[SuppressionKey],
) -> Set[str]:
    """Return quickfix entries for directives that matched no findings.

    Only files present in LLVM's coverage export are considered. A directive is
    unused when it did not suppress an uncovered line, branch, or function.
    """
    entries: Set[str] = set()

    for relpath, suppression_info in suppressions_by_path.items():
        source_lines = source_cache[relpath]
        for lineno in suppression_info.directive_lines:
            if SuppressionKey(relpath, lineno) in triggered_suppressions:
                continue
            text = source_line_text(source_lines, lineno)
            entries.add(
                f"{relpath}:{lineno}:1: unused coverage suppression: {text}"
            )

    return entries


def format_metric(label: str, metric: Dict[str, Any]) -> str:
    """Render one coverage percentage/count pair for the summary file."""
    return f"{label}: {metric['percent']:.2f}% of {metric['count']}"


def main() -> int:
    """Transform `llvm-cov export` JSON into text coverage artifacts."""
    if len(sys.argv) != 8:
        print(
            "usage: build-coverage-report.py ROOT_DIR BUILD_DIR PROFDATA JSON "
            "SUMMARY UNCOVERED_QF UNUSED_SUPPRESSIONS_QF",
            file=sys.stderr,
        )
        return 1

    (
        root_dir,
        build_dir,
        profdata_path,
        json_path,
        summary_path,
        uncovered_path,
        unused_suppressions_path,
    ) = sys.argv[1:]

    with open(json_path, encoding="utf-8") as stream:
        export_data = json.load(stream)

    try:
        ignore_rules = load_ignore_rules(root_dir)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    payload = export_data["data"][0]
    source_cache: Dict[str, List[str]] = {}
    suppressions_by_path: Dict[str, SuppressionInfo] = {}
    triggered_suppressions: Set[SuppressionKey] = set()
    uncovered_entries: Set[str] = set()
    branch_totals: Dict[BranchKey, Dict[str, Any]] = {}
    function_totals: Dict[FunctionKey, Dict[str, Any]] = {}
    # Store `(repo_relative_path, file_summary)` pairs so the final summary can
    # be sorted by path before writing.
    summary_rows: List[Tuple[str, Dict[str, Any]]] = []

    for file_data in payload["files"]:
        relpath = rel_project_path(root_dir, file_data["filename"])
        if relpath is None:
            continue

        source_lines = load_cached_source_lines(root_dir, relpath, source_cache,
                                                suppressions_by_path)
        if source_lines is None:
            continue

        summary_rows.append((relpath, file_data["summary"]))
        add_uncovered_lines(
            relpath,
            source_lines,
            file_data.get("segments", []),
            suppressions_by_path[relpath],
            ignore_rules,
            find_executed_expansion_wrappers(
                file_data.get("expansions", [])
            ),
            triggered_suppressions,
            uncovered_entries,
        )

    for function in payload.get("functions", []):
        relpath = function_rel_project_path(root_dir, function)
        if relpath is None:
            continue
        if load_cached_source_lines(root_dir, relpath, source_cache,
                                    suppressions_by_path) is None:
            continue
        collect_function_branches(
            root_dir,
            function,
            ignore_rules,
            source_cache,
            suppressions_by_path,
            branch_totals,
        )

    add_merged_branches(branch_totals, triggered_suppressions,
                        uncovered_entries)
    collect_functions(
        root_dir,
        payload.get("functions", []),
        ignore_rules,
        suppressions_by_path,
        function_totals,
    )
    add_merged_functions(function_totals, triggered_suppressions,
                         uncovered_entries)
    unused_suppression_entries = find_unused_suppression_entries(
        source_cache, suppressions_by_path, triggered_suppressions
    )

    summary_rows.sort(key=lambda item: item[0])
    totals = payload["totals"]

    with open(summary_path, "w", encoding="utf-8") as stream:
        stream.write("# Generated by `make coverage`. Do not edit by hand.\n\n")
        stream.write(f"Build dir: {build_dir}\n")
        stream.write(f"Merged profile: {profdata_path}\n\n")

        for relpath, summary in summary_rows:
            stream.write(f"File '{relpath}'\n")
            stream.write(f"{format_metric('Lines executed', summary['lines'])}\n")
            stream.write(f"{format_metric('Regions executed', summary['regions'])}\n")
            stream.write(f"{format_metric('Functions executed', summary['functions'])}\n")
            stream.write(f"{format_metric('Branches covered', summary['branches'])}\n\n")

        stream.write("Totals\n")
        stream.write(f"{format_metric('Lines executed', totals['lines'])}\n")
        stream.write(f"{format_metric('Regions executed', totals['regions'])}\n")
        stream.write(f"{format_metric('Functions executed', totals['functions'])}\n")
        stream.write(f"{format_metric('Branches covered', totals['branches'])}\n\n")
        stream.write(f"Uncovered locations: {len(uncovered_entries)}\n")

    with open(uncovered_path, "w", encoding="utf-8") as stream:
        for entry in sorted(uncovered_entries, key=uncovered_entry_sort_key):
            stream.write(entry)
            stream.write("\n")

    with open(unused_suppressions_path, "w", encoding="utf-8") as stream:
        for entry in sorted(unused_suppression_entries,
                            key=uncovered_entry_sort_key):
            stream.write(entry)
            stream.write("\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
