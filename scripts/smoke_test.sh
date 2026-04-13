#!/bin/bash
# Smoke test for STAR builds
# Usage: ./scripts/smoke_test.sh <STAR_EXE> <DATA_DIR>
#
# DATA_DIR must contain:
#   fastq/R1_1M.fastq, fastq/R2_1M.fastq
#   genome_cynomolgus/ (genome index)
#   whitelists/3M-february-2018.txt

set -e

STAR_EXE="${1:?Usage: $0 <STAR_EXE> <DATA_DIR>}"
DATA_DIR="${2:?Usage: $0 <STAR_EXE> <DATA_DIR>}"
OUT_DIR=$(mktemp -d)

echo "=== STAR Smoke Test ==="
echo "Binary: $STAR_EXE"
echo "Data:   $DATA_DIR"
echo "Output: $OUT_DIR"

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
  --outFileNamePrefix "$OUT_DIR/"

READS=$(grep "Number of input reads" "$OUT_DIR/Log.final.out" | awk -F'|\t' '{print $2}' | tr -d ' ')
UNIQUE=$(grep "Uniquely mapped reads number" "$OUT_DIR/Log.final.out" | awk -F'|\t' '{print $2}' | tr -d ' ')

echo ""
echo "Input reads:    $READS"
echo "Uniquely mapped: $UNIQUE"

if [ "$READS" = "0" ] || [ -z "$READS" ]; then
  echo "SMOKE TEST: FAILED (0 reads processed)"
  rm -rf "$OUT_DIR"
  exit 1
fi

echo "SMOKE TEST: PASSED"
rm -rf "$OUT_DIR"
