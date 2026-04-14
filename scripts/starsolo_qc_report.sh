#!/bin/bash
# STARsolo QC Report Generator
# Generates a self-contained HTML report from Solo.out directory
# Usage: ./scripts/starsolo_qc_report.sh <SOLO_OUT_DIR> [OUTPUT_HTML]
#
# No external dependencies — pure bash + embedded HTML/CSS

set -e

SOLO_DIR="${1:?Usage: $0 <path-to-Solo.out-parent> [output.html]}"
OUT_HTML="${2:-${SOLO_DIR}/qc_report.html}"

# Verify required files
for f in Solo.out/GeneFull_Ex50pAS/Summary.csv Solo.out/Gene/Summary.csv Solo.out/Barcodes.stats Log.final.out; do
    if [ ! -f "$SOLO_DIR/$f" ]; then
        echo "ERROR: Missing $SOLO_DIR/$f" >&2
        exit 1
    fi
done

echo "Generating STARsolo QC Report..."
echo "  Input: $SOLO_DIR"
echo "  Output: $OUT_HTML"

# Parse Summary.csv into shell variables
parse_summary() {
    local file="$1" prefix="$2"
    while IFS=, read -r key val; do
        key=$(echo "$key" | tr ' :+/' '____' | tr -d '()')
        eval "${prefix}_${key}=\"$val\""
    done < "$file"
}

parse_summary "$SOLO_DIR/Solo.out/Gene/Summary.csv" "gene"
parse_summary "$SOLO_DIR/Solo.out/GeneFull_Ex50pAS/Summary.csv" "gf"

# Parse Log.final.out
uniquely_mapped=$(grep "Uniquely mapped reads %" "$SOLO_DIR/Log.final.out" | awk -F'|' '{print $2}' | tr -d ' \t')
multi_mapped=$(grep "% of reads mapped to multiple" "$SOLO_DIR/Log.final.out" | head -1 | awk -F'|' '{print $2}' | tr -d ' \t')
unmapped_short=$(grep "% of reads unmapped: too short" "$SOLO_DIR/Log.final.out" | awk -F'|' '{print $2}' | tr -d ' \t')
total_reads=$(grep "Number of input reads" "$SOLO_DIR/Log.final.out" | awk -F'|' '{print $2}' | tr -d ' \t')
mapping_speed=$(grep "Mapping speed" "$SOLO_DIR/Log.final.out" | awk -F'|' '{print $2}' | tr -d ' \t')
splices=$(grep "Number of splices: Total" "$SOLO_DIR/Log.final.out" | awk -F'|' '{print $2}' | tr -d ' \t')

# Parse Barcodes.stats
bc_exact=$(grep "yesWLmatchExact" "$SOLO_DIR/Solo.out/Barcodes.stats" | awk '{print $2}')
bc_1mm=$(grep "yesOneWLmatchWithMM" "$SOLO_DIR/Solo.out/Barcodes.stats" | awk '{print $2}')
bc_noMatch=$(grep "noNoWLmatch" "$SOLO_DIR/Solo.out/Barcodes.stats" | awk '{print $2}')
bc_total=$(grep "nNinBarcode\|nUMIhomopolymer\|nTooMany\|nNoMatch\|nExactMatch\|nOneMM" "$SOLO_DIR/Solo.out/Barcodes.stats" | awk '{s+=$2}END{print s}')

# Parse Features.stats
feat_gene_file="$SOLO_DIR/Solo.out/Gene/Features.stats"
feat_gf_file="$SOLO_DIR/Solo.out/GeneFull_Ex50pAS/Features.stats"
if [ -f "$feat_gene_file" ]; then
    feat_noUnmapped=$(grep "noUnmapped" "$feat_gene_file" | awk '{print $2}')
    feat_noNoFeature=$(grep "noNoFeature" "$feat_gene_file" | awk '{print $2}')
    feat_Ambiguous=$(grep "Ambiguous" "$feat_gene_file" | head -1 | awk '{print $2}')
    feat_Assigned=$(grep "yesWLmatch|yessubWLmatch|Assigned" "$feat_gene_file" | tail -1 | awk '{print $2}')
fi

# Parse STAR version and run configuration from Log.out
star_version=$(grep "STAR version=" "$SOLO_DIR/Log.out" 2>/dev/null | head -1 | sed "s/.*=//")
star_genomedir=$(grep "genomeDir " "$SOLO_DIR/Log.out" 2>/dev/null | head -1 | awk '{print $NF}')
star_threads=$(grep "runThreadN " "$SOLO_DIR/Log.out" 2>/dev/null | grep "RE-DEFINED" | awk '{print $2}')

# Badge helper
badge() {
    local val="$1" pass="$2" warn="$3"
    # Strip % sign for comparison
    local num=$(echo "$val" | tr -d '%')
    if (( $(echo "$num >= $pass" | bc -l 2>/dev/null || echo 1) )); then
        echo "pass"
    elif (( $(echo "$num >= $warn" | bc -l 2>/dev/null || echo 1) )); then
        echo "warn"
    else
        echo "fail"
    fi
}

# Knee plot data (subsample UMIperCellSorted.txt)
KNEE_FILE="$SOLO_DIR/Solo.out/GeneFull_Ex50pAS/UMIperCellSorted.txt"
knee_data=""
if [ -f "$KNEE_FILE" ]; then
    total_barcodes=$(wc -l < "$KNEE_FILE")
    knee_data=$(awk -v total="$total_barcodes" '
    BEGIN { step = log(total)/log(10) / 200; prev_idx = -1 }
    {
        idx = int(log(NR)/log(10) / step);
        if (idx != prev_idx) {
            printf "%d,%s\n", NR, $1;
            prev_idx = idx;
        }
    }
    END { printf "%d,%s\n", NR, $1 }
    ' "$KNEE_FILE")
fi

# Generate HTML
cat > "$OUT_HTML" << 'HTMLEOF'
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>STARsolo QC Report</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #f8fafc; color: #1e293b; line-height: 1.6; padding: 2rem; max-width: 1200px; margin: 0 auto; }
h1 { font-size: 1.8rem; margin-bottom: 0.5rem; }
h2 { font-size: 1.3rem; margin: 2rem 0 1rem; border-bottom: 2px solid #e2e8f0; padding-bottom: 0.5rem; }
.subtitle { color: #64748b; margin-bottom: 2rem; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1rem; }
.card { background: white; border-radius: 8px; padding: 1.5rem; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
.card h3 { font-size: 0.9rem; color: #64748b; text-transform: uppercase; letter-spacing: 0.05em; }
.card .value { font-size: 2rem; font-weight: 700; margin: 0.5rem 0; }
.badge { display: inline-block; padding: 2px 8px; border-radius: 4px; font-size: 0.75rem; font-weight: 600; color: white; }
.badge-pass { background: #22c55e; }
.badge-warn { background: #f59e0b; }
.badge-fail { background: #ef4444; }
table { width: 100%; border-collapse: collapse; margin: 1rem 0; }
th, td { padding: 0.5rem 1rem; text-align: left; border-bottom: 1px solid #e2e8f0; }
th { background: #f1f5f9; font-weight: 600; font-size: 0.85rem; }
td { font-size: 0.9rem; }
.chart-container { max-width: 600px; margin: 1rem auto; }
.two-col { display: grid; grid-template-columns: 1fr 1fr; gap: 2rem; }
@media (max-width: 768px) { .two-col { grid-template-columns: 1fr; } }
</style>
</head>
<body>
HTMLEOF

cat >> "$OUT_HTML" << EOF
<h1>STARsolo QC Report</h1>
<p class="subtitle">Generated $(date '+%Y-%m-%d %H:%M')</p>

<h2>Run Configuration</h2>
<table>
<tr><th>Parameter</th><th>Value</th></tr>
<tr><td>STAR Version</td><td>$star_version</td></tr>
<tr><td>Genome Directory</td><td>$star_genomedir</td></tr>
<tr><td>Threads</td><td>$star_threads</td></tr>
</table>

<h2>Run Overview</h2>
<div class="grid">
  <div class="card"><h3>Total Reads</h3><div class="value">$(printf "%'d" $total_reads)</div></div>
  <div class="card"><h3>Estimated Cells</h3><div class="value">$(printf "%'d" $gf_Estimated_Number_of_Cells)</div></div>
  <div class="card"><h3>Median UMI/Cell</h3><div class="value">$(printf "%'d" $gf_Median_UMI_per_Cell)</div></div>
  <div class="card"><h3>Median Genes/Cell</h3><div class="value">$(printf "%'d" $gf_Median_GeneFull_Ex50pAS_per_Cell)</div></div>
  <div class="card"><h3>Total UMIs in Cells</h3><div class="value">$(printf "%'d" $gf_UMIs_in_Cells)</div></div>
  <div class="card"><h3>Genes Detected</h3><div class="value">$(printf "%'d" $gf_Total_GeneFull_Ex50pAS_Detected)</div></div>
</div>

<h2>Mapping Statistics</h2>
<table>
<tr><th>Metric</th><th>Value</th></tr>
<tr><td>Uniquely Mapped</td><td>$uniquely_mapped</td></tr>
<tr><td>Multi-mapped</td><td>$multi_mapped</td></tr>
<tr><td>Unmapped (too short)</td><td>$unmapped_short</td></tr>
<tr><td>Mapping Speed</td><td>$mapping_speed M reads/hr</td></tr>
<tr><td>Total Splices</td><td>$(printf "%'d" $splices)</td></tr>
</table>

<h2>Barcode Quality</h2>
<table>
<tr><th>Category</th><th>Count</th></tr>
<tr><td>Exact Whitelist Match</td><td>$(printf "%'d" $bc_exact)</td></tr>
<tr><td>1-Mismatch Correction</td><td>$(printf "%'d" $bc_1mm)</td></tr>
<tr><td>No Match</td><td>$(printf "%'d" $bc_noMatch)</td></tr>
</table>

<h2>STARsolo Metrics</h2>
<div class="two-col">
<div>
<h3 style="margin-bottom:0.5rem">Gene (Exonic)</h3>
<table>
<tr><th>Metric</th><th>Value</th></tr>
<tr><td>Valid Barcodes</td><td>${gene_Reads_With_Valid_Barcodes}</td></tr>
<tr><td>Sequencing Saturation</td><td>${gene_Sequencing_Saturation}</td></tr>
<tr><td>Q30 CB+UMI</td><td>${gene_Q30_Bases_in_CB_UMI}</td></tr>
<tr><td>Mapped Unique</td><td>${gene_Reads_Mapped_to_Genome__Unique}</td></tr>
<tr><td>Reads in Cells</td><td>${gene_Fraction_of_Unique_Reads_in_Cells}</td></tr>
<tr><td>Mean Reads/Cell</td><td>${gene_Mean_Reads_per_Cell}</td></tr>
<tr><td>Mean UMI/Cell</td><td>${gene_Mean_UMI_per_Cell}</td></tr>
<tr><td>Median UMI/Cell</td><td>${gene_Median_UMI_per_Cell}</td></tr>
<tr><td>Mean Genes/Cell</td><td>${gene_Mean_Gene_per_Cell}</td></tr>
<tr><td>Median Genes/Cell</td><td>${gene_Median_Gene_per_Cell}</td></tr>
</table>
</div>
<div>
<h3 style="margin-bottom:0.5rem">GeneFull_Ex50pAS (Exonic + Intronic)</h3>
<table>
<tr><th>Metric</th><th>Value</th></tr>
<tr><td>Valid Barcodes</td><td>${gf_Reads_With_Valid_Barcodes}</td></tr>
<tr><td>Sequencing Saturation</td><td>${gf_Sequencing_Saturation}</td></tr>
<tr><td>Q30 CB+UMI</td><td>${gf_Q30_Bases_in_CB_UMI}</td></tr>
<tr><td>Mapped Unique</td><td>${gf_Reads_Mapped_to_Genome__Unique}</td></tr>
<tr><td>Reads in Cells</td><td>${gf_Fraction_of_Unique_Reads_in_Cells}</td></tr>
<tr><td>Mean Reads/Cell</td><td>${gf_Mean_Reads_per_Cell}</td></tr>
<tr><td>Mean UMI/Cell</td><td>${gf_Mean_UMI_per_Cell}</td></tr>
<tr><td>Median UMI/Cell</td><td>${gf_Median_UMI_per_Cell}</td></tr>
<tr><td>Mean Genes/Cell</td><td>${gf_Mean_GeneFull_Ex50pAS_per_Cell}</td></tr>
<tr><td>Median Genes/Cell</td><td>${gf_Median_GeneFull_Ex50pAS_per_Cell}</td></tr>
</table>
</div>
</div>

<h2>Knee Plot (UMI per Barcode)</h2>
<div class="chart-container" style="max-width:800px">
<canvas id="kneeChart"></canvas>
</div>

<script>
const kneeData = [
EOF

# Inject knee plot data
if [ -n "$knee_data" ]; then
    echo "$knee_data" | while IFS=, read rank umi; do
        echo "  {x: $rank, y: $umi}," >> "$OUT_HTML"
    done
fi

cat >> "$OUT_HTML" << EOF
];
const cellCount = $gf_Estimated_Number_of_Cells;

new Chart(document.getElementById('kneeChart'), {
  type: 'scatter',
  data: { datasets: [{
    label: 'UMI per Barcode',
    data: kneeData,
    pointRadius: 1.5,
    pointBackgroundColor: '#3b82f6',
    showLine: true,
    borderColor: '#3b82f6',
    borderWidth: 1,
    fill: false
  }]},
  options: {
    responsive: true,
    scales: {
      x: { type: 'logarithmic', title: { display: true, text: 'Barcode Rank' }},
      y: { type: 'logarithmic', title: { display: true, text: 'UMI Count' }}
    },
    plugins: {
      annotation: {
        annotations: {
          cellLine: {
            type: 'line',
            xMin: cellCount, xMax: cellCount,
            borderColor: '#ef4444',
            borderWidth: 2,
            borderDash: [5, 5],
            label: { display: true, content: cellCount + ' cells', position: 'start' }
          }
        }
      }
    }
  }
});
</script>

<hr style="margin:2rem 0;border:none;border-top:1px solid #e2e8f0">
<p style="color:#94a3b8;font-size:0.8rem">Generated by STARsolo QC Report Script | STAR 2.7.11b (Community Fork)</p>
</body></html>
EOF

echo "Report generated: $OUT_HTML"
echo "  Cells: $gf_Estimated_Number_of_Cells"
echo "  Median UMI: $gf_Median_UMI_per_Cell"
echo "  Genes: $gf_Total_GeneFull_Ex50pAS_Detected"
