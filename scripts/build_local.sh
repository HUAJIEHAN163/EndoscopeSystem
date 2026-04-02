#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$PROJECT_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_V4L2=ON

make -j$(nproc)

echo ""
echo "构建完成: $BUILD_DIR/endoscope"
