# References

Books and papers behind STAR and STARsolo, and literature that could guide
future STAR-Cross work. DOIs and ISBNs were checked against Crossref and Open
Library on 2026-11-01. Years are print years; Crossref sometimes lists the
earlier online year (e.g. 2012 for the STAR paper).

## Books

| # | Title | Authors | Publisher, year | ISBN (print) | DOI | Relevance |
|---|---|---|---|---|---|---|
| 1 | Algorithms on Strings, Trees, and Sequences | Dan Gusfield | Cambridge UP, 1997 | 978-0-521-58519-4 | [10.1017/CBO9780511574931](https://doi.org/10.1017/CBO9780511574931) | Suffix arrays and exact matching (the theory behind seed search) |
| 2 | Compact Data Structures | Gonzalo Navarro | Cambridge UP, 2016 | 978-1-107-15238-0 | [10.1017/CBO9781316588284](https://doi.org/10.1017/CBO9781316588284) | Smaller index structures |
| 3 | Pearls of Algorithm Engineering | Paolo Ferragina | Cambridge UP, 2023 | 978-1-009-12328-0 | [10.1017/9781009128933](https://doi.org/10.1017/9781009128933) | Cache-aware search, suffix-array engineering |
| 4 | Genome-Scale Algorithm Design, 2nd ed. | Mäkinen, Belazzougui, Cunial, Tomescu | Cambridge UP, 2023 (1st ed. 2015: ISBN 978-1-107-07853-6) | 978-1-009-34123-3 | [10.1017/9781009341257](https://doi.org/10.1017/9781009341257) | BWT and FM-indexes, read alignment, splicing |
| 5 | Biological Sequence Analysis | Durbin, Eddy, Krogh, Mitchison | Cambridge UP, 1998 | 978-0-521-62971-3 (pbk), 978-0-521-62041-3 (hbk) | [10.1017/CBO9780511790492](https://doi.org/10.1017/CBO9780511790492) | Alignment scoring |
| 6 | What Every Programmer Should Know About Memory | Ulrich Drepper | Red Hat, 2007 (free PDF) | none | none | TLBs, huge pages, memory latency |

## What STAR and STARsolo build on

| # | Title | First author | Venue, year | DOI | Relevance |
|---|---|---|---|---|---|
| 1 | STAR: ultrafast universal RNA-seq aligner | Dobin | *Bioinformatics*, 2013 | [10.1093/bioinformatics/bts635](https://doi.org/10.1093/bioinformatics/bts635) | The STAR algorithm |
| 2 | STARsolo: accurate, fast and versatile mapping/quantification of single-cell and single-nucleus RNA-seq data | Kaminow | bioRxiv, 2021 | [10.1101/2021.05.05.442755](https://doi.org/10.1101/2021.05.05.442755) | STARsolo |
| 3 | Suffix arrays: a new method for on-line string searches | Manber | *SIAM J. Computing*, 1993 | [10.1137/0222058](https://doi.org/10.1137/0222058) | STAR's index structure |
| 4 | Identification of common molecular subsequences | Smith | *J. Mol. Biol.*, 1981 | [10.1016/0022-2836(81)90087-5](https://doi.org/10.1016/0022-2836(81)90087-5) | Local alignment scoring |
| 5 | Striped Smith–Waterman speeds database searches six times over other SIMD implementations | Farrar | *Bioinformatics*, 2007 | [10.1093/bioinformatics/btl582](https://doi.org/10.1093/bioinformatics/btl582) | SIMD alignment (the clipping libraries were built on it) |
| 6 | Parasail: SIMD C library for global, semi-global, and local pairwise sequence alignments | Daily | *BMC Bioinformatics*, 2016 | [10.1186/s12859-016-0930-z](https://doi.org/10.1186/s12859-016-0930-z) | Former clipping library, now replaced by `ClipCR4` |
| 7 | Opal (SIMD Smith–Waterman library) | Šošić | software, no paper | none ([github.com/Martinsos/opal](https://github.com/Martinsos/opal)) | Original CellRanger4 clipping |
| 8 | EmptyDrops: distinguishing cells from empty droplets in droplet-based single-cell RNA sequencing data | Lun | *Genome Biology*, 2019 | [10.1186/s13059-019-1662-y](https://doi.org/10.1186/s13059-019-1662-y) | `EmptyDrops_CR` |
| 9 | Good-Turing frequency estimation without tears | Gale | *J. Quantitative Linguistics*, 1995 | [10.1080/09296179508590051](https://doi.org/10.1080/09296179508590051) | Bundled `SimpleGoodTuring` |
| 10 | RNA velocity of single cells | La Manno | *Nature*, 2018 | [10.1038/s41586-018-0414-6](https://doi.org/10.1038/s41586-018-0414-6) | `--soloFeatures Velocyto` |
| 11 | WASP: allele-specific software for robust molecular quantitative trait locus discovery | van de Geijn | *Nature Methods*, 2015 | [10.1038/nmeth.3582](https://doi.org/10.1038/nmeth.3582) | `--waspOutputMode` |
| 12 | Massively parallel digital transcriptional profiling of single cells | Zheng | *Nature Communications*, 2017 | [10.1038/ncomms14049](https://doi.org/10.1038/ncomms14049) | 10x / CellRanger design |
| 13 | UMI-tools: modeling sequencing errors in Unique Molecular Identifiers to improve quantification accuracy | Smith | *Genome Research*, 2017 | [10.1101/gr.209601.116](https://doi.org/10.1101/gr.209601.116) | UMI deduplication |
| 14 | Accuracy assessment of fusion transcript detection via read-mapping and de novo fusion transcript assembly-based methods | Haas | *Genome Biology*, 2019 | [10.1186/s13059-019-1842-9](https://doi.org/10.1186/s13059-019-1842-9) | STAR-Fusion (uses chimeric output) |
| 15 | Two-pass alignment improves novel splice junction quantification | Veeneman | *Bioinformatics*, 2016 | [10.1093/bioinformatics/btv642](https://doi.org/10.1093/bioinformatics/btv642) | 2-pass mode |

## Papers that could help future work

| # | Title | First author | Venue, year | DOI | Relevance |
|---|---|---|---|---|---|
| 1 | Sapling: accelerating suffix array queries with learned data models | Kirsche | *Bioinformatics*, 2021 | [10.1093/bioinformatics/btaa911](https://doi.org/10.1093/bioinformatics/btaa911) | Learned seed-search hints (plan P2) |
| 2 | The Case for Learned Index Structures | Kraska | SIGMOD, 2018 | [10.1145/3183713.3196909](https://doi.org/10.1145/3183713.3196909) | Idea behind Sapling and PLA |
| 3 | Accelerating minimap2 for long-read sequencing applications on modern CPUs (mm2-fast) | Kalikar | *Nature Computational Science*, 2022 | [10.1038/s43588-022-00201-8](https://doi.org/10.1038/s43588-022-00201-8) | CPU optimisation without output changes (plan P1) |
| 4 | mm2-gb: GPU Accelerated Minimap2 for Long Read DNA Mapping | Dong | bioRxiv 2024 / ACM BCB 2024 | [10.1101/2024.03.23.586366](https://doi.org/10.1101/2024.03.23.586366), [10.1145/3698587.3701366](https://doi.org/10.1145/3698587.3701366) | GPU chaining (plan P4, CUDA path) |
| 5 | Minimap2: pairwise alignment for nucleotide sequences | Li | *Bioinformatics*, 2018 | [10.1093/bioinformatics/bty191](https://doi.org/10.1093/bioinformatics/bty191) | STARlong design ideas |
| 6 | Efficient Architecture-Aware Acceleration of BWA-MEM for Multicore Systems (BWA-MEM2) | Vasimuddin | IPDPS, 2019 | [10.1109/ipdps.2019.00041](https://doi.org/10.1109/ipdps.2019.00041) | Faster aligner with identical output |
| 7 | Graph-based genome alignment and genotyping with HISAT2 and HISAT-genotype | Kim | *Nature Biotechnology*, 2019 | [10.1038/s41587-019-0201-4](https://doi.org/10.1038/s41587-019-0201-4) | Alternative spliced-aligner design |
| 8 | Alevin efficiently estimates accurate gene abundances from dscRNA-seq data | Srivastava | *Genome Biology*, 2019 | [10.1186/s13059-019-1670-y](https://doi.org/10.1186/s13059-019-1670-y) | STARsolo comparison |
| 9 | Alevin-fry unlocks rapid, accurate and memory-frugal quantification of single-cell RNA-seq data | He | *Nature Methods*, 2022 | [10.1038/s41592-022-01408-3](https://doi.org/10.1038/s41592-022-01408-3) | STARsolo comparison |
| 10 | Modular, efficient and constant-memory single-cell RNA-seq preprocessing (kallisto\|bustools) | Melsted | *Nature Biotechnology*, 2021 | [10.1038/s41587-021-00870-2](https://doi.org/10.1038/s41587-021-00870-2) | STARsolo comparison |
| 11 | Generalizing RNA velocity to transient cell states through dynamical modeling (scVelo) | Bergen | *Nature Biotechnology*, 2020 | [10.1038/s41587-020-0591-3](https://doi.org/10.1038/s41587-020-0591-3) | Main user of Velocyto output |
| 12 | Simulation-based comprehensive benchmarking of RNA-seq aligners | Baruzzo | *Nature Methods*, 2017 | [10.1038/nmeth.4106](https://doi.org/10.1038/nmeth.4106) | Ground-truth validation data |
| 13 | Systematic evaluation of spliced alignment programs for RNA-seq data (RGASP) | Engström | *Nature Methods*, 2013 | [10.1038/nmeth.2722](https://doi.org/10.1038/nmeth.2722) | Ground-truth validation data |
| 14 | Producing wrong data without doing anything obviously wrong! | Mytkowicz | ASPLOS, 2009 | [10.1145/1508244.1508275](https://doi.org/10.1145/1508244.1508275) | Why benchmarks alternate run order (ABBA) |
| 15 | Rigorous benchmarking in reasonable time | Kalibera | ISMM, 2013 | [10.1145/2464157.2464160](https://doi.org/10.1145/2464157.2464160) | Paired ratios and A/A calibration |
| 16 | Statistically rigorous Java performance evaluation | Georges | OOPSLA, 2007 | [10.1145/1297027.1297033](https://doi.org/10.1145/1297027.1297033) | Benchmark statistics |
| 17 | Coordinated and Efficient Huge Page Management with Ingens | Kwon | OSDI, 2016 | none (USENIX; short version in *SIGOPS OSR* 2017, [10.1145/3139645.3139659](https://doi.org/10.1145/3139645.3139659)) | Huge-page latency spikes |

## Methods cited by STAR's components

| # | Title | First author | Venue, year | DOI / ISBN | Relevance |
|---|---|---|---|---|---|
| 1 | Accurate and efficient detection of gene fusions from RNA sequencing data (Arriba) | Uhrig | *Genome Research*, 2021 | [10.1101/gr.257246.119](https://doi.org/10.1101/gr.257246.119) | Second major user of chimeric output |
| 2 | Permutation p-values should never be zero: calculating exact p-values when permutations are randomly drawn | Phipson | *Stat. Appl. Genet. Mol. Biol.*, 2010 | [10.2202/1544-6115.1585](https://doi.org/10.2202/1544-6115.1585) | EmptyDrops p-values |
| 3 | Controlling the false discovery rate: a practical and powerful approach to multiple testing | Benjamini | *J. R. Stat. Soc. B*, 1995 | [10.1111/j.2517-6161.1995.tb02031.x](https://doi.org/10.1111/j.2517-6161.1995.tb02031.x) | EmptyDrops FDR correction |
| 4 | Empirical Linguistics | Sampson | Continuum, 2001 | ISBN 978-0-8264-5794-3 / 978-0-8264-4883-5 | Reference Simple Good-Turing code |

## Datasets

| # | Title | Creator | Publisher, year | DOI | Relevance |
|---|---|---|---|---|---|
| 1 | Benchmarking Illumina RNA-seq fusion transcript detection methods – simulated 101 base PE reads | Haas | Zenodo, 2019 | [10.5281/zenodo.13359589](https://doi.org/10.5281/zenodo.13359589) | Chimeric detection truth set (31.9 GB) |
| 2 | Benchmarking Illumina RNA-seq fusion transcript detection methods – simulated 50 base PE reads | Haas | Zenodo, 2019 | [10.5281/zenodo.13354907](https://doi.org/10.5281/zenodo.13354907) | Same, 50-base reads (12.8 GB) |

Not found on Crossref: the **PLA-index** and **mm2-plus** papers used in
[the literature optimization plan](LITERATURE_OPTIMIZATION_PLAN.md). Take their
titles and DOIs from the local copies that plan refers to.
