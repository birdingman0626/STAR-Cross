#include "cramOutput.h"
#include "htslib/htslib/sam.h"
#include "ErrorWarning.h"
#include <iostream>
#include <cstdio>

bool bamToCramReferenceless(const std::string &inBam, const std::string &outCram, Parameters &P) {

    samFile *in = sam_open(inBam.c_str(), "rb");
    if (in==NULL) {
        warningMessage("Could not open intermediate BAM for CRAM conversion: "+inBam+" ; keeping BAM output.", std::cerr, P.inOut->logMain, P);
        return false;
    };

    sam_hdr_t *hdr = sam_hdr_read(in);
    if (hdr==NULL) {
        sam_close(in);
        warningMessage("Could not read BAM header for CRAM conversion: "+inBam+" ; keeping BAM output.", std::cerr, P.inOut->logMain, P);
        return false;
    };

    samFile *out = sam_open(outCram.c_str(), "wc"); //"wc" = write CRAM
    if (out==NULL) {
        sam_hdr_destroy(hdr);
        sam_close(in);
        warningMessage("Could not open output CRAM file: "+outCram+" ; keeping BAM output.", std::cerr, P.inOut->logMain, P);
        return false;
    };

    // Referenceless CRAM: encode sequences verbatim, no external reference needed.
    hts_set_opt(out, CRAM_OPT_NO_REF, 1);
    if (P.runThreadN>1)
        hts_set_threads(out, P.runThreadN);

    bool ok = (sam_hdr_write(out, hdr) >= 0);
    bam1_t *b = bam_init1();
    int r = 0;
    while (ok && (r = sam_read1(in, hdr, b)) >= 0) {
        if (sam_write1(out, hdr, b) < 0) {
            ok = false;
            break;
        };
    };
    if (r < -1) //sam_read1 returns -1 at EOF, < -1 on error
        ok = false;

    bam_destroy1(b);
    sam_hdr_destroy(hdr);
    sam_close(in);
    if (sam_close(out) < 0)
        ok = false;

    if (ok) {
        remove(inBam.c_str()); //drop the intermediate BAM
        P.inOut->logMain << "CRAM conversion: " << inBam << " -> " << outCram << " (referenceless)\n" << std::flush;
    } else {
        remove(outCram.c_str()); //failed conversion: drop partial CRAM, keep BAM
        warningMessage("CRAM conversion failed for "+inBam+" ; keeping BAM output.", std::cerr, P.inOut->logMain, P);
    };
    return ok;
};
