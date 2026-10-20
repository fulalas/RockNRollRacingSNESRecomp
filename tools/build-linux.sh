#!/usr/bin/env bash
set -euo pipefail

APP_NAME="RockNRollRacingSNESRecomp"
ICON_NAME="rocknrollracingsnesrecomp"
ROM_EXTS="sfc smc"

LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
APPIMAGETOOL_URL=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage

REPO="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(tr -d '\r\n' < "$REPO/VERSION")"
BUILD=""
OUT="$REPO/release-linux"
JOBS="$(nproc 2>/dev/null || echo 4)"
TOOLS_DIR="${APPIMAGE_TOOLS_DIR:-}"

while [ $# -gt 0 ]; do
  case "$1" in
    --version) VERSION="$2"; shift 2;;
    --build) BUILD="$2"; shift 2;;
    --out) OUT="$2"; shift 2;;
    --jobs) JOBS="$2"; shift 2;;
    --tools-dir) TOOLS_DIR="$2"; shift 2;;
    -h|--help) sed -n '1,30p' "$0"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done

GEN_DIR="$REPO/src/gen"
SUFFIX=""
[ -n "$BUILD" ] || BUILD="$REPO/build-linux-prod$SUFFIX"
[ -n "$TOOLS_DIR" ] || TOOLS_DIR="$BUILD/appimage-tools"
[ -d "$GEN_DIR" ] && ls "$GEN_DIR"/*.c >/dev/null 2>&1 || {
  echo "$GEN_DIR is empty; run bash tools/regen.sh --rom /path/to/rom first" >&2
  exit 1
}
[ -f "$REPO/snesrecomp/runner/runner.cmake" ] || { echo "snesrecomp submodule missing" >&2; exit 1; }
[ -f "$REPO/recomp-ui/recomp_ui.cmake" ] || { echo "recomp-ui submodule missing" >&2; exit 1; }

echo "[1/4] configure"
. "$REPO/tools/buildflags.sh"
cmake -S "$REPO" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="$BUILD_C_FLAGS" \
  -DCMAKE_CXX_FLAGS="$BUILD_CXX_FLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$BUILD_LD_FLAGS" \
  -DSNESRECOMP_BUILD_VERSION="$VERSION" \
  -DSNESRECOMP_SDL_BACKEND="${SNESRECOMP_SDL_BACKEND:-SDL3}"

echo "[2/4] build"
cmake --build "$BUILD" --target "$APP_NAME" -j"$JOBS"
BIN="$BUILD/$APP_NAME"
[ -x "$BIN" ] || { echo "no executable at $BIN" >&2; exit 1; }

echo "[3/4] package AppImage"
mkdir -p "$OUT" "$TOOLS_DIR"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
APPDIR="$WORK/AppDir"
mkdir -p "$APPDIR"

fetch_tool() {
  local url="$1" dest="$2"
  [ -f "$dest" ] || curl -fL --retry 3 "$url" -o "$dest"
  chmod 0755 "$dest"
}
fetch_tool "$LINUXDEPLOY_URL" "$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
fetch_tool "$APPIMAGETOOL_URL" "$TOOLS_DIR/appimagetool-x86_64.AppImage"
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage --appimage-extract-and-run"
APPIMAGETOOL="$TOOLS_DIR/appimagetool-x86_64.AppImage --appimage-extract-and-run"

ICON="$WORK/$ICON_NAME.png"
cp "$REPO/assets/img/icon.png" "$ICON"
DESKTOP="$WORK/$ICON_NAME.desktop"
cat > "$DESKTOP" <<DESK
[Desktop Entry]
Type=Application
Name=Rock N' Roll Racing SNES Recomp
Exec=$APP_NAME
Icon=$ICON_NAME
Categories=Game;
Terminal=false
DESK

$LINUXDEPLOY --appdir "$APPDIR" --executable "$BIN" --desktop-file "$DESKTOP" --icon-file "$ICON"

if [ -d "$APPDIR/usr/lib" ]; then
  KEEP="$WORK/appdir-keep.txt"
  env -u LD_LIBRARY_PATH ldd "$APPDIR/usr/bin/$APP_NAME" \
    | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^\//) print $i }' \
    | while read -r p; do readlink -f "$p" 2>/dev/null || true; done \
    | sort -u > "$KEEP"
  for f in "$APPDIR"/usr/lib/*; do
    [ -e "$f" ] || continue
    if ! grep -qxF "$(readlink -f "$f")" "$KEEP"; then
      echo "  prune unreachable bundled lib: $(basename "$f")"
      rm -f "$f"
    fi
  done
  if env -u LD_LIBRARY_PATH ldd "$APPDIR/usr/bin/$APP_NAME" | grep -q 'not found'; then
    echo "binary has unresolved libraries without LD_LIBRARY_PATH" >&2
    env -u LD_LIBRARY_PATH ldd "$APPDIR/usr/bin/$APP_NAME" | grep 'not found' >&2
    exit 1
  fi
fi

[ -d "$BUILD/assets" ] || { echo "launcher assets missing beside $BIN" >&2; exit 1; }
cp -r "$BUILD/assets" "$APPDIR/usr/bin/assets"
[ -d "$BUILD/mods" ] && cp -r "$BUILD/mods" "$APPDIR/usr/bin/mods"
rm -f "$APPDIR/usr/bin/mods/state.toml"
python3 "$REPO/tools/stage_linux_notices.py" "$APPDIR"

rm -f "$APPDIR/AppRun"
cat > "$APPDIR/AppRun" <<APPRUN
#!/bin/sh
HERE="\$(dirname "\$(readlink -f "\$0")")"
export RECOMP_HOST_LD_LIBRARY_PATH="\${LD_LIBRARY_PATH:-}"
export SDL_JOYSTICK_HIDAPI_STEAM=1
export SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1
SELF="\${APPIMAGE:-\$0}"
ROMDIR="\$(dirname "\$(readlink -f "\$SELF")")"

if [ -d "\$HERE/usr/bin/mods/preloaded/packages" ] && [ -w "\$ROMDIR" ]; then
  mkdir -p "\$ROMDIR/mods/preloaded/packages" 2>/dev/null || true
  cp -a "\$HERE/usr/bin/mods/preloaded/packages/." "\$ROMDIR/mods/preloaded/packages/" 2>/dev/null || true
fi

ROM=""
for ext in $ROM_EXTS; do
  for file in "\$ROMDIR"/*."\$ext"; do [ -e "\$file" ] && ROM="\$file" && break 2; done
done
if [ -n "\$ROM" ]; then
  cached=""
  [ -f "\$ROMDIR/rom.cfg" ] && cached="\$(head -n1 "\$ROMDIR/rom.cfg" 2>/dev/null | tr -d '\\r\\n')"
  if [ -z "\$cached" ] || [ ! -f "\$cached" ]; then
    [ -w "\$ROMDIR" ] && printf '%s\\n' "\$ROM" > "\$ROMDIR/rom.cfg" 2>/dev/null || true
  fi
fi

cd "\$ROMDIR" 2>/dev/null || true
exec "\$HERE/usr/bin/$APP_NAME" "\$@"
APPRUN
chmod +x "$APPDIR/AppRun"

APP="$OUT/$APP_NAME$SUFFIX-linux-$VERSION-x86_64.AppImage"
rm -f "$APP"
RUNTIME_ARGS=()
[ -f "$TOOLS_DIR/runtime-x86_64" ] && RUNTIME_ARGS=(--runtime-file "$TOOLS_DIR/runtime-x86_64")
ARCH=x86_64 $APPIMAGETOOL "${RUNTIME_ARGS[@]}" "$APPDIR" "$APP"
chmod +x "$APP"

echo "[4/4] layout check"
test -x "$APPDIR/usr/bin/$APP_NAME"
test -f "$APPDIR/usr/bin/assets/fonts/LatoLatin-Regular.ttf"
test -f "$APPDIR/usr/share/doc/rocknrollracingsnesrecomp/licenses/SDL3.txt"
! grep -Eq '^[[:space:]]*export[[:space:]]+LD_LIBRARY_PATH=' "$APPDIR/AppRun"
if find "$APPDIR" -type f \( -iname '*.sfc' -o -iname '*.smc' \) | grep -q .; then
  echo "ROM data found in AppDir; refusing to ship" >&2; exit 1
fi
ls "$APPDIR/usr/lib" 2>/dev/null || true
sha256sum "$APP"
