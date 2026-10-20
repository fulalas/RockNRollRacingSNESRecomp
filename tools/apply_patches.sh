#!/usr/bin/env bash
# Apply patches/<submodule>.patch to each submodule checkout. Idempotent:
# an already-applied patch is skipped; one that applies neither way fails.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for sub in snesrecomp recomp-ui; do
  patch="$ROOT/patches/$sub.patch"
  [ -s "$patch" ] || continue
  dir="$ROOT/$sub"
  [ -e "$dir/.git" ] || { echo "apply_patches: $sub is not checked out; run: git submodule update --init --recursive" >&2; exit 1; }
  if git -C "$dir" apply --reverse --check "$patch" 2>/dev/null; then
    continue
  elif git -C "$dir" apply --check "$patch" 2>/dev/null; then
    git -C "$dir" apply "$patch"
    echo "apply_patches: applied patches/$sub.patch"
  else
    echo "apply_patches: patches/$sub.patch does not apply to $sub at $(git -C "$dir" rev-parse --short HEAD)" >&2
    exit 1
  fi
done
