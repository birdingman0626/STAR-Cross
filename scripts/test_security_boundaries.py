#!/usr/bin/env python3
"""Compile actual private HTSlib helpers with small adversarial inputs.

No production test hooks or copied implementations: extract the two private
functions from the checked-out source. Public BAM/Parasail regressions live in
star_tests. --baseline HEAD is a negative control, expected to fail.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(path, name, baseline):
    text = (subprocess.check_output(["git", "show", f"{baseline}:{path}"], cwd=ROOT, text=True)
            if baseline else (ROOT / path).read_text())
    start = text.index(f"static int {name}(")
    end = text.index("\n}\n", start) + 3
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--baseline")
    parser.add_argument("--case", choices=["all", "cache", "phasing"], default="all")
    args = parser.parse_args()
    helpers = function("source/htslib/cram/cram_io.c", "expand_cache_path", args.baseline)
    helpers += function("source/htslib/vcf.c", "updatephasing", args.baseline)
    if not args.baseline:
        helpers += function("source/htslib/cram/cram_io.c", "valid_md5_name", None)
    harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
static const unsigned char bcf_type_shift[] = {0,0,1,2,0,2,0,0};
'''
    tests = r'''
int main(void) {
#ifndef TEST_PHASING_ONLY
    char path[PATH_MAX], normal[] = "cache/%2s/%2s/%s";
    assert(expand_cache_path(path, normal, "abcdef") == 0);
    assert(strcmp(path, "cache/ab/cd/ef") == 0);
    char literal[] = "cache/%x/%12x";
    assert(expand_cache_path(path, literal, "abc") == 0);
    assert(strcmp(path, "cache/%x/%12x/abc") == 0);
    // Unknown escapes used to consume output without reducing capacity.
    char oversized[PATH_MAX + 4];
    for (size_t i=0; i<PATH_MAX; i++) oversized[i] = i%2 ? 'x' : '%';
    oversized[PATH_MAX] = 'a'; oversized[PATH_MAX+1] = 0;
    assert(expand_cache_path(path, oversized, "") == -1);
    // Exercise the digit-but-not-s branch independently.
    for (size_t i=0; i<PATH_MAX; i++) oversized[i] = i%2 ? '1' : '%';
    assert(expand_cache_path(path, oversized, "") == -1);
    char incomplete[] = "cache/%";
    assert(expand_cache_path(path, incomplete, "abc") == -1);
    char exact[PATH_MAX]; memset(exact, 'a', sizeof(exact)-1); exact[PATH_MAX-1]=0;
    assert(expand_cache_path(path, exact, "") == 0);
    assert(strlen(path) == PATH_MAX-1);
    assert(expand_cache_path(path, exact, "a") == -1);
#endif
#ifndef TEST_CACHE_ONLY
    uint8_t phase[] = {2,3,4,4}; uint8_t *next = NULL;
    assert(updatephasing(phase, phase+4, &next, 2, 2, 1) == 0);
    assert(phase[0] == 3 && phase[2] == 4 && next == phase+4);
    // Products of these dimensions wrap to zero in the original code.
    assert(updatephasing(phase, phase+4, &next, 65536, 65536, 1) == 1);
    assert(updatephasing(phase, phase+4, &next, INT_MAX, 2, 3) == 1);
    assert(updatephasing(phase, phase+4, &next, -1, 2, 1) == 1);
    assert(updatephasing(phase, phase+4, &next, 1, 0, 1) == 1);
    assert(updatephasing(phase, phase, &next, 0, 0, 1) == 0 && next == phase);
#endif
    return 0;
}
'''
    if not args.baseline:
        tests = tests.replace("int main(void) {", '''int main(void) {
    assert(valid_md5_name("0123456789abcdefABCDEF0123456789"));
    assert(!valid_md5_name(NULL));
    assert(!valid_md5_name("../0123456789abcdef0123456789abcde"));
    assert(!valid_md5_name("0123456789abcdef0123456789abcdef/"));
    assert(!valid_md5_name("/123456789abcdef0123456789abcdef"));
    assert(!valid_md5_name("0123456789abcdef0123456789abcdeg"));
    assert(!valid_md5_name("abc"));''')
    with tempfile.TemporaryDirectory(prefix="star-security-") as directory:
        source = Path(directory) / "boundaries.c"
        binary = Path(directory) / "boundaries"
        source.write_text(harness + helpers + tests)
        command = [args.cc, "-std=c99", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-O1", "-g"]
        if args.case != "all":
            command += ["-DTEST_" + args.case.upper() + "_ONLY"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        subprocess.run(command + [str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("PASS: CRAM cache bounds and VCF phasing dimensions")


if __name__ == "__main__":
    main()
