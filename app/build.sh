#!/bin/bash
set -eu
# Requires licensed QNX SDP 8.0 with qnxsdp-env.sh sourced (sets QNX_HOST/QNX_TARGET).
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/app"
qcc -Vgcc_ntox86_64 -std=c11 -D_QNX_SOURCE -O2 -Wall -Wextra -Werror \
  runtime.c motion.c contract.c -lm -lsecpol -o sl
