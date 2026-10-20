#!/usr/bin/env bash
# Cross-build the Windows x64 executable from Linux with llvm-mingw.
#   LLVM_MINGW_ROOT=/path/to/llvm-mingw bash tools/build-windows.sh
# Output: release-windows/RockNRollRacingSNESRecomp-windows-<version>-x64.zip
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_NAME="RockNRollRacingSNESRecomp"
BUILD="${BUILD:-$REPO/build-windows}"
OUT="$REPO/release-windows"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
VERSION="$(tr -d '[:space:]' < "$REPO/VERSION")"
: "${LLVM_MINGW_ROOT:?set LLVM_MINGW_ROOT to the unpacked llvm-mingw directory}"
[ -n "$(ls -A "$REPO/src/gen" 2>/dev/null)" ] || { echo "src/gen is empty; run bash tools/regen.sh --rom <rom> first" >&2; exit 1; }

echo "[1/3] configure"
. "$REPO/tools/buildflags.sh"
# ELF-only switches are dropped: PIE, TLS dialect, libmvec and the lld LTO knobs.
WIN_CFLAGS="-O3 -march=$ARCHITECTURE_LEVEL -mtune=generic -fno-semantic-interposition -fno-trapping-math -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -fno-ident -Wno-unused-command-line-argument"
WIN_CXXFLAGS="$WIN_CFLAGS -fvisibility-inlines-hidden"
WIN_LDFLAGS="-Wl,--gc-sections -Wl,-O2 -Wl,--strip-all -Wl,--icf=safe"
cmake -S "$REPO" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$REPO/tools/cmake/mingw-w64-x86_64.cmake" \
  -DLLVM_MINGW_ROOT="$LLVM_MINGW_ROOT" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="$WIN_CFLAGS" \
  -DCMAKE_CXX_FLAGS="$WIN_CXXFLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$WIN_LDFLAGS" \
  -DSNESRECOMP_BUILD_VERSION="$VERSION" \
  -DSNESRECOMP_STATIC_RUNTIME=ON \
  -DSNESRECOMP_SDL_BACKEND=SDL3 \
  -DSNESRECOMP_FRAME_IMPL=LLE \
  ${SNESRECOMP_SDL3_SOURCE_DIR:+-DSNESRECOMP_SDL3_SOURCE_DIR="$SNESRECOMP_SDL3_SOURCE_DIR"}

echo "[2/3] build"
cmake --build "$BUILD" --target "$APP_NAME" -j"$JOBS"
EXE="$BUILD/$APP_NAME.exe"
[ -f "$EXE" ] || { echo "no executable at $EXE" >&2; exit 1; }

echo "[3/3] package"
mkdir -p "$OUT"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PKG="$STAGE/$APP_NAME"
mkdir -p "$PKG"
cp "$EXE" "$PKG/"
for d in assets mods launcher_assets; do
  [ -d "$BUILD/$d" ] && cp -r "$BUILD/$d" "$PKG/"
done
for f in "$BUILD"/*.dll; do [ -f "$f" ] && cp "$f" "$PKG/"; done
cp "$REPO/README.md" "$PKG/"
[ -d "$REPO/licenses" ] && cp -r "$REPO/licenses" "$PKG/"
ZIP="$OUT/$APP_NAME-windows-$VERSION-x64.zip"
rm -f "$ZIP"
(cd "$STAGE" && zip -qr "$ZIP" "$APP_NAME")
ls -la "$ZIP"
