#ifndef CODE_streamFuns
#define CODE_streamFuns

#include "Parameters.h"
#include <fstream>

unsigned long long fstreamReadBig(std::ifstream &S, char* A, unsigned long long N);
void fstreamWriteBig(std::ofstream &S, char* A, unsigned long long N, std::string fileName, std::string errorID, Parameters &P) ;
// Close a file STAR wrote. A failed open, write or final flush is fatal, so a
// missing or truncated output never ends in "finished successfully".
void ofstrClose(std::ofstream &S, const std::string &fileName, const std::string &errorID, Parameters &P);
void ofstrClose(std::fstream &S, const std::string &fileName, const std::string &errorID, Parameters &P);

std::fstream  &fstrOpen  (std::string fileName, std::string errorID, Parameters &P, bool flagDelete);
std::ofstream &ofstrOpen (std::string fileName, std::string errorID, Parameters &P);
std::ofstream ofstrOpenOwned (std::string fileName, std::string errorID, Parameters &P);
// Text-mode opens for line-oriented outputs (Windows keeps CRLF). A file that
// cannot be created is fatal here, before any mapping work.
std::ofstream ofstrOpenOwnedText (std::string fileName, std::string errorID, Parameters &P);
void ofstrOpenText (std::ofstream &S, std::string fileName, std::string errorID, Parameters &P);
std::ifstream &ifstrOpen (std::string fileName, std::string errorID, std::string solutionString, Parameters &P);
std::ifstream &ifstrOpenGenomeFile (std::string fileName, std::string errorID, Parameters &P);
void createDirectory(const std::string dirPathIn, const mode_t dirPerm, const std::string dirParameter, Parameters &P);

void copyFile(std::string fileIn, std::string fileOut);
#endif
