#ifndef H_readLoad
#define H_readLoad

#include "IncludeDefine.h"
#include "Parameters.h"
#include "SequenceFuns.h"

int readLoad(istream& readInStream, Parameters& P, uint64& Lread, uint64& LreadOriginal, \
		     char* readName, char* Seq, char* SeqNum, char* Qual, vector<ClipMate> &clipOneMate, \
			 uint64 &iReadAll, uint32 &readFilesIndex, char &readFilter, string &readNameExtra);

#endif
