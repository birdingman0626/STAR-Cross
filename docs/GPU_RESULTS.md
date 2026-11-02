# GPU measurement record

Measurements below are workload- and revision-scoped. They do not establish the
complete-computation 3x gate. Current work: [GPU plan](GPU_FULL_PIPELINE_PLAN.md).
CLI/support contracts: [batched seeding](GPU_BATCHED_SEEDING.md).
Earlier query/remap experiments: [GPU_EXPERIMENT.md](GPU_EXPERIMENT.md).

## 2026-11-01: 5M-prefix progression

The initial eight-worker hit-expansion control took CPU62.785s/CUDA65.489s whole
job, one sequential pair with background CPU about12.9%/14.9%.
It produced1,651,530,618 projected hits across7,002 batches and returned24.61GiB.
CPU/CUDA133-case CTest and the declared5M scientific contract passed at that stage.
Receipt roots: data/validation/gpu-hit-expansion-20261101/,
gpu-window-filter-20261101/ and gpu-stitch-20261101/ (under data/validation/).

Subsequent changes kept the CPU oracle and device-injection comparisons:

| Change | Observation and limitation |
| --- | --- |
| Conservative device window filter | Retained about4.2% of projected hits;74/74 differential checks and5M declared contract passed |
| Initial per-read StitchCore kernel | CPU26s / seed-only25s / seed+stitch29s mapping; duplicate CPU window building erased the benefit; about0.003%fallback |
| Four-stage ring / bounded seed and stitch slots | Reduced project/stitch waits; work within a read stayed ordered |
| Cost-bucket stitch scheduling | Used1+sum(nA²), descending cost then round-robin warp assignment; contiguous cost sorting was about6x slower |
| Index upload and asynchronous setup | In-place host registration or bounded pinned-ring fallback; setup overlap can switch within a chunk; required-mode failure remains fatal |
| Device CR4 clipping | Exact int32 DP; representative worker clip2.5→0.7s; original-output comparisons passed |
| Device whitelist query | Exact keys plus48 Hamming-1 candidates for16bp; host retains quality/abundance posterior |
| Host parser/clipping/Solo improvements | Shared CPU/CUDA changes: trimming CR/LF, lazy unclipped sequences, numeric-only CUDA reads, buffer reuse and direct filebuf scanning |
| Speculative bounded result downloads | CB/seed overlap and occupied-prefix copies reduce trips; insufficient estimates trigger additional copies |

These are recorded experiments, not standalone causal speed estimates.
Intermediate runs with other applications active remain directional only.
At the parser/Solo stage, GPU and CPU5M outputs (BAM,SJ,Log.final and33Solo files)
matched reference r-clip; r10 also passed seed-slot tests,74 differential checks
and PASSED_DECLARED_RAW_ARTIFACTS. Exact raw records remain in local validation
directories and the original implementation chronology in git d02862d.

## Matched eight-worker screenings

Values are seconds. BGZF settings differ between rows; do not compare rows as
a controlled effect of a single patch.

| Stage / condition | CPU map / wall | auto map / wall | required map / wall |
| --- | --- | --- | --- |
| Whitelist/lazy-read, BGZF single-thread |24.71 /63.56|19.80 /58.89|21.31 /59.99|
| Same stage, all arms STAR_BAM_THREADS=8 |23.12 /61.70|18.83 /58.11|20.39 /60.98|
| Parser improvements, BGZF single-thread |21.85 /60.81|15.54 /53.98|16.47 /55.00|
| Later common BGZF=8 screening |21.47 /60.5|14.33 /54.8|15.50 /56.7|

The last screening used three alternating rounds and excluded its first cold
post-reboot CPU run. That exclusion is retained as a limitation; this is not
formal performance admission. Later round-trip changes had an A/B mapping
comparison13.99→13.89s, despite device wait reductions: they were not the critical
path at eight workers.

## Thread and batch screening

Single runs on Core Ultra7 265F (8P+12E); mapping time only.

| Workers | CPU | GPU auto |
| --- | ---: | ---: |
|8|21.5|14.0|
|12|15.75|13.35; batch4096:14.07|
|16|13.84|13.77; batch4096:12.81; batch2048:14.72|
|20|14.29|not measured|

GPU scaling flattened beyond eight workers. At16 workers, per-worker align time
stayed about6.5s despite half as many reads; reducing batch size lowered align
cost while increasing device waits. In-flight cache pressure is a hypothesis,
not proven causation. Measurements after20:44 overlapped another GPU task using
about94%VRAM and were marked invalid.

## Interpretation

Mapping-only speedup, isolated kernel throughput and whole-job speedup have
different denominators. Output, window preparation, global Solo work, index/GTF
initialization and transfer/scheduling costs remain material.
Formal promotion requires new frozen, matched complete-computation trials,
scientific parity, memory accounting and an independent confirming session.
