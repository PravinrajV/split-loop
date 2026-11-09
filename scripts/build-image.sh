#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ -z "${QNX_HOST:-}" ] || [ -z "${QNX_TARGET:-}" ]; then
  echo "QNX_HOST/QNX_TARGET unset; source qnxsdp-env.sh first." >&2
  exit 1
fi
MKQNXIMAGE="${MKQNXIMAGE:-$(command -v mkqnximage || true)}"
if [ -z "$MKQNXIMAGE" ]; then
  for c in "$QNX_HOST/usr/bin/mkqnximage" /home/box/qnx800/host/common/bin/mkqnximage; do
    [ -x "$c" ] && MKQNXIMAGE=$c && break
  done
fi
if [ -z "$MKQNXIMAGE" ] || [ ! -x "$MKQNXIMAGE" ]; then
  echo "mkqnximage not found" >&2
  exit 1
fi
if [ ! -x "$ROOT/app/sl" ] || [ ! -x "$ROOT/app/sl-baseline" ]; then
  echo "Build app/sl and app/sl-baseline first (app/build.sh, app/build-baseline.sh)." >&2
  exit 1
fi
WORKDIR="$ROOT/images/split-loop"
mkdir -p "$WORKDIR"
exec 9>"$WORKDIR/.run.lock"
flock -n 9 || { echo "Image is in use; stop your own target session first."; exit 1; }
# Fresh disposable tree via mkqnximage itself when empty; otherwise reuse.
if [ ! -d "$WORKDIR/local" ]; then
  (cd "$WORKDIR" && "$MKQNXIMAGE" --type=qemu --arch=x86_64 --boot-mode=qemu \
    --graphics=no --cpu=1 --ram=512M --sys-size=8 --data-size=16 --ssh-ident=none \
    --secpol=secure --policy="$ROOT/images/split-loop-policy.txt" --build=no || true)
fi
mkdir -p "$WORKDIR/local/snippets"
cp "$ROOT/images/snippets/"*.custom "$WORKDIR/local/snippets/" 2>/dev/null || true
cp "$ROOT/images/snippets/post_start.smoke" "$WORKDIR/local/snippets/"
sed "s|PLACEHOLDER_APP_SL|$ROOT/app/sl|; s|PLACEHOLDER_APP_SL_BASELINE|$ROOT/app/sl-baseline|" \
  "$ROOT/images/snippets/ifs_files.smoke" > "$WORKDIR/local/snippets/ifs_files.smoke"
(cd "$WORKDIR" && "$MKQNXIMAGE" --type=qemu --arch=x86_64 --boot-mode=qemu \
  --graphics=no --cpu=1 --ram=512M --sys-size=8 --data-size=16 --ssh-ident=none \
  --secpol=secure --policy="$ROOT/images/split-loop-policy.txt" --build)
echo "Image built under $WORKDIR/output (do not redistribute proprietary QNX IFS/disk)."
