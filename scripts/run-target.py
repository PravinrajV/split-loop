#!/usr/bin/env python3
"""Drive a disposable Split Loop QEMU guest over serial and capture evidence."""
import subprocess, select, time, sys, os, json, hashlib, fcntl
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
base = ROOT / "reports"
work = ROOT / "images" / "split-loop"
base.mkdir(parents=True, exist_ok=True)

name = sys.argv[1]
session_lock = open(work / ".run.lock", "w")
fcntl.flock(session_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)

paths = [
    "README.md",
    "app/motion.c",
    "app/sl",
    "images/split-loop-policy.txt",
]
hashes = {}
for s in paths:
    fp = ROOT / s
    if fp.exists():
        hashes[s] = hashlib.sha256(fp.read_bytes()).hexdigest()
ifs = work / "output" / "ifs.bin"
if ifs.exists():
    hashes["images/split-loop/output/ifs.bin"] = hashlib.sha256(ifs.read_bytes()).hexdigest()
(base / (name + "-setup.json")).write_text(
    json.dumps({"capture": "Before QEMU start", "hashes": hashes}, indent=2)
)

commands = Path(sys.argv[2]).read_text().splitlines() if len(sys.argv) > 2 else []
cmd = [
    "/usr/bin/qemu-system-x86_64",
    "-accel", "tcg", "-cpu", "max", "-smp", "1", "-m", "512M",
    "-drive", "file=output/disk-qemu.vmdk,format=vmdk,if=ide",
    "-kernel", "output/ifs.bin",
    "-netdev", "user,id=net0",
    "-device", "virtio-net-pci,netdev=net0",
    "-object", "rng-random,filename=/dev/urandom,id=rng0",
    "-device", "virtio-rng-pci,rng=rng0",
    "-display", "none", "-serial", "stdio", "-monitor", "none",
]
(base / (name + "-command.json")).write_text(json.dumps(cmd, indent=2))
logfile = (base / (name + ".log")).open("wb")
p = subprocess.Popen(cmd, cwd=work, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
interactive = os.environ.get("SL_INTERACTIVE") == "1"
events = Path(os.environ.get("SL_EVENTS_FILE", "/tmp/sl-no-events"))
event_pos = 0
finished = False
log = bytearray()
deadline = time.monotonic() + 900
sent = False
last = time.monotonic()
try:
    while time.monotonic() < deadline:
        if select.select([p.stdout], [], [], 0.2)[0]:
            b = os.read(p.stdout.fileno(), 65536)
            if not b:
                break
            log.extend(b)
            last = time.monotonic()
            logfile.write(b)
            logfile.flush()
        if not sent and b"SMOKE_HELLO_END" in log:
            time.sleep(2)
            for command in commands:
                p.stdin.write((command + "\n").encode())
                p.stdin.flush()
                time.sleep(0.25)
            sent = True
        if sent and b"COMMANDS_FINISHED" in log and not finished:
            finished = True
            time.sleep(1)
            break
        if interactive and events.exists():
            data = events.read_bytes()
            if len(data) > event_pos:
                chunk = data[event_pos:]
                event_pos = len(data)
                for line in chunk.splitlines():
                    if line:
                        p.stdin.write(line + b"\n")
                        p.stdin.flush()
        if p.poll() is not None:
            break
        if sent and time.monotonic() - last > 120:
            break
finally:
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()
    logfile.close()
print("Wrote", base / (name + ".log"))
