# Split Loop demo v2 — raw capture notes

**Not a medical device; not certified; not a patient-treatment product. Simulated plant on QEMU. No hardware timing claims.**

Captured Mon Nov 9 2026, about 12:20 PM EST (guest boot reported as `Nov 09 17:20:31 UTC 2026`). Raw material only: no final edit, nothing uploaded or published.

## Clips

| File | Duration | What it is |
|---|---|---|
| `live-session-02-terminal-and-live-schematic.mp4` | 55.43 s, 1920×1080, 30 fps, H.264, no audio, 26.4 MB | **Real-time** render of a single unedited live serial take. Left: terminal (DejaVu Sans Mono 22 px, dark). Right: schematic driven **only** by the guest's own `T,` telemetry lines in that same serial stream, drawn when each line arrived. Nothing in the clip is sped up. |
| `race-r01-vs-r00-replay.mp4` | 12.83 s, 1920×1080, 30 fps, 0.48 MB | **Replay of recorded samples** `acceptance-r01-planner-none-0` vs `acceptance-r00-baseline-none-0` from `reports/acceptance.json`, side by side, real-time pacing (each frame shows the latest sample whose CLOCK_MONOTONIC offset ≤ wall time; no interpolation). Header says "Replay"; both run ids are on screen. 1.5 s lead-in. |
| `live-session-02.cast` | 55.4 s | asciinema v2 cast: every serial output chunk (`o`) and every driver keystroke (`i`) with host-monotonic timestamps. Play it with `asciinema play`. |
| `live-session-02.serial.log` | — | Byte-exact serial output. |
| `live-session-02.meta.json`, `live-session-02.steps.json` | — | QEMU argv, artifact SHA-256s (app/sl, policy, ifs.bin), the step script and timestamped marks. |
| `live-session-02-parsed.json` | — | Live numbers parsed by the existing `reports/parse_report.py` (nearest-rank percentiles). |

Failed and exploratory takes are kept in `../work/failed-takes/`. explore-00/01/02 found that `which` and `uname` are missing, that ksh **cannot create pipes** under the enforcing policy, and that job control needs `set +m`. live-session-01 failed on a detection regex: the prompt was prefixed to the `ROLES` line. No take was edited. The renderers are in `../render/`, the capture driver is `../capture.py`, and the venv is `.venv-capture (local)` (pyte, pillow).

**Input disclosure:** commands were typed into the serial console by the capture driver (`capture.py`) in real time at 25–40 characters/s, not by a human. The advisor PID for `kill -9` was read live from the `pidin arg` output in the same session.

## Session chapter timecodes (clip time = cast time)

| Time | Chapter | Exact guest command |
|---|---|---|
| 0.0–3.6 s | QEMU TCG boot → `Startup complete`, banner `QNX qnx-smoke 8.0.0 … x86pc x86_64` (real time; boot is 3.6 s, so no speed-up was needed) | — |
| 5.3 s | | `set +m` |
| 6.6–13.9 s | Kernel/CPU info | `/proc/boot/toybox uname -a; pidin info` |
| 14.0–21.9 s | Live legal transfer | `echo RUN_ID=live-01-legal; /proc/boot/sl run planner none 0 0 14; echo DONE_01` |
| 21.95 s | Isolated transfer started in background | `echo RUN_ID=live-02-advisor-kill; /proc/boot/sl run planner none 0 0 14 &` |
| 22.39 s | Process listing mid-transfer: `sl joint`, `sl advice`, `sl watchdog`, `sl advisor 0`, `sl telemetry`, `sl control 0` | `pidin arg` |
| 22.80 s | Advisor killed mid-transfer (around telemetry tick 29–59, about 0.3–0.6 s into a 3 s transfer) | `kill -9 712725` |
| about 23.4 s | Guest prints `ADVISOR_RESTART,old=712725,new=725013`. Only the advisor was restarted; the control, joint and advice PIDs are unchanged. | — |
| 26.5 s | | `wait; echo DONE_02` |
| 32.3–43.0 s | Isolated advisor hang | `echo RUN_ID=live-03-isolated-hang; /proc/boot/sl run planner hang 0 0 14; echo DONE_03` |
| 43.0–53.2 s | Same hang, naive synchronous `MsgSend` | `echo RUN_ID=live-04-naive-hang; /proc/boot/sl run planner hang 1 0 14; echo DONE_04` |
| 53.2–55.4 s | OS policy test | `echo RUN_ID=live-05-secpol; /proc/boot/sl policytest; echo DONE_05` |

Each `RUN_ID=` line is printed by the guest shell itself and shown in the schematic header. The run ids are therefore visible in both the shell and the schematic.

Capture/render scripts and mp4/cast files are kept in the working tree under `proof/demo-v2/` and are not part of this public package. QEMU argv matched acceptance: `qemu-system-x86_64 -accel tcg -cpu max -smp 1 -m 512M` on the same disposable IFS/disk (hashes in the working-tree meta.json). Nothing was rebuilt for the live take.


## Live numbers vs acceptance.json (live values are from this take, never substituted)

| Case | Live (live-session-02) | acceptance.json |
|---|---|---|
| Legal transfer, cycle incl. settle | **2.960191256 s**, DOCK, 0 misses, 326 samples, ACCEPT only, 0 keep-out / 0 jerk breaches | r01: 2.959989901 s, 0 misses |
| Wake lateness p50/p95/p99/max (legal) | 0.327 / 0.513 / 0.733 / 2.475 ms | r01: 0.327 / 0.397 / 0.527 / 0.584 ms |
| Advisor `kill -9` mid-transfer (isolated) | **DOCK, 2.960044521 s, 0 missed periods, 1 advisor restart**, reasons ACCEPT only (no fallback triggered), lateness max 9.164 ms | No identical case. The nearest is r10 (internal crash fault): DOCK, 1 restart, 0 misses |
| Isolated hang | HOME, 0 misses, 1 restart, 424 samples, lateness max 9.026 ms | r09: HOME, 0 misses, 1 restart, 424 samples |
| Naive hang | **HOME, 21 missed periods** (20 consecutive, ticks 69–88, after one blocking ask, plus one isolated late tick 163), max blocking ask **190.876 ms**, max command completion 197.735 ms, lateness p99/max 148.919 / 188.428 ms, 1 restart | r11: 19 misses, ask 186.795 ms, completion 192.143 ms |
| Policy | control `fd=3 errno=0 mode=0666 fstat_ok=1 OPEN_ALLOWED`; advisor `fd=-1 errno=1 Operation not permitted`; `POLICY_RESULT control_ok=1 advisor_denied=1` | identical |
| Earlier failed take live-session-01 (not used) | legal run 2.960051418 s with **2 misses**; the background run finished before the kill step (detection bug). Retained in failed-takes. | — |

The race replay shows the stored r01/r00 numbers (2.960 s vs 7.990 s), labelled as a replay.

## Review answers

**1. Does the naive stall cause a visible plant event?** No. The live run (live-04) and r11 show the same pattern. One synchronous ask blocks control for 190.9 ms live (186.8 ms in r11) at tick 69, during the FALLBACK_NO_ADVICE recovery. The following 19 periods (ticks 70–88) wake late, from 188.4 ms down to about 14 ms, as the loop catches up with its absolute deadlines. Over the whole run: **0 collision samples, 0 jerk-breach samples, max |jerk| 1277 mm/s³ < 1500, max per-axis speed 123 mm/s < 150**, and the run finishes at HOME (250.4, 180.2). There is no splash, overshoot or keep-out entry. That is partly a property of the model, not proof of safety: the simulated plant advances one 10 ms step per executed control iteration, so **the arm does not move during the stall**, then takes the missed steps back to back. A real arm would keep coasting at about 140 mm/s for about 190 ms (about 27 mm) with no new command. The simulator does not model that, so the edit must not imply a consequence. Honest render: miss counter, plus the arm frozen during the stall. In the live clip the 10 Hz telemetry shows exactly this: no T line arrives during the stall.

**2. Live schematic from the guest?** Done the quick way. The schematic panel in the session clip is driven by the guest's own `T,` telemetry lines, read from the **same serial stream** as the shell, at their recorded arrival times. Shell and schematic share one clock, so `ADVISOR_RESTART` and the kill appear in the same second in both. It is a post-capture render of the live stream, not a second live screen. It is 10 Hz and lossy, so the S-sample dump at the end of each run is not used. No network/virtio channel was built; guest networking is not validated. The race clip is a separately labelled **replay** of r00/r01, with run ids on screen.

**3. What happens after the advisor is killed?** Code (`app/runtime.c` `watchdog()` and `control()`):
- The watchdog polls every 10 ms. `waitpid(WNOHANG)` sees the dead advisor and respawns only `sl advisor 1`. Control, joint and advice are untouched.
- Control keeps using the last advice snapshot until its expiry (80 ms). If a fresh proposal from the restarted advisor arrives before expiry, the transfer continues to the **DOCK on the planner profile**; that is what happened live (DOCK 2.960 s, ACCEPT only, 0 misses).
- If the advice expires first, control latches `FALLBACK_NO_ADVICE` and runs a checked quintic recovery to **HOME**. That is permanent for the run, so it never resumes toward the dock (r09, live-03 hang).
- The code has no "finish on the Safe-via baseline" path.

Estimate to add one: about 4–8 h. It needs a checked joining profile from an arbitrary mid-transfer state onto the Safe-via path (keep-out sweep, v/a/j caps, settle), host tests across phases like the 213-HOME sweep, an image rebuild, then a rerun of the 16-run acceptance suite (about 90 s) plus review. Not started, per instructions.

## Caveats

QEMU TCG guest CLOCK_MONOTONIC measurements on a shared host. Not WCET; not a hardware real-time guarantee. Serial output volume (the S dump) is real and scrolls quickly. Guest SSH/network warnings at boot are known and unused.
