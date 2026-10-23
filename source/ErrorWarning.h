#ifndef CODE_ErrorWarning
#define CODE_ErrorWarning

#include "IncludeDefine.h"
#include "Parameters.h"

void exitWithError(std::string messageOut, std::ostream &streamOut1, std::ostream &streamOut2, int errorInt, Parameters &P);
void warningMessage(std::string messageOut, std::ostream &streamOut1, std::ostream &streamOut2, Parameters &P);
#endif
