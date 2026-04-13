# Release Validation Test Plan

## Test Tiers

| Tier | Reads | Time | When to Use |
|---|---|---|---|
| **Smoke test** | ~1M (subsample) | ~2-5 min | Every build, during development |
| **Full release test** | 434M (complete) | ~2 hours | Before release only |

The smoke test validates that the binary runs correctly end-to-end. The full test validates bit-exact output match against the reference.

---

## Tier 1: Smoke Test (Fast, ~5 min)

### Prepare Subsampled FASTQ (One-Time Setup)

Extract the first 1 million reads (4M lines in FASTQ format) from the full dataset:

```bash
DATA_DIR=data

# 从压缩文件提取前1M reads
zcat $DATA_DIR/fastq/Tibia-plate-3_S1_L001_R1_001.fastq.gz | head -4000000 > $DATA_DIR/fastq/R1_1M.fastq
zcat $DATA_DIR/fastq/Tibia-plate-3_S1_L001_R2_001.fastq.gz | head -4000000 > $DATA_DIR/fastq/R2_1M.fastq

# Windows 上如果已有解压的FASTQ:
# head -4000000 $DATA_DIR/fastq/R1.fastq > $DATA_DIR/fastq/R1_1M.fastq
# head -4000000 $DATA_DIR/fastq/R2.fastq > $DATA_DIR/fastq/R2_1M.fastq
```

### Generate Reference for Smoke Test (One-Time Setup)

Run the known-good binary on the subsampled data to produce a smoke-test reference:

```bash
STAR_GOOD=<path-to-known-good-STAR>
SMOKE_REF=$DATA_DIR/smoke_ref/Tibia-plate-S-3-batch1

rm -rf "$SMOKE_REF"
mkdir -p "$SMOKE_REF"

$STAR_GOOD \
  --runMode alignReads \
  --runThreadN 12 \
  --genomeDir $DATA_DIR/genome_cynomolgus \
  --readFilesIn $DATA_DIR/fastq/R2_1M.fastq $DATA_DIR/fastq/R1_1M.fastq \
  --sjdbGTFfile $DATA_DIR/genome_cynomolgus/Macaca_fascicularis_6.0.115.cellranger_filtered.gtf \
  --soloType CB_UMI_Simple \
  --soloCBwhitelist $DATA_DIR/whitelists/3M-february-2018.txt \
  --soloCBstart 1 --soloCBlen 16 \
  --soloUMIstart 17 --soloUMIlen 12 \
  --soloBarcodeReadLength 0 \
  --clipAdapterType CellRanger4 \
  --soloFeatures Gene GeneFull_Ex50pAS \
  --soloMultiMappers EM \
  --soloCellFilter EmptyDrops_CR \
  --outSAMtype None \
  --outFileNamePrefix "$SMOKE_REF/"
```

### Run Smoke Test

```bash
STAR_EXE=<path-to-test-STAR>
SMOKE_OUT=$DATA_DIR/smoke_out/Tibia-plate-S-3-batch1

rm -rf "$SMOKE_OUT"
mkdir -p "$SMOKE_OUT"

$STAR_EXE \
  --runMode alignReads \
  --runThreadN 12 \
  --genomeDir $DATA_DIR/genome_cynomolgus \
  --readFilesIn $DATA_DIR/fastq/R2_1M.fastq $DATA_DIR/fastq/R1_1M.fastq \
  --sjdbGTFfile $DATA_DIR/genome_cynomolgus/Macaca_fascicularis_6.0.115.cellranger_filtered.gtf \
  --soloType CB_UMI_Simple \
  --soloCBwhitelist $DATA_DIR/whitelists/3M-february-2018.txt \
  --soloCBstart 1 --soloCBlen 16 \
  --soloUMIstart 17 --soloUMIlen 12 \
  --soloBarcodeReadLength 0 \
  --clipAdapterType CellRanger4 \
  --soloFeatures Gene GeneFull_Ex50pAS \
  --soloMultiMappers EM \
  --soloCellFilter EmptyDrops_CR \
  --outSAMtype None \
  --outFileNamePrefix "$SMOKE_OUT/"
```

### Smoke Test Pass Criteria

```bash
PASS=true

# 1. 进程正常退出
# (exit code 0 from above command)

# 2. Solo.out 目录存在且有输出
if [ ! -d "$SMOKE_OUT/Solo.out/Gene/raw" ]; then
  echo "FAIL: Solo.out/Gene/raw missing"
  PASS=false
fi

# 3. 与 smoke_ref 进行文件对比
for f in \
  Solo.out/Gene/raw/matrix.mtx \
  Solo.out/Gene/raw/barcodes.tsv \
  Solo.out/Gene/raw/features.tsv \
  Solo.out/GeneFull_Ex50pAS/raw/matrix.mtx \
  Solo.out/GeneFull_Ex50pAS/raw/barcodes.tsv \
  Solo.out/GeneFull_Ex50pAS/raw/features.tsv \
; do
  if ! diff -q "$SMOKE_REF/$f" "$SMOKE_OUT/$f" > /dev/null 2>&1; then
    echo "FAIL: $f"
    PASS=false
  else
    echo "OK:   $f"
  fi
done

if $PASS; then
  echo "SMOKE TEST: PASSED"
else
  echo "SMOKE TEST: FAILED"
  exit 1
fi
```

The smoke test checks **raw count matrices only** (skips filtered/EmptyDrops which needs sufficient cell count to be meaningful on 1M reads).

---

## Tier 2: Full Release Test (~2 hours)

### Objective

Validate full bit-exact output match against the reference (`orig_count`). **100% match on all count matrices and statistics is required for release.**

## Test Data

| Item | Path |
|---|---|
| FASTQ R1 (cDNA) | `data/fastq/Tibia-plate-3_S1_L001_R1_001.fastq.gz` |
| FASTQ R2 (barcode+UMI) | `data/fastq/Tibia-plate-3_S1_L001_R2_001.fastq.gz` |
| Genome index | `data/genome_cynomolgus/` (Macaca fascicularis 6.0) |
| GTF annotation | `data/genome_cynomolgus/Macaca_fascicularis_6.0.115.cellranger_filtered.gtf` |
| Barcode whitelist | `data/whitelists/3M-february-2018.txt` |
| Reference output | `data/orig_count/Tibia-plate-S-3-batch1/` |
| Test output | `data/my_count/Tibia-plate-S-3-batch1/` |

## Step 1: Run STAR Alignment

```bash
STAR_EXE=<path-to-built-STAR>
DATA_DIR=data
OUT_DIR=$DATA_DIR/my_count/Tibia-plate-S-3-batch1

# 清空旧的测试输出
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

$STAR_EXE \
  --runMode alignReads \
  --runThreadN 12 \
  --genomeDir $DATA_DIR/genome_cynomolgus \
  --readFilesIn \
      $DATA_DIR/fastq/Tibia-plate-3_S1_L001_R2_001.fastq.gz \
      $DATA_DIR/fastq/Tibia-plate-3_S1_L001_R1_001.fastq.gz \
  --readFilesCommand zcat \
  --sjdbGTFfile $DATA_DIR/genome_cynomolgus/Macaca_fascicularis_6.0.115.cellranger_filtered.gtf \
  --soloType CB_UMI_Simple \
  --soloCBwhitelist $DATA_DIR/whitelists/3M-february-2018.txt \
  --soloCBstart 1 --soloCBlen 16 \
  --soloUMIstart 17 --soloUMIlen 12 \
  --soloBarcodeReadLength 0 \
  --clipAdapterType CellRanger4 \
  --soloFeatures Gene GeneFull_Ex50pAS \
  --soloMultiMappers EM \
  --soloCellFilter EmptyDrops_CR \
  --outSAMtype None \
  --outFileNamePrefix "$OUT_DIR/"
```

**Expected:** Exit code 0, `Solo.out/` directory created with Gene and GeneFull_Ex50pAS subdirectories.

**Note for Windows:** Use uncompressed FASTQ (`R1.fastq`, `R2.fastq`) and drop the `--readFilesCommand zcat` flag since zcat is not available natively.

## Step 2: Verify Output Structure

Check that `my_count` produces the same file tree as `orig_count`:

```bash
ORIG=$DATA_DIR/orig_count/Tibia-plate-S-3-batch1
NEW=$DATA_DIR/my_count/Tibia-plate-S-3-batch1

# 对比文件树 (排除日志和临时文件)
diff <(cd "$ORIG" && find Solo.out -type f | sort) \
     <(cd "$NEW"  && find Solo.out -type f | sort)
```

**Expected files (21 total):**

```
Solo.out/Barcodes.stats
Solo.out/Gene/Features.stats
Solo.out/Gene/Summary.csv
Solo.out/Gene/UMIperCellSorted.txt
Solo.out/Gene/filtered/barcodes.tsv
Solo.out/Gene/filtered/features.tsv
Solo.out/Gene/filtered/matrix.mtx
Solo.out/Gene/raw/barcodes.tsv
Solo.out/Gene/raw/features.tsv
Solo.out/Gene/raw/matrix.mtx
Solo.out/Gene/raw/UniqueAndMult-EM.mtx
Solo.out/GeneFull_Ex50pAS/Features.stats
Solo.out/GeneFull_Ex50pAS/Summary.csv
Solo.out/GeneFull_Ex50pAS/UMIperCellSorted.txt
Solo.out/GeneFull_Ex50pAS/filtered/barcodes.tsv
Solo.out/GeneFull_Ex50pAS/filtered/features.tsv
Solo.out/GeneFull_Ex50pAS/filtered/matrix.mtx
Solo.out/GeneFull_Ex50pAS/raw/barcodes.tsv
Solo.out/GeneFull_Ex50pAS/raw/features.tsv
Solo.out/GeneFull_Ex50pAS/raw/matrix.mtx
Solo.out/GeneFull_Ex50pAS/raw/UniqueAndMult-EM.mtx
```

**Pass criteria:** Identical file list. Missing or extra files = FAIL.

## Step 3: Compare Count Matrices (Critical)

These are the core release-gate files. Must be **byte-identical**.

```bash
PASS=true

for f in \
  Solo.out/Gene/raw/matrix.mtx \
  Solo.out/Gene/raw/UniqueAndMult-EM.mtx \
  Solo.out/Gene/raw/barcodes.tsv \
  Solo.out/Gene/raw/features.tsv \
  Solo.out/Gene/filtered/matrix.mtx \
  Solo.out/Gene/filtered/barcodes.tsv \
  Solo.out/Gene/filtered/features.tsv \
  Solo.out/GeneFull_Ex50pAS/raw/matrix.mtx \
  Solo.out/GeneFull_Ex50pAS/raw/UniqueAndMult-EM.mtx \
  Solo.out/GeneFull_Ex50pAS/raw/barcodes.tsv \
  Solo.out/GeneFull_Ex50pAS/raw/features.tsv \
  Solo.out/GeneFull_Ex50pAS/filtered/matrix.mtx \
  Solo.out/GeneFull_Ex50pAS/filtered/barcodes.tsv \
  Solo.out/GeneFull_Ex50pAS/filtered/features.tsv \
; do
  if ! diff -q "$ORIG/$f" "$NEW/$f" > /dev/null 2>&1; then
    echo "FAIL: $f"
    PASS=false
  else
    echo "OK:   $f"
  fi
done
```

**Pass criteria:** All 14 files identical. Any diff = FAIL.

## Step 4: Compare Statistics

```bash
for f in \
  Solo.out/Barcodes.stats \
  Solo.out/Gene/Features.stats \
  Solo.out/Gene/Summary.csv \
  Solo.out/Gene/UMIperCellSorted.txt \
  Solo.out/GeneFull_Ex50pAS/Features.stats \
  Solo.out/GeneFull_Ex50pAS/Summary.csv \
  Solo.out/GeneFull_Ex50pAS/UMIperCellSorted.txt \
; do
  if ! diff -q "$ORIG/$f" "$NEW/$f" > /dev/null 2>&1; then
    echo "FAIL: $f"
    PASS=false
  else
    echo "OK:   $f"
  fi
done
```

**Pass criteria:** All 7 statistics files identical.

## Step 5: Final Verdict

```bash
if $PASS; then
  echo "========================================="
  echo "RELEASE VALIDATION: PASSED"
  echo "All 21 Solo.out files match orig_count."
  echo "========================================="
else
  echo "========================================="
  echo "RELEASE VALIDATION: FAILED"
  echo "See FAIL lines above for mismatches."
  echo "========================================="
  exit 1
fi
```

## Release Gate

| Condition | Required |
|---|---|
| STAR exits with code 0 | Yes |
| Solo.out file tree matches orig_count | Yes |
| 14 count matrix files byte-identical | **Yes (hard gate)** |
| 7 statistics files byte-identical | **Yes (hard gate)** |
| **Overall: 21/21 files match** | **Release approved** |

Any mismatch → investigate diff, fix, rebuild, re-run. No exceptions.

## Notes

- Log files (`Log.out`, `Log.progress.out`, `Log.final.out`) are excluded from comparison — they contain timestamps and runtime-specific information.
- `_STARtmp/` and `_STARgenome/` temporary directories are excluded.
- `SJ.out.tab` is excluded because `--outSAMtype None` may affect its generation depending on build; the Solo count matrices are the authoritative output.
- The reference `orig_count` was generated by upstream STAR on Linux. Any platform-specific floating-point divergence in EmptyDrops_CR filtering would surface in `filtered/` matrices.
