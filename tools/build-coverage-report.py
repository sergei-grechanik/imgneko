#!/usr/bin/env python3

"""Generate text coverage artifacts from `llvm-cov export` JSON.

Usage:
    build-coverage-report.py ROOT_DIR BUILD_DIR PROFDATA JSON SUMMARY UNCOVERED_QF

Inputs:
    ROOT_DIR: repository root, used to filter coverage down to project files
    BUILD_DIR: build directory label written into the summary
    PROFDATA: merged `.profdata` path written into the summary
    JSON: output from `llvm-cov export`

Outputs:
    SUMMARY: per-file and total coverage percentages
    UNCOVERED_QF: Vim quickfix-style uncovered locations with source text
"""

import json
import os
import sys
from fnmatch import fnmatchcase
from typing import Any, Dict, List, NamedTuple, Optional, Sequence, Set, Tuple


IGNORE_FILE = "coverage-ignore"
SUPPRESSION_MARKER = "IMGNEKO_UNCOVERED_OK"


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


def source_line_text(source_lines: Sequence[str], lineno: int) -> str:
    """Normalize a single source line for compact quickfix output."""
    if lineno < 1 or lineno > len(source_lines):
        return ""
    return " ".join(source_lines[lineno - 1].strip().split())


def function_rel_project_path(root_dir: str,
                              function: Dict[str, Any]) -> Optional[str]:
    """Return the first project file associated with a function record."""
    for filename in function.get("filenames", []):
        relpath = rel_project_path(root_dir, filename)
        if relpath is not None:
            return relpath
    return None


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


def suppressed_source_lines(source_lines: Sequence[str]) -> Set[int]:
    """Return source lines suppressed by `IMGNEKO_UNCOVERED_OK` comments.

    A marker on the same line suppresses that exact line. When the marker
    appears on a standalone comment line, it also suppresses the next physical
    source line so callers can write:

        // IMGNEKO_UNCOVERED_OK: fatal error path
        if (bad_input)
            die("...");
    """
    suppressed: Set[int] = set()
    suppress_next_code_line = False

    for lineno, line in enumerate(source_lines, start=1):
        stripped = line.strip()
        if SUPPRESSION_MARKER in line:
            suppressed.add(lineno)
            leading = line.lstrip()
            suppress_next_code_line = (
                leading.startswith("//") or
                leading.startswith("/*")
            )
            continue

        if not suppress_next_code_line:
            continue
        suppressed.add(lineno)
        suppress_next_code_line = False

    return suppressed


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
    """Return the first covered source line associated with a function record."""
    line_numbers = [int(region[0]) for region in function.get("regions", []) if region]
    if not line_numbers:
        return None
    return min(line_numbers)


def add_uncovered_lines(
    entries: Set[str],
    relpath: str,
    source_lines: Sequence[str],
    segments: Sequence[Sequence[Any]],
    suppressed_lines: Set[int],
    ignore_rules: Sequence[IgnoreRule],
) -> None:
    """Add entries for zero-count executable regions from `llvm-cov` segments.

    `llvm-cov export` reports line coverage as a sequence of source-coordinate
    segments. A zero-count segment starts an uncovered region, and the next
    segment marks where that region ends. Expand those ranges back into source
    lines so quickfix can jump directly to the uncovered code.
    """
    if is_ignored_file(ignore_rules, relpath):
        return

    for current, nxt in zip(segments, segments[1:]):
        line, _column, count, has_count, _is_region_entry, is_gap = current[:6]
        next_line, next_column = nxt[0], nxt[1]
        if not has_count or is_gap or count != 0:
            continue

        end_line = next_line
        if next_column <= 1:
            end_line -= 1
        if end_line < line:
            continue

        for lineno in range(line, end_line + 1):
            if lineno in suppressed_lines:
                continue
            text = source_line_text(source_lines, lineno)
            if not text:
                continue
            entries.add(f"{relpath}:{lineno}:1: uncovered line: {text}")


def collect_function_branches(
    branch_totals: Dict[BranchKey, Dict[str, Any]],
    relpath: str,
    display_name: str,
    source_lines: Sequence[str],
    branches: Sequence[Sequence[Any]],
    ignore_rules: Sequence[IgnoreRule],
    suppressed_lines: Set[int],
) -> None:
    """Accumulate branch counts for one source branch site across functions.

    Header-only inline helpers are instantiated in multiple translation units,
    and `llvm-cov export` reports one branch record per instantiated function.
    Aggregate those records by `(path, line, column, source_text)` so quickfix
    shows one branch site, not one line per translation unit. A branch is only
    uncovered if the merged counts still miss one side.
    """
    if is_ignored_file(ignore_rules, relpath):
        return

    for branch in branches:
        if len(branch) < 6:
            continue
        lineno = int(branch[0])
        if lineno in suppressed_lines:
            continue
        column = int(branch[1])
        true_count = int(branch[4])
        false_count = int(branch[5])
        text = source_line_text(source_lines, lineno)
        if not text:
            continue
        key = BranchKey(relpath, lineno, column, text)
        totals = branch_totals.setdefault(
            key,
            {"true_count": 0, "false_count": 0, "instances": 0},
        )
        totals["true_count"] += true_count
        totals["false_count"] += false_count
        totals["instances"] += 1


def add_merged_branches(entries: Set[str],
                        branch_totals: Dict[BranchKey, Dict[str, Any]]) -> None:
    """Emit one quickfix entry per still-uncovered merged branch site."""
    for key, totals in sorted(branch_totals.items()):
        relpath, lineno, column, text = key
        true_count = int(totals["true_count"])
        false_count = int(totals["false_count"])
        if true_count > 0 and false_count > 0:
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
    function_totals: Dict[FunctionKey, Dict[str, Any]],
    root_dir: str,
    functions: Sequence[Dict[str, Any]],
    ignore_rules: Sequence[IgnoreRule],
    suppressed_lines_by_path: Dict[str, Set[int]],
) -> None:
    """Accumulate execution counts for one source function site across TUs."""
    for function in functions:
        relpath = function_rel_project_path(root_dir, function)
        if relpath is None:
            continue

        lineno = function_start_line(function)
        if lineno is None:
            continue
        display_name = function_display_name(function.get("name", "<unknown>"))
        if lineno in suppressed_lines_by_path.get(relpath, set()):
            continue
        if is_ignored_function(ignore_rules, relpath, display_name):
            continue
        key = FunctionKey(relpath, lineno, display_name)
        totals = function_totals.setdefault(
            key,
            {"count": 0, "instances": 0},
        )
        totals["count"] += int(function.get("count", 0))
        totals["instances"] += 1


def add_merged_functions(entries: Set[str],
                         function_totals: Dict[FunctionKey, Dict[str, Any]]) -> None:
    """Emit one entry per source function site that never ran anywhere."""
    for key, totals in sorted(function_totals.items()):
        relpath, lineno, display_name = key
        if int(totals["count"]) != 0:
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


def format_metric(label: str, metric: Dict[str, Any]) -> str:
    """Render one coverage percentage/count pair for the summary file."""
    return f"{label}: {metric['percent']:.2f}% of {metric['count']}"


def main() -> int:
    """Transform `llvm-cov export` JSON into summary.txt and uncovered.qf."""
    if len(sys.argv) != 7:
        print(
            "usage: build-coverage-report.py ROOT_DIR BUILD_DIR PROFDATA JSON SUMMARY UNCOVERED_QF",
            file=sys.stderr,
        )
        return 1

    root_dir, build_dir, profdata_path, json_path, summary_path, uncovered_path = sys.argv[1:]

    with open(json_path, encoding="utf-8") as stream:
        export_data = json.load(stream)

    try:
        ignore_rules = load_ignore_rules(root_dir)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    payload = export_data["data"][0]
    source_cache: Dict[str, List[str]] = {}
    suppressed_lines_by_path: Dict[str, Set[int]] = {}
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

        try:
            source_lines = read_source_lines(root_dir, relpath)
        except OSError:
            continue

        source_cache[relpath] = source_lines
        suppressed_lines_by_path[relpath] = suppressed_source_lines(source_lines)
        summary_rows.append((relpath, file_data["summary"]))
        add_uncovered_lines(
            uncovered_entries,
            relpath,
            source_lines,
            file_data.get("segments", []),
            suppressed_lines_by_path[relpath],
            ignore_rules,
        )

    for function in payload.get("functions", []):
        relpath = function_rel_project_path(root_dir, function)
        if relpath is None:
            continue
        source_lines = source_cache.get(relpath)
        if source_lines is None:
            try:
                source_lines = read_source_lines(root_dir, relpath)
            except OSError:
                continue
            source_cache[relpath] = source_lines
            suppressed_lines_by_path[relpath] = suppressed_source_lines(source_lines)
        collect_function_branches(
            branch_totals,
            relpath,
            function_display_name(function.get("name", "<unknown>")),
            source_lines,
            function.get("branches", []),
            ignore_rules,
            suppressed_lines_by_path[relpath],
        )

    add_merged_branches(uncovered_entries, branch_totals)
    collect_functions(
        function_totals,
        root_dir,
        payload.get("functions", []),
        ignore_rules,
        suppressed_lines_by_path,
    )
    add_merged_functions(uncovered_entries, function_totals)

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
        for entry in sorted(uncovered_entries):
            stream.write(entry)
            stream.write("\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
