# Literature optimization execution record

Date: 2026-10-08. Baseline HEAD: `5a398763d14bc2b3d8b2474e04bf4c6c3688da5d`, plus the locally frozen dirty source. C++17 and default CPU alignment behavior are unchanged. No commit, push or release is part of this execution.

## Decisions

| Package | Evidence and decision |
| --- | --- |
| P0 | Implemented bounded, deterministic diagnostics; fixed legacy multi-worker capture overwrites; full scientific comparisons passed on two real 100K paired-read segments. |
| P1 | Tested a bounded four-byte forward comparison. Exact fields passed, but three paired screens were slower; experimental production code removed. |
| P2 | Implemented replay-only PWL/PLA rank hints. All fields exact across 12 parameter combinations and two segments, but no combination passed the 15% screening gate. Not integrated into production. |
| P3 | Not triggered: no useful P2 trade-off demonstrated and no confirmed requirement to replace SAi. No sidecar format/dependency added. |
| P4 stitching | Immutable-parent copy reduction was implemented and scientifically equivalent, but all three 1M pairs were slower (median 4.17%); candidate reverted. No new sound splice-aware rejection bound is established and no pruning added. |
| P4 GPU / BiWFA / LCP-RMQ | No new transfer-inclusive opportunity or exact-domain certificate established. Existing optional CUDA route unchanged; no production migration. |

These are measured acceptance/rejection decisions, not a claim that every technique in the literature was implemented or disproved.

### Background-load recheck (2026-10-08)

The user reported concurrent applications during the earlier measurements. Treat the P1/P2 performance classifications as **provisional screening decisions**, not definitive algorithmic regressions; their exact-field correctness evidence remains valid and neither candidate is promoted.

A new serial recheck preserved the frozen binaries/inputs, increased repetitions from 51 to 501 and added Windows system/process CPU telemetry. The first attempted round still experienced severe background load: the P1 baseline's whole-process median background CPU was about 98.5%, with intervals at 100%. Available physical RAM during a later replay was about 6 GiB; the replay index itself requires about 25.3 GiB. A local model server held about 66.6 GiB of working set, but that observation does not attribute all CPU/pressure to that process. The user applications were not stopped by the test runner.

The partial execution was interrupted and preserved under `quiet-recheck/`, explicitly marked `INTERRUPTED_BACKGROUND_CPU_AND_MEMORY_PRESSURE`. Its timings do not overturn or strengthen the earlier performance conclusions. Only the agent's own wrapper and child replay processes were stopped. The subsequent complete `quiet-recheck-r4` is reported separately below; it does not retroactively validate this interrupted run.

`scripts/recheck_seed_performance.py` now preflights both before and after fingerprinting: five seconds of CPU sampling, median system CPU <=10%, and available physical RAM >=40 GiB. These are operational headroom gates, not guarantees of core/frequency isolation. It hashes inputs outside timing, alternates process order, retains all raw four-field checks, and records background CPU by subtracting child process CPU. Its four focused tests pass. Sampling every 200 ms across the whole process cannot certify individual sub-millisecond kernels; full-pipeline promotion still requires separate calibrated measurements.

Resume into a **new** output directory after freeing background CPU/RAM:

```sh
python scripts/recheck_seed_performance.py --evidence data/validation/literature-optimization-20261008 --output <new-directory> --rounds 3 --repeats 501
```

## P0: correctness and scope

Diagnostics are excluded unless `STAR_CAPTURE_SEEDS=ON`. Sampling is `splitmix64(read_id)%64==0`, independent of pthread scheduling. Each worker uses its actual STAR chunk/thread ID, not `omp_get_thread_num()`; the latter returned zero for STAR's pthread workers and overwrote the legacy capture. The invalid initial execution is preserved and excluded.

The capture now records extension requests, interval/length/multiplicity histograms, prefix shortcuts, comparisons, inspected bases, packed-SA loads, range expansions, stitching windows/seeds/transitions, and ordered seven-field semantic seed tables before stitching. The uninitialized `PC_Str` slot is deliberately not serialized. Tables include sampled reads with zero seeds.

Final hardening rejects two-pass, SJ remapping, WASP and mate-overlap remapping when capture is enabled. Ordinary mapping modes remain supported without capture. Per-worker limits are 5,000 queries, 5,000 sampled seed tables and 100,000 seed rows; overflow disables screening rather than silently qualifying a biased prefix. Failed stream closure does not emit completion statistics. The collector checks native ABI, complete binary records, worker/counter agreement, seed order and coverage over the entire observed input extent. Captures remain native-layout, not portable index files.

| Real segment | Reads observed | Sampled reads | Replayed extension requests | Seed rows | Selected drops |
| --- | ---: | ---: | ---: | ---: | ---: |
| Existing first 100K | 100,000 | 1,602 | 7,974 | 7,587 | 0 |
| Source pairs 700,001–800,000 | 100,000 | 1,602 | 8,074 | 7,709 | 0 |

The second fixture is a synchronized offset segment of the existing read-only 1M fixture, not an independent animal/library. Both populate all ten input-position deciles. Normal versus diagnostic runs match complete BAM records/scientific header, splice rows, all Solo files, scientific final-log fields, and the spliced/unspliced/ambiguous layers. Ordered seed tables also match across one versus two workers on the miniature fixture. Dense, D=2 and D=3 miniature geometry were exercised.

First-segment diagnostic extension time is about 43% and stitching about 48% of sampled instrumented mapping time. Singleton eligible intervals are only about 0.2% of extension requests. These are nested instrumented worker fractions, **not normal-run wall-time speedup bounds**. Counter overhead remains; first effective-index dumping and waiting are subtracted from nested timers but included in process wall time. Normal 100K runs take roughly 38–48 seconds, with index loading and other stages dominating this small workload. The diagnostic runs take roughly 62–69 seconds including dumping.

## P1: exact block-comparison screen

Hypothesis: a safe four-byte equality screen might reduce forward genome-comparison work (CPU locality/exact-comparison direction from mm2-fast and suffix-array search texts). Loads were bounded by both the read and genome; scalar mismatch ordering and reverse/sentinel behavior were preserved.

Three order-alternated process pairs, each with 51 replay repetitions, verified all four extension fields on 7,974 captured requests. Median replay time was **7.6%, 31.4%, and 11.7% slower** than the existing CPU implementation. Sub-millisecond kernels are noisy; these measurements justify rejection, not precise universal regression estimates. The candidate code/macro was removed. Local frozen binaries and receipts remain for reproducibility.

## P2: sampled rank-hint screen

Hypothesis: Sapling-style fixed-bin interpolation or PLA-style slope envelopes might choose a useful first probe inside STAR's existing SAi interval. This is a small implementation from the reviewed algorithm concepts, not vendored upstream code, a replacement suffix-array builder, or a complete error-certified PLA index.

Models use up to 65,536 uniformly sampled SA ranks from the actual runtime-augmented packed, dual-strand index. N/spacer keys are excluded from training, duplicate keys are collapsed, and nonmonotone valid samples are rejected. PWL uses 4,096 bins; PLA uses sampled error targets 16/64/256; both use k=14/18/21. Invalid/short/ambiguous queries and non-interior predictions fall back to the original CPU search. No predicted pruning occurs. All original search bounds, comparison semantics and multiplicity expansion are preserved.

Each combination uses 51 alternating CPU/candidate pairs in the same process. Prediction, key encoding and fallback are inside candidate timing; model construction is reported separately. Both segments reproduce captured production length/lower/upper/multiplicity, including every measured round.

The table reports median **candidate/CPU time ratio**; lower is better, and the screening gate requires at most 0.85.

| Model | k | Sampled error | First segment | Held-out segment |
| --- | ---: | ---: | ---: | ---: |
| PWL | 14 | unused | 1.001 | 1.033 |
| PWL | 18 | unused | 1.022 | 1.050 |
| PWL | 21 | unused | 1.030 | 1.130 |
| PLA | 14 | 16 | 1.125 | 1.142 |
| PLA | 14 | 64 | 1.102 | 1.141 |
| PLA | 14 | 256 | 1.097 | 1.096 |
| PLA | 18 | 16 | 1.097 | 1.153 |
| PLA | 18 | 64 | 1.065 | 1.081 |
| PLA | 18 | 256 | 1.084 | 1.129 |
| PLA | 21 | 16 | 1.074 | 1.099 |
| PLA | 21 | 64 | 1.066 | 1.057 |
| PLA | 21 | 256 | 1.141 | 1.152 |

Only about 1.6–2.2% of PWL queries and 7.1–8.3% of PLA queries admit an interior hint. This supports the inference that a coarse global predictor adds little to the already narrow SAi domains in this workload; it does not rule out every future certified/local predictor. No positive break-even query count is established because candidate search is not faster, before even amortizing construction.

Model allocation is about 1.03 MiB for PWL and 3.11 MiB for PLA, with conservative construction buffer bounds below 6.3 MiB. Construction takes about 36–56 ms. Replay peak host working set is around 25.3 GiB because the captured effective index is loaded. This is not a separately qualified <=1% **production** peak-growth claim. No model or new buffer is added to normal STAR.

## Follow-up: immutable stitching state and leak repair

The P0 stitching hotspot triggered a narrowly scoped candidate: pass the parent
`Transcript` by const reference through synchronous recursion, copying it only
for a mutable include branch or terminal extension. Exclusion and empty-terminal
branches no longer make redundant parent copies. Candidate order, scoring,
extension, tie-breaking and output copies remain unchanged. No new score bound
or pruning is introduced. The caller owns the initial state; recursive include
locals remain alive until their child calls return; no input reference escapes.

Frozen pre-change and candidate binaries remain under `frozen/STAR.pre-stitch.exe`
and `frozen/STAR.stitch.exe`. Sparse-D=3 miniature full-output regression passes,
including the supported remapping/output/count modes. A 1M-pair pilot was 5.75%
slower; a separate three-pair order-alternated screen then gave:

| Pair | Baseline seconds | Candidate seconds | Candidate/baseline |
| --- | ---: | ---: | ---: |
| 1 | 50.674 | 52.787 | 1.042 |
| 2 (candidate first) | 53.477 | 56.405 | 1.055 |
| 3 | 53.191 | 54.973 | 1.033 |

All six runs reproduce 1,254,350 BAM records, scientific headers, splice rows,
every Solo output and final scientific fields. Median paired slowdown is 4.17%;
the median peak-RSS delta is only about 176 KiB (no meaningful memory saving).
Outside-child CPU medians are 7.1–9.9%, although short setup spikes remain and
this is not an affinity/frequency-isolated study. **Rejected and removed from
production source.** Removing logical copies does not prove faster generated
code; no specific compiler/cache explanation was established. The frozen
candidate/receipts remain in `stitch-1m-pilot` and `stitch-1m-paired`, so this
experiment need not be repeated without a new evidence-based hypothesis.

Leak-enabled Linux ASan integration also exposed three temporary GTF arrays and
heap-created output streams leaked during genome generation (5,360 bytes in the
miniature fixture). Their local ownership is now explicit; allocation geometry,
stream mode and close order are preserved. Legacy stream callers retain their
existing interface. The new value-returning factory is used only at the verified
genome-generation sites. The same integration passes with `detect_leaks=1`, and
CI now adds this focused leak-enabled check rather than suppressing it. This does
not establish that every legacy CLI path is leak-free.

Final follow-up checks: native CPU CTest 103/103 after reconfiguration; CUDA C++17 CTest 103/103 and
sparse-D=3 integration; Linux ASan/UBSan CTest 105/105 and broad sparse-D=3 CLI
integration; native final-versus-pre-stitch broad integration; Python 42 passed,
4 environment-dependent skips (46 discovered). The broad sanitized CLI check
retains its existing leak-disabled scope; the separate genome-generation/seed
check has leak detection enabled. Earlier CUDA compute-sanitizer replay reported
zero memory errors; its timings are not performance evidence. Remote ARM/macOS/
s390x CI and release qualification are not yet established for these changes.
After removing the slower stitching candidate, native CPU and CUDA builds were
rebuilt and miniature integration rerun. Linux ASan/UBSan was rebuilt too; the
leak-enabled capture/replay integration still passes. These final binaries do not
contain the rejected const-reference stitching candidate.

Two additional attempts (`quiet-recheck-r2`, `quiet-recheck-r3`) were rejected
before any timed replay: post-fingerprint CPU preflight exceeded the fixed 10%
median gate despite sufficient RAM. Their failed receipts are preserved, not
reclassified as performance evidence. The runner now retains each preflight
attempt and allows three short settling checks without weakening the threshold.
An unrelated `elsv_merge.py` process was observed during diagnosis; it was not
stopped by this work. A later low-load retry uses a new output directory.

`quiet-recheck-r4` completed all three serial rounds (501 repetitions each),
including both segments and all 12 P2 combinations; every four-field check passes
and post-execution input/binary hashes match. P1 candidate/baseline median-time
ratios per process pair are 0.419, 0.894 and 1.107. The inconsistent improvement
and final regression do not qualify the removed candidate. Across the three-round
model medians, P2 ratios range from 1.030 to 1.168; no model reaches 0.85. Thus P2
remains replay-only and P3 is not triggered.

The preflight passed with about 93 GiB available. Whole-process median CPU outside
the measured child ranged from 9.3% to 24.0%, with setup spikes; this telemetry
does not isolate short warm kernels from OS work or unrelated activity. The run
confirms no repeatable qualifying screen, **not definitive intrinsic slowdown**
or a fully controlled timing environment. No performance threshold is relaxed.
The full-pipeline runner now also records 100ms outside-child CPU, pairwise time
ratios and their median reduction; older receipts without telemetry stay
unverified for that environment criterion.

### Recheck of the previously retained C1

The old frozen pre-C1 and C1-only executables were compared independently of
index reuse, using the same uncached 1M full-output profile. The three paired
candidate/baseline ratios are 0.987, 1.017 and 0.990 (median reduction 1.03%).
All six scientific comparisons pass. Outside-child CPU medians ranged from 9.0%
to 13.6%; the difference is small and inconsistent. This **does not reproduce a
reliable 7.32% time gain**. C1's previously verified removal of unnecessary
temporary allocations remains valid, but its time benefit is unverified; no
new speed claim or automatic scale promotion follows from these measurements.
The existing C1 implementation was not replaced by another unqualified algorithm.
Evidence: `c1-1m-recheck` (separate from the original confounded receipt).

### Isolated C3 and independent calibration

`c3-1m-recheck` uses the same final CPU executable on both sides, changing only
the qualified original-index/GTF versus persisted effective-index profile.
All six complete scientific comparisons pass. Paired ratios are 0.753, 0.715
and 0.795: an observed median reduction of 24.65%, with median peak RSS about
1.63 GiB lower. This removes the earlier C1/C3 attribution confound, but does
not yet qualify a general speed claim or automatic cache mechanism.

An independent five-pair same-binary/same-arguments A/A control (`aa-1m-calibration`)
passes scientific equality but has median absolute paired deviation 5.45%,
above the predeclared 2% calibration criterion. Outside-child CPU and setup
spikes remain. Its numerical timing gate is **INCONCLUSIVE**, not PASS.

Windows processor topology reports 8 efficiency-class-1 cores and 12 class-0
cores, one group and no SMT sibling pairs. Higher class means greater intrinsic
performance under Microsoft's [processor relationship definition](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-processor_relationship).
A separate five-pair control (`aa-1m-pcore`) pins only the benchmark parent and
its STAR children to the 8 higher-performance cores (local mask `0xc03c3`, still
8 STAR threads). Child affinity is checked. User applications, global power
settings and default STAR behavior are not changed. Typical absolute deviation
improves to 2.39% but still exceeds 2%; the affinity experiment does not establish
that heterogeneous scheduling was the sole cause or qualify the 1M timing claim.
Neither failed control is discarded or combined with later conditions.

The next calibration uses a genuine synchronized 5M prefix to test whether
longer useful work reduces relative preparation jitter, with the same 2% gate.
It is an independently qualified workload, not a reinterpretation of the 1M
failure. Larger candidate promotion remains conditional on its own control,
scientific equality, time and memory gates.

### 5M calibration after memory was released

`aa-5m-pcore` completed all ten timed runs with the same final CPU binary,
same 8-thread arguments and verified class affinity. The synchronized 5M
prefix was extracted from the read-only source library; its first 1M pairs
match the earlier fixture byte-for-byte. This is not an independent animal
or full-library cell-calling qualification. Source size/mtime and extracted
output hashes are recorded; a prefix extraction does not verify the full
compressed source checksum or its end-of-stream CRC.

| Pair | Baseline seconds | Identical-program seconds | Ratio |
| --- | ---: | ---: | ---: |
| 1 | 98.976 | 97.419 | 0.9843 |
| 2 | 94.845 | 112.748 | 1.1888 |
| 3 | 94.161 | 95.086 | 1.0098 |
| 4 | 94.357 | 114.030 | 1.2085 |
| 5 | 100.156 | 112.403 | 1.1223 |

Median absolute paired deviation is **12.23%**, failing the unchanged 2%
criterion. The deterministic paired-median bootstrap interval is
approximately **[0.9843, 1.2085]** (95%, seed 1729); it does not support a
stable same-program timing baseline. Peak RSS is about 29.29 GiB, and available system memory was
about 88 GiB during the run: the former RAM exhaustion is not observed.
Outside-child CPU medians range from 6.3% to 11.7%, with transient spikes;
affinity does not reserve cores or isolate frequencies, memory bandwidth
or storage. Logs for pair 2 show mapping taking approximately 37 versus
55 seconds while preparation takes approximately 32 seconds on both sides.
Thus longer work did not eliminate the variation, and startup jitter alone
cannot explain that pair. The actual cause is **unverified**.

The timing decision is **INCONCLUSIVE**. No C3 scale promotion, 10M/full-library
performance claim or automatic cache implementation is justified by this
control. Scientific output validation is a separate gate: all ten runs match
**6,265,370 full BAM records**, scientific header, splice rows, Solo matrices,
axes/feature statistics, scientific final-log fields and the
spliced/unspliced/ambiguous layers. The run exits successfully with
`PASS_SCIENTIFIC_COMPARISON`; final input and comparator-code fingerprints
match, and frozen binary fingerprints were checked after every execution.
This is not a performance PASS (`low_background_load_observed` is false).
Timing equality is not implied by output
equality. Do not discard noisy pairs,
relax the threshold or repeat an unchanged condition until a favorable
median appears.

### Current closure and remaining boundaries

- P0 diagnostic/receipt hardening, P1/P2 replay experiments, the independent
  C1/C3 1M rechecks, the P4 copy trial/reversion and the specific
  genome-generation lifetime repair are recorded with their tests and decisions.
- The released-memory 5M A/A workload is scientifically equivalent but fails
  timing calibration. Further unchanged repetitions are not a missing
  implementation step. A new controlled timing condition or diagnostic evidence
  is required before treating small performance differences as real.
- C3 5M/10M/full-library promotion and automatic cache integration remain
  **not qualified**, rather than silently bypassing the failed control. The
  1M manual reuse result is evidence scoped to that profile, not every library.
- P3, a new stitching bound and further GPU migration remain conditional
  research branches; no unsupported method is added merely to exhaust a list.
- Current local CPU/CUDA/sanitizer checks do not substitute for a new remote
  multi-platform CI run or a release artifact audit. No commit, push, tag,
  deployment or new release was performed in this execution.

## Verification and provenance boundaries (original screen)

- Native Windows: 103/103 CTest checks; Python discovery: 38 passed, 4 environment-dependent skips (42 discovered). Property checks cover 133,370 singleton cases, 71,230 sorted forward/default-versus-hinted cases, and 34,464 predictor cases.
- Linux x86-64/GCC13: CPU-only benchmark and both property targets built and passed; diagnostic STAR built and the sparse D=2 capture/replay integration passed, including ordered seed tables across worker counts, BAM/SJ equality and the 12-model hint sweep. This does not qualify Linux ARM64, macOS, sanitizers or CUDA.
- The real-data P0 runs used the frozen `STAR.cpu.exe` and `STAR.capture-r4.exe`; final hardening is exercised by the later miniature `STAR.capture-r5.exe` run, including the 12-combination replay hint sweep on sparse D=3 captures. Their sampled row counts are below the new cap and their complete binary captures pass the hardened collector, but their producer row-cap contract is explicitly legacy/unverified; do not call them a fresh real-data execution of r5.
- P2 receipts bind source archive contents and frozen executable hashes, with inputs/index hashes verified unchanged before/after execution. This is a local receipt, not independent reproducible-build proof. P0's source snapshot was taken after execution and is **not attested prebuild provenance**; its binaries/inputs and scientific comparisons are verified. P1 likewise does not have the new P2 build receipt.
- Small replay timings are warm-cache feasibility screens, not cold-cache/end-to-end performance qualification. Machine-load/cross-library/scale evidence is insufficient to advertise a speedup. No 5M/10M/full-library promotion run was justified after the screening gate failed.

## Reproduction and next trigger

Local evidence is under ignored `data/validation/literature-optimization-20261008/`: P0 `p0-100k-r4`/`p0-heldout`, P1 `p1-replay-*`, P2 `p2-discovery`/`p2-heldout`, frozen binaries, build receipts and test logs. Large effective-index captures are research artifacts, not release assets. Literature originals remain ignored under `knowledge/`.

```sh
python scripts/profile_seed_capture.py --normal <normal-STAR> --diagnostic <capture-STAR> --reference-run <frozen-full-output-run> --output <new-directory> --modulus 64
python scripts/run_seed_experiment.py --benchmark <CPU-benchmark> --genome <capture> --fastq <capture> --output <new-directory> --executor hint-sweep --reads 40000 --repeats 51 --cpu-threads 8 --build-receipt <receipt.json>
```

Any further stitching rejection experiment requires a **new proven STAR-specific bound**, using the observed transition distribution. It must first reproduce complete ordered candidates/tie outcomes on an independent fixture. The immutable-parent copy candidate has already been rejected; do not repeat it without materially new evidence. Do not introduce minimap2 chain boundaries, GPU dispatch or another index layer merely because this screen failed.
