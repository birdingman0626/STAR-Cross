# CPU qualification before experimental GPU work

The CPU remains the scientific oracle. A successful unit suite or visually similar
matrix is not full-library, cross-platform or GPU qualification. Keep each run in a
new directory; do not replace old results to conceal disagreement.

The implemented paired runner accepts `--full-contract --warmups 1 --rounds 5`
with two labels pointing to the same frozen binary for A/A calibration. After a
passing calibration on an uncontended machine, use `--rounds 10` with reference
and candidate binaries. `--calibration-receipt` records the prior result; it does
not by itself certify environment isolation. Default single runs remain only
screening. Full-contract reused runs are deliberately rejected.

Windows peak working set/commit is measured using the retained process handle;
GNU time provides Linux peak RSS. These are process peaks, not sums of samples
or complete descendant/shared-memory accounting. BAM comparison uses disk-backed
exact record ordering; comparison time is outside the measured STAR process.
Floats default to exact equality, and integer counts never receive a tolerance.
See [the execution record](QUALIFICATION_RESULTS_20261007.md) and
[ownership inventory](OWNERSHIP_AUDIT.md) for remaining gates.

## Repeatable checks (Linux / WSL)

```sh
cmake -S source -B build-cpu -DCMAKE_BUILD_TYPE=Release -DSTAR_BUILD_TESTS=ON -DSTAR_USE_LIBDEFLATE=OFF
cmake --build build-cpu --parallel 8
ctest --test-dir build-cpu/test --no-tests=error --output-on-failure
python3 -m unittest discover -s scripts -p 'test_*py'
python3 scripts/test_cpu_upstream.py --star-exe "$PWD/build-cpu/STAR" --ref-exe /absolute/frozen/STAR
```

Repeat miniature integration with `--threads 1`. The integration profile covers
mapped/spliced/unmapped reads, sorted and transcriptome BAM, FASTQ comments, SAM
auxiliary arrays, optional feature biotypes and native Velocity layers. It does not
qualify deep-droplet cell calling or every optional/chimeric mode.

## Real engineering fixture

```sh
python3 scripts/subset_fastq_pairs.py --r1 /absolute/library_R1.fastq.gz --r2 /absolute/library_R2.fastq.gz --pairs 1000000 --output-dir /absolute/new-fixture
python3 scripts/benchmark_cpu_subset.py --fixture /absolute/new-fixture --data-dir /absolute/data --output-dir /absolute/new-run --binary reference=/absolute/frozen/STAR --binary candidate=/absolute/build/STAR
```

Current runner profile is specifically cynomolgus CB16/UMI12, R2 cDNA / R1 barcode,
CellRanger4 clipping, Gene/GeneFull_Ex50pAS/Velocyto, EM, EmptyDrops_CR and unsorted
BAM. Its reference layout is explicit in the runner, not a chemistry autodetector.
Changing chemistry or references requires reviewing the command contract.

Synchronized prefix extraction preserves read bytes and validates mate names and
record lengths. Prefix sampling is not random or cell-representative. The manifest
does not claim full-source gzip CRC or full-library SHA verification.

Runner receipts hash reads, manifest, index contents, GTF, whitelist and binaries;
retain command, source snapshots, completion, time/RSS and output signatures.
`--reference-run` only reuses a completed run with matching actual input contents,
arguments, artifact signatures and unchanged producer binary. Missing provenance is
unverified and requires a fresh run, never retroactive relabeling.

Raw matrices must have valid integer coordinates/nnz/dimensions and matching axis
lengths. Compare exact integer counts and axes; report coordinate serialization
order separately. No numerical tolerance applies to integer counts. Required
artifacts include all three native Velocity matrices and SJ rows. This runner does
**not** itself establish complete BAM/header/index equivalence; inspect those
separately before qualification. `compare_raw_counts.py` is a diagnostic report,
not a pass/fail gate by its exit code.

## Known algorithm boundary and experimental entry

Unsafe stitching branch-and-bound was removed: its bound omitted positive extension
and junction/scoring contributions. Old-pruning output is a diagnostic comparison,
not the correctness oracle. Compare against independently pinned upstream semantics.

CellRanger4 uses Parasail **scan**, not striped: low gap penalties permit opposing
gaps for which striped produced different scores/endpoints from upstream Opal.
Independent scalar unit tests protect this contract. Do not restore striped for
speed without naming and validating an algorithm change.

Start GPU work with one bounded subproblem, CPU fallback and CUDA disabled by
default. Require exact supported-domain decisions, immutable candidate/result
fixtures and failure tests before end-to-end integration. Profiling/instrumentation
can identify a candidate but is not an end-to-end speedup claim. Keep full-library,
filtered-cell, chimeric, alternative chemistry and platform qualification explicit.
