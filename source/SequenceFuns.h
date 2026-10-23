/**  general basic functions to operate on sequences, no classes
 */

#ifndef SEQUENCEFUNS_DEF
#define SEQUENCEFUNS_DEF

#include "IncludeDefine.h"

void complementSeqNumbers(char*, char*, uint64);
void convertNucleotidesToNumbers(const char* R0, char* R1, const uint64 Lread);
uint64 convertNucleotidesToNumbersRemoveControls(const char* R0, char* R1, const uint64 Lread);
void revComplementNucleotides(char* ReadsIn, char* ReadsOut, uint64 Lread); //complement the numeric sequences
void revComplementNucleotides(string &seq);
char nuclToNumBAM(char cc);
void nuclPackBAM(char* ReadsIn, char* ReadsOut, uint64 Lread);
char convertNt01234(const char R0);//transform sequence  from ACGT into 0-1-2-3-4 code

uint64 chrFind(uint64, uint64, uint64*); // find chromosome from global locus
uint64 localSearch(const char*, uint64, const char*, uint64, double); //local search to clip adapter
uint64 localSearchNisMM(const char *x, uint64 nx, const char *y, uint64 ny, double pMM);
uint32 localAlignHammingDist(const string &text, const string &query, uint32 &pos);
uint32 localSearchGeneral(const char *text, const uint32 textL, const vector<char> &query, const int32 textStart, const int32 textEnd, double pMM, vector <uint32> vecMM, uint32 &nMM);

uint64 qualitySplit(char*, uint64, uint64, uint64, uint64**);

int32 convertNuclStrToInt32(const string S, uint32 &intOut);
string convertNuclInt32toString(const uint32 nuclNum, const uint32 L);

int64 convertNuclStrToInt64(const string S, uint64 &intOut);
string convertNuclInt64toString(const uint64 nuclNum, const uint32 L);

#endif
