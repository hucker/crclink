"""Quality gate: count ruff, ty, and pytest failures; fail if anything failed.

crclink gates on a clean lint, a clean type check, and a green test suite
rather than a coverage threshold. This runs all three, prints their counts, and
exits nonzero if anything fails. Run it locally or in CI:

    uv run python scripts/quality.py

ruff and ty emit machine-readable diagnostic lists (ruff's JSON, ty's GitLab
Code Quality JSON); pytest writes a JUnit XML report. The counts are read from
those, not scraped from human-readable output.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

# ruff lints the package and the tests; ty checks the package. Both are asked
# for a JSON list of diagnostics, so len() of the parsed output is the count.
RUFF_CMD = ["ruff", "check", "--output-format", "json", "src", "tests"]
TY_CMD = ["ty", "check", "--output-format", "gitlab", "src"]


def _count_json(cmd: list[str], tool: str) -> int:
    """Run a checker and return its diagnostic count from JSON stdout.

    Args:
        cmd: The checker invocation, configured to emit a JSON list. The
            checker exits nonzero when it finds diagnostics, which is expected;
            the count comes from stdout, not the exit code.
        tool: Tool name, used in the error message if stdout is not JSON.

    Returns:
        The number of diagnostics the tool reported.

    Raises:
        SystemExit: With code 2 if the tool produced no parseable JSON (for
            example a config error), so a broken checker fails loudly instead
            of silently reporting zero.
    """
    result = subprocess.run(cmd, capture_output=True, text=True)
    out = result.stdout.strip()
    try:
        return len(json.loads(out or "[]"))
    except json.JSONDecodeError:
        sys.stderr.write(f"{tool} did not emit JSON:\n{result.stdout}{result.stderr}\n")
        raise SystemExit(2) from None


def _run_pytest() -> tuple[int, int, int]:
    """Run pytest and return (failures, passed, total) from its JUnit report.

    ``failures`` folds in errored/collection failures, not just failed asserts.
    On failure the captured pytest output is echoed so the run is debuggable;
    counts alone are not actionable.

    Returns:
        A ``(failures, passed, total)`` triple.

    Raises:
        SystemExit: With code 2 if pytest wrote no parseable report.
    """
    with tempfile.TemporaryDirectory() as tmp:
        report = Path(tmp) / "report.xml"
        result = subprocess.run(
            ["pytest", "-q", f"--junit-xml={report}"],
            capture_output=True,
            text=True,
        )
        try:
            suites = list(ET.parse(report).getroot().iter("testsuite"))
        except (ET.ParseError, FileNotFoundError):
            sys.stderr.write(f"pytest produced no report:\n{result.stdout}{result.stderr}\n")
            raise SystemExit(2) from None

    def total(attr: str) -> int:
        return sum(int(suite.get(attr, "0")) for suite in suites)

    tests = total("tests")
    failures = total("failures") + total("errors")
    passed = tests - failures - total("skipped")
    if failures:
        sys.stdout.write(result.stdout)
    return failures, passed, tests


def main() -> int:
    """Print ruff, ty, and pytest counts; return 1 if anything failed."""
    ruff_errors = _count_json(RUFF_CMD, "ruff")
    ty_errors = _count_json(TY_CMD, "ty")
    test_fails, test_passed, test_total = _run_pytest()

    print(f"ruff:   {ruff_errors} errors")
    print(f"ty:     {ty_errors} errors")
    print(f"pytest: {test_fails} failed, {test_passed} passed ({test_total} total)")

    problems = ruff_errors + ty_errors + test_fails
    if problems:
        print(f"FAIL: {problems} problem(s)", file=sys.stderr)
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
