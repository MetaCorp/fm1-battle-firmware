#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# FM-1 Battle: the battle mode's host test (tests/battle_test.c) under AddressSanitizer + UBSan, as editor_fuzz.sh.
#   sh tests/battle_test.sh [ITER]     (from the repo root, after ./build.sh)
set -e
cd "$(dirname "$0")/.."
OUT=build/host/battle
mkdir -p "$OUT/gen"
{ echo '#define SMP_DATA (smp_data_v)'; sed 's/^static const uint8_t SMP_DATA\[/static const uint8_t SMP_DATA_ARR[/' build/gen/felucca_samples.h;
  echo 'static const uint8_t *volatile smp_data_v = SMP_DATA_ARR;'; } > "$OUT/gen/felucca_samples.h"
${CC:-cc} -O1 -g -w -fsanitize=address,undefined -fno-sanitize=shift-base,signed-integer-overflow -fno-sanitize-recover=all \
    -I"$OUT/gen" -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/battle_test" tests/battle_test.c -lm
"$OUT/battle_test" "${1:-3000}"
