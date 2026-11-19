#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# FM-1 Battle: build the renderer (tools/fm1_render.c) for the render worker (render/ in the monorepo).
#   sh tools/fm1_render.sh       (from the repo root, after ./build.sh)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host/render
${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/host/render/fm1_render tools/fm1_render.c -lm
echo "build/host/render/fm1_render"
