#!/bin/bash
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/app"
qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra \
  baseline.c motion.c -lm -o sl-baseline
