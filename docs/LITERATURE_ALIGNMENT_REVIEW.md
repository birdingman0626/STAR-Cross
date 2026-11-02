# Implementation vs. literature review

Date: 2026-11-01. Revision checked: `1367fb6`, CPU code paths only (CUDA/GPU
branches are out of scope). Sources: the converted texts in `knowledge/docling/`
(gitignored; Markdown plus docling JSON per document, listed in
`knowledge/docling/manifest.json`). Bibliography: [REFERENCES.md](REFERENCES.md).

The question is whether the code follows the approach the papers describe.
Each section names the code that was read, deliberate deviations, and open
gaps. "Matches" means the method was compared with the paper's text, not that
every output was re-derived. This is a method-level review, not an acceptance
result: a "Matches" verdict does not show that outputs agree with the reference
tool, and a maintainer review found four places where the first version
over-claimed (EM trigger, UMI grouping, CellRanger equivalence, Velocyto
workaround; corrected below).

## Summary

| Area | Source | Code | Verdict |
| --- | --- | --- | --- |
| Seed search (MMP) | Dobin 2013 §2.1; Manber & Myers 1993 | `ReadAlign_maxMappableLength2strands.cpp`, `SuffixArrayFuns.cpp` | Matches |
| Junction insertion, two-pass | Veeneman 2016; Mäkinen et al. 2023, Insight 16.2 | `sjdbInsert*`, `twopassMode` | Matches; the paper's artifact filter is not available |
| EmptyDrops cell calling | Lun 2019; Phipson & Smyth 2010; Benjamini & Hochberg 1995; CellRanger source | `SoloFeature_emptyDrops_CR.cpp` | Main statistical steps correspond to CellRanger; deviations from the paper are CellRanger's; BH step-up correct. Not shown to give identical p-values or cell sets (no pinned CellRanger commit, no end-to-end diff, different RNG) |
| Ambient profile smoothing | Gale & Sampson 1995; Sampson 2001 ch. 7 | `SimpleGoodTuring/sgt.h` | Matches the published recipe step by step; slope/fit not checked |
| UMI deduplication | Smith 2017 (UMI-tools) | `SoloFeature_collapseUMIall.cpp` | Link threshold matches (`1MM_Directional_UMItools`); **grouping differs** (greedy, not graph traversal) |
| Multi-gene reads (EM) | Kaminow 2021 §5.1.7 | `SoloFeature_collapseUMIall.cpp` | Matches; **NaN for UMIs whose gene set has > 100 genes, reachable with default settings** |
| Velocyto counts | La Manno 2018, Methods | `Transcriptome_classifyAlign.cpp`, `SoloFeature_countVelocyto.cpp` | Matches; no repeat-mask filter |
| WASP filtering | van de Geijn 2015 | `ReadAlign_waspMap.cpp`, `Variation.cpp` | Matches the re-mapping test |
| Chimeric detection | Dobin 2013; Haas 2019 (STAR-Fusion); Uhrig 2021 (Arriba) | `ChimericAlign*.cpp`, `ChimericDetection_*.cpp` | Default scoring differs from upstream only with `--chimMultimapNmax > 0`, which STAR-Fusion's published command uses; not validated against a fusion truth set |
| Alignment scoring | Durbin 1998 ch. 2; Smith & Waterman 1981 | `scoreGap*`, `scoreDel*`, `scoreIns*` | Matches (affine gaps, local extension) |
| Benchmark statistics | Kalibera & Jones 2013; Georges 2007; Mytkowicz 2009 | `scripts/qualification.py`, `scripts/linux_paired_benchmark.py` | Mostly matches; no setup randomization |
| Huge pages | Drepper 2007 §4.3.2, §6.2.4; Kwon 2016 (Ingens) | `source/HugePages.h` | Matches; tail latency measured |
| Learned seed-search hints | Kirsche 2021; PLA-index 2024; Kraska 2018 | seed replay benchmark | Evaluated 2026-10-08 and rejected ([results](LITERATURE_OPTIMIZATION_RESULTS_20261008.md)) |
| Exact-output optimization | Kalikar 2022 (mm2-fast); Vasimuddin 2019 (BWA-MEM2) | project policy | Same principle: speed-ups keep output identical |

## Recommended actions

### Implementation follow-up on `codex/gpu-batched-seeding`

The findings above describe the reviewed `1367fb6` implementation. The working
branch now repairs the first two findings:

- `1MM_Directional_UMItools` follows directed reachability rather than a greedy
  first-parent chain. Equal-count roots use deterministic sequence order;
  representative UMI strings are not claimed identical to every upstream input
  ordering. The stricter `1MM_Directional` and CellRanger dedup modes retain
  their previous behavior. Affected directional molecule counts intentionally
  change; this is a correctness repair, not an exact-output optimization.
- EM keeps pre-threshold weights available. A molecule with zero truncated
  support uses its prior weights locally, or uniform weights if those are also
  zero. Other molecules retain the original thresholded update and convergence
  comparison. Ordinary finite updates remain bit-identical in regression tests.
- `test_SoloCounting.cpp` checks independent graph transitive closure on all
  two-base UMI triples with counts 1–3, input-order permutations, integer-count
  overflow, and EM conservation. `scripts/test_solo_counting.py` checks actual
  STAR output for all 144 singleton-path cases, an Exact-mode control, and
  100/101/150 overlapping genes with uniquely mapped reads and the default
  alignment multiplicity limit. These checks do not establish real-data effect
  frequency or reference-tool equivalence outside the tested contracts.

The retained actions, in original priority order:

1. **EM NaN — repaired on the working branch** (multi-gene reads, below). It needs only
   `--soloMultiMappers EM`, not a raised multimap limit. The fix preserves the
   ordinary finite update; 100/101/150-gene unit and CLI regressions now cover
   the threshold boundary and conservation.
2. **UMI-tools directional grouping — repaired on the working branch** (below):
   the old link rule matched, but its grouping did not. The counterexamples and
   an independent transitive-closure oracle are now regression tests.
3. **Validate the default chimeric scoring** against simulated fusions with known
   truth, comparing default and `--legacy`. The change only affects
   `--chimMultimapNmax > 0`, which STAR-Fusion's published command sets (10);
   Arriba's published command leaves it at 0 and is unaffected. Until then,
   recommend `--legacy` to STAR-Fusion users and anyone using multimapping
   chimeric detection, and port the acceptance test the cherry-pick plan deferred.
4. **Warn when the Good-Turing fit is unusable** (slope ≥ −1). Output-neutral.
5. **Setup randomization** for benchmark claims below about 5%.
6. Optional features: a short-overhang count per junction (two-pass artifacts),
   and a read-level repeat-mask filter for Velocyto. Both would be opt-in so
   existing outputs stay identical.

## Details

### Seed search

The STAR paper defines the Maximal Mappable Prefix as the natural result of binary
search in an uncompressed suffix array, and the code does exactly that. A prefix
lookup (`SAi`, `gSAindexNbases`) narrows the range, then `maxMappableLength`
runs the binary search. It uses Manber & Myers' simple accelerant: comparisons
start after `min(L1, L2)` characters already known to match at both interval
ends. It does not use their LCP-array refinement (O(P + log N) worst case), which
needs extra arrays of suffix-array size. That is consistent with the paper's
memory/speed trade-off. The backlog profile also shows memory latency, not
character comparisons, dominating `compareSeqToGenome`, so the refinement would
gain little.

### Junction insertion and two-pass mode

Mäkinen et al. (Insight 16.2) describe the principled approach: index sequences
built by joining the end of one exon to the start of the next. They note it needs
flanks at least as long as the read. That is STAR's junction insertion, with
`--sjdbOverhang` documented as ideally mate length − 1. Two-pass mode follows Veeneman
et al.'s framework: first-pass junctions are inserted, then all reads are
re-mapped.

Gap: Veeneman et al. flag second-pass artifacts with 93% sensitivity and 99.8%
specificity when ≥ 80% of a junction's reads span it by ≤ 12 nt. `SJ.out.tab`
reports only the maximum overhang (column 9), so users cannot apply that filter.
An opt-in per-junction count of short-overhang reads, in a separate file, would
enable it without changing `SJ.out.tab`.

### EmptyDrops (`EmptyDrops_CR`)

Deviations from Lun et al., each of which corresponds to a step in CellRanger 3/4
(read in `lib/python/cellranger/cell_calling_stats.py`,
`compute_multinomial_ambient_pvalues`; the checkout was not pinned to a commit):

| Paper | `EmptyDrops_CR` and CellRanger |
| --- | --- |
| Dirichlet-multinomial likelihood with fitted α | Multinomial (α → ∞): no overdispersion |
| Ambient pool = barcodes with total ≤ T (100) | Barcodes ranked `indMin`–`indMax` (default 45,000–90,000) |
| Known cells = total ≥ knee U from a spline fit | Known cells = the simple (CellRanger 2.2) filter |
| BH over candidates plus known cells (p = 0) | BH over candidates only |
| p = (R_b + 1)/(R + 1), R_b counts L′ ≤ L | R_b counts L′ < L (CellRanger: `sim_loglk < obs_loglk`) |

Checked against the primary sources:
- **p-value** (Phipson & Smyth 2010): p = (b + 1)/(m + 1), where b counts
  simulations "at least as extreme" as the observed one. That is `≤` in
  likelihood. The strict `<` used by CellRanger and STAR differs only on exact
  ties of the simulated log-likelihood, which a continuous-valued statistic
  makes rare. The +1 in numerator and denominator is present, so no p-value is 0.
- **FDR** (Benjamini & Hochberg 1995): the step-up rule rejects all hypotheses up
  to the largest rank i with p₍ᵢ₎ ≤ i·q/m. STAR computes `padj = p·m/rank` and
  takes the running minimum from the largest rank down, which is the standard
  adjusted-p form of the same rule.

Limits of this check: it compares the statistical steps, not outputs. No
specific CellRanger commit was pinned and no end-to-end diff was run. STAR and
CellRanger also use different random number generators and seeding, so the
simulated p-values, and therefore the final cell set near the threshold, are not
guaranteed to be identical. "Corresponds to CellRanger" is the supported claim;
"identical to CellRanger" is not.

No change to the method is needed: the mode is named and documented as the
CellRanger variant. `test_EmptyDrops.cpp` pins the strict comparison. An
end-to-end comparison against a pinned CellRanger release would be needed before
claiming identical cell calls.

### Simple Good-Turing

`sgt.h` is Elworthy's C++ port of Gale & Sampson's reference code. It does a
log–log least-squares fit of Z_r, and switches from raw to smoothed estimates at
the first r where the two differ by less than 1.96 standard deviations. The paper relies on a
downward-sloping line ("guaranteed to satisfy" r* < r; observed slopes lie
between −1 and −2). The code does not check the slope. With a flat or rising fit,
r* can exceed r, and the ambient profile is distorted without notice. Suggested
change: expose the slope from `SGT::analyse()` and log a warning in
`emptyDrops_CR` when it is ≥ −1.

Sampson (*Empirical Linguistics*, 2001, ch. 7) gives the procedure step by step,
and `sgt.h` follows each step:
- Z_r = 2·n_r/(t − q) for neighbouring nonzero frequencies q < r < t, with
  t = 2r − q for the last row;
- least-squares fit of log Z_r on log r;
- the 1.96σ switch from raw (Turing) to smoothed (LGT) estimates, after which
  smoothed values are used for all higher r;
- a forced switch when n_{r+1} is missing, because the raw estimate is then
  undefined;
- P₀ = n₁/N for unseen items, then renormalisation of the seen probabilities.

The chapter's footnote 14 states the risk directly: regression returns a line
for any data, and the method is inappropriate if the frequency distribution is
not roughly log-linear. This supports the warning above. STAR divides P₀ evenly
among the genes with zero ambient count. The book gives only the total P₀ and
calls an equal split the simple default that a model of the unseen items can
beat, so this is a STAR choice, not a deviation.

### UMI deduplication

UMI-tools' directional rule links UMI a → b when they differ by one base and
n_a ≥ 2·n_b − 1. `umiArrayCorrect_Directional` with `dirCountAdd = -1`
(`1MM_Directional_UMItools`) applies the same inequality. UMIs are processed in
descending count order and each inherits its parent's correction, so chains
collapse onto the most abundant UMI along a single parent chain.
`1MM_Directional` (`dirCountAdd = 0`) is STAR's own, slightly stricter variant.

**The grouping differs.** The link threshold is the same, but STAR assigns each
UMI greedily to its first qualifying parent, while UMI-tools' directional method
groups every UMI reachable through the directed graph. These are not
equivalent. Counterexample (found in review): among the 144 synthetic cases of
three UMIs, each seen once, forming a connected one-mismatch path, the current
implementation reports 2 molecules in 36 cases; UMI-tools v1.1.6
`directional` puts all three in one group (1 molecule) in every case. This
shows the methods disagree on constructed inputs; it does not measure how often
the difference occurs, or how large it is, in real data. Verdict: threshold
consistent, aggregation different. Suggested action: add these cases as a
regression test, then either implement the graph grouping for
`1MM_Directional_UMItools` or document the difference.

### Multi-gene reads (EM)

The method matches §5.1.7 of the STARsolo paper:
- gene sets of a UMI's reads are intersected;
- `Uniform` gives 1/N to each gene;
- `Rescue` is one EM step;
- `EM` iterates with unique-gene counts held fixed. It starts from unique plus
  uniform counts and stops when the largest change is < 0.01, or after 100
  iterations.

**Defect.** Before each iteration, gene values below 0.01 are set to 0. A UMI
spreads one count over its gene set, so at least one of its genes holds
≥ 1/|set|. With more than 100 genes and no unique support, every gene of the set
can be below 0.01. All are then zeroed, `norm1 = 1/0`, and `0 · ∞` writes NaN
into the EM matrix. A replay of the loop (one UMI, no unique counts) is finite
for 50 and 100 genes and gives NaN for 101 and 150.

The gene set is the set of genes the UMI's reads overlap, not the set of
alignment loci, so `--outFilterMultimapNmax` does not bound it. A uniquely
mapped read that overlaps more than 100 annotated genes is enough. This was
reproduced with the real STAR binary at default limits, every read uniquely
mapped: 100 overlapping genes give finite counts, 101 give NaN for all 101
genes. The only setting required is `--soloMultiMappers EM`. Suggested fix: when a UMI's set sums to
zero after the cut, skip the cut for that UMI (fall back to the previous values).
That leaves every currently finite result unchanged.

### Velocyto

La Manno et al. (Methods, "Annotation of spliced and unspliced reads"): only
unique mappers, reads grouped per molecule, compatible transcripts intersected;
spliced if all reads are exonic in the compatible models, unspliced if every
compatible model has a spanning or intronic read, ambiguous otherwise.
`countVelocyto` intersects transcript sets per UMI (OR-ing the per-transcript
type bits), drops multi-gene UMIs and applies the same three rules.
`Transcriptome_classifyAlign` uses velocyto.py's constants (`MIN_FLANK=5`, no
intronic call across introns > 1 Mb).

Gap: the paper discards reads in repeat-masked regions; STARsolo has no
repeat-mask input. The workarounds are not equivalent and must be done at the
right level:
- Editing the GTF changes the annotation model (exon and intron boundaries,
  transcript compatibility), not just which reads are counted.
- Filtering the finished matrices cannot work: read and UMI assignment is gone
  by then.
- Reproducing velocyto's behaviour means removing reads that overlap masked
  regions at the read/BAM level, then recounting.

An opt-in `--soloVelocytoRepeatMask` (BED/GTF) that drops such reads before
velocyto classification would close the gap.

### WASP

The paper's test: swap the alleles of each SNP a read overlaps, re-map, and
discard the read unless every version maps to the same unique location.
`waspMap` re-maps all 2^n allele combinations (up to 10 SNPs; more is flagged
`vW:7`), and requires a unique mapping with identical exon blocks. Homozygous
genotypes are excluded at VCF load (`Variation.cpp`). Not part of STAR, and not
expected to be: WASP's random duplicate removal and its combined haplotype test.
Minor: the comparison checks exon read/genome start and length but not strand. A
same-coordinate opposite-strand remap is practically impossible.

### Chimeric detection

STAR-Fusion uses `Chimeric.out.junction`. Haas et al. ran STAR with
`--chimSegmentMin 12 --chimJunctionOverhangMin 12 --chimOutJunctionFormat 1
--alignSJDBoverhangMin 10 --alignMatesGapMax 100000 --alignIntronMax 100000
--alignSJstitchMismatchNmax 5 -1 5 5 --outSAMstrandField intronMotif`, plus the
multimapping options listed below. All these options still exist.

STAR-Cross changed default chimeric scoring (commit `29be20a`; `--legacy`
restores upstream 2.7.11b):
- the junction exon pair is chosen as the pair that overlaps most in read space
  and lies on different chromosomes;
- exons on the far side of the junction are trimmed before stitching;
- segment overlap is computed per exon block;
- the a2 negative-strand coordinate uses `a2.Lread` instead of `a1.Lread`.

Observations:
- The new exon-pair choice skips same-chromosome pairs ("same chromosome —
  splice, not chimeric") and falls back to the upstream positional choice.
  Intra-chromosomal chimeras (strand switches, distant same-chromosome
  breakpoints) therefore keep the old selection, while inter-chromosomal ones
  get the new one. The comment is inaccurate; the behaviour is consistent but
  asymmetric.
- The new scoring lives only in the multimapping path. `ReadAlign_chimericDetection.cpp`
  calls `chimericDetectionOld()` when `--chimMultimapNmax 0` (the default) and
  `chimericDetectionMult()` otherwise; `legacyScoring` is read only by
  `ChimericAlign*` code used from the Mult path. With the default
  `--chimMultimapNmax 0`, default and `--legacy` produce the same chimeric output.
- STAR-Fusion's published command (Haas et al. 2019, Methods, STAR 2.6.1a) also
  sets `--chimMultimapScoreRange 10 --chimMultimapNmax 10
  --chimNonchimScoreDropMin 10 --twopassMode Basic`, so
  STAR-Fusion runs the Mult path and **is affected** by the new default scoring.
  Its published accuracy was measured with upstream scoring.
- Arriba's published command (Uhrig et al. 2021, STAR 2.5.3a:
  `--chimSegmentMin 10 --chimJunctionOverhangMin 10 --chimScoreDropMax 30
  --chimScoreJunctionNonGTAG 0 --chimScoreSeparation 1 --chimSegmentReadGapMax 3
  --chimMainSegmentMultNmax 10`) leaves `--chimMultimapNmax` at 0, so that
  configuration uses the unchanged Old path. Arriba's later documentation adds
  `--chimMultimapNmax`, which would reach the changed code.
- Arriba reads chimeric alignments from `Chimeric.out.sam` or from the BAM
  (`--chimOutType WithinBAM`), not from `Chimeric.out.junction`. A compatibility
  check must cover the SAM/BAM chimeric records (supplementary flags, `SA` tags)
  as well as the junction file.
- No test compares default and `--legacy` outputs, or either against simulated
  fusions with known truth. `test_cpu_upstream.py` exercises chimeric output for
  teardown only. The cherry-pick plan
  (`.agentdocs/workflow/260413-ggpeti-chimscore-cherry-pick-plan.md`) lists an
  acceptance-test port as phase 2; it was not done.

Recommendation: run both scorings with the STAR-Fusion command above (which
sets `--chimMultimapNmax 10`) on a simulated fusion set and compare true/false
fusion calls. Haas et al. published such sets alongside their benchmark (data, not a paper; see
below). Port the planned acceptance test. Until then, document `--legacy` as the
upstream-compatible setting for `--chimMultimapNmax > 0`.

### Alignment scoring

Durbin et al. (ch. 2) and Smith & Waterman define local alignment with affine gap
costs. STAR's stitching uses affine deletion and insertion penalties
(`scoreDelOpen/Base`, `scoreInsOpen/Base`), a separate junction penalty
(`scoreGap*`, by motif) and extension that keeps the best-scoring local end.
This matches the classical model, extended with splice-specific gap costs.
`ClipCR4` (CellRanger4 adapter clipping) is an inter-sequence vectorized aligner
(64 reads side by side) rather than Farrar's striped layout. Its equivalence to
the earlier Opal/Parasail results is covered by the repository's own tests; no
paper describes CellRanger4's clipping rules.

### Benchmark method

Aligned with the literature:
- every run is a separate process, the highest level of random variation
  (Kalibera & Jones);
- results are a paired ratio with a bootstrap 95% interval, not a significance
  test (Kalibera & Jones; Georges et al.);
- A/A calibration is required before trusting small effects;
- ABBA ordering cancels drift;
- the background-load gate is recorded.

Gap: Mytkowicz et al. show environment size and link order alone move timings by
several percent, and recommend setup randomization. The harness runs one build
of each binary in one environment. Measured effects so far (8–22% for huge
pages, 2× on Windows) are well above that. A claim below about 5% should be
repeated with varied environment sizes (e.g. a padding variable) or relinked builds.

### Huge pages

Drepper (§4.3.2 "Influencing TLB Performance", §6.2.4 "Optimizing TLB Usage")
and Ingens support huge pages for large, randomly accessed working sets, which
is the case for the genome and suffix array. Ingens documents the risks: memory
bloat and latency from synchronous compaction. Both were measured: the WSL2
outliers, and 65k compaction stalls with no outlier runs on a KVM guest. See
[the backlog](CXX20_OPTIMIZATION_BACKLOG.md).

## Missing material

Identifiers checked against Crossref, Zenodo and Open Library on 2026-11-01.
Items 4–7 were received and are used above; items 1–3 are still to download.

| # | Full title | Authors, venue, year | DOI / ISBN | Unblocks |
|---|---|---|---|---|
| 1 | Benchmarking Illumina RNA-seq fusion transcript detection methods – simulated 101 base PE reads (dataset) | Haas, Zenodo, 2019 | [10.5281/zenodo.13359589](https://doi.org/10.5281/zenodo.13359589) | Truth-based comparison of default vs `--legacy` chimeric scoring. 31.9 GB; one sample is enough to start: `sim1_reads.zip` (6.4 GB) plus `sim_101PE_metadata.zip` |
| 2 | Benchmarking Illumina RNA-seq fusion transcript detection methods – simulated 50 base PE reads (dataset) | Haas, Zenodo, 2019 | [10.5281/zenodo.13354907](https://doi.org/10.5281/zenodo.13354907) | Optional short-read version of the same test (12.8 GB) |
| 3 | Supplementary Material of "STAR: ultrafast universal RNA-seq aligner" | Dobin et al., *Bioinformatics*, 2013 | [10.1093/bioinformatics/bts635](https://doi.org/10.1093/bioinformatics/bts635) (supplement on the article page) | Original description of chimeric detection and stitching scores |
| 4 (received) | Accurate and efficient detection of gene fusions from RNA sequencing data (Arriba) | Uhrig et al., *Genome Research*, 2021 | [10.1101/gr.257246.119](https://doi.org/10.1101/gr.257246.119) | Second major user of STAR's chimeric output: which fields must stay compatible |
| 5 (received) | Permutation p-values should never be zero: calculating exact p-values when permutations are randomly drawn | Phipson & Smyth, *Stat. Appl. Genet. Mol. Biol.*, 2010 | [10.2202/1544-6115.1585](https://doi.org/10.2202/1544-6115.1585) | The p-value formula EmptyDrops cites (`<` vs `≤`) |
| 6 (received) | Controlling the false discovery rate: a practical and powerful approach to multiple testing | Benjamini & Hochberg, *J. R. Stat. Soc. B*, 1995 | [10.1111/j.2517-6161.1995.tb02031.x](https://doi.org/10.1111/j.2517-6161.1995.tb02031.x) | EmptyDrops' FDR correction |
| 7 (received) | Empirical Linguistics | Geoffrey Sampson, Continuum, 2001 | ISBN 978-0-8264-5794-3 or 978-0-8264-4883-5 (two bindings) | Sampson's reference Good-Turing code and its slope check |

The paper's original data link
(`data.broadinstitute.org/Trinity/STAR_FUSION_PAPER/.../sim_reads/`) returns 404;
the Zenodo records are named in the README of the Broad server's
`CTAT_FUSIONTRANS_BENCHMARKING/on_simulated_data/` directory. The paper used the
GRCh37 / GENCODE v19 reference.

Public code, fetched directly when needed (no DOI):
- **velocyto.py** at the version STAR targets: its `Permissive10X` logic differs
  from the paper's text in details;
- **UMI-tools** (`network.py`), to confirm the multi-parent case;
- **STAR-Fusion benchmarking repository** (`STAR-Fusion/STAR-Fusion_benchmarking_data`
  on GitHub), which holds the simulated truth sets.

The books were used for the points above (Mäkinen ch. 16, Durbin ch. 2, Drepper
ch. 4 and 6, Sampson ch. 7, and the sections already cited in
[the literature optimization plan](LITERATURE_OPTIMIZATION_PLAN.md)). They were
not reviewed cover to cover.
