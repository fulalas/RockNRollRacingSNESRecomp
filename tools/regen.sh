#!/usr/bin/env bash
# Regenerate src/gen/*.c for Rock N' Roll Racing from a verified ROM.
#
# Generated C is derived from copyrighted ROM data and is never committed;
# every developer regenerates from their own copy. The ROM digests come from
# rom_identity.txt, which the build and the release workflow read too, so
# there is one place to change when a revision changes.
#
# Flags:
#   --rom <path>  ROM to generate from. Defaults to SNESRECOMP_ROM, a known
#                 filename at the repo root, then the path in rom.cfg (which
#                 the setup wizard writes). The ROM does not have to live in
#                 the repo — keeping it on your own drive is the better
#                 habit, and SNESRECOMP_ROM sets it once for a shell.
#   --no-verify   skip the ROM digest check (for a revision this project has
#                 not been pinned to yet — expect the generated C to differ)
#   --cfg-roots   seed analysis from every func declaration in recomp/*.cfg
#   -h|--help     this message
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

VERIFY=1
CFG_ROOTS=0
ROM=""
while [ $# -gt 0 ]; do
  case "$1" in
    --rom) ROM=$2; shift 2 ;;
    --no-verify) VERIFY=0; shift ;;
    --cfg-roots) CFG_ROOTS=1; shift ;;
    -h|--help) sed -n '2,/^set -euo/p' "$0" | sed -n '/^# /p' | sed 's/^# //'; exit 0 ;;
    *) echo "regen.sh: unknown flag: $1 (try --help)" >&2; exit 2 ;;
  esac
done

IDENTITY_FILE="rom_identity.txt"
CFG_DIR="recomp"
OUT_DIR="src/gen"
ROM_NAME="Rock N' Roll Racing (U).smc"

bash "$ROOT/tools/apply_patches.sh"
SNESRECOMP_ROOT="${SNESRECOMP_ROOT:-snesrecomp}"
CLI="$SNESRECOMP_ROOT/snesrecomp_cli.py"
if [ ! -f "$CLI" ]; then
  echo "regen.sh: $CLI missing — run: git submodule update --init --recursive" >&2
  exit 1
fi

PYTHON=${PYTHON:-}
if [ -z "$PYTHON" ]; then
  # Windows App Execution Aliases can exist on PATH without running Python.
  for _snes_python in python3 python; do
    if "$_snes_python" -c 'import sys; sys.exit(sys.version_info[0] != 3)' >/dev/null 2>&1; then
      PYTHON=$_snes_python
      break
    fi
  done
fi
if [ -z "$PYTHON" ]; then
  echo "regen.sh: no python3 on PATH" >&2
  exit 1
fi

# The framework parses rom_identity.txt (the same parser CI uses), so this
# script carries no copy of a digest to forget on a revision bump.
IDENTITY="$SNESRECOMP_ROOT/tools/rom_identity.py"
EXPECTED_CRC32="${SNESRECOMP_EXPECTED_CRC32:-$("$PYTHON" "$IDENTITY" "$ROOT/$IDENTITY_FILE" --get expected_crc32)}"
EXPECTED_SHA256="${SNESRECOMP_EXPECTED_SHA256:-$("$PYTHON" "$IDENTITY" "$ROOT/$IDENTITY_FILE" --get expected_sha256)}"

# The framework owns the search: --rom, then SNESRECOMP_ROM, then a known
# name at the root, then rom.cfg (the path the setup wizard recorded).
RESOLVE=(resolve-rom --project-root "$ROOT"
         --name "$ROM_NAME" --name "rocknrollracing.sfc" --name "rocknrollracing.smc")
if [ -n "$ROM" ]; then RESOLVE+=(--rom "$ROM"); fi
if ! ROM=$("$PYTHON" "$CLI" "${RESOLVE[@]}"); then
  echo "regen.sh: pass --rom \"/path/to/Rock N' Roll Racing (U).smc\", set SNESRECOMP_ROM, put it" >&2
  echo "          at the repo root, or record its path in rom.cfg. You must" >&2
  echo "          legally own a copy of Rock N' Roll Racing." >&2
  exit 1
fi
ROM=${ROM%$'\r'}  # Windows Python ends lines with CRLF

if [ "$VERIFY" -eq 1 ]; then
  echo "== Verifying $ROM (whole file) =="
  "$PYTHON" - "$ROM" "$EXPECTED_SHA256" <<'PY'
import hashlib, sys
got = hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest()
if got != sys.argv[2].lower():
    sys.exit(f"ROM SHA-256 mismatch: got {got}, want {sys.argv[2]}")
PY
else
  echo "== Skipping ROM verification (--no-verify) =="
fi

PATCHED="$(mktemp --suffix=.sfc)"
PROFILE_TMP="$(mktemp --suffix=.json)"
trap 'rm -f "$PATCHED" "$PROFILE_TMP"' EXIT
"$PYTHON" "$ROOT/tools/gen_fps30_patch.py" --roms "$ROM" "${RNRR_FPS30_REF:-$ROOT/roms/ROCK30.smc}" --apply "$ROM" "$PATCHED"
"$PYTHON" "$ROOT/tools/patch_rom.py" "$PATCHED" "$PATCHED"

EMIT_ARGS=(--rom "$PATCHED" --cfg-dir "$CFG_DIR" --out-dir "$OUT_DIR" --cfg-roots --no-host-root-scan)
if [ -f "$CFG_DIR/tier2_coverage.json" ]; then
  "$PYTHON" "$ROOT/tools/rebind_profile.py" "$CFG_DIR/tier2_coverage.json" "$PROFILE_TMP" "$PATCHED"
  EMIT_ARGS+=(--historical-profile-manifest "$PROFILE_TMP")
fi

echo "== Generating $OUT_DIR =="
. "$ROOT/tools/buildflags.sh"
"$PYTHON" "$SNESRECOMP_ROOT/tools/build_native_analyzer.py" >/dev/null
"$PYTHON" "$SNESRECOMP_ROOT/tools/v2_emit.py" "${EMIT_ARGS[@]}"
"$PYTHON" "$ROOT/tools/apply_overrides.py" --gen-dir "$OUT_DIR"
"$PYTHON" "$SNESRECOMP_ROOT/tools/v2_sync_funcs_h.py" --cfg-dir "$CFG_DIR" --out "$CFG_DIR/funcs.h"

echo
echo "Done. Build with:"
echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"
