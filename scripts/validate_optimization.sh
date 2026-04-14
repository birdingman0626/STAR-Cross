#!/bin/bash
# Optimization validation script
# Builds STAR, runs smoke test, compares output against baseline
# Usage: ./scripts/validate_optimization.sh <STAR_EXE> <DATA_DIR> [BASELINE_DIR]
#
# BASELINE_DIR defaults to DATA_DIR/smoke_ref (generate once with a known-good build)

set -e

STAR_EXE="${1:?Usage: $0 <STAR_EXE> <DATA_DIR> [BASELINE_DIR]}"
DATA_DIR="${2:?Usage: $0 <STAR_EXE> <DATA_DIR> [BASELINE_DIR]}"
BASELINE="${3:-$DATA_DIR/smoke_ref}"
OUT_DIR="$DATA_DIR/smoke_validate"

echo "=== Optimization Validation ==="
echo "Binary:   $STAR_EXE"
echo "Data:     $DATA_DIR"
echo "Baseline: $BASELINE"

# Step 1: Version check
VERSION=$("$STAR_EXE" --version 2>&1)
echo "Version:  $VERSION"
if [ -z "$VERSION" ]; then
    echo "FAIL: binary does not report version"
    exit 1
fi

# Step 2: Prepare test data if needed
if [ ! -f "$DATA_DIR/fastq/R1_1M.fastq" ]; then
    echo "Creating 1M-read test files..."
    head -4000000 "$DATA_DIR/fastq/R1.fastq" > "$DATA_DIR/fastq/R1_1M.fastq"
    head -4000000 "$DATA_DIR/fastq/R2.fastq" > "$DATA_DIR/fastq/R2_1M.fastq"
fi

# Step 3: Run smoke test
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

START_TIME=$(date +%s)
"$STAR_EXE" \
    --runMode alignReads --runThreadN 4 \
    --genomeDir "$DATA_DIR/genome_cynomolgus" \
    --readFilesIn "$DATA_DIR/fastq/R2_1M.fastq" "$DATA_DIR/fastq/R1_1M.fastq" \
    --sjdbGTFfile "$DATA_DIR/genome_cynomolgus/Macaca_fascicularis_6.0.115.cellranger_filtered.gtf" \
    --soloType CB_UMI_Simple \
    --soloCBwhitelist "$DATA_DIR/whitelists/3M-february-2018.txt" \
    --soloCBstart 1 --soloCBlen 16 --soloUMIstart 17 --soloUMIlen 12 --soloBarcodeReadLength 0 \
    --clipAdapterType CellRanger4 --soloFeatures Gene GeneFull_Ex50pAS --soloMultiMappers EM --soloCellFilter EmptyDrops_CR \
    --outSAMtype None \
    --outFileNamePrefix "$OUT_DIR/" 2>&1
END_TIME=$(date +%s)

ELAPSED=$((END_TIME - START_TIME))
READS=$(grep "Number of input reads" "$OUT_DIR/Log.final.out" | awk -F'|\t' '{print $2}' | tr -d ' ')
UNIQUE=$(grep "Uniquely mapped reads number" "$OUT_DIR/Log.final.out" | awk -F'|\t' '{print $2}' | tr -d ' ')

echo ""
echo "=== Results ==="
echo "Reads:    $READS"
echo "Unique:   $UNIQUE"
echo "Time:     ${ELAPSED}s"

if [ "$READS" = "0" ] || [ -z "$READS" ]; then
    echo "FAIL: 0 reads processed"
    exit 1
fi

# Step 4: Compare against baseline if available
if [ -d "$BASELINE/Solo.out" ]; then
    echo ""
    echo "=== Comparison vs Baseline ==="
    PASS=true
    for f in \
        Solo.out/Gene/raw/matrix.mtx \
        Solo.out/Gene/raw/barcodes.tsv \
        Solo.out/Gene/raw/features.tsv \
        Solo.out/GeneFull_Ex50pAS/raw/matrix.mtx \
        Solo.out/GeneFull_Ex50pAS/raw/barcodes.tsv \
        Solo.out/GeneFull_Ex50pAS/raw/features.tsv \
    ; do
        if [ -f "$BASELINE/$f" ] && [ -f "$OUT_DIR/$f" ]; then
            if diff <(tr -d '\r' < "$BASELINE/$f") <(tr -d '\r' < "$OUT_DIR/$f") > /dev/null 2>&1; then
                echo "OK:   $f"
            else
                echo "FAIL: $f"
                PASS=false
            fi
        fi
    done

    if $PASS; then
        echo "VALIDATION: PASSED"
    else
        echo "VALIDATION: FAILED"
        exit 1
    fi
else
    echo "(No baseline at $BASELINE — skipping comparison)"
    echo "To create baseline: cp -r $OUT_DIR $BASELINE"
fi

echo ""
echo "=== Timing ==="
echo "Wall time: ${ELAPSED}s"
grep "Mapping speed" "$OUT_DIR/Log.final.out" 2>/dev/null || true
