# Architecture implementation and acceptance record

Scope: C++17; existing tested mapping modes preserve scientific outputs.
Insertion crash repairs have separate acceptance boundaries below. This is not
a performance promotion. Baseline commit: `7140819`.

## Ordered disposition of the eight recommendations

| Recommendation | Implemented boundary | Remaining boundary |
| --- | --- | --- |
| Narrow Parameters | Real `AlignFilterConfig` and `OutputConfig` storage; existing CLI names/default registration retained. Suffix-array search no longer includes Parameters or Genome. | Remaining parameter domains are not migrated wholesale; parser remains the central composition root. |
| Separate output | Synchronous `AlignmentResultView`, dedicated serialization translation unit, independent bounded byte-packet transport for SAM/BAM. | The borrowed view is **not queueable**. Async packets own already serialized bytes; no speculative copy of the complete read/transcript object graph. |
| Index abstraction | Read-only `SeedIndexView` with caller-owned lifetime, used by suffix search and replay. No virtual lookup or index copy. | CPU/GPU executor selection is not storage ownership; shared-memory lifetime remains Genome's responsibility. Recreate views after effective-index mutation. |
| Batch threading | Independent CPU query batch executor; optional single producer, persistent logical mapper workers, independent output consumer; bounded requests/packets and propagated exceptions. | Experimental and default-off. Parallel Random multimapper ordering is explicitly rejected with the chunk pipeline because worker assignment changes RNG use. No unqualified speed promotion. |
| RAII | Audited existing Genome/ReadAlign/ReadAlignChunk/SpliceGraph/Solo storage owners; reused them instead of duplicating ownership. Sanitizer tests exercise the changed boundaries. | Not a claim of universal leak freedom or safe fatal-exit unwinding. Public raw pointers can be borrowed views; inspect each allocation before converting it. |
| Independent SeedSearch | `SeedSearchEngine` batch interface, neutral CPU/CUDA query/result types, lightweight test index. Tests no longer supply fake Genome/Parameters constructors or link SharedMemory just for suffix search. | Production adaptive mapping still calls the existing exact search routine; isolated GPU replay is not full STAR GPU integration. |
| ReadAlign decomposition | Pure filter/output selection, separate serializer, allocation-free paired merger, classic and long-read window workspaces without ReadAlign ownership. Existing ChimericDetection component retained. | Stitching still borrows Parameters/Genome and scratch synchronously. No unnecessary duplicate chimeric detector or giant copied alignment-result hierarchy. |
| HTSlib maintenance | Locked upstream archive plus reproducible patch queue and hashes; CI reconstructs and checks the vendor tree. No history rewrite/subtree import needed. | Dependency upgrades still need source review and supported-platform runtime checks. Hash equality does not qualify a new HTSlib version. |

## Initial boundary verification (superseded by final continuation below)

- Native Windows CPU C++17: 104/104 CTest tests passed.
- Native Windows CUDA C++17: 104/104 passed; CUDA mini-index replay
  passed for batch sizes 1/7/256/65536 and sparse-SA factor 3.
- Linux GCC C++17 ASan/UBSan: 106/106 passed, including IPC lifecycle checks.
- Native scientific integration compared against the frozen prior binary:
  BAM records/scientific headers, SJ and supported STARsolo fixtures passed.
- ASan with leak detection enabled: mini-index generation, CPU batch replay,
  production capture, ordered seed comparisons, BAM/SJ comparisons passed.
- Patch queue reconstructs 962 maintained HTSlib files; fingerprints also
  guard 35 retained non-build checkout extras. Patch is about 15 KB, rather
  than copying a second full vendor tree.
- Python checks: 49 tests executed, 45 passed and 4 conditionally skipped;
  archive mismatch/path traversal/newline normalization have negative controls.

Local receipts: `data/validation/architecture-20261009/` (ignored). The original
scientific reference is frozen; current binaries must be matched by the hashes
recorded in each integration receipt. CI macOS, ARM and other platform results
are **not** implied by local Windows/Linux results.

## Why conditional rewrites are not defaults

The small integration capture confirms lock telemetry works, not 32-thread
contention or a speedup. Aggregate lock-held/wait durations overlap across
workers and are not end-to-end wall time. Use representative data and paired
timings before replacing the chunk reader or promoting batching.

Before a producer/worker/consumer or GPU pipeline change, preserve chunk identity
and RNG streams, read-pool leases, adaptive seed order, post-insertion effective
index identity, all output modes and exception/cancellation cleanup. Compare
complete scientific outputs against this synchronous path. GPU index residency
still needs an explicit memory budget; a view does not remove a 27 GB upload.

An owning alignment result needs read names/sequences/qualities, tags, CB/UMI,
mate state, transforms and transcript data. Do not enqueue the borrowed view or
reuse ReadAlign scratch while it is being serialized. First specify bounded
ownership and backpressure, then benchmark allocations before expanding scope.

No speed or memory reduction is claimed for this refactor. No C++20 migration,
default GPU enablement, full-library performance qualification or release was
performed.

## Continuation: output policy, paired merging and insertion failures

The output-reference selection rule now lives in an allocation-free component,
independent of streams/Genome/Parameters. It preserves stable pointer compaction,
KeepOnly precedence, chromosome threshold and primary-flag side effects. The
serializer still invokes this preparation synchronously; it is not a pure,
owning async serializer yet.

`PairedReadMerger` owns no buffers or mapper state. It receives numeric mate
storage and lengths, selects the same overlap, performs the same memmoves, and
returns overlap/start/merged length. ReadAlign only updates read metadata,
complements and remaps. One thousand randomized fixtures compare **all buffer
bytes** and decisions with the frozen pre-extraction implementation. Capacity
failure is tested before mutation, including the reverse-merge scratch branch.

New on-the-fly reference fixtures exposed two pre-existing failures:

1. Unlimited `genomeSuffixLengthMax=-1` became a huge memcmp count. The default
   insertion path now uses the existing separator-terminated suffix comparator;
   finite comparisons are bounded by actual storage. Both comparators return
   zero for identical keys. This repairs undefined/crashing behavior, not a
   proven speedup or an unchanged valid-result claim for every finite setting.
2. Rebuilding SAi allocated over an already-owned PackedArray. The old prefix
   table is explicitly released before rebuild; insertion arrays also use RAII
   without additional allocations or value-initialization overhead.

Frozen Windows reference and Linux ASan both reproduced the insertion failure.
The frozen binary is **not** an oracle for repaired insertion: fixtures instead
check mapped reference IDs and compare repaired default/finite comparator paths.
Other established integration modes retain their frozen-binary comparisons.
The failing fixture IDs/logs are retained locally; the original data are untouched.

The subsequent continuation implements bounded production chunk dispatch,
owning byte transport and window workspaces below. Full GPU dispatch remains
outside the requested scope.

Continuation verification: native CPU/CUDA CTest 110/110 each, Linux ASan/UBSan
112/112; native scientific integration retains the frozen-binary comparisons
except for the explicitly repaired insertion fixtures. The insertion fixture
also completes with ASan leak detection enabled. CUDA sparse-index batch replay
and 49 Python checks (4 conditional skips) pass. Current receipts replace the
earlier counts in the verification section; no new performance promotion or
nonlocal CI qualification is inferred from them.

## Non-GPU continuation: bounded input and output

The implementation reuses the persistent logical mapper workers, rather than
creating a second thread pool or migrating reads through unbounded buffers.
`ChunkInputDispatcher` serves requests on one producer thread; each caller
retains its chunk and scratch storage and waits for the completed read before
mapping. Request exceptions reach the correct worker, all mapper threads join,
and the mapping failure is propagated to the main process. Input locks use RAII.
Captured seed telemetry temporarily borrows the requesting worker's counters
while that worker is waiting; the producer does not invent another RNG stream.

`AsyncByteWriter` transports **owning serialized packets** with one queued and
one active packet per output stream. Blocked producers do not allocate more
payloads. SAM and unsorted/transcriptome BAM use the same checked sink as before;
writers drain before normal completion and close. Coordinate-sort bins retain
their existing transport. This isolates I/O without duplicating all read names,
qualities, tags, transforms, Solo metadata and Transcript graphs. It does not
make the borrowed alignment view safe to enqueue.

Opt-in environment switches (value `1`, otherwise unset):

- `STAR_CHUNK_PIPELINE`: one input producer and persistent logical workers.
- `STAR_ASYNC_BAM`: separate unsorted/transcriptome BAM writer threads.
- `STAR_ASYNC_SAM`: separate SAM writer, including ordered chunk concatenation.

They are independent and off by default. Maximum extra packet payload is
**twice `chunkOutBAMsizeBytes` per enabled stream**, plus queue/thread overhead;
existing per-worker buffers remain. Zero-BAM internal first passes retain their
valid synchronous path. With the chunk pipeline, Random multimapper ordering
requires one logical worker: keeping the RNG seed alone does not preserve which
reads consume each worker's RNG. Invalid combinations fail with a diagnostic.
The main exception boundary reports asynchronous failures and owns stream
lifetime on C++ unwinding; existing fatal `exit()` paths are not claimed to unwind.

Window extraction preserves both classic recursion and long-read dynamic
programming. Neither component accesses ReadAlign parsing, output or ownership;
all scratch/result lifetime remains with the caller. Paired merging, output
selection and chimeric detection reuse their already established components.

The 100,000-read, eight-worker cynomolgus/STARsolo screen compares the same binary
under normal/input/output/combined profiles. Exact BAM record multisets,
scientific reference headers, SJ, scientific logs and matrix/axis artifacts
match. Observed times: 47.94/46.82/45.92/45.69 seconds; process peak RSS is about
31.4 GB in all four runs. These single sequential observations, alongside local
build activity, **do not establish a speedup or unchanged memory on larger jobs**.
The receipt identifies binary `49173aa8` and its source command; later stream
leak fixes/diagnostic changes are separately covered by final integration.

Regression coverage includes packet lifetime/backpressure/error handling,
concurrent producer requests, multiple input/output chunks, two-pass/BySJout,
SAM ordering, BAM/transcriptome output and explicit unsupported-RNG rejection.
Synthetic fixtures are not full-library or all-platform qualification. Receipts
remain under `data/validation/architecture-20261009/` rather than shipping data.

## Final local acceptance

- Windows CPU and CUDA C++17: 115/115 CTest tests each.
- Linux GCC ASan/UBSan C++17 and optimized STARlong: 117/117 each.
- Final native scientific integration retains frozen-reference comparisons;
  repaired insertion uses the independent oracle described above.
- Complete Linux scientific integration passes with LeakSanitizer enabled,
  including sorted BAM, BySJout, two-pass, paired/chimeric/WASP auxiliaries and
  STARsolo fixtures. Successful paths are checked; fatal exits are not universal
  unwind proofs.
- Final STARlong 1200/1600-base spliced/exonic outputs match the frozen STARlong
  reference; Full and SuperTranscriptome generation/mapping pass leak checks.
  SuperTranscriptome remains graph diagnostics, not qualified BAM/CRAM alignment.
- Pipeline-enabled production capture preserves BAM/SJ and ordered seed tables
  across one/two logical workers, with nonzero requesting-worker input metrics.
- Existing isolated CUDA sparse-SA replay passes; no full GPU path was added.
- Python: 49 checks, 45 passed and 4 conditional skips. HTSlib reconstruction
  validates all 962 maintained files against the locked archive/patch queue.

An early incremental STARlong artifact threw `std::bad_cast` in Solo parameter
initialization. A full optimized rebuild with symbols and subsequent final
source rebuilds pass the reference checks. No algorithm change is attributed to
that transient artifact; its logs are retained and it is excluded from acceptance.
The new CI step builds a fresh optimized long-read artifact and runs its fixture.
CI includes leak-checked pipeline capture and super-transcriptome lifecycle;
remote CI/macOS/ARM results remain pending until an authorized push executes them.

Final scientific integration binary hashes: Windows CPU `29df0722`, Linux
ASan `1e231efd`, STARlong `3cc5ccff`. Full hashes, commands and artifacts are in
the individual retained receipts. The latest 100K pipeline receipt is
`real-100k-complete/result.json`; it checks the final native artifact against
its synchronous profile. This is a correctness screen, not a release or a
paired low-interference performance qualification.

The final 100K profile times are 48.12/45.98/43.92/47.49 seconds, with peak RSS
about 31.4 GB. The preceding screen had 50.08/47.33/52.14/52.06 seconds. The
different rankings and concurrent qualification activity reinforce the decision
not to promote the experimental pipeline on these observations. Default CPU
behavior remains synchronous; representative paired, idle-machine timing is
required before changing that default.
