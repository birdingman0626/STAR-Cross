#ifndef CODE_BAMbinSortByCoordinate
#define CODE_BAMbinSortByCoordinate
#include "IncludeDefine.h"
#include "Parameters.h"
#include "Genome.h"
#include "Solo.h"

#include SAMTOOLS_BGZF_H

void BAMbinSortByCoordinate(uint32 iBin, uint64 binN, uint64 binS, uint64 nThreads, std::string dirBAMsort, Parameters &P, Genome &mapGen, Solo &solo);

#endif