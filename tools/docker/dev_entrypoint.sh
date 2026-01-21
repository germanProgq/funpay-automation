#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${FPV_ROOT_DIR:-/workspace}"
BUILD_DIR="${FPV_BUILD_DIR:-${ROOT_DIR}/build-docker}"
GENERATOR="${FPV_CMAKE_GENERATOR:-Ninja}"
BUILD_TYPE="${FPV_BUILD_TYPE:-Debug}"
TARGETS="${FPV_TARGETS:-fpv_app valorant_client}"

configure() {
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
}

build_targets() {
  for target in $TARGETS; do
    cmake --build "$BUILD_DIR" --target "$target"
  done
}

watch_files() {
  find "$ROOT_DIR" \
    -path "$ROOT_DIR/.git" -prune -o \
    -path "$ROOT_DIR/build*" -prune -o \
    -type f \( \
      -name '*.c' -o \
      -name '*.h' -o \
      -name '*.cpp' -o \
      -name '*.hpp' -o \
      -name '*.cc' -o \
      -name '*.cxx' -o \
      -name 'CMakeLists.txt' -o \
      -name '*.cmake' \
    \) -print
}

if [[ "${FPV_WATCH:-1}" != "1" ]]; then
  configure
  build_targets
  exit 0
fi

configure
build_targets

watch_files | entr -r env \
  FPV_WATCH=0 \
  FPV_ROOT_DIR="$ROOT_DIR" \
  FPV_BUILD_DIR="$BUILD_DIR" \
  FPV_CMAKE_GENERATOR="$GENERATOR" \
  FPV_BUILD_TYPE="$BUILD_TYPE" \
  FPV_TARGETS="$TARGETS" \
  /usr/local/bin/fpv-dev
