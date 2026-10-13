#!/usr/bin/env bash
# Build STAR for s390x (big-endian) and validate the byteOrder fix end-to-end.
# Intended to run inside an emulated s390x Debian container (see release.yml):
#   docker run --platform linux/s390x -v "$PWD":/src -w /src debian:trixie \
#       bash extras/tests/scripts/build_validate_s390x.sh
set -euxo pipefail

export DEBIAN_FRONTEND=noninteractive
apt_options=(-o Acquire::Retries=1 -o Acquire::http::Timeout=15 -o Acquire::https::Timeout=15)
timeout 180s apt-get "${apt_options[@]}" update --error-on=any
timeout 600s apt-get "${apt_options[@]}" install -y --no-install-recommends \
    g++ cmake ninja-build zlib1g-dev git ca-certificates make gawk python3

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/../../../source" && pwd)"
cd "$SRC_DIR"

# Sanity: confirm we are actually building for a big-endian target.
python3 - <<'PY'
import sys, platform
assert sys.byteorder == 'big' and platform.machine() == 's390x'
PY

standard="${STAR_CXX_STANDARD:-17}"
case "$standard" in 17|20) ;; *) echo 'Expected STAR_CXX_STANDARD=17 or 20' >&2; exit 2;; esac
mkdir -p build
python3 - <<'PY'
import json, sys
from pathlib import Path
sys.path.insert(0, str(Path.cwd().parent / 'scripts'))
from qualify_language_pair import source_signature
Path('build/initial-source-signature.json').write_text(json.dumps(source_signature(Path.cwd()), indent=2))
PY
# Build the C++17 baseline first when qualifying C++20. The comparison then
# makes only one transition, leaving the shipping C++20 binary in this cache.
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTAR_BUILD_TESTS=ON -DSTAR_CXX_STANDARD=17

# CMake's hash-verified Parasail patch step also handles s390x CPUID.

# Build only the executable and the unit-test binary used below. Native
# platforms qualify the full native test suite; emulated s390x retains its
# endian/BAM subset and runs the scientific fixture for both language modes.
cmake --build build --target STAR star_tests -j2
./build/STAR --version
ctest --test-dir build/test --no-tests=error --output-on-failure -R 'PackedArray|byteOrder|unaligned|BAM'
if [ "$standard" = 20 ]; then
    python3 "$SCRIPT_DIR/../../../scripts/qualify_language_pair.py" \
        --build-dir "$SRC_DIR/build" --initial-standard 17 --parallel 2 \
        --evidence-root "$SRC_DIR/build/language-pair-evidence" \
        --initial-source-signature "$SRC_DIR/build/initial-source-signature.json" \
        --build-target STAR star_tests star_seed_properties \
        --ctest-regex 'PackedArray|byteOrder|unaligned|BAM|^seed_search_properties$'
else
    python3 "$SCRIPT_DIR/../../../scripts/test_cpu_upstream.py" --star-exe "$SRC_DIR/build/STAR" --sa-sparse 3
fi

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
