# STAR-Cross

A cross-platform derivative of [STAR](https://github.com/alexdobin/STAR), the
spliced RNA-seq aligner and STARsolo single-cell quantification tool.

STAR-Cross adds native Windows builds, Apple Silicon builds and an optional
browser interface. It also includes portability fixes and CPU optimizations.
Existing STAR command-line workflows remain the starting point; compatibility
and performance are validated for specific test profiles, not guaranteed for
every dataset or option.

## Download

Get binaries from [STAR-Cross Releases](https://github.com/birdingman0626/STAR-Cross/releases).
Asset availability depends on the release:

| Platform | Asset |
| --- | --- |
| Linux x86-64 | `STAR-linux-x86_64.tar.gz` |
| macOS Apple Silicon | `STAR-macos-aarch64.tar.gz` |
| Windows x86-64 | `STAR-windows-x86_64.zip` |
| macOS Intel | `STAR-macos-x86_64.tar.gz` |
| Linux s390x | `STAR-linux-s390x.tar.gz` |

On Windows, extract the ZIP and keep its runtime DLLs beside `STAR.exe`.
On Linux/macOS, extract the archive to obtain `STAR`. Each package includes
the required license texts; build records remain in CI rather than separate
Release downloads. Examples below assume
the executable is named `STAR` and is on your PATH; use `STAR.exe` on Windows.

```sh
STAR --version
```

Release tags use the commit hash; the binary reports `STAR-Cross <commit>`.
Older binaries retain their original version strings. AVX2 is disabled by default;
enable it for compatible x86-64 CPUs with `-DSTAR_USE_AVX2=ON`.

## Quick start

With an existing compatible STAR genome index and paired, uncompressed FASTQ files:

```sh
STAR --runThreadN 8 --genomeDir /path/to/index \
  --readFilesIn reads_R1.fastq reads_R2.fastq \
  --outSAMtype BAM SortedByCoordinate --outFileNamePrefix results/
```

This is an ordinary RNA-seq example, not a STARsolo chemistry preset. For
single-cell barcode/UMI settings, see the [STARsolo guide](docs/STARsolo.md).
For index generation and alignment parameters, see the
[upstream STAR manual](https://github.com/alexdobin/STAR/blob/master/doc/STARmanual.pdf).

Prefer a browser interface? Start the local server:

```sh
STAR --runMode webui --webuiPort 8080 --outFileNamePrefix results/
```

Open `http://127.0.0.1:8080`. The interface offers job submission, a command
preview, queue status and logs. See [Web UI usage](docs/webui/README.md).

## Differences and limits

- **Native platforms:** Windows and macOS ARM builds use the same C++ codebase
  as Linux. Adapter clipping uses Parasail; it no longer uses the old bundled Opal/SIMDe implementation.
- **CPU optimizations:** dense-index seed search avoids temporary allocations;
  other changes address index generation, per-read resets and UMI processing.
  See [measured results and rejected experiments](docs/OPTIMIZATION_RESULTS_20261006.md).
- **Additional output:** optional referenceless CRAM output transcodes the main
  BAM outputs at finalization. Transcriptome output remains BAM. Conversion
  failure preserves the BAM; inspect run logs rather than assuming CRAM was produced.
- **Algorithm differences:** chimeric scoring fixes can change alignments.
  `--legacy` selects the upstream-style chimeric scoring path; it is not a
  blanket guarantee of upstream-identical results. Unsafe stitching
  branch-and-bound pruning was removed.
- **Windows limits:** shared-memory genome loading is unavailable; use
  `--genomeLoad NoSharedMemory`. `--readFilesCommand` uses temporary files rather
  than FIFO streaming, which can require additional disk space.
- **CUDA is experimental and OFF by default.** The junction-index adapter and
  isolated seed-search benchmark do not establish production read-alignment
  acceleration or an end-to-end speedup. See [GPU experiments](docs/GPU_EXPERIMENT.md).

## Validation

Tests cover unit-level behavior and miniature end-to-end comparisons of
alignment records, junctions and STARsolo outputs, including all three native
Velocity matrices. Real-subset benchmarks record inputs, commands and output
comparisons. Cross-platform builds and individual releases have their own
qualification boundaries.

Do not interpret successful tests as universal byte identity with upstream, or
a workload-specific speedup as a full-library guarantee. See
[CPU validation](docs/CPU_VALIDATION.md) and
[optimization verification](docs/OPTIMIZATION_RESULTS_20261006.md) for scope and evidence.

## Build and support

- [Build from source](docs/BUILD.md): Linux, macOS and Windows; build options and tests.
- [Report a STAR-Cross issue](https://github.com/birdingman0626/STAR-Cross/issues):
  include `--version`, platform, command, relevant logs and a small reproducible
  example when possible. Remove credentials and sensitive sample information.

STAR was developed by Alexander Dobin and contributors. STAR-Cross retains
upstream attribution and is distributed under the [MIT license](LICENSE).
For the original STAR paper and citation guidance, see the
[upstream README](https://github.com/alexdobin/STAR#readme).
