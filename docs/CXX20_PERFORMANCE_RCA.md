# C++20 performance admission RCA — 2026-10-15

## Decision

On 2026-10-16 the user explicitly cancelled further performance testing and
authorized default C++20 migration without calibrated performance admission.
The recurring continuation is paused and no benchmark was running when this
request was inspected. No ten-pair C++17/20 admission is claimed. The completed
mean-only A/A attempt is retained, not deleted or recast as C++17/20 evidence.

Previous evidence-based recommendation: keep the default at C++17. Ambient load and waiting-time variability are
demonstrated; an intrinsic C++20 slowdown is neither established nor ruled out.
The new short diagnostic comparisons are **not** promotion evidence. No alignment
algorithm, optimization flags, dependencies or admission thresholds were changed.

## Evidence and attribution

The original five identical-binary A/A pairs failed their [0.98, 1.02] interval
bound. Ten C++17/20 pairs had a median elapsed ratio of 1.012124 and interval
[1.004932, 1.064893]. That upper bound fails non-regression admission, but is
not proof that the true slowdown exceeds 5%.

Original console timestamps (one-second resolution) put both modes' median
mapping phase at 12 seconds and Solo counting at 6 seconds. Median index loading
was 19.5 vs 21 seconds, and GTF preparation 9 vs 11 seconds (17 vs 20). These
coarse timings locate variation; they cannot exclude a small compute regression.

Fresh comparisons use the same frozen MSVC 19.51 Release binaries, dependency
lock, full cynomolgus index, one-million synchronized TMJ prefix pairs and eight
threads. Each diagnostic uses one warmup per binary followed by two balanced
A/B–B/A pairs. Complete BAM/header, SJ, Gene/GeneFull/Velocity, filtered/EM,
scientific-log and memory checks pass in both diagnostics.

| Diagnostic | Median paired wall ratio 20/17 | Individual paired process-CPU ratios | Admission |
| --- | --- | --- | --- |
| Default scheduler | 0.944177 | 1.012863, 0.975662 | INCONCLUSIVE; two pairs, busy host |
| Same eight P cores | 1.111628 | 1.000137, 1.042217 | INCONCLUSIVE; two pairs, busy host |

The changing wall-time direction is inconsistent with a stable effect identified
by these small samples. It does not prove that every difference is environmental.
In particular, the pinned C++20 observations also had higher competing load;
this is not a clean estimate of a compiler-only effect.

Concrete observed waiting components:

- In the default-scheduler diagnostic, one C++17 index-loading window took
  25.81 seconds wall and 21.16 seconds process CPU, versus 21.93/21.73 seconds
  for C++20. Both transferred approximately 29.17 GB in process-accounted reads.
  The window therefore includes substantial non-executing time; these counters
  cannot distinguish storage waits from scheduler waits or attribute a service.
- In the pinned diagnostic's first pair, whole-process wall times were
  54.8325/57.9982 seconds (17/20), but summed process CPU times were
  113.921875/113.9375 seconds. Mapping observations were 12.01/12.06 seconds.
  Solo observations were 5.75/9.68 seconds wall and 5.70/7.12 seconds CPU.
  The wall gap is not simply an equivalent increase in total CPU execution.
- Maximum monitoring gaps in these formal runs were 0.263–0.426 seconds.
  Phase boundaries are polling observations, not exact instrumentation events.
  Aggregate process CPU sums all threads and is not an independent wall-time
  admission metric. Read/write counters are logical transfers, not physical I/O.

A separate 30-second control ran **no STAR**, only a sleeping child. Its CPU
time was 0.078125 seconds; measured background CPU mean/p95 were
37.3410%/68.8474%. Thus substantial load exists independently of STAR or C++20.
System-time deltas in the pinned runs also tracked elapsed time times 20 logical
CPUs (median normalized ratios approximately 1.00–1.01); a gross denominator or
processor-group mismatch does not explain the reported load. Small quantization
and sampling errors remain possible, not a reason to treat this as idle.

The host has heterogeneous processors. Windows reports faster efficiency-class
cores at logical indexes 0, 1, 6, 7, 8, 9, 18, 19. The diagnostic mask `0xc03c3`
was applied and read back from each STAR process. It controls core eligibility,
not exclusive ownership, frequency, thermal conditions or external contention.
The earlier traces do not record which core executed each instruction; attributing
the original slowdown specifically to E cores would be speculation.

## Repairs and verification

- Rename the statistical failure label from `TIME_REGRESSION` to
  `NONREGRESSION_NOT_ESTABLISHED`. All existing quantitative gates remain intact.
- Record process CPU time, logical I/O transfers, raw CPU samples and observed
  phase boundaries. No hot-path instrumentation was added to STAR.
- Add an optional STAR-only Windows affinity mask, verify the applied mask and
  bind it into the calibration profile. Old/unpinned calibration cannot qualify
  a pinned experiment.
- Add a ten-second idle preflight before admission-sized Windows experiments
  (five or more pairs). A busy/unknown host exits with
  `UNVERIFIED_ENVIRONMENT_PREFLIGHT` before launching STAR. Short diagnostics
  remain available but cannot satisfy pair-count/calibration requirements.
- A real fresh five-pair admission attempt exercised this guard: preflight
  mean/p95 24.5888%/36.4458%, zero STAR runs launched, no performance PASS.
- Seven Windows helper/negative-control tests, five language-pair tests, the CI
  contract test and the Linux fixture/negative-gate test pass. Windows CI now
  runs the Windows-specific telemetry tests as well.

Local evidence is retained under `data/validation/` (ignored, not release assets):
`cxx20-rca-phase-1m-20261015`, `cxx20-rca-pcore-1m-20261015`,
`cxx20-rca-admission-preflight-20261015`, and
`architecture-20261009/cxx20-rca-idle-control.json`. Each scientific run retains
its binary/input hashes, helper snapshot, commands, measurements and signatures.

The independent s390x blocker is resolved: run
[38742508396](https://github.com/birdingman0626/STAR-Cross/actions/runs/38742508396)
passed 17/17 jobs. Downloaded artifact `11980547752` records same-source 17→20
`PASS` with source-signature SHA256
`55d7b48dca5583d75f4e69261685bafaaf2531de03ed71473b7b0a46c9ab545f`.
This proves exercised scientific/endian parity, not emulated performance.

## Remaining experiment

### Prospective threshold revision authorized 2026-10-16

The subsequent user instruction explicitly removes p95 from admission for the
new attempt: use `--background-cpu-policy mean-only` together with
`--max-background-cpu-percent 10`. Record p95 without blocking on it. This
policy and threshold are bound into calibration compatibility; old receipts
cannot qualify this experiment. No evidence establishes that observed spikes
were caused solely by CPU queries. Other scientific/statistical/RSS gates are
unchanged, and any PASS applies to this revised policy, not the original idle
contract. The runner's default remains the original mean-and-p95 policy.

The user explicitly revised the background CPU mean and p95 ceiling to 10%
for a fresh attempt. Pass `--max-background-cpu-percent 10` to both the new
A/A calibration and subsequent A/B experiment. Do not reclassify historical
receipts. Scientific equality, pair counts, calibration dispersion/interval,
elapsed-ratio and RSS bounds remain unchanged. This is qualification under a
more permissive background-load contract, not proof of an idle host.

The first fresh 10% attempt (`cxx20-aa-limit10-20261016-attempt1`) passed the
30-second readiness sample (mean 3.2478%, p95 6.6327%), but its independent
runner preflight measured mean 4.3772%, p95 10.3806%. It therefore exited as
`UNVERIFIED_ENVIRONMENT_PREFLIGHT`, with zero STAR runs and no calibration.

Use a demonstrably quiet host/window; do not terminate unrelated applications.
Keep source, compiler, inputs, flags and affinity identical; qualify five fresh
A/A pairs, then ten balanced A/B pairs with that exact calibration. Retain the
same 5% background, 2% calibration, 1.05 elapsed-interval and memory thresholds.
Only then decide on default promotion. If a repeatable residual exceeds the
bound, use the recorded phase CPU/wall windows to investigate the implicated
loader/parser/mapping/counting path; no such production defect is identified yet.

API interpretation: [system times](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getsystemtimes),
[process CPU times](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes),
[I/O counters](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprocessiocounters),
[CPU efficiency classes](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-system_cpu_set_information).
