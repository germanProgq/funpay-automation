#!/usr/bin/env bash
# FunPay Vertex AppImage build helper.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-"$ROOT_DIR/build-appimage"}"
APPDIR="$BUILD_DIR/AppDir"

mkdir -p "$BUILD_DIR"

BUILD_JOBS=1
if command -v nproc >/dev/null 2>&1; then
  BUILD_JOBS="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
  BUILD_JOBS="$(sysctl -n hw.ncpu)"
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DFPV_BUILD_VALORANT_CLIENT=OFF

cmake --build "$BUILD_DIR" -- -j"$BUILD_JOBS"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

install -Dm644 "$ROOT_DIR/packaging/linux/funpay_vertex.desktop" \
  "$APPDIR/usr/share/applications/funpay_vertex.desktop"
install -Dm644 "$ROOT_DIR/packaging/linux/funpay_vertex.svg" \
  "$APPDIR/usr/share/icons/hicolor/scalable/apps/funpay_vertex.svg"

LINUXDEPLOY="${LINUXDEPLOY:-$BUILD_DIR/linuxdeploy-x86_64.AppImage}"
LINUXDEPLOY_GTK="${LINUXDEPLOY_GTK:-$BUILD_DIR/linuxdeploy-plugin-gtk.sh}"

if [ ! -x "$LINUXDEPLOY" ]; then
  curl -L -o "$LINUXDEPLOY" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
  chmod +x "$LINUXDEPLOY"
fi

if [ ! -x "$LINUXDEPLOY_GTK" ]; then
  curl -L -o "$LINUXDEPLOY_GTK" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-gtk/releases/download/continuous/linuxdeploy-plugin-gtk.sh"
  chmod +x "$LINUXDEPLOY_GTK"
fi

"$LINUXDEPLOY" \
  --appdir "$APPDIR" \
  --desktop-file "$APPDIR/usr/share/applications/funpay_vertex.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/funpay_vertex.svg" \
  --plugin gtk \
  --output appimage
