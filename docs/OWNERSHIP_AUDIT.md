# Ownership inventory and migration boundaries

Audited 2026-10-07. This file distinguishes implemented storage ownership from
unverified whole-program teardown. C++17 remains sufficient for these changes.

| Resource | Owner / borrowers | Current release boundary | Status |
| --- | --- | --- | --- |
| PackedArray allocation | `storage` owns allocation base; `charArray` is a mutable view, potentially offset or rebound | Explicit idempotent `deallocateArray()` or automatic destruction | Implemented; copying borrows, moving transfers an owner; owner-copy assignment rejects |
| Genome suffix-array aliases | SA, SAinsert, SApass1, SApass2 may point inside another reservation | Explicit `Snapshot::Borrowed` in generation, insertion and pass1; never outlive source | No implicit Genome copy/assignment; explicit free clears all SA views/owners |
| Genome G/G1 | Unique heap allocation base; G/G1 remain views; shared mapping has a separate unique owner | Idempotent `freeMemory()`; shared release before Parameters log destruction | Sequence load/generation/replacement/output buffers migrated; large buffers are not newly zero-filled |
| Genome metadata | chrBin/SA-start/SJ tables have named owners; previous tables retained for insertion snapshots | Normal Genome destruction; borrowed snapshots must end first | Main tables migrated with unchanged capacities; retained small prior tables prevent dangling insertion borrows |
| Variation / output genome / super-transcriptome | Unique owners; SNP loci, VCF stream and sort scratch explicitly scoped | Owning Genome destruction; chunks/graph consumers finish first | Migrated; graph diagnostics exercised, not complete experimental alignment output |
| Parameter registration | Shared registration owner, raw field addresses still borrow original Parameters | Last pass1 copy destroys registry; original parameter values must outlive readers | Genome reader borrows CLI streams without allocating a discarded stream set; stream deletion remains explicit |
| BAM buffers/bin arrays | BAMoutput unique owners; binStart elements borrow offsets in bamArray | After mapping, Solo, sorting and other BAM consumers finish | Implemented without zero-filling the large buffer |
| BAM temporary streams | BAMoutput owns streams returned by `ofstrOpen()` | Checked `finalize()`, before deleting temporary files | Implemented; destructor closes best-effort only; deferred disk-full regression added |
| BAM BGZF | Parameters owns open handle; BAMoutput borrows | Checked flush/close in STAR; borrower destruction never closes it | Implemented for unsorted/transcriptome handles; other standalone BAM writers still need a separate audit |
| ReadAlign arenas | Named unique allocation bases in ReadAlign::Storage; raw fields are algorithm views | Chunk owns primary/WASP/merged instances; destruction after worker join | Read/window/transcript/BAM arenas migrated; merged junction stream is explicitly borrowed |
| Chunk input/output and Transcriptome | Chunk owns input streams/buffers, junction outputs, local Transcriptome object and quantifications; shared metadata borrowed | After Solo/counting/BAM sorting; pass1 chunks released before pass2 | Scoped owners migrated; complete Transcriptome/Solo aggregate metadata remains outside this change |
| Per-read Solo / clipping / graph | Owned barcode/features and record streams, CR4 scorer and SpliceGraph row/column/seed allocations | Their ReadAlign/chunk lifetime; aggregate Solo consumers finish first | Barcode early release clears the borrow; graph destructor no longer uses an incorrect fixed row count |
| CUDA allocation | Existing DeviceBuffer/Buffer scoped owners | Synchronizing adapter execution followed by cudaFree | Actual adapter/seed checks and memcheck; stream/event/global ownership not broadened |
| SharedMemory | Noncopyable named IPC attachment and counter; external log stream borrowed | Detach both mappings; explicit Remove or verified last-user policy removes names | SysV/POSIX two-process lifecycle and failure fixtures pass on Linux; full Genome ownership and concurrent admission remain pending |

`PackedArray` view copies intentionally do not extend the source lifetime. They
are not deep copies or reference-counted ownership. Moving a borrowed view into
an owner keeps its reservation alive, avoiding a self-alias use-after-free.
Borrowers must never read after owner release. A const/span-only public interface
is a later API migration, not established by this change.

`exitWithError()` calls `exit()`: normal automatic-stack unwinding is not
guaranteed on fatal paths. Do not remove the full-CLI leak exception or claim
complete exception/cancellation cleanup from these scoped migrations.

Next stages require independent fixtures for pass2 growth, concurrent IPC admission,
shared-memory load/keep/remove modes, partial construction, long-read/chimeric
and worker teardown. No blanket shared_ptr or zero-filled large vectors.

## Shared-memory continuation

The small IPC fixtures now cover keep/reopen, cross-process payload visibility,
first-owner detach without removing a live borrower's segment, last-user removal,
counter attachment balance, repeated explicit cleanup and allocation overflow.
They refuse pre-existing keys and keep the temporary key-file inode reserved;
parallel executions choose an unused project key instead of deleting collisions.
On platforms exposing `SHM_DEST`, an invalidated counter must not authorize
automatic data removal. Cleanup errors detach best-effort, not unconditional
`Clean()`. The exception's error-only constructor initializes all its fields.

Mapping size comes from kernel metadata, not an uninitialized shared header.
The existing header/payload offset remains reserved, preserving genome layout.
Unix SysV and POSIX fixtures ran under ASan/UBSan with leak detection; real STAR
SysV load/keep/remove modes were compared against NoSharedMemory. Windows still
supports only NoSharedMemory. These tests do not prove atomic last-user decisions
against concurrent new admissions, all syscall-failure paths, or orphan-free
constructor failure. Do not mark all of O3 complete from this bounded work.

## Transcriptome / Solo aggregate continuation

Transcriptome copies share a named metadata allocation owner, preserving the
existing per-thread raw views without duplicating the large annotation arrays.
ID/name vectors retain their existing copy behavior. Quantification counts
remain independently allocated and owned by each ReadAlignChunk. The root
Transcriptome is scoped in main; copies still borrow Parameters and must not
outlive it. Copy assignment is forbidden rather than silently rebinding it.

Solo owns the aggregate barcode summary, feature objects and feature-pointer
array. Each SoloFeature owns its summary, borrowed-thread pointer array and
redistribution streams; it does not own the thread feature objects. Explicit
idempotent aggregate teardown occurs after all sorting/tag consumers and before
chunk teardown/temp deletion. Destructors do not access those borrowed chunks
or logging streams. Temporary counting arrays retain their original early-free
points, sizes and uninitialized allocation semantics. Heap-created annotation,
whitelist, counting and reporting streams now have local owners.

These changes qualify ordinary short-read Gene, GeneFull variants and Velocity
paths. SmartSeq/Transcript3p redistribution, allocation failures and fatal exits
still require independent fixtures. Standalone soloCellFiltering retains its
process-exit contract: an attempted test exposed a pre-existing single-barcode
boundary (loadRawMatrix leaves nCB as the final zero-based index). It is not
silently repaired in an ownership-only change or claimed as leak-qualified.

## Standalone filtering and SmartSeq follow-up

The previously deferred standalone filtering defect is now repaired separately:
loadRawMatrix preserves the detected-cell count while filling arrays with a
local index. Read medians are only computed when read observations exist.
Successful standalone filtering returns through main and releases aggregate
storage and CLI streams; fatal/error paths still call exit.

Four hand-computed fixtures cover one cell, sparse/shuffled columns including
the last cell, the existing TopCells threshold policy, and fractional rounding.
No threshold, tie or rounding policy was changed. ASan/UBSan and LeakSanitizer
pass on these valid inputs. General malformed/duplicate MatrixMarket input
validation and EmptyDrops boundary qualification are not implied.

Two-cell single-end SmartSeq fixtures exercise redistribution with Exact and
NoDedup, Gene/GeneFull, two threads and nonzero independently predicted counts.
They revealed a separate native Windows bug: only the final preprocessing
command's output was redirected, and FILE markers were absent. Each file now
has checked, redirected output and a POSIX-compatible boundary marker; no reads
are silently dropped or assigned to the wrong cell. The manifest stream is
also scoped. Linux matches the preserved reference; the defective Windows
reference is not the correctness oracle for this path. Its documented failure
is retained rather than suppressed. These fixtures do not qualify every paired
SmartSeq case, Transcript3p, arbitrary preprocessing commands or fatal exits.

## Paired SmartSeq, matrix input and Transcript3p boundaries

Two-cell paired-end SmartSeq Exact/NoDedup now pass independent Gene/GeneFull
count oracles and targeted leak checks. This is bounded PE coverage, not every
preprocessing command or chemistry.

Standalone matrix loading now validates header, dimensions, entry completeness,
coordinates, finite non-negative values, uint32 count/offset capacity, per-cell
totals and axis lengths. Input file size is checked before allocating a claimed
large entry array; barcode storage grows with observed rows rather than an
untrusted column claim. Unsupported duplicate coordinates are rejected with a
canonicalization diagnostic, not silently interpreted as independent genes.
Old commented-out alternate loaders were removed instead of adding another
parser abstraction. Fatal input exits remain outside automatic cleanup claims.

Transcript3p now enables its existing classifyAlign dependency independently of
Gene output. Exact whitelist membership replaces insertion-position membership;
empty/unmatched clusters, zero/conflicting indices and unusable normalization
fail explicitly. Distribution searches are bounds-checked. The estimator and
cutoff rule are otherwise unchanged. A single uniquely matched transcript with
one UMI produces one finite count; its normal teardown passes LeakSanitizer.
This does not qualify ambiguous-transcript EM accuracy on real tissue data.

## EmptyDrops and optional CUDA continuation

An ASan fixture reproduced an out-of-bounds read when the requested candidate
count exceeded the barcode array. Candidate selection now uses a widened,
barcode-bounded end and explicitly handles an empty candidate interval. Ambient
count accumulation rejects overflow; factorial sizes and simulation iteration
use widened arithmetic. Insufficient SGT frequencies or an unusable ambient
profile explicitly retain knee-filtered cells only, rather than use an
uninitialized estimate. This fallback is not successful extra-cell detection.

The SGT cache now stores only observed frequencies instead of allocating through
the maximum ambient count. Existing estimates, normalization, candidate rules
and random seeds remain unchanged on supported inputs. This removes a dense
allocation, not a measured end-to-end performance claim. Truly huge candidate
counts can still require large factorial tables and long simulations.

The isolated CUDA seed executable must link SharedMemory.cpp after Genome's
shared-memory holder became uniquely owned. It now links the production
implementation rather than adding a dummy destructor or weakening ownership.
Host/device C++17 remap and seed correctness tests and targeted device memcheck
pass locally. This does not establish whole-application race freedom, global
cleanup, all IPC modes or fatal-error unwinding.
