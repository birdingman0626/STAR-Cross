# ThreadSanitizer audit of multi-threaded mapping

Audited 2026-10-23 on `codex/cxx20-migration` (base `b89be0c`).

## Method

- Build: Clang 21.1.8, `-DSTAR_TSAN=ON` (new opt-in option: `-fsanitize=thread`
  on STAR, bundled HTSlib and the test binaries), RelWithDebInfo, LLVM OpenMP
  (`libomp`) with its Archer tool (`OMP_TOOL_LIBRARIES=libarcher.so`) so OpenMP
  regions get happens-before edges. Mapping threads are plain pthreads; OpenMP
  is used for genome generation, junction insertion, BAM sorting and SmartSeq.
- Suppressions (`scripts/tsan_suppressions.txt`): two entries,
  `called_from_lib:libomp.so*` and `called_from_lib:libarcher.so*`. The
  distribution `libomp` is not instrumented and TSan otherwise reports its own
  thread-pool bookkeeping (`__kmp_reap_worker`); Archer 18 (Ubuntu 24.04) reports
  a "double lock" of its own mutex at the start of OpenMP regions, with the
  `#pragma` line as the only STAR frame. Races in STAR code inside OpenMP
  regions are still reported.
- Workloads:
  - all 128 unit tests; the miniature integration harness at 2 and 4 threads
    (with shared memory); the existing seed, super-transcriptome, BAM-tool,
    EmptyDrops, Transcript3p and malformed-input harnesses;
  - `scripts/test_thread_sanitizer.py` (new): a 1.3 Mb simulated annotated genome
    with 30,000 read pairs (spliced, intergenic, inter-chromosomal chimeras,
    repeat multimappers, unmappable) and 30,000 droplet reads. A 250 KB input
    buffer gives ~100 pairs per chunk, so ~300 chunks interleave across 8 threads.

`test_thread_sanitizer.py` runs every scenario with `--runThreadN 1` and with
`--threads`, and requires thread-count-independent outputs to match (exact bytes
for SJ.out.tab, gene counts, signal tracks, Solo matrices and index files; record
order for coordinate-sorted BAM and input-ordered SAM; record multisets for
unsorted outputs). It fails on any TSan report and on any crash.

| Scenario | Threaded paths exercised |
| --- | --- |
| `genome` / `genome_plain` index builds | OpenMP suffix-array sort, sjdb insertion at generation |
| `pe_full` | unsorted + coordinate-sorted BAM (20 bins), transcriptome BAM, GeneCounts, BySJout two-stage mapping, mate-overlap merging, chimeric detection (multimapping), unmapped FASTX, bedGraph signal |
| `pe_chim_within_bam` | chimeric WithinBAM output with sorted BAM |
| `twopass_sam` | two-pass mapping, input-ordered SAM (`PairedKeepInputOrder`) |
| `async_bam`, `async_sam` | `STAR_CHUNK_PIPELINE` input dispatcher, asynchronous BAM/SAM writers |
| `transcriptome_read_index` | transcriptome BAM with `--quantTranscriptomeBAMprimary ReadIndex`, compared exactly |
| `sjdb_insert` | on-the-fly junction insertion (OpenMP) before mapping |
| `solo_droplet` | CB/UMI barcode matching, Gene/GeneFull/SJ/Velocyto, multimapper Uniform+EM, CellRanger2.2 filtering, CB/UB-tagged sorted BAM |
| `solo_smartseq` | SmartSeq manifest counting (OpenMP) |

## Findings

| # | Finding | Kind | Status |
| --- | --- | --- | --- |
| 1 | `Log.out` written by workers under the wrong mutex or none: end-of-input message (input mutex), junction-buffer growth messages (none), BAM sort-bin layout (`mutexBAMsortBins`), per-read transcript-space warning (none). Other writers hold `mutexLogMain`, so two locks gave no mutual exclusion | data race (TSan) | Fixed: `OptionalMutexLock` on `mutexLogMain` at all four sites |
| 2 | `--twopassMode Basic --outSAMorder PairedKeepInputOrder` with more than one thread wrote a header-only `Aligned.out.sam`: chunk counters were not reset between passes, so main-pass chunks were numbered after the first pass while the concatenator waited for chunk 0, and the temp directory deletion discarded them | threading bug (thread-count invariance) | Fixed: counters reset per mapping pass in `mapThreadsSpawn`; regression check in `test_cpu_upstream.py` |
| 3 | `--limitIObufferSize` input buffer per read end of 101,302 bytes or less (e.g. 150000 with paired-end) made the unsigned chunk fill limit wrap, and reads were copied past the end of the chunk array | heap overflow (TSan SEGV) | Fixed: rejected at parameter validation with the minimum size; regression check |
| 4 | STARsolo `--soloFeatures SJ`: `featuresNumber` was taken in the constructor, before mapping, when the junction list was still empty (0). Per-cell arrays held one element (heap overrun for every cell with junction reads; crash at 1 thread, use-after-free report at 8) and `matrix.mtx` declared 0 rows | heap overflow, invalid output (inherited from upstream; the frozen baseline has it) | Fixed: set after the junction list is loaded; regression check that the matrix declares one row per `features.tsv` line |
| 5 | BySJout stage 1: when the filtered-junction buffer `chunkOutSJ1` filled, the code doubled `chunkOutSJ` and compared against its size, letting `chunkOutSJ1` grow past its own array. Needs more than `--limitOutSJcollapsed` (1,000,000) collapsed junctions in one thread | heap overflow (code review) | Fixed: `chunkOutSJ1` grows and is checked against its own capacity |
| 6 | Windows: the Solo SJ `features.tsv` link target was built as `cwd + "/" + prefix` because only `/` was treated as absolute, so drive-letter prefixes failed | portability (found by the new check) | Fixed with `std::filesystem::absolute` (same string on POSIX) |
| 7 | `test/benchmark_seed_search.cpp` (seed-interleaving spike, `b89be0c`) used variables named `far`/`near`, which `<windows.h>` defines as empty macros; MSVC could not compile the test target | build break (found while checking the Windows build) | Fixed: renamed |

## Accepted, documented behavior

- **Transcriptome BAM primary flag.** `ReadAlign_quantTranscriptome.cpp` picks the
  primary among a read's transcript alignments with `rngMultOrder`, seeded per
  thread (`runRNGseed*(iThread+1)`). Threads take chunks dynamically, so FLAG
  0x100 in `Aligned.toTranscriptome.out.bam` can change with the thread count and
  between runs; the alignments themselves do not. This is upstream behavior and
  is kept as the default for frozen-baseline parity, and the harness compares
  this file with 0x100 masked. For a reproducible flag, use the opt-in
  `--quantTranscriptomeBAMprimary ReadIndex`. It picks the primary from
  `--runRNGseed` and the read's number in the input
  (`source/transcriptomePrimary.h`), so the file is the same for any
  `--runThreadN`. The thread's random draw still happens, so other random
  choices are unchanged. Scenario `transcriptome_read_index` compares this file
  exactly, 0x100 included.
- **Fatal-error path.** `exitWithError()` serializes on `mutexError` and writes
  `Log.out` without `mutexLogMain`, while other workers may still log; the
  process then calls `exit()` with workers alive (TSan also reports these as
  thread leaks). Taking `mutexLogMain` there could self-deadlock when a caller
  already holds it, and the process is terminating; left as is.
- `warningMessage()` has the same locking but is only called from
  single-threaded phases (GTF loading, genome generation, parameter setup,
  post-mapping CRAM conversion).

## Results

Before the fixes (`b89be0c`): the `Log.out` race in every multi-threaded run,
the four memory/ordering bugs above, and a header-only two-pass ordered SAM.

After the fixes (`584c5a6` + tooling):

| Run | Result |
| --- | --- |
| `test_thread_sanitizer.py --threads 8`, Oracle Linux 10, Clang 21 / Archer 21 | PASS: 10 scenarios, 0 TSan reports, every compared output identical at 1 and 8 threads |
| Same at `--threads 4` in `ubuntu:24.04` (the CI job: Clang 18, `libomp.so.5`, Archer 18) | PASS after adding the Archer suppression, 0 reports; 128/128 unit tests with `halt_on_error=1` |
| 128 unit tests under TSan | 0 reports |
| Miniature harness at 2 threads and at 4 threads with shared memory; with the chunk pipeline + async BAM at 4 threads; super-transcriptome, BAM tools, EmptyDrops, Transcript3p and malformed-input harnesses | All pass; 37 TSan reports, every one on the fatal-error path of an expected-failure fixture (racing STAR frame is an `exitWithError` call, or a thread leak at that exit); 0 unexplained |
| Local replay of the Linux `build.yml` jobs on `5d20722` (gcc, clang, frozen-baseline index and alignment equivalence, ASan/UBSan, new thread-sanitizer job) | 33/33 steps pass; frozen-baseline outputs unchanged |
| Windows MSVC 14.51 build, 125/125 CTest, miniature harness with the new checks | Pass (after fixes 6 and 7) |

Output changes versus the frozen baseline occur only where the old behavior
was broken: the Solo SJ `matrix.mtx` row count (0 → number of junctions; the
entries are unchanged), two-pass ordered SAM with threads (header-only →
complete), and configurations that previously overran memory (undersized input
buffers now stop with a parameter error).

The macOS lane was not rerun for this change (its Homebrew toolchain was
removed after the previous round); the changed code is platform-neutral except
fix 6, which is Windows-specific.

## Follow-up: input dispatcher error hand-off

CI run `40055923992` (commit `df20588`) reported a race in the unit test
"Input dispatcher serializes concurrent requests and delivers errors". When a
read failed, `ChunkInputDispatcher` delivered the exception through a
`std::packaged_task`. The producer thread could then drop the last reference
to the exception after the caller had read it. That ordering exists only
through libstdc++'s exception reference count, which TSan does not instrument,
so TSan reported a race. It depends on timing, so earlier runs passed. The
producer now runs the read itself and hands the error to the caller under the
dispatcher mutex, keeping no reference of its own. The behavior is unchanged:
requests are still serialized, and errors still reach the requesting worker.

## Not covered

Cross-process shared-memory genome admission (handled separately, see
`SHARED_MEMORY_ADMISSION.md`), CUDA paths,
STARlong, WASP, `CB_UMI_Complex` barcodes, `--readFilesCommand` subprocess
input, and macOS/Windows thread runtimes (TSan ran on Linux only; Windows and
macOS builds use the same locking code).
