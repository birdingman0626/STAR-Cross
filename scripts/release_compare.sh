#!/bin/bash
# Release comparison: compare my_count vs orig_count (21 Solo.out files)
# Usage: ./scripts/release_compare.sh <ORIG_DIR> <NEW_DIR>
#
# Returns exit code 0 if all files match, 1 if any fail

set -e

ORIG="${1:?Usage: $0 <orig_dir> <new_dir>}"
NEW="${2:?Usage: $0 <orig_dir> <new_dir>}"

PASS=true
OK_COUNT=0
FAIL_COUNT=0

echo "=== RELEASE VALIDATION ==="
echo "Reference: $ORIG"
echo "Test:      $NEW"
echo ""

for f in \
  Solo.out/Barcodes.stats \
  Solo.out/Gene/Features.stats \
  Solo.out/Gene/Summary.csv \
  Solo.out/Gene/UMIperCellSorted.txt \
  Solo.out/Gene/filtered/barcodes.tsv \
  Solo.out/Gene/filtered/features.tsv \
  Solo.out/Gene/filtered/matrix.mtx \
  Solo.out/Gene/raw/barcodes.tsv \
  Solo.out/Gene/raw/features.tsv \
  Solo.out/Gene/raw/matrix.mtx \
  Solo.out/Gene/raw/UniqueAndMult-EM.mtx \
  Solo.out/GeneFull_Ex50pAS/Features.stats \
  Solo.out/GeneFull_Ex50pAS/Summary.csv \
  Solo.out/GeneFull_Ex50pAS/UMIperCellSorted.txt \
  Solo.out/GeneFull_Ex50pAS/filtered/barcodes.tsv \
  Solo.out/GeneFull_Ex50pAS/filtered/features.tsv \
  Solo.out/GeneFull_Ex50pAS/filtered/matrix.mtx \
  Solo.out/GeneFull_Ex50pAS/raw/barcodes.tsv \
  Solo.out/GeneFull_Ex50pAS/raw/features.tsv \
  Solo.out/GeneFull_Ex50pAS/raw/matrix.mtx \
  Solo.out/GeneFull_Ex50pAS/raw/UniqueAndMult-EM.mtx \
; do
  if [ ! -f "$ORIG/$f" ]; then
    echo "SKIP: $f (not in reference)"
    continue
  fi
  if [ ! -f "$NEW/$f" ]; then
    echo "FAIL: $f (missing in test output)"
    PASS=false
    FAIL_COUNT=$((FAIL_COUNT + 1))
    continue
  fi

  if diff <(tr -d '\r' < "$ORIG/$f") <(tr -d '\r' < "$NEW/$f") > /dev/null 2>&1; then
    echo "OK:   $f"
    OK_COUNT=$((OK_COUNT + 1))
  else
    # Check if it's an EM file with FP rounding
    NDIFF=$(diff <(tr -d '\r' < "$ORIG/$f") <(tr -d '\r' < "$NEW/$f") | grep "^[<>]" | wc -l)
    NTOTAL=$(wc -l < "$ORIG/$f")
    echo "FAIL: $f ($NDIFF lines differ out of $NTOTAL)"
    PASS=false
    FAIL_COUNT=$((FAIL_COUNT + 1))
  fi
done

echo ""
echo "========================================="
if $PASS; then
  echo "RELEASE VALIDATION: PASSED ($OK_COUNT/$((OK_COUNT + FAIL_COUNT)) files)"
else
  echo "RELEASE VALIDATION: $OK_COUNT OK, $FAIL_COUNT FAIL"
fi
echo "========================================="

$PASS
