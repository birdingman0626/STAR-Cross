#!/usr/bin/env bash
# validate_build.sh — differential build + smoke validation harness
#
# Phase 1 of 260413-algorithmic-optimizations-retry.md: repeatable validation
# that proves correctness after any change to algorithm code.
#
# Usage:
#   scripts/validate_build.sh [--star-exe /path/to/STAR] [--data-dir /path/to/data]
#
# Environment variables (override command-line):
#   STAR_EXE        path to the STAR binary to test
#   STAR_REF_EXE    path to the known-good reference STAR binary (optional)
#   DATA_DIR        root of the test data directory
#
# Exit codes:
#   0  all checks passed
#   1  build failed
#   2  version check failed
#   3  smoke test failed

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Defaults ──────────────────────────────────────────────────────────────────
STAR_EXE="${STAR_EXE:-$REPO_ROOT/source/build/STAR}"
DATA_DIR="${DATA_DIR:-$REPO_ROOT/data}"

# Parse args
while [[ $# -gt 0 ]]; do
  case $1 in
    --star-exe)   STAR_EXE="$2";   shift 2 ;;
    --data-dir)   DATA_DIR="$2";   shift 2 ;;
    --ref-exe)    STAR_REF_EXE="$2"; shift 2 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

pass() { echo "[PASS] $*"; }
fail() { echo "[FAIL] $*" >&2; }

# ── Step 1: Version check ─────────────────────────────────────────────────────
echo "=== Step 1: Version check ==="
if [[ ! -x "$STAR_EXE" ]]; then
  fail "STAR binary not found or not executable: $STAR_EXE"
  exit 2
fi
VERSION=$("$STAR_EXE" --version 2>&1 | head -1)
echo "  $VERSION"
if [[ "$VERSION" != *"STAR"* ]]; then
  fail "Unexpected version output"
  exit 2
fi
pass "Version check"

# ── Step 2: Unit tests (if CTest is available) ────────────────────────────────
echo "=== Step 2: Unit tests ==="
BUILD_DIR="$(dirname "$STAR_EXE")"
if command -v ctest &>/dev/null && [[ -f "$BUILD_DIR/CTestTestfile.cmake" ]]; then
  pushd "$BUILD_DIR" > /dev/null
  if ctest --output-on-failure -R star_tests 2>&1; then
    pass "Unit tests"
  else
    fail "Unit tests failed"
    exit 3
  fi
  popd > /dev/null
else
  echo "  ctest not available or no CTestTestfile.cmake in $BUILD_DIR — skipping"
fi

# ── Step 3: Smoke test ────────────────────────────────────────────────────────
echo "=== Step 3: Smoke test ==="

SMOKE_R1="$DATA_DIR/fastq/R1_1M.fastq"
SMOKE_R2="$DATA_DIR/fastq/R2_1M.fastq"
GENOME_DIR="$DATA_DIR/genome_cynomolgus"
GTF="$DATA_DIR/genome_cynomolgus/Macaca_fascicularis_6.0.115.cellranger_filtered.gtf"
WHITELIST="$DATA_DIR/whitelists/3M-february-2018.txt"
SMOKE_REF="$DATA_DIR/smoke_ref/Tibia-plate-S-3-batch1"
SMOKE_OUT="$DATA_DIR/smoke_out_validate/Tibia-plate-S-3-batch1"

if [[ ! -f "$SMOKE_R1" || ! -f "$SMOKE_R2" ]]; then
  echo "  Smoke FASTQ files not found in $DATA_DIR/fastq/ — skipping smoke test"
  echo "  (Run: zcat <R1.gz> | head -4000000 > $SMOKE_R1 to prepare)"
  exit 0
fi

# Time the run
START_T=$SECONDS

rm -rf "$SMOKE_OUT"
mkdir -p "$SMOKE_OUT"

"$STAR_EXE" \
  --runMode alignReads \
  --runThreadN "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)" \
  --genomeDir "$GENOME_DIR" \
  --readFilesIn "$SMOKE_R2" "$SMOKE_R1" \
  --sjdbGTFfile "$GTF" \
  --soloType CB_UMI_Simple \
  --soloCBwhitelist "$WHITELIST" \
  --soloCBstart 1 --soloCBlen 16 \
  --soloUMIstart 17 --soloUMIlen 12 \
  --soloBarcodeReadLength 0 \
  --clipAdapterType CellRanger4 \
  --soloFeatures Gene GeneFull_Ex50pAS \
  --soloMultiMappers EM \
  --soloCellFilter EmptyDrops_CR \
  --outSAMtype None \
  --outFileNamePrefix "$SMOKE_OUT/" \
  2>&1

ELAPSED=$((SECONDS - START_T))
echo "  Run time: ${ELAPSED}s"

# Compare raw matrices only (filtered/EmptyDrops not meaningful on 1M reads)
PASS=true
for f in \
  Solo.out/Gene/raw/matrix.mtx \
  Solo.out/Gene/raw/barcodes.tsv \
  Solo.out/Gene/raw/features.tsv \
  Solo.out/GeneFull_Ex50pAS/raw/matrix.mtx \
  Solo.out/GeneFull_Ex50pAS/raw/barcodes.tsv \
  Solo.out/GeneFull_Ex50pAS/raw/features.tsv \
; do
  if [[ -f "$SMOKE_REF/$f" ]]; then
    if diff -q "$SMOKE_REF/$f" "$SMOKE_OUT/$f" > /dev/null 2>&1; then
      pass "$f"
    else
      fail "$f"
      PASS=false
    fi
  else
    echo "  [SKIP] No reference for $f"
  fi
done

if $PASS; then
  pass "Smoke test (${ELAPSED}s)"
else
  fail "Smoke test — see FAIL lines above"
  exit 3
fi

echo ""
echo "All validation checks passed."
