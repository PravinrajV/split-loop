# Split Loop

Claim redesign for the QNX Global Hackathon 2026, Robotics & Medical.
Rewritten November 7, 2026 (America/New_York) after a Tenth Man review.
Claim locked November 7, 2026 after Tenth Man confirmation. GO IF this locked claim is what we actually build and film.
Not submitted. Not a medical device, not certified, and not a patient-treatment product.

## One-sentence pitch

Split Loop is a checkable laboratory cassette transfer on QNX 8.0 QEMU: the arm must move a sealed sample from a rack slot to an analyzer dock without hitting occupied neighbors, the housing, or a jerk limit that would splash the liquid, while an untrusted planner tries to beat a conservative baseline on cycle time and a same-image experiment shows what happens when that planner is coupled naively versus isolated.

## What we claim is new

The physical job stays the same: a two-axis lab shuttle. We do not change it into a warehouse picker, surgical arm, or CNC machine. Those would still be the same architecture with new rectangles.

The isolation architecture is not the novelty claim. Resource managers, a FIFO control loop, an untrusted planner, a watchdog that restarts only the planner, a cockpit, and fault injection are already public:

- [moco / SafeGuard](https://devpost.com/software/gg-6zqrfd) (cuHacking 7, project started July 12, 2026; QNX catalog honorable mention): untrusted planner versus deterministic referee, requested versus executed path, a jerk limiter, kill-the-AI, and an explicit Simplex citation. Their Pi jitter-under-load chart was still listed as future work.
- [QNX-CortexCage](https://devpost.com/software/qnx-cortexcage) (created November 5, 2026; already submitted to this same hackathon): a QEMU resource-manager Simplex-style gate, low-priority neural planner, FIFO actuator loop, watchdog, chaos injection, WebSocket cockpit with 3D trajectory, and author-reported sub-microsecond timing. Public repo: https://github.com/fokrulanthro16-eng/qnx-cortex-cage

Citing Simplex / runtime assurance is honest. It is not a differentiator. Both of those projects already did that. A judge who reads both architecture sections will see twins if we lead with isolation.

What we will sell, and what must be the plot of the under-3-minute video:

1. A named transfer whose failure a judge can see without narration: the cassette enters an occupied slot, hits the analyzer housing, or splashes from excess jerk.
2. A same-image A/B of naive blocking `MsgSend` versus isolated non-blocking advice, with a visible missed period when the planner hangs in naive mode.
3. A command contract that treats stale, replayed, and expired advice as first-class threats, not only crash-the-planner, and emits reason codes.
4. An OS security-policy denial of the advisor opening `/dev/joint`, only if a disposable Everywhere image can compile and enforce that policy. If it cannot, this bullet is dropped from the pitch. An application `if` or a `chmod` is not OS mandatory access control.
5. Timing published as QEMU measurements with raw samples, setup notes, and percentiles. Not a hard-real-time guarantee, not WCET, and not a hardware proof. We will not try to win a microsecond war on QEMU.

Devpost and the video lead with the transfer task and the A/B experiment. The architecture is how we built it, not what we claim to have invented. The write-up will cite CortexCage, moco, and Simplex, and say exactly what we measured that they did not.

## User and job

An instrument technician runs a small lab shuttle that moves a sealed liquid cassette from a numbered rack slot to an analyzer dock.

- Success: the cassette arrives in the dock without entering an occupied neighboring slot, without hitting the analyzer housing, and without exceeding the published simulator jerk limit.
- Visible failure: a near-collision or occupied-slot entry, a splash animation when the simulator's published jerk limit is exceeded (not proof a real cassette would have spilled), a missed cycle, or a hold at a named safe pose (HOME). Splash, collision, and occupied-slot entry must be readable with the sound off.
- Why stale advice matters: a faster path is only useful while it is still current. Old, replayed, or expired advice must not freeze the arm mid-span or yank it through a keep-out.

This is a simulated laboratory-automation job on QEMU, in a market QNX serves (robotics and medical). It is not a real instrument run, not a patient-treatment claim, and not a certified IVD product. The market sentence must not outrun what the simulated shuttle actually shows. Every public sentence (Devpost, README, video cards, narration) keeps the not-a-medical-device, not-certified, not-patient-treatment wording.

## Planner goal

One published goal: shorter cycle time than a conservative baseline, under the same keep-out and jerk constraints, with zero keep-out violations and no jerk-limit breaches.

We drop the dual goal of "faster or smoother." If the planner cannot beat the baseline on that metric, we do not call it AI. A stub or a conservative trapezoid labeled AI is an honesty failure. The planner is a small native profiler or spline on-target, not ONNX, not a camera stack, and not a copy of CortexCage's TinyML MLP.

The threat model we name, because CortexCage and similar demos under-sell it: stale advice, replayed sequence numbers, and expired advice, in addition to planner crash, hang, and malformed payloads.

## Frozen definitions

These numbers and algorithms are locked before the planner or host UI exist. Do not weaken them later to manufacture a planner win.

Coordinate frame: origin at the bottom-left of the table, X right, Y up, units millimeters, time seconds. Table is 500 mm by 400 mm. The cassette is represented by its center point. A keep-out is already inflated, so a collision is the center entering a keep-out.

Rack slots 0 through 3 are 40 mm squares centered at (80 + 70*i, 70). Slot extents are [center_x - 20, center_x + 20] by [50, 90]. An occupied slot's keep-out is that square inflated by 5 mm on every side. Unoccupied slots are not keep-outs.

Analyzer housing keep-out: the rectangle x in [360, 480], y in [300, 390].

Dock pose: (390, 250). Dock acceptance window: 8 mm from the dock pose. HOME pose: (250, 180). Default start is slot 0 at (80, 70). A rack recipe may change the start slot and which slots are occupied; it may not change these geometries, HOME, the dock pose, or the limits below.

Cycle time includes settle.
- The clock starts at the CLOCK_MONOTONIC time of the first control period after Start in which the cassette center is more than 2 mm from the start-slot center.
- The clock stops at the start of the first run of 10 consecutive control periods during which the center stays inside the dock window, speed on each axis is at most 5 mm/s, and acceleration on each axis is at most 50 mm/s^2.
- Cycle time is stop minus start.
- If the run ends in HOME, a collision, a keep-out entry, or any other unsuccessful finish, cycle time is FAIL. That run cannot count as a planner win.

Published motion limits:
- Splash / clamp jerk `j_splash` = 1500 mm/s^3. Crossing it is a simulator splash event, labeled as the published limit being exceeded.
- Command-contract velocity limit = 150 mm/s. Acceleration limit = 400 mm/s^2.
- Starting control period target = 10 ms (100 Hz). A faster period has to be earned by measurement.

Conservative baseline algorithm, named Safe-via sequential trapezoid, frozen before the planner exists:
1. Move in Y only from the start-slot center to Y = 180 mm, holding X.
2. Move in X only to X = 390 mm, holding Y = 180 mm.
3. Move in Y only to Y = 250 mm, holding X = 390 mm.
Each segment is a trapezoidal velocity profile with `v_max` = 80 mm/s, `a_max` = 200 mm/s^2, and `j_max` = 600 mm/s^3. Those values stay strictly below `j_splash`. Do not later raise baseline `v_max`, `a_max`, or `j_max`, shorten the via, or allow a diagonal cut, to help the planner look better.

If the planner cannot beat this frozen baseline on cycle time with zero keep-out violations and no jerk-limit breaches, drop the AI label. Do not weaken the baseline. Video beat 1 then becomes a legal conservative-baseline transfer, not a recorded planner win.

## Operator workflow

1. Load a rack recipe that can change which slots are occupied, plus start slot, dock pose, and period. Occupied slots must be able to change between runs so the path is not canned.
2. Choose isolated mode (default) or naive-coupling mode.
3. Start a transfer cycle.
4. Watch requested path versus executed path, advice age, accept/clamp/reject reason, cycle time versus baseline, and timing health on a readable top-down lab schematic.
5. Inject a fault: planner overload, hang, crash, stale or replayed or expired or malformed advice, or a direct open of the joint device.
6. Recover or reset. Isolated mode holds or finishes with the fallback controller. Naive-coupling mode is allowed to miss its period when the planner hangs; that miss is the lesson. It is fair evidence that naive blocking coupling misses a period on this QNX image. It is not a claim that only QNX can isolate a planner. The QNX-specific substance we will point at in source is the FIFO loop, the resource managers, and the policy test if the OS actually enforces it.
7. Export a JSON/CSV run report tied to that config, including the planner-versus-baseline number a judge can read in ten seconds.

## On-target architecture

This is implementation, not the pitch. All of it runs on QNX 8.0 (QEMU is the development and demo target). The host UI is presentation only.

```
advisor (lower priority)
  writes advice only to /dev/advice
  cannot open /dev/joint (security policy, if the image can enforce it)

control (SCHED_FIFO, higher priority)
  wakes on CLOCK_MONOTONIC
  reads sensors and writes commands on /dev/joint
  reads the latest advice without ever blocking on the advisor
  accept / clamp / reject, else conservative fallback to HOME

/dev/joint   resource manager: simulated 2-axis plant, command validation
/dev/advice  resource manager: latest proposal with sequence, timestamp, expiry

injector     scripted faults; the matrix is allowed to show a failure
telemetry    bounded, lossy, cannot stall control
watchdog     may restart the advisor only, never control or the joint manager
```

The controller never uses a blocking `MsgSend` to the advisor in isolated mode. A synchronous ask is the naive-coupling baseline, run on the same image so the comparison is fair. We will not publish an untested Linux or ROS 2 comparison.

We do not invent isolation. We cite Simplex / runtime assurance.

## Command contract

Every advice message carries a sequence number, units, axis targets or feed, a timestamp, and an expiry. The controller rejects or clamps:

- out of range, NaN, or oversized payloads
- stale advice past expiry
- replayed sequence numbers
- paths that enter a keep-out or break velocity, acceleration, or jerk limits

Each decision emits a reason code (`ACCEPT`, `CLAMP_JERK`, `REJECT_KEEP_OUT`, `REJECT_STALE`, `REJECT_REPLAY`, `REJECT_EXPIRED`, `REJECT_MALFORMED`, `FALLBACK_NO_ADVICE`). Clamps are engineering controls, not formal safety, not a certified safety function, and not "the safety layer."

The plant is a two-axis second-order model (inertia and friction) plus rectangular keep-outs for occupied slots and the analyzer housing. Simulator truth, noisy sensor readings, and UI animation stay separate.

## Evidence we will actually collect

- Cycle time versus the conservative baseline, with keep-out and jerk breach counts (the ten-second judge number)
- Wake-up lateness separate from command-completion latency
- Miss count, p50 / p95 / p99 / max, advice age, run length, and sample count
- Host and guest setup: QEMU version, acceleration, vCPU count, image name, QNX version, chosen period
- Fault matrix with expected versus observed results; a failed case stays failed
- One security-policy negative test, only if the smoke test passes: advisor open of `/dev/joint` is denied by the OS
- Raw sample files published with the report

Start with a conservative period and earn any faster claim by measurement. Maximum observed latency is not WCET. QEMU numbers are not hardware real-time guarantees. We will not copy or chase CortexCage's author-reported figures (sub-1.5 µs gate, 0.89 µs P50 wake-up, <3.5 µs max jitter, and similar).

Optional short `tracelogger` capture is deferred until the six demo beats work. Tracing overhead would be disclosed if we add it later.

## Demo video (under 3 minutes, on the QEMU target)

Lock these six beats, in this order. Storyboard each one against CortexCage's public bullets, with the sound off, before building the host UI. If a judge could swap the titles, we are not differentiated yet. The first 30 seconds must not be process boxes, a requested-versus-executed overlay, and a hang-and-recover dashboard.

1. If the planner beats the frozen baseline on cycle time with zero keep-out violations and no jerk-limit breaches, this beat is that legal planner transfer. If it cannot, drop the AI label and this beat is a legal conservative-baseline transfer. Do not record a planner win we did not get.
2. Shortcut through a keep-out or a jerk spike that would splash, rejected or clamped with a visible reason code and a visible near-miss or splash.
3. Stale, replayed, or expired advice: rejected with the matching reason code. The arm does not follow the old path.
4. Planner crash or hang in isolated mode: shuttle still finishes or holds at HOME.
5. Same hang in naive-coupling mode: the period is missed on the same QNX image.
6. Planner tries to open `/dev/joint` and is denied by policy (or this beat is cut if the smoke test failed), then the exported report with cycle time versus baseline, miss counts, percentiles, sample count, and pass/fail.

No third-party music or trademarks. Labels must be readable without narration. Judges may watch only the video.

## Host UI

A readable top-down lab schematic, not a 3D mission-control cockpit. Show the rack, occupied slots, analyzer housing, cassette, requested versus executed path, reason codes, advice age, and the baseline comparison. We will not try to out-dashboard CortexCage (no 3D 20 Hz theater, no live oscilloscope show, no six-module in-dashboard codelab). Rules cap a project at one bonus prize; we will not aim a long codelab at that slot unless the six beats already work and we later choose that path with fresh approval.

## What we will not build on the critical path

- Camera, TensorFlow Lite, people-tracking, ONNX, or PyTorch inference on QEMU
- A Linux or ROS 2 comparison we did not run
- CortexCage numbers, screens, or vocabulary (`cortex_gate`, tangent cone, hallucination vectors, Simplex Gate)
- Certification, patient-safety, IVD, or hardware worst-case claims
- A custom SAT viewer, extra boards, or a new BSP
- Dual planner goals, or calling the planner AI before it beats the baseline
- Selling the architecture as novel

## Early integration risk

`secpolcompile` is license-checked and `secpolpush` needs the right abilities. A bad policy can lock out a VM, so policy work stays in a disposable image. First implementation step this week: QEMU hello-world and a security-policy smoke test. If QNX Everywhere cannot compile and enforce a policy, drop that bullet from the pitch immediately.

## Official judging, and where we are weak

The four criteria are equally weighted ([official rules](https://qnx.devpost.com/rules)): Technological Implementation, Design, Potential Impact, and Innovation & AI Creativity.

Innovation & AI Creativity is the weak leg if we lead with isolation, because that use of QNX is now obvious in this contest. The official wording asks whether this is a creative, non-obvious use of QNX and whether we understand the problem space. It does not require an LLM or a large on-target model. We fix that by leading with the transfer and the A/B experiment, and by making the planner earn the word AI against a published cycle-time metric.

Potential Impact is the next risk if splash is only a toast message. The official wording asks for a credible, specific case in a market QNX serves, and why a real-time foundation like QNX is the right choice. The visible spill or collision, plus the same-image A/B miss, is that case. Do not replace it with an untested Linux comparison.

Design is in good shape if the operator loop (recipe, run, fault, recover, export) actually works. Implementation looks non-trivial if the contract, faults, and measurements are real, and like a scheduler tutorial if they are not.

## Implementation order

1. QNX Everywhere account (created) and a QEMU hello-world on a disposable image, plus the security-policy smoke test. If the image cannot compile and enforce a policy, drop that bullet from the pitch immediately.
2. Joint resource manager plus the simulated plant and a ticking FIFO controller, using the frozen geometry and limits.
3. Code the frozen Safe-via baseline and the cycle-time clock. Run the baseline against itself before any planner work or UI polish. A sane result is a finite successful cycle time and zero keep-out or jerk breaches.
4. Advice channel, contract checks (including stale, replay, expiry), and fallback controller.
5. Native planner that must beat the frozen baseline on cycle time, or we drop the AI label and rewrite video beat 1 as a conservative transfer.
6. Fault injector and a machine-readable matrix that is allowed to fail.
7. Timing log, raw samples, and the same-image A/B coupling mode.
8. Security policy and the denied-access test, only if step 1 passed.
9. Six-beat silent storyboard against CortexCage's public bullets (title-swap test) before any host UI polish.
10. Host top-down lab schematic and report export, including the ten-second baseline number.
11. Reproducible image snippet, judge README, a short prior-art paragraph for Devpost, then the six-beat demo video.

Public repo, Devpost project, and submission still need explicit approval.
