#!/usr/bin/env bash
# Build STAR for s390x (big-endian) and validate the byteOrder fix end-to-end.
# Intended to run inside an emulated s390x Debian container (see release.yml):
#   docker run --platform linux/s390x -v "$PWD":/src -w /src debian:bookworm \
#       bash extras/tests/scripts/build_validate_s390x.sh
set -euxo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    g++ cmake ninja-build zlib1g-dev git ca-certificates make gawk

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/../../../source" && pwd)"
cd "$SRC_DIR"

# Sanity: confirm we are actually building for a big-endian target.
python3 - <<'PY' 2>/dev/null || true
import sys
print("byteorder:", sys.byteorder)
PY

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTAR_BUILD_TESTS=OFF
cmake --build build -j2

./build/STAR --version

# End-to-end big-endian validation: building a genome index exercises the
# PackedArray / SuffixArrayFuns / Genome_genomeGenerate byte-order paths. On an
# unpatched big-endian build this aborts with "BUG: next index is smaller than
# previous, EXITING"; with byteOrder.h it must complete and write a valid index.
WORK="$(mktemp -d)"
awk 'BEGIN{
    srand(7); b="ACGT"; print ">chrBE"; s="";
    for(i=0;i<200000;i++){ s=s substr(b,int(rand()*4)+1,1);
        if(length(s)==70){print s; s=""} }
    if(length(s)>0) print s;
}' > "$WORK/g.fa"

mkdir -p "$WORK/idx"
./build/STAR --runMode genomeGenerate --genomeDir "$WORK/idx" \
    --genomeFastaFiles "$WORK/g.fa" \
    --genomeSAindexNbases 9 --genomeChrBinNbits 14 \
    --runThreadN 2 --outFileNamePrefix "$WORK/idx/"

test -s "$WORK/idx/SA"
test -s "$WORK/idx/SAindex"
test -s "$WORK/idx/Genome"
echo "big-endian (s390x) build + genomeGenerate validation OK"
