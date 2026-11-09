#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
mkdir -p reports
name="run-$(date +%s)"
python3 scripts/run-target.py "$name" scripts/acceptance.commands
python3 scripts/parse_report.py "reports/$name.log"
echo "Report: $ROOT/reports/$name.json"
