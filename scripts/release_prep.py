"""Prepare release artifacts and checks for crclink."""

from __future__ import annotations

import subprocess

COMMANDS = [
    ["uv", "sync"],
    ["uv", "run", "pytest"],
    ["uvx", "ruff", "check", "src", "tests"],
]


def main() -> int:
    """Run pre-release checks.

    Returns:
        Exit code (0 for success).
    """
    for command in COMMANDS:
        print("Running:", " ".join(command))
        result = subprocess.run(command, check=False)
        if result.returncode != 0:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
