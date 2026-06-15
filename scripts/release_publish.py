"""Publish crclink to package index."""

from __future__ import annotations

import subprocess

COMMANDS = [
    ["uv", "build"],
    ["uv", "publish"],
]


def main() -> int:
    """Build and publish the package.

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
