#include "readLoad.h"
#include "ErrorWarning.h"
#include "FixedStreamBuf.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
using namespace std;

namespace {
// Reads lines straight from STAR's own chunk buffer when the stream is a FixedInputBuf.
// Each call takes the fast path only when the result is certain to equal the istream
// call it replaces (good stream, '\n' found before the length limit); otherwise it makes
// that istream call, before anything has been consumed.
struct ChunkLines {
    istream& is;
    FixedInputBuf* buf;
    std::streamsize count=0; // what gcount() would report for the last getline
    explicit ChunkLines(istream& s):is(s),buf(dynamic_cast<FixedInputBuf*>(s.rdbuf())) {}
    int peek() {
        if (buf && is.good() && buf->current()<buf->end()) return (unsigned char)*buf->current();
        return is.peek();
    }
    void getline(char* dst, std::streamsize n) {
        if (buf && is.good() && n>1) {
            char* c=buf->current();
            const size_t limit=std::min<size_t>(buf->end()-c, size_t(n-1)); // newline must come before n-1 stored chars
            if (const void* nl=std::memchr(c,'\n',limit)) {
                const size_t k=static_cast<const char*>(nl)-c;
                std::memcpy(dst,c,k); dst[k]=0;
                buf->advanceTo(c+k+1); count=std::streamsize(k+1);
                return;
            }
        }
        is.getline(dst,n); count=is.gcount();
    }
    void getCharAndSkipOne(char& ch) { // is.get(ch); is.ignore();
        if (buf && is.good() && buf->end()-buf->current()>=2) {
            char* c=buf->current(); ch=c[0]; buf->advanceTo(c+2);
            return;
        }
        is.get(ch); is.ignore();
    }
};
}
int readLoad(istream& readInStream, Parameters& P, uint64& Lread, uint64& LreadOriginal, \
            char* readName, char* Seq, char* SeqNum, char* Qual, vector<ClipMate> &clipOneMate, \
            uint64 &iReadAll, uint32 &readFilesIndex, char &readFilter, string &readNameExtra)
{//load one read from a stream
    int readFileType=0;

    ChunkLines in(readInStream);
    const int first=in.peek();
    if (first!='@' && first!='>')
        return -1; //end of the stream

    // Read header line directly into readName buffer, then parse fields with char* arithmetic
    // This avoids std::string + istringstream heap allocations per read
    static thread_local char line1buf[DEF_readNameLengthMax + 256];
    in.getline(line1buf, sizeof(line1buf));

    // Parse readName (first token)
    char *p = line1buf;
    char *dst = readName;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && dst - readName < (int)DEF_readNameLengthMax - 1)
        *dst++ = *p++;
    *dst = '\0';

    if (strlen(readName)>=DEF_readNameLengthMax-1) {
        ostringstream errOut;
        errOut << "EXITING because of FATAL ERROR in reads input: read name is too long:" << in.count<<"\nRead Name="<<readName<<"\nDEF_readNameLengthMax="<<DEF_readNameLengthMax<<'\n';
        errOut << "SOLUTION: increase DEF_readNameLengthMax in IncludeDefine.h and re-compile STAR\n";
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    };

    // Parse iReadAll, readFilter, readFilesIndex from remaining fields
    while (*p == ' ' || *p == '\t') ++p;
    iReadAll = (uint64)strtoull(p, &p, 10);
    while (*p == ' ' || *p == '\t') ++p;
    if (*p && *p != '\n' && *p != '\r') readFilter = *p++;
    while (*p == ' ' || *p == '\t') ++p;
    readFilesIndex = (uint32)strtoul(p, &p, 10);
    while (*p == ' ' || *p == '\t') ++p;

    // Rest of line is readNameExtra
    char *extraStart = p;
    char *extraEnd = p;
    while (*extraEnd && *extraEnd != '\n' && *extraEnd != '\r') ++extraEnd;
    readNameExtra.assign(extraStart, extraEnd - extraStart);

    in.getline(Seq,DEF_readSeqLengthMax+1); //extract sequence

    Lread=in.count-1;

    if (Lread<1) {
        ostringstream errOut;
        errOut << "EXITING because of FATAL ERROR in reads input: short read sequence line: " << Lread <<"\n";
        errOut << "Read Name="<< readName <<'\n'<< "Read Sequence=\"" << Seq <<"\"\nDEF_readNameLengthMax="<< DEF_readNameLengthMax <<'\n'<< "DEF_readSeqLengthMax="<<DEF_readSeqLengthMax<<'\n';
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    };
    if (Lread>DEF_readSeqLengthMax) {
        ostringstream errOut;
        errOut << "EXITING because of FATAL ERROR in reads input: Lread>=" << Lread << "   while DEF_readSeqLengthMax=" << DEF_readSeqLengthMax <<'\n'<< "Read Name="<<readName<<'\n';
        errOut << "SOLUTION: increase DEF_readSeqLengthMax in IncludeDefine.h and re-compile STAR\n";
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    };

    LreadOriginal=Lread;

    convertNucleotidesToNumbers(Seq,SeqNum,Lread);
    
    if (readName[0]=='@') {//fastq format: read the '+' line
        readFileType=2;
        in.getCharAndSkipOne(clipOneMate[0].clippedInfo); //clip info char (used if clipChunk is activated, only 5' for now), then \n
    };

    clipOneMate[0].clip(Lread, SeqNum); //5p clip
    clipOneMate[1].clip(Lread, SeqNum); //3p clip

    if (readName[0]=='@') {//fastq format, read qualities
        in.getline(Qual,DEF_readSeqLengthMax);//read qualities
        if ((uint64) in.count != LreadOriginal+1) {//inconsistent read sequence and quality
            ostringstream errOut;
            errOut << "EXITING because of FATAL ERROR in reads input: quality string length is not equal to sequence length\n";
            errOut << readName <<'\n'<< Seq <<'\n'<< Qual <<'\n'<< "SOLUTION: fix your fastq file\n";
            exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
        };
        if (P.outQSconversionAdd!=0) {
            for (uint64 ii=0;ii<LreadOriginal;ii++) {
                int qs=int(Qual[ii])+P.outQSconversionAdd;
                if (qs<33) {
                    qs=33;
                } else if (qs>126) {
                    qs=126;
                };
                Qual[ii]=qs;
            };
        };
    
    } else if (readName[0]=='>') {//fasta format, assign Qtop to all qualities
        readFileType=1;
        for (uint64 ii=0;ii<LreadOriginal;ii++) 
            Qual[ii]='A';
        Qual[LreadOriginal]=0;
    } else {//wrong header
        ostringstream errOut;
        errOut <<"Unknown reads file format: header line does not start with @ or > : "<< readName<<"\n";
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    };

    //trim read name TODO this is needed only for one mate
    for (uint64 ii=0; ii<P.readNameSeparatorChar.size(); ii++) {
        char* pSlash=strchr(readName,P.readNameSeparatorChar.at(ii)); //trim everything after ' '
        if (pSlash!=NULL) *pSlash=0;
    };
    return readFileType;
};
