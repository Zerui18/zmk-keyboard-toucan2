#!/usr/bin/env bash

set -euo pipefail

readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$REPO_ROOT"

[[ "$(git branch --show-current)" == main ]] || {
    printf 'error: switch to main before syncing upstream\n' >&2
    exit 1
}

[[ -z "$(git status --porcelain)" ]] || {
    printf 'error: commit or stash local changes before syncing upstream\n' >&2
    exit 1
}

git remote get-url upstream >/dev/null 2>&1 || {
    printf 'error: no upstream remote is configured\n' >&2
    exit 1
}

printf '==> Fetching upstream\n'
git fetch upstream

printf '==> Merging upstream/main into main\n'
git merge --no-edit upstream/main

printf '==> Upstream sync complete; publish it with: git push origin main\n'

