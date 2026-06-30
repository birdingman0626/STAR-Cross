#!/usr/bin/env bash
# Memory-safety check for the splice-junction insertion path
# (sjdbInsertJunctions -> sjdbBuildIndex -> the SAi-insertion loop), run under
# AddressSanitizer. Regression coverage for upstream issue #2657: the SAi loop
# could read the sentinel offset indArray[2*nInd+1] == (uint64)-999, i.e.
# funCalcSAi(Gsj-999, ...), an out-of-bounds read before Gsj that segfaults on
# some heap layouts. ASAN's redzone before Gsj turns that read into a hard,
# layout-independent failure.
#
# Usage: validate_sjdb_asan.sh <ASAN_STAR> [WORKDIR]
#   ASAN_STAR  path to a STAR binary built with -DSTAR_ASAN=ON
set -euo pipefail

STAR="${1:?need ASAN-built STAR}"
WORK="${2:-$(mktemp -d)}"
mkdir -p "$WORK"
export ASAN_OPTIONS="abort_on_error=1:halt_on_error=1:detect_leaks=0"

CHR_LEN=2000000
GENOME="$WORK/genome.fa"
awk -v n="$CHR_LEN" 'BEGIN{
    srand(20260629); b="ACGT"; print ">chrTest"; line="";
    for(i=0;i<n;i++){ line=line substr(b,int(rand()*4)+1,1);
        if(length(line)==70){print line; line=""} }
    if(length(line)>0) print line;
}' > "$GENOME"

# A spread of splice junctions (intron chrStart/chrEnd, 1-based) across the contig,
# so the SAi-insertion loop processes many new indices and scans to the end (nInd).
SJDB="$WORK/sjdb.tsv"
: > "$SJDB"
for s in 50000 200000 350000 500000 650000 800000 950000 1100000 1250000 1400000 1550000 1700000; do
    e=$((s + 2500))
    printf "chrTest\t%d\t%d\t+\n" "$s" "$e" >> "$SJDB"
done
echo "genome=$(wc -c < "$GENOME")B  junctions=$(wc -l < "$SJDB")"

idx="$WORK/idx"
mkdir -p "$idx"
echo "=== genomeGenerate WITH sjdb under ASAN (exercises sjdbBuildIndex) ==="
"$STAR" --runMode genomeGenerate --genomeDir "$idx" \
    --genomeFastaFiles "$GENOME" \
    --sjdbFileChrStartEnd "$SJDB" --sjdbOverhang 49 \
    --genomeSAindexNbases 11 --genomeChrBinNbits 16 \
    --runThreadN 2 --outFileNamePrefix "$idx/" > "$idx/gen.log" 2>&1 || {
        echo "FAILED (ASAN error or crash in sjdb insertion):"; tail -40 "$idx/gen.log"; exit 1; }

# Sanity: the junction database must have been written.
test -s "$idx/sjdbList.out.tab" || { echo "FAILED: sjdbList.out.tab missing/empty"; exit 1; }
echo "OK: sjdb-inserted index built under ASAN with no memory errors ($(wc -l < "$idx/sjdbList.out.tab") junctions in sjdbList.out.tab)"
