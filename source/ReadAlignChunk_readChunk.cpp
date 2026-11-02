#include "ReadAlignChunk.h"
#include "ThreadControl.h"
#include "ErrorWarning.h"
#include "SequenceFuns.h"
#include "GlobalVariables.h"
#include "SeedDiagnostics.h"
#include <chrono>
#include <algorithm>
#include <cstring>
#include "ChunkInputPrefetcher.h"
using namespace std;
inline uint64 fastqReadOneLine(ifstream&,char*);
inline void removeStringEndControl(string&);

namespace {
// Lines of a read file straight from its stream buffer's get area. A call returns false,
// having consumed nothing, unless the whole line ('\n' included) is already buffered and
// the stream is good; the caller then makes the original istream call. So every result is
// the one the istream call would give, and only lines that cross a buffer refill go the
// slow way.
struct BufferedLine : std::streambuf {
    static bool find(std::istream& in, size_t limit, const char*& begin, size_t& length) {
        if (!in.good()) return false;
        std::streambuf* b=in.rdbuf();
        char* g=(b->*(&BufferedLine::gptr))();
        char* e=(b->*(&BufferedLine::egptr))();
        if (!g || g>=e) return false;
        const void* nl=std::memchr(g,'\n',std::min<size_t>(size_t(e-g),limit));
        if (!nl) return false;
        begin=g; length=size_t(static_cast<const char*>(nl)-g);
        return true;
    }
    static void consume(std::istream& in, size_t n) {//n<=line length+1, all inside the get area
        std::streambuf* b=in.rdbuf();
        (b->*(&BufferedLine::gbump))(static_cast<int>(n));
    }
};
inline bool isFormattedSpace(char c) {return c==' '||c=='\t'||c=='\n'||c=='\v'||c=='\f'||c=='\r';}
// in>>word; getline(in,rest);  for a header line that is fully buffered and starts with a non-space
bool fastHeader(std::istream& in, std::string& word, std::string& rest) {
    const char* p; size_t n;
    if (!BufferedLine::find(in,size_t(-1),p,n) || n==0 || isFormattedSpace(p[0])) return false;
    size_t w=1;
    while (w<n && !isFormattedSpace(p[w])) ++w;
    word.assign(p,w); rest.assign(p+w,n-w);
    BufferedLine::consume(in,n+1);
    return true;
}
// getline(in,line)
bool fastGetline(std::istream& in, std::string& line) {
    const char* p; size_t n;
    if (!BufferedLine::find(in,size_t(-1),p,n)) return false;
    line.assign(p,n);
    BufferedLine::consume(in,n+1);
    return true;
}
// in.ignore(count,'\n')
bool fastIgnoreLine(std::istream& in, std::streamsize count) {
    const char* p; size_t n;
    if (count<1 || !BufferedLine::find(in,size_t(count),p,n)) return false; // '\n' must be among the first count chars
    BufferedLine::consume(in,n+1);
    return true;
}
struct InputLock {
    pthread_mutex_t* mutex;
    explicit InputLock(bool enabled):mutex(enabled ? &g_threadChunks.mutexInRead : nullptr) {
        if (mutex && pthread_mutex_lock(mutex)) throw std::runtime_error("input mutex lock failed");
    }
    ~InputLock(){if(mutex) pthread_mutex_unlock(mutex);}
};
}

namespace {
using ChunkSizes = std::array<uint64,MAX_N_MATES>;

// SAM input: copy one read (all its mate lines) into the chunk as FASTQ records
void readSamRecord(Parameters& P,char** chunkIn,ChunkSizes& chunkInSizeBytesTotal,bool& newFile) {
    string str1;
    P.inOut->readIn[0] >> str1;
    if (str1=="FILE") {
        newFile=true;
    } else {
        P.iReadAll++; //increment read number

        uint64 flag1;
        P.inOut->readIn[0] >> flag1;
        uint64 imate1=0;
        for (uint64 imate=0;imate<P.readNmates;imate++) {//not readNends: this is SAM input
            if (imate>0) {
                string str2;
                uint64 flag2;
                P.inOut->readIn[0] >> str2; //for imate=0 str1 was already read
                P.inOut->readIn[0] >> flag2; //read name and flag

                if ( str1 != str2 ) {
                    ostringstream errOut;
                    errOut << ERROR_OUT <<" EXITING because of FATAL ERROR in input BAM file: the consecutive lines in paired-end BAM have different read IDs:\n"
                           << str1 <<"   vs   "<< str2 << '\n'
                           << "\n SOLUTION: fix BAM file formatting. Paired-end reads should be always consecutive lines, with exactly 2 lines per paired-end read" ;
                    exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
                };

                if (! ( ((flag1 & 0x40) && (flag2 & 0x80)) || ((flag2 & 0x40) && (flag1 & 0x80)) ) ) {
                    ostringstream errOut;
                    errOut << ERROR_OUT <<" EXITING because of FATAL ERROR in input BAM file: the consecutive lines in paired-end BAM have wrong mate FLAG bits:\n"
                           << str1 <<"   "<< flag1 <<"   vs   "<< str2 <<"   "<< flag2 << '\n'
                           << "\n SOLUTION: fix BAM file formatting. Paired-end reads should be always consecutive lines, with exactly 2 lines per paired-end read."
                           << " Mate1 should have 0x40 bit set in the FLAG, Mate2 should have 0x80 bit set in the FLAG";
                    exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
                };

                str1 = str2;   //used below for both mates
                flag1 = flag2; //used below for both mates
            };
            char passFilterIllumina=(flag1 & 0x800 ? 'Y' : 'N');

            if (imate==1) {//2nd line is always opposite of the 1st one
                imate1=1-imate1;
            } else if (P.readNmates==2 && (flag1 & 0x80)) {//not readNends: this is SAM input
                imate1=1;
            } else {
                imate1=0;
            };

            //read ID or number
            if (P.outSAMreadID=="Number") {
                chunkInSizeBytesTotal[imate1] += snprintf(chunkIn[imate1] + chunkInSizeBytesTotal[imate1], P.chunkInSizeBytesArray - chunkInSizeBytesTotal[imate1], "@%llu", P.iReadAll);
            } else {
                chunkInSizeBytesTotal[imate1] += snprintf(chunkIn[imate1] + chunkInSizeBytesTotal[imate1], P.chunkInSizeBytesArray - chunkInSizeBytesTotal[imate1], "@%s", str1.c_str());
            };

            //iReadAll, passFilterIllumina, passFilterIllumina
            chunkInSizeBytesTotal[imate1] += snprintf(chunkIn[imate1] + chunkInSizeBytesTotal[imate1], P.chunkInSizeBytesArray - chunkInSizeBytesTotal[imate1], " %llu %c %i", P.iReadAll, passFilterIllumina, P.readFilesIndex);

            string dummy;
            for (int ii=3; ii<=9; ii++)
                P.inOut->readIn[0] >> dummy; //skip fields until sequence

            string seq1,qual1;
            P.inOut->readIn[0]  >> seq1 >> qual1;
            if (flag1 & 0x10) {//sequence reverse-coomplemented
                revComplementNucleotides(seq1);
                reverse(qual1.begin(),qual1.end());
            };

            string attrs;
            getline(P.inOut->readIn[0], attrs); //rest of the SAM line: str1 is now all SAM attributes - it's added to the read ID line (1st "fastq" line)
            chunkInSizeBytesTotal[imate1] += snprintf(chunkIn[imate1] + chunkInSizeBytesTotal[imate1], P.chunkInSizeBytesArray - chunkInSizeBytesTotal[imate1], "%s\n%s\n+\n%s\n", attrs.c_str(), seq1.c_str(), qual1.c_str());
        };
    };
}

// FASTQ, 1st stage: read the ID line of every mate and write the shared read ID line
void copyFastqReadID(Parameters& P,char** chunkIn,ChunkSizes& chunkInSizeBytesTotal,std::vector<std::string>& headerExtra) {
    string readID;
    // Preserve each mate's original comment after STAR's internal
    // bookkeeping fields (including Illumina sample indexes).
    const bool headerParsed=fastHeader(P.inOut->readIn[0], readID, headerExtra[0]);
    if (!headerParsed) P.inOut->readIn[0] >> readID;
    removeStringEndControl(readID);
    if (P.outSAMreadIDnumber) {
        readID="@"+to_string(P.iReadAll);
    };
    if (!headerParsed) getline(P.inOut->readIn[0], headerExtra[0]);
    removeStringEndControl(headerExtra[0]);
    for (uint64 imate=1; imate<P.readNends; imate++) {
        if (!fastGetline(P.inOut->readIn[imate], headerExtra[imate]))
            getline(P.inOut->readIn[imate], headerExtra[imate]);
        removeStringEndControl(headerExtra[imate]);
        const size_t comment = headerExtra[imate].find_first_of(" \t");
        if (comment == string::npos) headerExtra[imate].clear();
        else headerExtra[imate].erase(0, comment);
    }
    char passFilterIllumina='N';
    if (!headerExtra[0].empty()) {
        const size_t field2 = headerExtra[0].find_first_not_of(" \t");
        if (field2 != string::npos && headerExtra[0].size()-field2>=4 &&
            headerExtra[0][field2+1]==':' && headerExtra[0][field2+2]=='Y' && headerExtra[0][field2+3]==':' )
            passFilterIllumina='Y';
    };

    //add extra information to readID line
    readID += ' '+ to_string(P.iReadAll)+' '+passFilterIllumina+' '+to_string(P.readFilesIndex);

    //copy the same readID to both mates
    for (uint64 imate=0; imate<P.readNends; imate++) {
        if (readID.size() + headerExtra[imate].size() >= DEF_readNameLengthMax)
            exitWithError("EXITING: FASTQ header including comment is too long\n", std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
        chunkInSizeBytesTotal[imate] += readID.copy(chunkIn[imate] + chunkInSizeBytesTotal[imate], readID.size(),0);
        chunkInSizeBytesTotal[imate] += headerExtra[imate].copy(chunkIn[imate] + chunkInSizeBytesTotal[imate], headerExtra[imate].size(),0);
        chunkInSizeBytesTotal[imate]++;
        chunkIn[imate][chunkInSizeBytesTotal[imate]-1]='\n';
    };
}

// FASTQ: copy 3 (4 for stage 2) lines of every mate: sequence, dummy, quality
void copyFastqBodies(Parameters& P,char** chunkIn,ChunkSizes& chunkInSizeBytesTotal) {
    for (uint64 imate=0; imate<P.readNends; imate++) {
        // read 1st line for 2nd stage only
        if (P.outFilterBySJoutStage == 2)
            chunkInSizeBytesTotal[imate] += fastqReadOneLine(P.inOut->readIn[imate], chunkIn[imate] + chunkInSizeBytesTotal[imate]);
        //sequence
        chunkInSizeBytesTotal[imate] += fastqReadOneLine(P.inOut->readIn[imate], chunkIn[imate] + chunkInSizeBytesTotal[imate]);
        //skip 3rd line, record '+'
        if (!fastIgnoreLine(P.inOut->readIn[imate], DEF_readNameSeqLengthMax))
            P.inOut->readIn[imate].ignore(DEF_readNameSeqLengthMax, '\n');
        chunkIn[imate][chunkInSizeBytesTotal[imate]] = '+';
        chunkIn[imate][chunkInSizeBytesTotal[imate]+1] = '\n';
        chunkInSizeBytesTotal[imate] += 2;
        //quality
        uint64 lenIn = fastqReadOneLine(P.inOut->readIn[imate], chunkIn[imate] + chunkInSizeBytesTotal[imate]);
        chunkInSizeBytesTotal[imate] += lenIn;
    };
}

// FASTA, can be multiline, which is converted to single line
void readFastaRecord(Parameters& P,char** chunkIn,ChunkSizes& chunkInSizeBytesTotal) {
    for (uint64 imate=0; imate<P.readNends; imate++) {
        if (P.outFilterBySJoutStage!=2) {//not the 2nd stage of the 2-stage mapping

            if (P.outSAMreadID=="Number") {
                chunkInSizeBytesTotal[imate] += snprintf(chunkIn[imate] + chunkInSizeBytesTotal[imate], P.chunkInSizeBytesArray - chunkInSizeBytesTotal[imate], ">%llu", P.iReadAll);
            } else {
                string fastaReadID;
                P.inOut->readIn[imate] >> fastaReadID;
                // C++20 removes unbounded char* extraction. Reserve room
                // for the internal numeric/filter/file suffix as well.
                if (fastaReadID.size() + 64 >= DEF_readNameLengthMax)
                    exitWithError("EXITING: FASTA header is too long\n", std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
                chunkInSizeBytesTotal[imate] += fastaReadID.copy(
                    chunkIn[imate] + chunkInSizeBytesTotal[imate], fastaReadID.size());
            };

            P.inOut->readIn[imate].ignore(DEF_readNameSeqLengthMax,'\n');

            chunkInSizeBytesTotal[imate] += snprintf(chunkIn[imate] + chunkInSizeBytesTotal[imate], P.chunkInSizeBytesArray - chunkInSizeBytesTotal[imate], " %llu %c %i \n", P.iReadAll, 'N', P.readFilesIndex);
        };

        //read multi-line fasta
        char nextChar=P.inOut->readIn[imate].peek();
        while (nextChar!='@' && nextChar!='>' && nextChar!=' ' && nextChar!='\n' && P.inOut->readIn[imate].good()) {
            P.inOut->readIn[imate].getline(chunkIn[imate] + chunkInSizeBytesTotal[imate], DEF_readSeqLengthMax + 1 );
            if (P.inOut->readIn[imate].gcount()<2)
                break; //no more input

            chunkInSizeBytesTotal[imate] += P.inOut->readIn[imate].gcount()-1; //-1 because \n was counted, bu wee need to remove it
            if ( int(chunkIn[imate][chunkInSizeBytesTotal[imate]-1]) < 33 ) {//remove control char at the end if present
                chunkInSizeBytesTotal[imate]--;
            };

            nextChar=P.inOut->readIn[imate].peek();
        };
        chunkIn[imate][chunkInSizeBytesTotal[imate]]='\n';
        chunkInSizeBytesTotal[imate] ++;
    };
}

// a line that is not a read: it must be the FILE marker of the next input file
void readFileMarker(Parameters& P,bool& newFile) {
    string word1;
    P.inOut->readIn[0] >> word1;
    if (word1=="FILE") {//new file marker
        newFile=true;
    } else {//error
        ostringstream errOut;
        string str1;
        std::getline(P.inOut->readIn[0], str1);
        errOut << ERROR_OUT <<" EXITING because of FATAL ERROR in input reads: wrong read ID line format: the read ID lines should start with @ or > \n";
        errOut << "Offending line for read # " << P.iReadAll+1 << "\n" << word1 <<" "<< str1 << "\n";
        errOut << "SOLUTION: verify and correct the input read files\n";
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    };
}

void startNextReadFile(Parameters& P,bool& newFile) {
    P.inOut->readIn[0] >> P.readFilesIndex;
    pthread_mutex_lock(&g_threadChunks.mutexLogMain);
    P.inOut->logMain << "Starting to map file # " << P.readFilesIndex<<"\n";
    for (uint64 imate=0; imate<P.readFiles.names.size(); imate++) {
        P.inOut->logMain << "mate " <<imate+1 <<":   "<<P.readFiles.names.at(imate).at(P.readFilesIndex) <<"\n";
        P.inOut->readIn[imate].ignore(numeric_limits<streamsize>::max(),'\n');
    };
    P.inOut->logMain<<flush;
    pthread_mutex_unlock(&g_threadChunks.mutexLogMain);
    newFile=false;
}

// number the chunk and terminate every mate's buffer
void finishInputChunk(Parameters& P,char** chunkIn,const ChunkSizes& chunkInSizeBytesTotal,bool& noReadsLeft,uint64& iChunkIn) {
    //TODO: check here that both mates are zero or non-zero
    if (chunkInSizeBytesTotal[0]==0) {
        noReadsLeft=true; //true if there no more reads left in the file
        iChunkIn=g_threadChunks.chunkInN;//to keep things consistent
        g_threadChunks.chunkInN++;
    } else {
        noReadsLeft=false;
        iChunkIn=g_threadChunks.chunkInN;
        g_threadChunks.chunkInN++;
    };

    for (uint64 imate=0; imate<P.readNends; imate++)
        chunkIn[imate][chunkInSizeBytesTotal[imate]]='\n';//extra empty line at the end of the chunks
}
}

// Reads the next chunk of input reads into chunkIn; the caller holds the input lock.
void fillInputChunk(Parameters& P,char** chunkIn,std::array<uint64,MAX_N_MATES>& chunkInSizeBytesTotal,
                    std::vector<std::string>& headerExtra,bool& newFile,int iThread,bool& noReadsLeft,uint64& iChunkIn) {
    chunkInSizeBytesTotal={0,0};

    while (chunkInSizeBytesTotal[0] < P.chunkInSizeBytes && chunkInSizeBytesTotal[1] < P.chunkInSizeBytes && P.inOut->readIn[0].good() && P.inOut->readIn[1].good()) {
        char nextChar=P.inOut->readIn[0].peek();
        if (P.iReadAll==P.readMapNumber) {//do not read any more reads
            break;

        } else if (P.readFiles.typeN==10 && P.inOut->readIn[0].good() && P.outFilterBySJoutStage!=2) {//SAM input && not eof && not 2nd stage
            if (nextChar=='@') {//with SAM input linest that start with @ are headers
                P.inOut->readIn[0].ignore(DEF_readNameSeqLengthMax,'\n'); //read line and skip it
                continue;
            };
            readSamRecord(P,chunkIn,chunkInSizeBytesTotal,newFile);

        } else if (nextChar=='@') {//fastq, not multi-line
            P.iReadAll++; //increment read number
            if (P.outFilterBySJoutStage!=2) {//not the 2nd stage of the 2-stage mapping, read ID from the 1st read
                copyFastqReadID(P,chunkIn,chunkInSizeBytesTotal,headerExtra);
            };
            copyFastqBodies(P,chunkIn,chunkInSizeBytesTotal);

        } else if (nextChar=='>') {//fasta, can be multiline, which is converted to single line
            P.iReadAll++; //increment read number
            readFastaRecord(P,chunkIn,chunkInSizeBytesTotal);

        } else if (nextChar==' ' || nextChar=='\n' || !P.inOut->readIn[0].good()) {//end of stream
            {OptionalMutexLock logLock(g_threadChunks.mutexLogMain, P.runThreadN>1);
            P.inOut->logMain << "Thread #" <<iThread <<" end of input stream, nextChar="<<int(nextChar) <<endl;}
            break;
        } else {
            readFileMarker(P,newFile);
        };

        if (newFile) {
            startNextReadFile(P,newFile);
        };
    };
    finishInputChunk(P,chunkIn,chunkInSizeBytesTotal,noReadsLeft,iChunkIn);
}

void ReadAlignChunk::readChunk() {
        if (P.outFilterBySJoutStage<2 && P.inOut->chunkInputPrefetcher) {//chunk already read by the input producer
            P.inOut->chunkInputPrefetcher->take(inputStorage,chunkInSizeBytesTotal,noReadsLeft,iChunkIn);
            for (uint64 imate=0; imate<P.readNends; imate++) chunkIn[imate]=inputStorage[imate].get();
        } else if (P.outFilterBySJoutStage<2) {//read chunks from input file

            #ifdef STAR_CAPTURE_SEEDS
            const auto lockRequested=std::chrono::steady_clock::now();
            #endif
            InputLock lock(P.runThreadN>1);
            const auto heldSince=std::chrono::steady_clock::now();
            struct HeldTimer {double& s; std::chrono::steady_clock::time_point t; ~HeldTimer(){s+=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();}} held{inputLockHeldSeconds,heldSince};
            #ifdef STAR_CAPTURE_SEEDS
            const auto lockAcquired=std::chrono::steady_clock::now();
            if (P.runThreadN>1) if(auto* w=seedTrace::worker()) {
                w->inputLockWaitNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(lockAcquired-lockRequested).count();
                ++w->inputLockChunks;
            }
            #endif

            fillInputChunk(P,chunkIn,chunkInSizeBytesTotal,headerExtra,newFile,iThread,noReadsLeft,iChunkIn);

            #ifdef STAR_CAPTURE_SEEDS
            if (P.runThreadN>1) if(auto* w=seedTrace::worker())
                w->inputLockHeldNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-lockAcquired).count();
            #endif

        } else {//read from one file per thread
            noReadsLeft=true;
            for (uint64 imate=0; imate<P.readNends; imate++) {
                RA->chunkOutFilterBySJoutFiles[imate].flush();
                RA->chunkOutFilterBySJoutFiles[imate].seekg(0,ios::beg);
                RA->readInStream[imate]=& RA->chunkOutFilterBySJoutFiles[imate];
            };
        };

}

inline uint64 fastqReadOneLine(ifstream &streamIn, char *arrIn)
{
    uint64 lenIn;
    const char* line; size_t n;
    if (BufferedLine::find(streamIn, DEF_readNameSeqLengthMax, line, n)) {//same as getline(arrIn,DEF_readNameSeqLengthMax+1) below
        std::memcpy(arrIn, line, n); arrIn[n]=0;
        BufferedLine::consume(streamIn, n+1);
        lenIn = n+1;
    } else {
        streamIn.getline(arrIn, DEF_readNameSeqLengthMax+1 );
        lenIn = streamIn.gcount(); //=seqLength+1: includes \0 but not \n. We will replace \0 with \n
    };

    if (lenIn < 2) {
        // Empty line (1) or nothing read (0, end of input): store nothing. The
        // previous code reached the same result by rewriting the preceding '\n'
        // through arrIn[-1], and could return (uint64)-1 at end of input.
        return 0;
    };

    if ( int(arrIn[lenIn-2]) < 33 ) {//remove control char at the end if present
        --lenIn;
    };

    arrIn[lenIn-1]='\n'; //replace \0 with \n
    return lenIn; //lenIn contains \n at the end
};

inline void removeStringEndControl(string &str)
{//removes control character (including space) from the end of the string
    if (!str.empty() && int(str.back())<33)
        str.pop_back();
};
