#ifndef CODE_signalFromBAM
#define CODE_signalFromBAM
#include "htslib/htslib/sam.h"
#include  <fstream>
#include <string>
#include "Stats.h"
#include "Parameters.h"



void signalFromBAM(const std::string bamFileName, const std::string sigFileName, Parameters &P);

#endif
