"""Quality gate: count ruff and ty diagnostics, fail if either reports any.

crclink gates on a clean lint + type check rather than a coverage threshold.
This runs both tools, prints their error counts, and exits nonzero if either
finds anything. Run it locally or in CI:

    uv run python scripts/quality.py

Both tools emit a machine-readable list of diagnostics (ruff's JSON, ty's
GitLab Code Quality JSON), so the counts are exact rather than scraped from
human-readable output.
"""

from __future__ import annotations

import json
import subprocess
import sys

# ruff lints the package and the tests; ty checks the package. Both are asked
# for a JSON list of diagnostics, so len() of the parsed output is the count.
RUFF_CMD = ["ruff", "check", "--output-format", "json", "src", "tests"]
TY_CMD = ["ty", "check", "--output-format", "gitlab", "src"]


def _count(cmd: list[str], tool: str) -> int:
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


def main() -> int:
    """Print ruff and ty error counts; return 1 if either is nonzero."""
    ruff_errors = _count(RUFF_CMD, "ruff")
    ty_errors = _count(TY_CMD, "ty")

    print(f"ruff: {ruff_errors} errors")
    print(f"ty:   {ty_errors} errors")

    total = ruff_errors + ty_errors
    if total:
        print(f"FAIL: {total} error(s)", file=sys.stderr)
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
