# Releasing crclink

This is the release runbook. It is written to be followed by a human or by an AI assistant (e.g. Claude Code) driving the two release scripts. The only step that needs judgement, rewriting the changelog into a user-facing summary, is called out explicitly.

crclink is pure Python: one `uv build` produces the universal `py3-none-any` wheel. This dev box prepares the source and pushes the tag; CI builds and uploads.

## Mental model

Two scripts, one human-in-the-loop gap between them:

```text
release_prep.py 0.2.0        # local: bump, refresh lock, test, CHANGELOG stub, commit
        |
        v
 (you / AI rewrite CHANGELOG.md, amend the commit)
        |
        v
release_publish.py --yes     # local: merge, tag, push  ->  CI builds + uploads to PyPI
```

- **prep** does everything that must happen before a tag exists and is fully reversible (just delete the branch).
- **publish** does the irreversible things (merge to main, tag, push) and hands off to CI for the actual PyPI upload.

## One-time setup: PyPI trusted publisher

Publishing uses OIDC Trusted Publishing (no API token in repo secrets). PyPI ties a trusted publisher to a specific workflow filename. Publishing lives in `publish.yml`.

Confirm the trusted publisher on <https://pypi.org/manage/project/crclink/settings/publishing/> lists:

| Field             | Value           |
| ----------------- | --------------- |
| Owner             | `hucker`        |
| Repository        | `crclink`       |
| Workflow filename | `publish.yml`   |
| Environment       | *(leave blank)* |

Symptom of getting this wrong: the CI publish step fails with *"not a trusted publisher"* after the build job goes green.

## Preconditions

- On `main`, clean tree, in sync with `origin/main`.
- `ruff` clean (the prep script hard-gates this).
- `git`, `uv`, `uvx`, `make`, and `gh` (authenticated) on PATH.
- Both suites pass locally (prep runs them): the Python suite (`pytest`) and the C firmware Unity suite (`make -C src/c/test test`, needs a C compiler).

## Step 1 — prep

```bash
python scripts/release_prep.py 0.2.0      # bare version, no leading 'v'
```

This cuts `rel/v0.2.0`, bumps `pyproject.toml`, refreshes `uv.lock`, runs both suites, refreshes the README badges (`scripts/update_badges.py`: the tox matrix, the C Unity suite, and ruff/ty), and inserts a CHANGELOG stub. It stops on the release branch with the release commit already made.

If it fails partway, discard the partial bump and retry:

```bash
git checkout -f main && git branch -D rel/v0.2.0
```

## Step 2 — rewrite the CHANGELOG (the human / AI step)

`release_prep.py` seeds `CHANGELOG.md` with a `## v0.2.0 — <date>` section containing a `<!-- TODO ... -->` marker and the raw `git log` subjects since the last tag.

Rewrite that section into a user-facing summary:

- Group by what the user sees: new framing behavior, CLI changes, fixes, C-side changes.
- Delete the `<!-- TODO ... -->` comment and the raw bullet list.
- `release_publish.py` refuses to run while any `TODO` remains, so this is enforced, not optional.

Then fold the edit into the release commit:

```bash
git add CHANGELOG.md
git commit --amend --no-edit
```

Review the whole diff before publishing:

```bash
git log -p main..HEAD
```

## Step 3 — publish

```bash
python scripts/release_publish.py --yes
```

This merges `rel/v0.2.0` into `main` with `--no-ff`, tags `v0.2.0`, pushes `main` + tag + release branch, and creates the GitHub release with notes pulled from the CHANGELOG section.

**It does not build or upload anything to PyPI.** Pushing the `v0.2.0` tag triggers `.github/workflows/publish.yml`, which builds the sdist + wheel with `uv` and uploads them via Trusted Publishing.

## Step 4 — watch CI finish

```bash
gh run watch          # or: Actions tab -> Publish to PyPI
```

The run builds the sdist + wheel, runs the suite once more as a gate, then the `publish` job uploads to PyPI. Confirm:

```bash
gh release view v0.2.0 --web
open https://pypi.org/project/crclink/0.2.0/
```

A smoke install on a clean machine is the final proof:

```bash
uvx --from crclink==0.2.0 crclink --help
```

## If something goes wrong

- **Prep failed:** `git checkout -f main && git branch -D rel/v<version>`, fix, re-run prep. Nothing was pushed; nothing to undo.
- **Publish failed before the tag push:** you are on `main` (merge done) or still on the rel branch. `git log --oneline -5` to see where you are; inspect before re-running.
- **Publish pushed the tag but CI's publish step failed:** the tag and GH release exist; do not re-tag. Fix the CI/PyPI issue (most commonly the trusted-publisher filename above), then re-run the failed `publish.yml` run from the Actions tab. The publish step passes `--check-url`, so a re-run skips files already on PyPI and uploads only what is missing. PyPI never lets you overwrite an existing file, so if a build is genuinely broken, bump to a new patch version.
