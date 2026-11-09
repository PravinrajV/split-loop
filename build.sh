#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
python3 - <<'CHECK'
import shutil,sys
if shutil.disk_usage('.').free < 4*1024**3:
    sys.exit('Need >=4GiB free before image build')
CHECK
if [ -z "${QNX_HOST:-}" ] || [ -z "${QNX_TARGET:-}" ]; then
  if [ -f /home/box/qnx800/qnxsdp-env.sh ]; then
    # shellcheck disable=SC1091
    source /home/box/qnx800/qnxsdp-env.sh
  else
    echo "Source your QNX SDP 8.0 qnxsdp-env.sh first (sets QNX_HOST/QNX_TARGET)." >&2
    exit 1
  fi
fi
"$ROOT/app/build-baseline.sh"
"$ROOT/app/build.sh"
"$ROOT/scripts/build-image.sh"
