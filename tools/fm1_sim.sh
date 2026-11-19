#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# FM-1 Battle: build the simulated FM-1 (tools/fm1_sim.c) for the web app and its tests.
#   sh tools/fm1_sim.sh          (from the repo root, after ./build.sh: it needs build/gen)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host/sim
${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/host/sim/fm1_sim tools/fm1_sim.c -lm
echo "build/host/sim/fm1_sim"
