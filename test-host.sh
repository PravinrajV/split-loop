#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
echo 'Not a medical device; not certified; not a patient-treatment product.'
for test in motion contract plant recovery; do
  sources="app/motion.c"
  if [ "$test" != motion ]; then sources="$sources app/contract.c"; fi
  cc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Werror \
    "app/test_${test}.c" $sources -lm -o "app/test-$test"
  "./app/test-$test"
done
python3 -m py_compile ui/server.py scripts/run-target.py scripts/parse_report.py
node --check ui/app.js
echo 'Host tests passed.'
