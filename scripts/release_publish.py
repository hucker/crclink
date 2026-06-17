"""Release publish: merge release branch to main, tag, push, GitHub release.

Run this after release_prep.py and after you've rewritten CHANGELOG.md into
a real user-facing summary (see scripts/RELEASE.md).

Usage:
    python scripts/release_publish.py --yes

What it does:
    1. Verify on rel/v<version> (parses version from branch name)
    2. Verify working tree clean; tag absent; CHANGELOG has no TODO marker;
       HEAD is the release commit
    3. Require --yes (no interactive prompt)
    4. Checkout main, merge release branch with --no-ff
    5. Create tag v<version>
    6. Push main, tag, and release branch to origin
    7. Create the GitHub release with notes pulled from CHANGELOG.md

This does NOT build or upload to PyPI from your machine. Pushing the
v<version> tag (step 6) triggers .github/workflows/publish.yml, which builds
the sdist + wheel with uv and uploads them via OIDC Trusted Publishing. Watch
that run finish.

Aborts loudly on any failure. Never force-pushes. Never deletes branches.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_common import (  # noqa: E402
    REPO_ROOT,
    assert_clean_tree,
    assert_gh_authenticated,
    assert_tag_does_not_exist,
    assert_tool_available,
    current_branch,
    die,
    info,
    ok,
    run,
    run_out,
    validate_version,
)

RELEASE_BRANCH_RE = re.compile(r"^rel/v(\d+\.\d+\.\d+)$")


def parse_version_from_branch() -> str:
    branch = current_branch()
    m = RELEASE_BRANCH_RE.match(branch)
    if not m:
        die(
            f"must be on a rel/vN.N.N branch, currently on {branch!r}. "
            f"Run release_prep.py first."
        )
    version = m.group(1)
    validate_version(version)
    return version


def assert_no_changelog_todos() -> None:
    path = REPO_ROOT / "CHANGELOG.md"
    text = path.read_text(encoding="utf-8")
    if "<!-- TODO" in text:
        die(
            "CHANGELOG.md still contains the stub's <!-- TODO --> marker. "
            "Rewrite the section (and amend the release commit) before publishing."
        )
    ok("CHANGELOG.md has no TODO markers")


def assert_release_commit_present(version: str) -> None:
    """HEAD should be the release commit (subject starts ``Release v<version>``).

    ``startswith`` (not equality) so an optional descriptive tagline is
    allowed, e.g. ``Release v0.2.0 — text framing fixes``.
    """
    subject = run_out(["git", "log", "-1", "--pretty=%s"])
    expected = f"Release v{version}"
    if not subject.startswith(expected):
        die(
            f"expected HEAD commit subject to start with {expected!r}, got {subject!r}. "
            f"Did you amend after editing CHANGELOG?"
        )
    ok(f"HEAD is {subject!r}")


def extract_changelog_notes(version: str) -> str:
    """Extract this version's section from CHANGELOG.md for the GH release.

    Matches from ``## v<version> ...`` up to the next ``## `` heading or end of
    file. Lenient about what follows the version on the heading line (date,
    em-dash, tagline) so a hand-edited heading still parses.
    """
    path = REPO_ROOT / "CHANGELOG.md"
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        rf"^## v{re.escape(version)}\b[^\n]*\n(.*?)(?=^## |\Z)",
        re.MULTILINE | re.DOTALL,
    )
    m = pattern.search(text)
    if not m:
        die(f"could not find '## v{version} ...' section in CHANGELOG.md")
    notes = m.group(1).strip()
    if not notes:
        die(f"CHANGELOG section for v{version} is empty")
    return notes


def merge_to_main(version: str, release_branch: str) -> None:
    info("Checking out main and merging release branch...")
    run(["git", "checkout", "main"])
    # Re-check sync: someone might have pushed to origin/main during the
    # prep -> review window.
    run(["git", "fetch", "origin", "main"])
    local = run_out(["git", "rev-parse", "main"])
    remote = run_out(["git", "rev-parse", "origin/main"])
    if local != remote:
        die(
            "local 'main' diverged from 'origin/main' since prep ran. "
            "Resolve manually before publishing."
        )
    run(["git", "merge", "--no-ff", release_branch, "-m", f"Merge {release_branch}"])
    ok(f"merged {release_branch} into main")


def create_tag(version: str) -> None:
    tag = f"v{version}"
    run(["git", "tag", tag])
    ok(f"created tag {tag}")


def push_all(version: str, release_branch: str) -> None:
    tag = f"v{version}"
    info("Pushing main, tag, and release branch to origin...")
    run(["git", "push", "origin", "main"])
    # This tag push is what triggers publish.yml to build + upload.
    run(["git", "push", "origin", tag])
    run(["git", "push", "origin", release_branch])
    ok("pushed main, tag, and release branch")


def create_github_release(version: str, notes: str) -> None:
    tag = f"v{version}"
    info(f"Creating GitHub release {tag}...")
    run(["gh", "release", "create", tag, "--title", tag, "--notes", notes])
    ok(f"GitHub release {tag} created")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--yes",
        action="store_true",
        help="Required. Confirms you reviewed the diff and rewrote the "
        "CHANGELOG, and want to merge + tag + push (which triggers the CI "
        "build + PyPI upload) and create the GitHub release.",
    )
    args = parser.parse_args()

    if not args.yes:
        die("refusing to publish without --yes. Review the diff first, then re-run with --yes.")

    info("Checking environment...")
    assert_tool_available("git")
    assert_tool_available("gh")
    assert_gh_authenticated()

    info("Checking branch and tree state...")
    version = parse_version_from_branch()
    release_branch = f"rel/v{version}"
    info(f"Publishing v{version} from {release_branch}")

    assert_clean_tree()
    assert_tag_does_not_exist(version)
    assert_no_changelog_todos()
    assert_release_commit_present(version)

    notes = extract_changelog_notes(version)

    # ── Merge ────────────────────────────────────────────────────────────────
    merge_to_main(version, release_branch)

    # ── Tag ──────────────────────────────────────────────────────────────────
    create_tag(version)

    # ── Push (this triggers the CI build + PyPI upload) ────────────────────────
    push_all(version, release_branch)

    # ── GitHub release ─────────────────────────────────────────────────────────
    # Publishing is triggered by the TAG push above (publish.yml on
    # ``push: tags: v*.*.*``), NOT by this release. So creating the GH release
    # here is purely the notes page -- there is no second publisher and thus no
    # upload race.
    create_github_release(version, notes)

    # ── Done ─────────────────────────────────────────────────────────────────
    print()
    ok(f"v{version} merged, tagged, and pushed")
    print()
    info("CI is now building the wheel + sdist and uploading to PyPI:")
    print("  - Watch it:   gh run watch  (or the Actions tab -> Publish to PyPI)")
    print(f"  - GH release: gh release view v{version} --web")
    print(f"  - PyPI (populates when the run finishes): https://pypi.org/project/crclink/{version}/")
    print("  - You are now on main, at the merge commit; the release branch is preserved.")
    print()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        die("aborted by user", code=130)
