# STARsolo Quality Report Generator

## Status: Plan Complete — Awaiting Execution
Last updated: 2026-04-13

## Goal

Generate a self-contained HTML quality report from STARsolo output (Solo.out/) without requiring BAM files or external dependencies (Python, R). Built in C++ as part of STAR itself — same binary, new `--runMode soloQC` subcommand.

## Design: C++ Stats + Client-Side Rendering

The trick is to **not** generate plots in C++. Instead:

1. **C++ parses** Solo.out files and computes all statistics
2. **C++ emits** a self-contained HTML file with stats embedded as JSON
3. **Chart.js** (embedded inline in the HTML) renders interactive plots in the browser

This means:
- Zero external dependencies — no Python, no R, no matplotlib
- Interactive plots (hover, zoom) — better than static PNGs
- Single HTML file — all JS/CSS/data inline, works offline
- Chart.js is 200KB minified — embedded once via `xxd` at compile time (same pattern as `parametersDefault.xxd`)

---

## Available Inputs (No BAM Required)

| File | Content | Used For |
|---|---|---|
| `Gene/Summary.csv` | 20 key metrics | Overview dashboard |
| `GeneFull_Ex50pAS/Summary.csv` | Same for intron-inclusive counting | Gene vs GeneFull comparison |
| `Barcodes.stats` | Barcode matching breakdown | Barcode QC |
| `Gene/Features.stats` | Read-to-feature assignment | Feature assignment QC |
| `Gene/UMIperCellSorted.txt` | Descending UMI counts per barcode | Knee plot |
| `Gene/filtered/matrix.mtx` | Sparse count matrix (filtered cells) | Per-cell QC |
| `Gene/filtered/barcodes.tsv` | Cell barcodes | Cell IDs |
| `Gene/filtered/features.tsv` | Gene IDs and names | Gene annotation, mito detection |
| `Gene/raw/matrix.mtx` | All barcodes matrix | Background comparison |

---

## Architecture

```
STAR --runMode soloQC --soloQCdir Solo.out/ --soloQCout report.html
     │
     ├── Parse Summary.csv (Gene + GeneFull)     → JSON: summaryData
     ├── Parse Barcodes.stats                     → JSON: barcodeData
     ├── Parse Features.stats                     → JSON: featureData
     ├── Parse UMIperCellSorted.txt               → JSON: kneeData (subsampled to ~5K points)
     ├── Parse filtered/matrix.mtx (sparse)       → JSON: cellStats {umis[], genes[], mito[]}
     ├── Compute per-gene stats from matrix       → JSON: geneStats {top20[], detectionCurve[]}
     ├── Match mitochondrial genes from features  → JSON: mitoGenes[]
     │
     └── Emit HTML:
         ├── <style> inline CSS </style>
         ├── <script> Chart.js (200KB, compiled in via xxd) </script>
         ├── <script> const data = { summaryData, barcodeData, ... } </script>
         └── <script> render charts using data </script>
```

### Compile-Time Embedding

Same approach STAR uses for `parametersDefault`:

```cmake
# In CMakeLists.txt, add:
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/chartjs.xxd
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/xxd.cmake
            ${CMAKE_SOURCE_DIR}/soloqc/chart.min.js
            ${CMAKE_BINARY_DIR}/chartjs.xxd
            chartjs_min_js
    DEPENDS ${CMAKE_SOURCE_DIR}/soloqc/chart.min.js
)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/qctemplate.xxd
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/xxd.cmake
            ${CMAKE_SOURCE_DIR}/soloqc/template.html
            ${CMAKE_BINARY_DIR}/qctemplate.xxd
            qc_template_html
    DEPENDS ${CMAKE_SOURCE_DIR}/soloqc/template.html
)
```

In C++:
```cpp
#include "chartjs.xxd"    // unsigned char chartjs_min_js[], unsigned int chartjs_min_js_len
#include "qctemplate.xxd" // unsigned char qc_template_html[], unsigned int qc_template_html_len
```

---

## Report Sections

### Section 1: Run Overview Dashboard

**Source:** `Summary.csv`

Parse the key=value CSV and display as a table with PASS/WARN/FAIL badges.

**C++ parsing:**
```cpp
// Summary.csv is simple: "Key,Value\n" per line
ifstream f("Gene/Summary.csv");
string line;
map<string, string> summary;
while (getline(f, line)) {
    auto pos = line.find(',');
    summary[line.substr(0, pos)] = line.substr(pos + 1);
}
```

**Thresholds (compiled in):**

| Metric | PASS | WARN | FAIL |
|---|---|---|---|
| Valid Barcodes | ≥ 90% | ≥ 80% | < 80% |
| Sequencing Saturation | ≥ 80% | ≥ 50% | < 50% |
| Q30 CB+UMI | ≥ 90% | ≥ 80% | < 80% |
| Mapped Unique | ≥ 50% | ≥ 30% | < 30% |
| Median UMI/Cell | ≥ 500 | ≥ 200 | < 200 |
| Median Genes/Cell | ≥ 200 | ≥ 100 | < 100 |

**Output JSON:** `summaryData: { gene: {key: val, ...}, geneFull: {key: val, ...}, thresholds: {...} }`

### Section 2: Barcode Quality

**Source:** `Barcodes.stats`

Parse the whitespace-delimited `key value` format. Compute percentages relative to total reads.

**Chart:** Stacked horizontal bar (Chart.js `type: 'bar'`, `indexAxis: 'y'`) showing:
- Exact whitelist match (green)
- 1MM correction (yellow)
- Multi-match (orange)
- No match / N-in-barcode / homopolymer (red shades)

### Section 3: Knee Plot

**Source:** `UMIperCellSorted.txt`

This file has ~770K lines (one UMI count per barcode). For Chart.js performance, subsample to ~5,000 points using logarithmic spacing:

```cpp
// Read all values
vector<uint32_t> umis;
// ... read file ...

// Subsample logarithmically for plotting
vector<pair<uint32_t, uint32_t>> kneePoints; // (rank, umi_count)
double step = log(umis.size()) / 5000.0;
for (int i = 0; i < 5000; i++) {
    uint32_t idx = min((uint32_t)exp(i * step), (uint32_t)(umis.size() - 1));
    kneePoints.push_back({idx + 1, umis[idx]});
}
```

**Chart:** Log-log scatter plot (Chart.js `type: 'scatter'` with logarithmic axes). Vertical dashed line at estimated cell count from Summary.csv.

### Section 4: Per-Cell Distributions

**Source:** `Gene/filtered/matrix.mtx` + `features.tsv`

**Matrix Market parsing in C++:**
```cpp
// Skip header lines starting with % or %%
// Read dimensions: nGenes nCells nEntries
// Read triplets: geneIdx cellIdx count
// Accumulate per-cell: umis_per_cell[cellIdx] += count
//                       genes_per_cell[cellIdx] += (first time seeing gene for this cell)
// Accumulate mito:     mito_per_cell[cellIdx] += count  (if gene is mitochondrial)

ifstream f("filtered/matrix.mtx");
string line;
// Skip comments
while (getline(f, line) && line[0] == '%') {}
// Parse dimensions
uint32_t nGenes, nCells, nEntries;
istringstream(line) >> nGenes >> nCells >> nEntries;
// Parse entries
vector<uint32_t> umisPerCell(nCells, 0);
vector<uint32_t> genesPerCell(nCells, 0);
vector<uint32_t> mitoPerCell(nCells, 0);
vector<unordered_set<uint32_t>> seenGenes(nCells); // for gene counting

uint32_t g, c, v;
while (f >> g >> c >> v) {
    g--; c--;  // 1-indexed to 0-indexed
    umisPerCell[c] += v;
    if (seenGenes[c].insert(g).second) genesPerCell[c]++;
    if (isMito[g]) mitoPerCell[c] += v;
}
```

**Memory note:** For 4,426 cells × 29K genes with 6.3M entries, the `seenGenes` sets use ~25MB. Acceptable. For very large datasets (>50K cells), switch to a bitset per cell.

**Charts:**
- Violin/histogram of UMIs per cell (Chart.js box plot plugin or histogram)
- Violin/histogram of genes per cell
- Scatter: genes vs UMIs (colored by mito fraction)
- Histogram of mitochondrial fraction with 20% threshold line

### Section 5: Feature Assignment

**Source:** `Features.stats`

Same parsing as Barcodes.stats. Key categories:
- UniqueFeature (green) — assigned to exactly one gene
- noNoFeature (gray) — mapped but intergenic
- MultiFeature (orange) — ambiguous
- noUnmapped (red) — unmapped

**Chart:** Doughnut chart (Chart.js `type: 'doughnut'`).

### Section 6: Gene Detection

**Source:** Computed from matrix parsing in Section 4.

```cpp
// Per-gene: total UMIs and number of cells detected
vector<uint32_t> umisPerGene(nGenes, 0);
vector<uint32_t> cellsPerGene(nGenes, 0);
// Accumulate during matrix parsing (same loop as Section 4)
```

**Charts:**
- Top 20 genes by total UMIs (horizontal bar chart, colored by type: ribosomal/mito/other)
- Gene detection curve: genes sorted by number of cells detected in

### Section 7: Gene vs GeneFull Comparison

**Source:** Both Summary.csv files.

Side-by-side grouped bar chart of key metrics. Highlights the intron-capture ratio.

---

## STAR Integration

### New RunMode

Add `--runMode soloQC` to the existing mode dispatch in `STAR.cpp`:

```cpp
if (P.runMode == "soloQC") {
    soloQCReport(P);
    exit(0);
}
```

### New Parameters

```
--soloQCdir     <path>    Solo.out directory to analyze (default: ./Solo.out)
--soloQCout     <path>    Output HTML file (default: Solo.out/qc_report.html)
--soloQCmito    <pattern> Mitochondrial gene regex (default: "^MT-|^mt-")
--soloQCcompare <path>    Optional second Solo.out for comparison
```

### Source Files

```
source/
├── SoloQC.h                    # QC report class declaration
├── SoloQC_report.cpp           # Main orchestrator
├── SoloQC_parseSummary.cpp     # Summary.csv parser
├── SoloQC_parseStats.cpp       # Barcodes.stats + Features.stats parser
├── SoloQC_parseMatrix.cpp      # Matrix Market parser + per-cell/gene stats
├── SoloQC_emitHTML.cpp         # HTML generation with embedded data
├── soloqc/
│   ├── chart.min.js            # Chart.js v4 minified (~200KB)
│   └── template.html           # HTML template with Chart.js rendering code
```

Follows STAR's file-per-method convention (`ClassName_methodName.cpp`).

### Build Integration

```cmake
# Generate xxd headers for embedded resources
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/chartjs.xxd ...)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/qctemplate.xxd ...)

# Add QC sources to STAR target
target_sources(STAR PRIVATE
    SoloQC_report.cpp
    SoloQC_parseSummary.cpp
    SoloQC_parseStats.cpp
    SoloQC_parseMatrix.cpp
    SoloQC_emitHTML.cpp
)
```

Binary size increase: ~200KB (Chart.js). Negligible compared to STAR's 3MB binary.

---

## HTML Template Structure

```html
<!DOCTYPE html>
<html>
<head>
  <title>STARsolo QC Report</title>
  <style>
    /* Inline CSS: dashboard layout, tables, badges */
    .badge-pass { background: #22c55e; }
    .badge-warn { background: #f59e0b; }
    .badge-fail { background: #ef4444; }
    /* ... */
  </style>
</head>
<body>
  <h1>STARsolo QC Report</h1>
  <p>Generated by STAR 2.7.11b | {{date}} | {{solo_dir}}</p>

  <section id="overview"><!-- Summary table with badges --></section>
  <section id="barcodes"><canvas id="barcodeChart"></canvas></section>
  <section id="knee"><canvas id="kneeChart"></canvas></section>
  <section id="cells">
    <canvas id="umiHist"></canvas>
    <canvas id="geneHist"></canvas>
    <canvas id="umiVsGene"></canvas>
    <canvas id="mitoHist"></canvas>
  </section>
  <section id="features"><canvas id="featureChart"></canvas></section>
  <section id="genes">
    <canvas id="topGenesChart"></canvas>
    <canvas id="detectionCurve"></canvas>
  </section>
  <section id="comparison"><!-- Gene vs GeneFull --></section>

  <script>/* {{CHARTJS_INLINE}} */</script>
  <script>
    const DATA = {{JSON_DATA}};  // C++ replaces this placeholder
    // Render all charts using DATA...
  </script>
</body>
</html>
```

C++ replaces `{{JSON_DATA}}` with the computed statistics and `{{CHARTJS_INLINE}}` with the embedded Chart.js source.

---

## Comparison Mode

When `--soloQCcompare` is provided, both Solo.out directories are parsed and the report shows:

- Side-by-side summary tables with delta column
- Overlaid knee plots (two colors)
- Per-cell distribution overlays
- Metric-by-metric MATCH/DIFF indicators

Useful for:
- Release validation (new build vs reference)
- STAR vs STARfast output comparison
- Batch effect detection across runs

---

## Mitochondrial Gene Detection

Pattern matching on gene names from `features.tsv`:

```cpp
// Default patterns (tried in order)
vector<string> mitoPatterns = {"^MT-", "^mt-", "^Mt-"};
// User override: --soloQCmito "^ENSMFAG.*MT"

regex mitoRegex(pattern);
vector<bool> isMito(nGenes, false);
for (uint32_t g = 0; g < nGenes; g++) {
    if (regex_search(geneNames[g], mitoRegex))
        isMito[g] = true;
}
```

Report shows: "Detected N mitochondrial genes matching pattern `^MT-`" — so users can verify correctness.

---

## Implementation Steps

- [ ] Create `source/SoloQC.h` with class declaration
- [ ] Implement `SoloQC_parseSummary.cpp` — CSV parser
- [ ] Implement `SoloQC_parseStats.cpp` — whitespace-delimited stats parser
- [ ] Implement `SoloQC_parseMatrix.cpp` — Matrix Market parser + per-cell/gene stats
- [ ] Implement `SoloQC_emitHTML.cpp` — JSON serialization + HTML template emission
- [ ] Download Chart.js v4 minified, add to `source/soloqc/chart.min.js`
- [ ] Create `source/soloqc/template.html` with Chart.js rendering code
- [ ] Add xxd generation to CMakeLists.txt and Makefile
- [ ] Add `--runMode soloQC` dispatch in STAR.cpp
- [ ] Add `--soloQC*` parameters to Parameters.cpp
- [ ] Test on Tibia-plate-S-3-batch1 dataset
- [ ] Implement `--soloQCcompare` mode
- [ ] Add to CI: generate report after smoke test, upload as artifact

## Future Extensions

- **JSON output mode** (`--soloQCformat json`) for programmatic consumption
- **Multi-sample report** — aggregate QC across multiple Solo.out directories
- **Custom thresholds** via config file or CLI
- **Ambient RNA estimation** — background profile from raw vs filtered matrix ratio
