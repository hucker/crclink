"""Prepare release artifacts and checks for crclink."""

from __future__ import annotations

import os
import subprocess
import sys
import tomllib
from pathlib import Path

# Run against the locked dependency set so a stale lockfile fails the release
# rather than silently resolving something newer than was tested.
COMMANDS = [
    ["uv", "sync", "--locked"],
    ["uv", "run", "--frozen", "pytest"],
    ["uvx", "ruff", "check", "src", "tests"],
]

_PYPROJECT = Path(__file__).resolve().parent.parent / "pyproject.toml"


def _package_version() -> str:
    """Return the static project version from pyproject.toml."""
    with _PYPROJECT.open("rb") as handle:
        return tomllib.load(handle)["project"]["version"]


def _check_tag_matches_version() -> int:
    """Fail if the release tag disagrees with the packaged version.

    The publish workflow passes the GitHub Release tag as RELEASE_TAG. When it is
    set (a CI release), the tag minus a leading "v" must equal the version in
    pyproject.toml, so a mistagged release fails here instead of uploading the
    wrong version to PyPI, which cannot be undone. When RELEASE_TAG is unset (a
    local checks run) the comparison is skipped.

    Returns:
        0 if the tag matches or there is nothing to check, 1 on mismatch.
    """
    tag = os.environ.get("RELEASE_TAG")
    if not tag:
        print("No RELEASE_TAG set; skipping tag/version check.")
        return 0

    version = _package_version()
    wanted = tag[1:] if tag.startswith("v") else tag
    if wanted != version:
        print(f"error: release tag {tag!r} != pyproject version {version!r}", file=sys.stderr)
        return 1

    print(f"Release tag {tag!r} matches version {version!r}.")
    return 0


def main() -> int:
    """Run pre-release checks.

    Returns:
        Exit code (0 for success).
    """
    code = _check_tag_matches_version()
    if code != 0:
        return code

    for command in COMMANDS:
        print("Running:", " ".join(command))
        result = subprocess.run(command, check=False)
        if result.returncode != 0:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
