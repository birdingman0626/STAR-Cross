# Memory Management Remediation Plan

## Status: Plan Complete — Awaiting Execution
Last updated: 2026-04-13

## Problem

STAR's codebase has a "allocate, never free, let OS clean up on exit()" philosophy. This causes:
- Valgrind/ASAN noise that drowns real bugs (impossible to use leak detection for finding new issues)
- Multi-GB waste in two-pass mode (pass-1 allocations persist through pass-2)
- Blocks any future library/embedding use
- File descriptor leaks from `streamFuns.cpp` pattern

17 confirmed issues: 5 missing destructors, 10 specific leaks, 2 fd leaks.

## Guiding Principles

1. **Fix the hot-path and large leaks first** — twoPassRunPass1 (GBs), bamRemoveDuplicates (GBs), Transcriptome (hundreds of MB)
2. **Fix structurally** — add destructors rather than sprinkling `delete` at call sites
3. **Don't change allocation patterns** — keep `new[]` for now, just add matching `delete[]`. Smart pointer migration is a separate future effort.
4. **Validate with ASAN** — each phase must pass ASAN smoke test with zero new leaks
5. **Preserve output** — memory management changes must not alter any output. Byte-identical smoke test after each phase.

---

## Phase 1: Critical Loop and Function Leaks (Effort: 2 hours, Risk: Low)

These are specific `new` calls with missing `delete` — the simplest fixes.

### 1.1 — `sjdbBuildIndex.cpp:55` — Loop leak (100K+ iterations)

```cpp
// Current: inside loop over 2*sjdbN iterations
char** seq1=new char*[2];
seq1[0]=Gsj+isj*mapGen.sjdbLength;
seq1[1]=G1c+isj*mapGen.sjdbLength;
// ... use seq1 ...
// no delete
```

**Fix:** Replace heap allocation with stack array — `seq1` is only 2 pointers, no need for `new`:
```cpp
char* seq1[2];
seq1[0]=Gsj+isj*mapGen.sjdbLength;
seq1[1]=G1c+isj*mapGen.sjdbLength;
```

- [ ] Replace `new char*[2]` with `char* seq1[2]` in the loop body
- [ ] Verify: smoke test byte-identical

### 1.2 — `insertSeqSA.cpp:53-72` — Three leaked allocations

```cpp
char** seq1=new char*[2];          // line 53
char* seqq=new char[4*nG1+3*...]; // line 54
uint64* indArray=new uint64[...];  // line 72
// ... function returns at line 318 without delete
```

**Fix:** Add cleanup before `return nInd`:
```cpp
delete[] indArray;
delete[] seqq;
delete[] seq1;
return nInd;
```

Or better — same as 1.1, `seq1` can be a stack array since it's only 2 pointers.

- [ ] Add `delete[]` for all three allocations before return
- [ ] Change `seq1` to stack array
- [ ] Verify: smoke test byte-identical

### 1.3 — `Transcriptome.cpp:124-145` — `gF` sort buffer

```cpp
uint64 *gF=new uint64[4*nGe];
// ... sort and copy back ...
// no delete[] gF
```

**Fix:** Add `delete[] gF;` after the copy-back loop.

- [ ] Add `delete[] gF;` after line 145
- [ ] Verify: smoke test byte-identical

### 1.4 — `bamSortByCoordinate.cpp:81` — `bamBinNames`

```cpp
char **bamBinNames = new char*[nBins];
// ... used for bam_cat() call ...
// function returns without delete
```

**Fix:** Add `delete[] bamBinNames;` after the `bam_cat()` call.

- [ ] Add `delete[] bamBinNames;` before function return
- [ ] Verify: smoke test byte-identical

### 1.5 — `Variation.cpp:90,106` — `snp.loci` and `s1`

```cpp
snp.loci=new uint[snp.N];   // line 90 — member, never freed
uint *s1=new uint[2*snp.N]; // line 106 — local, never freed
```

**Fix:** Add `delete[] s1;` before function return. For `snp.loci`, add a `~Variation()` destructor (or defer to Phase 3).

- [ ] Add `delete[] s1;` before function returns
- [ ] Add `~SNP()` destructor that frees `loci`, or add cleanup to `~Variation()`
- [ ] Verify: smoke test byte-identical

### 1.6 — `outputSJ.cpp:148-149` — `sjNovelStart`/`sjNovelEnd`

```cpp
P.sjNovelStart = new uint[P.sjNovelN];
P.sjNovelEnd = new uint[P.sjNovelN];
```

**Fix:** Add cleanup in `twoPassRunPass1.cpp` after pass-1 output is consumed, or in a `Parameters` cleanup method.

- [ ] Add `delete[] P.sjNovelStart; delete[] P.sjNovelEnd;` after pass-1 SJ output is consumed
- [ ] Set pointers to `nullptr` after delete
- [ ] Verify: two-pass smoke test byte-identical

---

## Phase 2: Large Memory Leaks (Effort: 3 hours, Risk: Medium)

These waste GBs of memory and are the most impactful fixes.

### 2.1 — `twoPassRunPass1.cpp:76` — Pass-1 chunks never freed

```cpp
vector<ReadAlignChunk*> RAchunk1(P.runThreadN);
for (int ii=0;ii<P1.runThreadN;ii++) {
    RAchunk1[ii]=new ReadAlignChunk(P1, genomeMain, transcriptomeMain, ii);
};
mapThreadsSpawn(P1, RAchunk1.data());
outputSJ(RAchunk1.data(),P1);
//  delete loop commented out
```

**Fix:** This depends on Phase 3 (destructors). Once `~ReadAlignChunk()` and `~ReadAlign()` exist:
```cpp
for (int ii=0; ii<P1.runThreadN; ii++) {
    delete RAchunk1[ii];
}
```

**Interim fix** (before destructors): manually free the large buffers inside each chunk:
```cpp
for (int ii=0; ii<P1.runThreadN; ii++) {
    delete[] RAchunk1[ii]->RA->chunkIn[0];  // large FASTQ buffer
    delete[] RAchunk1[ii]->RA->chunkIn[1];
    // ... other large buffers
}
```

- [ ] Add cleanup loop after `outputSJ()` call
- [ ] At minimum, free the chunk input buffers (largest allocations)
- [ ] Full cleanup after Phase 3 destructors are implemented
- [ ] Verify: two-pass smoke test byte-identical

### 2.2 — `bamRemoveDuplicates.cpp:136-137` — GBs leaked on normal exit

```cpp
char *bamRaw=new char[bamLengthMax];  // can be gigabytes
uint *aD=new uint[grNmax*2];
// ... function exits while(true) via break, never frees
```

**Fix:** Add cleanup after the main loop:
```cpp
// After the while(true) loop breaks:
delete[] bamRaw;
delete[] aD;
bgzf_close(bgzfOut);  // also close the output handle
```

Also add cleanup before `exitWithError` calls (lines 153, 189, 220) — or restructure to use RAII wrappers.

- [ ] Add `delete[] bamRaw; delete[] aD;` after main loop
- [ ] Add `bgzf_close(bgzfOut);` before `exitWithError` at line 220
- [ ] Verify: dedup smoke test (if test data exercises this path)

### 2.3 — `BAMbinSortByCoordinate.cpp:11-12` — Error path leaks

```cpp
char *bamIn=new char[binS+1];
uint *startPos=new uint[binN*3];
// exitWithError at lines 26, 36, 58 → leaks both + unclosed bgzfBin
```

**Fix:** Add cleanup before each `exitWithError`, or restructure with `unique_ptr`:
```cpp
// Simplest fix — add before each exitWithError:
delete[] startPos;
delete[] bamIn;
// Also close bgzfBin if opened
```

- [ ] Add cleanup before each `exitWithError` call
- [ ] Add `bgzf_close(bgzfBin)` before line-60 error exit
- [ ] Verify: smoke test byte-identical

---

## Phase 3: Destructors for Core Classes (Effort: 1 day, Risk: Medium)

The structural fix. Once these exist, all ownership is clear and Phase 2.1 becomes trivial.

### 3.1 — `~ReadAlign()` destructor

**File:** `source/ReadAlign.cpp` (add at end) or new `source/ReadAlign_destructor.cpp`

Must free every allocation from the constructor. The constructor allocates ~30 arrays. The destructor must mirror them:

```cpp
ReadAlign::~ReadAlign() {
    delete[] alignTrAll;

    for (uint ii=0; ii<2; ii++)
        delete[] winBin[ii];
    delete[] winBin;

    for (uint ii=0; ii<3; ii++)
        delete[] splitR[ii];
    delete[] splitR;

    delete[] PC;
    delete[] WC;
    delete[] nWA;
    delete[] nWAP;
    delete[] WALrec;
    delete[] WlastAnchor;

    for (uint ii=0; ii<P.alignWindowsPerReadNmax; ii++)
        delete[] WA[ii];
    delete[] WA;
    delete[] WAincl;
    delete[] swWinCov;
    delete[] scoreSeedToSeed;
    delete[] scoreSeedBest;
    delete[] scoreSeedBestInd;
    delete[] scoreSeedBestMM;
    delete[] seedChain;

    delete[] trAll;
    delete[] nWinTr;
    delete[] trArrayPointer;
    // trArray elements are within trAll — do NOT double-delete
    delete[] trArray;

    for (uint ii=0; ii<P.readNmates; ii++) {
        delete[] Read0[ii];
        delete[] Qual0[ii];
        delete[] readNameMates[ii];
    }
    delete[] Read0;
    delete[] Qual0;
    delete[] readNameMates;

    for (uint ii=0; ii<3; ii++)
        delete[] Read1[ii];
    delete[] Read1;

    for (uint ii=0; ii<P.readNmates; ii++)
        delete[] outBAMoneAlign[ii];
    delete[] outBAMoneAlign;
    delete[] outBAMoneAlignNbytes;

    // chunkOutChimJunction — only delete if we own it (not aliased from peMergeRA)
    // See latent risk note below

    delete chimDet;
    delete soloRead;
}
```

**Latent double-free risk:** `peMergeRA->chunkOutChimJunction` is aliased to the parent RA's pointer (ReadAlignChunk.cpp:111-113). The destructor must NOT delete `chunkOutChimJunction` in `peMergeRA`. Solution: add a `bool ownsChimJunction` flag, or set `peMergeRA->chunkOutChimJunction = nullptr` before deleting `peMergeRA`.

- [ ] Add `~ReadAlign()` matching every constructor allocation
- [ ] Handle `peMergeRA->chunkOutChimJunction` aliasing (ownership flag or nullptr)
- [ ] Declare destructor in `ReadAlign.h`
- [ ] Verify: ASAN smoke test, no double-frees

### 3.2 — `~ReadAlignChunk()` destructor

```cpp
ReadAlignChunk::~ReadAlignChunk() {
    for (uint ii=0; ii<P.readNmates; ii++) {
        delete[] chunkIn[ii];
        delete readInStream[ii];
    }
    delete[] chunkIn;
    delete[] readInStream;
    delete[] chunkOutBAM;

    // Delete child objects
    delete RA->peMergeRA;  // must happen before RA
    delete RA->waspRA;
    delete RA;
    // BAMoutput objects — depends on ownership model
}
```

- [ ] Add `~ReadAlignChunk()` matching constructor allocations
- [ ] Ensure child objects (RA, peMergeRA, waspRA) are deleted in correct order
- [ ] Declare in `ReadAlignChunk.h`
- [ ] Verify: ASAN smoke test

### 3.3 — `~BAMoutput()` destructor

```cpp
BAMoutput::~BAMoutput() {
    delete[] bamArray;
    for (uint ii=0; ii<nBins; ii++) {
        if (binStream[ii]) {
            binStream[ii]->close();
            delete binStream[ii];
        }
    }
    delete[] binStream;
    delete[] binStart;
    delete[] binBytes;
    delete[] binTotalN;
    delete[] binTotalBytes;
}
```

- [ ] Add `~BAMoutput()`
- [ ] Declare in `BAMoutput.h`
- [ ] Verify: ASAN smoke test

### 3.4 — `~Transcriptome()` destructor

```cpp
Transcriptome::~Transcriptome() {
    delete[] trS;
    delete[] trE;
    delete[] trEmax;
    delete[] trExI;
    delete[] trExN;
    delete[] trStr;
    delete[] trGene;
    delete[] trLen;
    delete[] exSE;
    delete[] exLenCum;
    delete[] exG.s;
    delete[] exG.e;
    delete[] geneFull.s;
    delete[] geneFull.e;
    delete[] geneFull.eMax;
    delete[] geneFull.str;
    delete[] geneFull.g;
    // ... check header for full list
}
```

- [ ] Add `~Transcriptome()` matching all constructor allocations
- [ ] Read `Transcriptome.h` to ensure every raw pointer member is covered
- [ ] Declare in `Transcriptome.h`
- [ ] Verify: ASAN smoke test

### 3.5 — `~Genome()` destructor (uncomment and complete)

The destructor is already written but commented out.

- [ ] Uncomment `~Genome()` in `Genome.cpp`
- [ ] Add missing cleanup: `chrBin`, `genomeSAindexStart`, sjdb arrays
- [ ] Verify: ASAN smoke test

---

## Phase 4: Stream Factory Pattern Fix (Effort: 4 hours, Risk: Medium)

The `ofstrOpen`/`ifstrOpen`/`fstrOpen` pattern in `streamFuns.cpp` is the most pervasive issue — used in dozens of call sites.

### Current Problem

```cpp
// streamFuns.cpp:96 — allocates with new, returns reference, pointer is lost
std::ofstream &ofstrOpen(std::string fileName, ...) {
    std::ofstream &ofStream = *new std::ofstream(fileName, ...);
    return ofStream;
}

// Caller — no way to delete:
ofstream &out = ofstrOpen("file.txt", ...);
out << data;
out.close();  // closes fd, but ofstream object memory leaked
```

### Fix Options

**Option A — Return `unique_ptr` (cleanest, requires changing all call sites):**
```cpp
std::unique_ptr<std::ofstream> ofstrOpen(std::string fileName, ...) {
    auto ofStream = std::make_unique<std::ofstream>(fileName, ...);
    // ... error checking ...
    return ofStream;
}

// Caller:
auto out = ofstrOpen("file.txt", ...);
*out << data;
// Automatically closed and freed when out goes out of scope
```

**Option B — Return raw pointer (minimal change, explicit ownership):**
```cpp
std::ofstream* ofstrOpen(std::string fileName, ...) {
    std::ofstream* ofStream = new std::ofstream(fileName, ...);
    // ... error checking ...
    return ofStream;
}

// Caller:
ofstream* out = ofstrOpen("file.txt", ...);
*out << data;
out->close();
delete out;
```

**Option C — Take reference parameter (no heap allocation at all):**
```cpp
void ofstrOpen(std::ofstream &ofStream, std::string fileName, ...) {
    ofStream.open(fileName, ...);
    // ... error checking ...
}

// Caller:
ofstream out;
ofstrOpen(out, "file.txt", ...);
out << data;
// Automatically closed when out goes out of scope
```

**Recommendation: Option C** — eliminates the heap allocation entirely. The caller owns the stream on the stack. This is the most C++-idiomatic approach and requires no `delete` anywhere.

### Migration

- [ ] Change `ofstrOpen` signature to `void ofstrOpen(std::ofstream &, ...)` 
- [ ] Change `ifstrOpen` signature to `void ifstrOpen(std::ifstream &, ...)`
- [ ] Change `fstrOpen` signature to `void fstrOpen(std::fstream &, ...)`
- [ ] Update all call sites (~30-40 across the codebase) to declare stream on stack
- [ ] Grep for all `ofstrOpen`, `ifstrOpen`, `fstrOpen` callers and update
- [ ] Keep old signatures temporarily with `[[deprecated]]` attribute if needed for incremental migration
- [ ] Verify: ASAN smoke test with zero stream leaks

---

## Phase 5: ASAN CI Gate (Effort: 1 hour, Risk: None)

After all fixes, enable ASAN as a CI gate so new leaks are caught before merge.

- [ ] Add ASAN build to CI matrix (already exists as option: `-DSTAR_ASAN=ON`)
- [ ] Run smoke test under ASAN — must report zero leaks
- [ ] Add `ASAN_OPTIONS=detect_leaks=1` to the CI environment
- [ ] Document in CONTRIBUTING.md: "All PRs must pass ASAN smoke test"

---

## Execution Order

| Phase | What | Effort | Risk | Memory Saved |
|---|---|---|---|---|
| 1 | Loop/function leaks (6 fixes) | 2 hours | Low | ~10 MB + eliminates 100K+ small leaks |
| 2 | Large memory leaks (3 fixes) | 3 hours | Medium | **GBs** (twoPass, bamDedup) |
| 3 | Core class destructors (5 classes) | 1 day | Medium | All remaining per-thread leaks |
| 4 | Stream factory refactor | 4 hours | Medium | ~30-40 stream objects + file descriptors |
| 5 | ASAN CI gate | 1 hour | None | Prevents regression |

**Total: ~3 days.** Phases 1-2 are independent and can be done first for immediate impact. Phase 3 is the structural fix that enables proper cleanup everywhere. Phase 4 is the most invasive (touches ~40 call sites) but eliminates the most pervasive pattern.

## Validation After Each Phase

```bash
# Build with ASAN
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug -DSTAR_ASAN=ON
cmake --build build_asan

# Run smoke test
./build_asan/STAR --runMode alignReads ... (1M reads)

# Check: zero leaks, output byte-identical to reference
diff Solo.out/Gene/raw/matrix.mtx smoke_ref/Solo.out/Gene/raw/matrix.mtx
```

Every phase must produce byte-identical output to the pre-fix reference. Memory management changes must never alter alignment results.
