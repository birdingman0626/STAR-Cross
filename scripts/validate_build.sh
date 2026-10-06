#!/usr/bin/env bash
# validate_build.sh — differential build + smoke validation harness
#
# Phase 1 of 260413-algorithmic-optimizations-retry.md: repeatable validation
# that proves correctness after any change to algorithm code.
#
# Usage:
#   scripts/validate_build.sh [--star-exe /path/to/STAR] [--data-dir /path/to/data] [--ref-exe /path/to/STAR.before]
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

# ── Step 2: Unit tests (required; doctest registers individual test names) ──
echo "=== Step 2: Unit tests ==="
BUILD_DIR="$(dirname "$STAR_EXE")"
if command -v ctest &>/dev/null && [[ -f "$BUILD_DIR/test/CTestTestfile.cmake" ]]; then
  if ctest --test-dir "$BUILD_DIR/test" --no-tests=error --output-on-failure 2>&1; then
    pass "Unit tests"
  else
    fail "Unit tests failed"
    exit 3
  fi
else
  fail "Unit tests unavailable in $BUILD_DIR/test; build with STAR_BUILD_TESTS=ON"
  exit 3
fi

# ── Step 3: Smoke test ────────────────────────────────────────────────────────
echo "=== Step 3: Smoke test ==="

SMOKE_R1="$DATA_DIR/fastq/R1_1M.fastq"
SMOKE_R2="$DATA_DIR/fastq/R2_1M.fastq"
GENOME_DIR="$DATA_DIR/genome_cynomolgus"
GTF="$DATA_DIR/genome_cynomolgus/Macaca_fascicularis_6.0.115.cellranger_filtered.gtf"
WHITELIST="$DATA_DIR/whitelists/3M-february-2018.txt"
SMOKE_REF="$DATA_DIR/smoke_ref"

if [[ ! -s "$SMOKE_R1" || ! -s "$SMOKE_R2" ]]; then
  fail "Required smoke FASTQs missing in $DATA_DIR/fastq/"
  exit 3
fi

for required in "$GENOME_DIR/Genome" "$GTF" "$WHITELIST"; do
  [[ -s "$required" ]] || { fail "Required input missing: $required"; exit 3; }
done
if [[ -n "${STAR_REF_EXE:-}" ]]; then
  [[ -x "$STAR_REF_EXE" ]] || { fail "Reference binary unavailable: $STAR_REF_EXE"; exit 3; }
else
  for feature in Gene GeneFull_Ex50pAS; do
    for artifact in matrix.mtx barcodes.tsv features.tsv; do
      [[ -s "$SMOKE_REF/Solo.out/$feature/raw/$artifact" ]] || {
        fail "Required reference missing: $feature/raw/$artifact"; exit 3;
      }
    done
  done
fi

# Time the run
START_T=$SECONDS

VALIDATION_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/star-validation.XXXXXX")
SMOKE_OUT="$VALIDATION_ROOT/candidate"
mkdir -p "$SMOKE_OUT"
echo "Evidence retained at: $VALIDATION_ROOT"
THREADS="${STAR_TEST_THREADS:-8}"

run_smoke() {
"$1" \
  --runMode alignReads \
  --runThreadN "$THREADS" \
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
  --outFileNamePrefix "$2/" \
  2>&1
local reads
reads=$(awk -F'|' '/Number of input reads/ {gsub(/[[:space:]]/, "", $2); print $2}' "$2/Log.final.out")
if [[ ! "$reads" =~ ^[0-9]+$ ]] || (( reads == 0 )); then
  fail "Smoke run did not verify nonzero processed reads: $2"
  return 3
fi
}
if [[ -n "${STAR_REF_EXE:-}" ]]; then
  SMOKE_REF="$VALIDATION_ROOT/reference"
  mkdir -p "$SMOKE_REF"
  run_smoke "$STAR_REF_EXE" "$SMOKE_REF"
fi
run_smoke "$STAR_EXE" "$SMOKE_OUT"

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
    fail "Missing reference for $f"
    PASS=false
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
