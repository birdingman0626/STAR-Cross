#!/usr/bin/env bash
# Validate that the genomeGenerate suffix-array rewrite (upstream PR #2687)
# produces a byte-identical genome index compared to the previous implementation,
# and that the result is deterministic across thread counts and chunk layouts.
#
# Usage: validate_genome_equivalence.sh <OLD_STAR> <NEW_STAR> [WORKDIR]
#   OLD_STAR  path to an independently validated, pinned baseline STAR binary
#   NEW_STAR  path to the candidate STAR binary (this branch)
#
# Exits non-zero if any generated index file differs.
set -euo pipefail

OLD_STAR="${1:?need OLD_STAR}"
NEW_STAR="${2:?need NEW_STAR}"
WORK="${3:-$(mktemp -d)}"
mkdir -p "$WORK"
if cmp -s "$OLD_STAR" "$NEW_STAR"; then
    echo "ERROR: baseline and candidate binaries are identical" >&2
    exit 2
fi
sha256sum "$OLD_STAR" "$NEW_STAR" > "$WORK/binary-sha256.txt"

echo "OLD_STAR=$OLD_STAR"
echo "NEW_STAR=$NEW_STAR"
echo "WORK=$WORK"

# ---- synthesize a small reproducible genome (no external data needed) -------
GENOME="$WORK/genome.fa"
awk 'BEGIN{
    srand(20260629); b="ACGT"; print ">chrTest";
    n=8000000;                       # 8 Mb: enough to exercise SA chunking
    line="";
    for(i=0;i<n;i++){
        line=line substr(b,int(rand()*4)+1,1);
        if(length(line)==70){print line; line=""}
    }
    if(length(line)>0) print line;
}' > "$GENOME"
sha256sum "$GENOME" > "$WORK/genome-sha256.txt"
echo "genome size: $(wc -c < "$GENOME") bytes"

# index files that must be reproducible byte-for-byte
IDX_FILES=(SA SAindex Genome chrNameLength.txt chrStart.txt chrLength.txt)

gen() { # gen <STAR> <outdir> <threads> <extraArgs...>
    local star="$1"; local out="$2"; local threads="$3"; shift 3
    mkdir -p "$out"
    "$star" --runMode genomeGenerate --genomeDir "$out" \
        --genomeFastaFiles "$GENOME" \
        --genomeSAindexNbases "${SAI_NBASES:-11}" --genomeChrBinNbits 16 \
        --runThreadN "$threads" "$@" \
        --outFileNamePrefix "$out/" > "$out/gen.log" 2>&1 || {
            echo "genomeGenerate FAILED ($star, threads=$threads)"; tail -20 "$out/gen.log"; exit 1; }
}

cmp_idx() { # cmp_idx <refdir> <testdir> <label>
    local ref="$1"; local test="$2"; local label="$3"; local ok=1
    for f in "${IDX_FILES[@]}"; do
        if ! cmp -s "$ref/$f" "$test/$f"; then
            echo "MISMATCH [$label]: $f differs"; ok=0
        fi
    done
    if [ "$ok" -eq 1 ]; then echo "OK [$label]: all index files byte-identical"; else exit 1; fi
}

echo "=== baseline (old, 1 thread) ==="
gen "$OLD_STAR" "$WORK/old_t1" 1

echo "=== candidate (new, 1 thread) ==="
gen "$NEW_STAR" "$WORK/new_t1" 1
cmp_idx "$WORK/old_t1" "$WORK/new_t1" "old-vs-new single-thread"

echo "=== candidate (new, 16 threads) ==="
gen "$NEW_STAR" "$WORK/new_t16" 16
cmp_idx "$WORK/old_t1" "$WORK/new_t16" "old-vs-new 16-thread"

echo "=== candidate (new, 16 threads, low RAM -> multi-chunk / disk-backed) ==="
gen "$NEW_STAR" "$WORK/new_t16_lowram" 16 --limitGenomeGenerateRAM 600000000
cmp_idx "$WORK/old_t1" "$WORK/new_t16_lowram" "old-vs-new 16-thread low-RAM"

# ---- small genomes: upstream's SAindex walk steps over short suffix runs ----
# (N-containing suffixes at strand ends). With >=16 threads the index is built
# from a parallel scan of every suffix, which must still reproduce that walk.
# The 8 Mb rand() genome above depends on the awk implementation and does not
# always hit this case; these exact-integer LCG genomes are the same in any awk.
lcg_genome() { # lcg_genome <seed> <out.fa>
    awk -v seed="$1" 'BEGIN{
        x=seed; b="ACGT"; s="";
        for(i=0;i<5000;i++){ x=(x*16807)%2147483647; s=s substr(b,x%4+1,1) }
        print ">chrLcg";
        for(i=1;i<=length(s);i+=60) print substr(s,i,60);
    }' > "$2"
}
lcg_genome 1 "$WORK/lcg1.fa"
LCG1_SHA=5bd8ea363deb18590776259fb4a6bd2cb3a8e32112a99c5872248fc69929966a
if [ "$(sha256sum < "$WORK/lcg1.fa" | cut -d' ' -f1)" != "$LCG1_SHA" ]; then
    echo "ERROR: awk produced an unexpected LCG genome" >&2; exit 2
fi
for seed in 1 2 3 4 5; do
    lcg_genome "$seed" "$WORK/lcg$seed.fa"
    for nb in 4 5; do
        echo "=== small genome seed=$seed SAindexNbases=$nb (old 1, new 1, new 16 threads) ==="
        GENOME="$WORK/lcg$seed.fa"
        d="$WORK/lcg${seed}_nb$nb"
        SAI_NBASES=$nb gen "$OLD_STAR" "$d/old_t1" 1
        SAI_NBASES=$nb gen "$NEW_STAR" "$d/new_t1" 1
        SAI_NBASES=$nb gen "$NEW_STAR" "$d/new_t16" 16
        cmp_idx "$d/old_t1" "$d/new_t1" "small seed=$seed nb=$nb single-thread"
        cmp_idx "$d/old_t1" "$d/new_t16" "small seed=$seed nb=$nb 16-thread"
    done
done

echo "ALL GENOME-INDEX EQUIVALENCE CHECKS PASSED"
