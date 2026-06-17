"""Refresh the README badge block with current, verified values.

Run standalone or as a release_prep step. It runs the checks, then rewrites the
badge block in README.md (between the ``<!-- badges:start -->`` and
``<!-- badges:end -->`` markers) so the static shields badges reflect reality at
release time: each Python version in the tox matrix and its exact interpreter,
the pytest count, the C Unity count, and ruff/ty status.

    uv run python scripts/update_badges.py

The PyPI version badge stays untouched (shields.io reads it live); the license
badge is constant. A failing check turns its badge red rather than aborting, so
the badges always describe the real state.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
README = REPO_ROOT / "README.md"
TOX_INI = REPO_ROOT / "tox.ini"

START = "<!-- badges:start -->"
END = "<!-- badges:end -->"
GREEN = "brightgreen"
RED = "red"


def _run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True)


def _enc(text: str) -> str:
    """Escape a shields.io static-badge segment (dash/underscore/space)."""
    return text.replace("-", "--").replace("_", "__").replace(" ", "%20")


def _badge(label: str, message: str, color: str, tooltip: str) -> str:
    url = f"https://img.shields.io/badge/{_enc(label)}-{_enc(message)}-{color}"
    return f'![{label}]({url} "{tooltip}")'


def _matrix_versions() -> list[str]:
    """The Python versions tox tests, e.g. ['3.11', ...], sorted numerically.

    Read from tox.ini, skipping comment lines so the usage examples in the
    header (which mention a `py3xx` env) do not leak in.
    """
    body = "\n".join(
        line
        for line in TOX_INI.read_text(encoding="utf-8").splitlines()
        if not line.lstrip().startswith("#")
    )
    versions = {f"{major}.{minor}" for major, minor in re.findall(r"py(\d)(\d+)", body)}
    return sorted(versions, key=lambda v: tuple(int(part) for part in v.split(".")))


def _tox_python_version(env: str) -> str:
    """Exact interpreter version for a built tox env, '' if not built yet."""
    for rel in (f".tox/{env}/Scripts/python.exe", f".tox/{env}/bin/python"):
        path = REPO_ROOT / rel
        if path.exists():
            return _run([str(path), "-V"]).stdout.strip().replace("Python ", "")
    return ""


def _python_matrix() -> tuple[list[tuple[str, str, bool]], int]:
    """Run the tox matrix; return [(version, exact, passed)] and the test count."""
    versions = _matrix_versions()
    envs = [f"py{v.replace('.', '')}" for v in versions]
    result = _run(["uvx", "--with", "tox-uv", "tox", "run", "-e", ",".join(envs)])
    out = result.stdout + result.stderr
    count_match = re.search(r"(\d+) passed", out)
    count = int(count_match.group(1)) if count_match else 0
    rows = [
        (v, _tox_python_version(env), re.search(rf"^\s*{env}: OK", out, re.MULTILINE) is not None)
        for v, env in zip(versions, envs, strict=True)
    ]
    return rows, count


def _unity() -> tuple[int, int, list[int]]:
    """Run the C Unity suite; return (tests, failures, per-runner test counts)."""
    result = _run(["make", "-C", "src/c/test", "test"])
    pairs = re.findall(r"(\d+) Tests\s+(\d+) Failures", result.stdout + result.stderr)
    tests = sum(int(t) for t, _ in pairs)
    failures = sum(int(f) for _, f in pairs)
    return tests, failures, [int(t) for t, _ in pairs]


def _diagnostic_count(cmd: list[str]) -> int:
    """Count diagnostics from a checker's JSON list output (ruff, ty)."""
    return len(json.loads(_run(cmd).stdout.strip() or "[]"))


def build_row() -> str:
    """Build the full badge row markdown from freshly-run checks."""
    badges = [
        '[![PyPI](https://img.shields.io/pypi/v/crclink "Latest release on PyPI")]'
        "(https://pypi.org/project/crclink/)",
        _badge("license", "MIT", "blue", "MIT license"),
    ]

    matrix, count = _python_matrix()
    for version, exact, passed in matrix:
        shown = exact or version
        badges.append(
            _badge(
                f"Py {version}",
                "passing" if passed else "failing",
                GREEN if passed else RED,
                f"{count} tests pass on CPython {shown}"
                if passed
                else f"tests fail on CPython {shown}",
            )
        )

    tests, failures, parts = _unity()
    breakdown = " + ".join(str(p) for p in parts) or str(tests)
    badges.append(
        _badge("unity", f"{tests} passed", GREEN, f"C Unity suite: {breakdown} = {tests}, 0 failures")
        if failures == 0
        else _badge("unity", f"{failures} failed", RED, f"C Unity suite: {failures} of {tests} failing")
    )

    for tool, src in (("ruff", ["ruff", "check", "--output-format", "json", "src", "tests"]),
                      ("ty", ["ty", "check", "--output-format", "gitlab", "src"])):
        errors = _diagnostic_count(src)
        kind = "lint" if tool == "ruff" else "type"
        badges.append(
            _badge(
                tool,
                "passing" if errors == 0 else f"{errors} errors",
                GREEN if errors == 0 else RED,
                f"{tool}: {errors} {kind} errors",
            )
        )

    return " ".join(badges)


def main() -> int:
    text = README.read_text(encoding="utf-8")
    if START not in text or END not in text:
        sys.stderr.write(f"README.md is missing the {START} / {END} markers\n")
        return 2
    block = f"{START}\n{build_row()}\n{END}"
    new = re.sub(re.escape(START) + r".*?" + re.escape(END), lambda _: block, text, flags=re.DOTALL)
    README.write_text(new, encoding="utf-8")
    print("README badges refreshed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
