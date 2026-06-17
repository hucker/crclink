"""Release prep: cut release branch, bump version, refresh lock, test, commit.

Stops before merging to main. You (or an AI assistant following
scripts/RELEASE.md) review the diff -- especially the CHANGELOG stub --
rewrite the changelog into a real user-facing summary, then run
release_publish.py to finish.

Usage:
    python scripts/release_prep.py 0.2.0      # bare version, no leading 'v'

What it does:
    1. Sanity-check git state (on main, clean, synced) + lint gate (ruff)
    2. Cut rel/v<version> from main
    3. Bump version in pyproject.toml and refresh uv.lock
    4. Run the full gate: the Python suite (pytest) + the C Unity suite
    5. Insert a CHANGELOG stub (raw git-log bullets + a TODO marker)
    6. Commit the release

Aborts loudly on any failure. Safe-restart: if it fails halfway, discard the
partial bump and delete the branch:

    git checkout -f main && git branch -D rel/v<version>

crclink is pure Python: the wheel is built and uploaded on CI when the tag
is pushed (see release_publish.py and .github/workflows/publish.yml). This
script never builds or uploads.
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
from pathlib import Path

# Allow running as `python scripts/release_prep.py`.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_common import (  # noqa: E402
    REPO_ROOT,
    assert_clean_tree,
    assert_main_in_sync_with_origin,
    assert_on_main,
    assert_tag_does_not_exist,
    assert_tool_available,
    die,
    info,
    last_tag,
    ok,
    run,
    run_out,
    validate_version,
)

# ── lint gate ──────────────────────────────────────────────────────────────────


def assert_zero_lint() -> None:
    """Hard-fail the release if ruff reports anything.

    crclink's quality gate treats ruff clean as a release precondition. ruff
    prints "All checks passed!" when clean and exits nonzero otherwise, so we
    capture with check=False and look for the success sentinel.
    """
    ruff = run_out(["uvx", "ruff", "check", "src", "tests"], check=False)
    if "All checks passed!" not in ruff:
        die("ruff is not clean. Fix on a chore branch first:\n" + ruff)
    ok("lint clean (ruff: All checks passed!)")


# ── version bumping ──────────────────────────────────────────────────────────────


def bump_pyproject(version: str) -> None:
    path = REPO_ROOT / "pyproject.toml"
    text = path.read_text(encoding="utf-8")
    new_text, n = re.subn(
        r'^version = "[^"]+"',
        f'version = "{version}"',
        text,
        count=1,
        flags=re.MULTILINE,
    )
    if n != 1:
        die('could not find a single `version = "..."` line in pyproject.toml')
    path.write_text(new_text, encoding="utf-8")
    ok(f"pyproject.toml -> {version}")


def refresh_lock() -> None:
    """Update uv.lock to the bumped version (crclink tracks the lockfile)."""
    run(["uv", "lock"])
    ok("uv.lock refreshed")


# ── test gate ──────────────────────────────────────────────────────────────────


def run_gate() -> None:
    """Run the full release gate: locked Python suite + the C firmware suite.

    A failure here aborts the release. crclink ships the Python package, and
    the C firmware companion under src/c is gated too (CLAUDE.md treats both
    suites as the bar), so a release proves both halves before the tag exists.
    """
    info("Syncing locked deps and running the Python suite...")
    run(["uv", "sync", "--locked"])
    run(["uv", "run", "--frozen", "pytest"])
    ok("Python suite green")

    info("Running the C firmware Unity suite...")
    run(["make", "-C", "src/c/test", "test"])
    ok("C suite green")


# ── changelog ────────────────────────────────────────────────────────────────────


def insert_changelog_stub(version: str) -> None:
    """Insert a CHANGELOG stub for the new version, seeded with git-log bullets.

    The stub carries a `<!-- TODO ... -->` marker; release_publish.py refuses
    to publish while any TODO remains, forcing the human/AI step of rewriting
    the raw commit list into a user-facing summary.
    """
    path = REPO_ROOT / "CHANGELOG.md"
    text = path.read_text(encoding="utf-8")

    try:
        prev = last_tag()
    except Exception:
        prev = ""

    log_range = f"{prev}..HEAD" if prev else "HEAD"
    commits = run_out(["git", "log", log_range, "--oneline", "--no-merges"])

    bullets = []
    for line in commits.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split(maxsplit=1)
        if len(parts) == 2:
            bullets.append(f"- {parts[1]}")
    bullet_block = "\n".join(bullets) if bullets else "- (no commits found)"

    today = dt.date.today().isoformat()
    stub = (
        f"## v{version} — {today}\n"
        f"\n"
        f"<!-- TODO: rewrite the raw commit bullets below into a user-facing "
        f"summary, then delete this comment. Commits since {prev or 'the beginning'}: -->\n"
        f"{bullet_block}\n"
        f"\n"
    )

    # Insert just after the "# Changelog" header. A lambda replacement keeps
    # backslashes in commit subjects from being read as regex back-references.
    new_text = re.sub(r"(# Changelog\n+)", lambda m: m.group(0) + stub, text, count=1)
    if new_text == text:
        die("could not find '# Changelog' header in CHANGELOG.md")
    path.write_text(new_text, encoding="utf-8")
    ok(f"CHANGELOG.md stub inserted for v{version} (edit before publishing)")


# ── git operations ───────────────────────────────────────────────────────────────


def cut_release_branch(version: str) -> None:
    branch = f"rel/v{version}"
    existing = run_out(["git", "branch", "--list", branch])
    if existing:
        die(
            f"branch {branch!r} already exists locally. "
            f"To start over: `git checkout -f main && git branch -D {branch}`"
        )
    run(["git", "checkout", "-b", branch])
    ok(f"on branch {branch}")


def commit_release(version: str) -> None:
    """Stage the release files and commit as ``Release v<version>``."""
    files = ["pyproject.toml", "uv.lock", "CHANGELOG.md"]
    run(["git", "add", *files])
    status = run_out(["git", "status", "--porcelain", *files])
    if not status:
        die("no release-bump changes to commit. Did the version bump steps run?")
    run(["git", "commit", "-m", f"Release v{version}"])
    ok(f"release commit created (Release v{version})")


# ── main ─────────────────────────────────────────────────────────────────────────


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="New version, e.g. 0.2.0 (no leading 'v')")
    args = parser.parse_args()

    version = args.version
    validate_version(version)

    info(f"Preparing release v{version}")
    total = 6

    def step(n: int, label: str) -> None:
        info(f"[{n}/{total}] {label}")

    step(1, "Checking environment, git state, and lint...")
    assert_tool_available("git")
    assert_tool_available("uv")
    assert_tool_available("uvx")
    assert_tool_available("make")
    assert_on_main()
    assert_clean_tree()
    assert_main_in_sync_with_origin()
    assert_tag_does_not_exist(version)
    assert_zero_lint()
    ok("git state is clean and ready")

    step(2, "Cutting release branch...")
    cut_release_branch(version)

    step(3, "Bumping version + refreshing lock...")
    bump_pyproject(version)
    refresh_lock()

    step(4, "Running full gate (pytest + C suite)...")
    run_gate()

    step(5, "Inserting CHANGELOG stub...")
    insert_changelog_stub(version)

    step(6, "Committing release...")
    commit_release(version)

    # ── Done ───────────────────────────────────────────────────────────────────
    print()
    ok(f"Release v{version} prepped on branch rel/v{version}")
    print()
    info("Next steps (see scripts/RELEASE.md):")
    print("  1. Review the diff:        git log -p main..HEAD")
    print("  2. Rewrite CHANGELOG.md    (the stub has a TODO marker + raw bullets)")
    print("  3. Amend the commit:       git add CHANGELOG.md && git commit --amend --no-edit")
    print("  4. Publish:                python scripts/release_publish.py --yes")
    print()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        die("aborted by user", code=130)
