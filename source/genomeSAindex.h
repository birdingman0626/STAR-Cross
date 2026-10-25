#ifndef CODE_genomeSAindex
#define CODE_genomeSAindex
#include "PackedArray.h"
#include "Parameters.h"
#include "Genome.h"

void genomeSAindex(char * G, PackedArray & SA, Parameters & P, PackedArray & SAip, Genome &mapGen);
void genomeSAindexChunk(char * G, PackedArray & SA, Parameters & P, PackedArray & SAi, uint64 iSA1, uint64 iSA2, Genome &mapGen);
void funSAiFindNextIndex(char *G, PackedArray &SA, uint64 isaStep, uint64 & isa, uint64 & indFull, int & iL4, Genome &mapGen);

#endif
