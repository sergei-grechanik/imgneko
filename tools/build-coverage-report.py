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
import re
import sys
from fnmatch import fnmatchcase
from typing import Any, Dict, List, NamedTuple, Optional, Sequence, Set, Tuple


IGNORE_FILE = "coverage-ignore"
SUPPRESSION_MARKER = "IMGNEKO_UNCOVERED_OK"
SUPPRESSION_START_MARKER = f"{SUPPRESSION_MARKER}_START"
SUPPRESSION_END_MARKER = f"{SUPPRESSION_MARKER}_END"
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
                             suppressed_lines_by_path: Dict[str, Set[int]]
                             ) -> Optional[List[str]]:
    """Return cached source lines for one project file.

    root_dir is the repository root used to resolve relpath on disk. relpath is
    the repo-relative path to load, such as `src/util/options.c`. source_cache
    memoizes previously read files so callers can reuse the same source text
    across line, branch, and function passes. suppressed_lines_by_path stores
    the precomputed `IMGNEKO_UNCOVERED_OK` suppression map for each cached file.

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
    suppressed_lines_by_path[relpath] = suppressed_source_lines(source_lines)
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


def suppressed_source_lines(source_lines: Sequence[str]) -> Set[int]:
    """Return source lines suppressed by `IMGNEKO_UNCOVERED_OK` comments.

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
    """
    suppressed: Set[int] = set()
    suppress_next_lines = 0
    block_depth = 0

    for lineno, line in enumerate(source_lines, start=1):
        if block_depth > 0 or suppress_next_lines > 0:
            suppressed.add(lineno)
        if suppress_next_lines > 0:
            suppress_next_lines -= 1

        if SUPPRESSION_START_MARKER in line:
            suppressed.add(lineno)
            block_depth += 1
            continue

        if SUPPRESSION_END_MARKER in line:
            suppressed.add(lineno)
            if block_depth > 0:
                block_depth -= 1
            continue

        count_match = SUPPRESSION_COUNT_RE.search(line)
        if count_match is not None:
            suppressed.add(lineno)
            suppress_next_lines = max(suppress_next_lines,
                                      int(count_match.group(1)))
            continue

        if SUPPRESSION_MARKER not in line:
            continue

        suppressed.add(lineno)
        leading = line.lstrip()
        if leading.startswith("//") or leading.startswith("/*"):
            suppress_next_lines = max(suppress_next_lines, 1)

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
    """Return the function definition line for one `llvm-cov` function record."""
    # The first region starts at the function definition itself. Later regions may come
    # from macro expansions inside the body and can point at earlier lines in the
    # header, so using the minimum region line splits one function into fake separate
    # sites.
    regions = function.get("regions", [])
    if not regions:
        return None
    return int(regions[0][0])


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
            if not text or text == "}":
                continue
            entries.add(f"{relpath}:{lineno}:1: uncovered line: {text}")


def collect_function_branches(
    branch_totals: Dict[BranchKey, Dict[str, Any]],
    root_dir: str,
    function: Dict[str, Any],
    source_cache: Dict[str, List[str]],
    suppressed_lines_by_path: Dict[str, Set[int]],
    ignore_rules: Sequence[IgnoreRule],
) -> None:
    """Accumulate branch counts for one source branch site across functions.

    Header-only inline helpers are instantiated in multiple translation units,
    and `llvm-cov export` reports one branch record per instantiated function.
    Aggregate those records by `(path, line, column, source_text)` so quickfix
    shows one branch site, not one line per translation unit. A branch is only
    uncovered if the merged counts still miss one side.

    branch_totals is the mutable accumulator keyed by the final quickfix source
    location. root_dir is used to resolve per-branch filename ids back to
    project-relative paths. function is one raw LLVM function record whose
    `branches` array is being merged. source_cache and
    suppressed_lines_by_path are shared caches populated on demand for whatever
    file each branch record actually points at. ignore_rules suppress whole-file
    findings from coverage-ignore.

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
                                                suppressed_lines_by_path)
        if source_lines is None:
            continue

        lineno = int(branch[0])
        if lineno in suppressed_lines_by_path.get(relpath, set()):
            continue
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

        source_lines = load_cached_source_lines(root_dir, relpath, source_cache,
                                                suppressed_lines_by_path)
        if source_lines is None:
            continue

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
        if load_cached_source_lines(root_dir, relpath, source_cache,
                                    suppressed_lines_by_path) is None:
            continue
        collect_function_branches(
            branch_totals,
            root_dir,
            function,
            source_cache,
            suppressed_lines_by_path,
            ignore_rules,
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
        for entry in sorted(uncovered_entries, key=uncovered_entry_sort_key):
            stream.write(entry)
            stream.write("\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
