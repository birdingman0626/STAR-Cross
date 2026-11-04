#include "Parameters.h"
#include "ErrorWarning.h"
#include "streamFuns.h"
#include <fstream>
#include <sys/stat.h>
#include "serviceFuns.cpp"
using namespace std;
void Parameters::readFilesInit() 
{//initialize read files - but do not open yet

    readFilesInitType();

    readFiles.prefixFinal=(readFiles.prefix=="-" ? "" : readFiles.prefix);
    
    if (readFiles.manifest[0]=="-") {//no manifest, file names in readFilesIn
        readFilesInitNamesFromIn();
    } else {//read file names from manifest
        readFilesInitManifest();
    };

    inOut->logMain << "Number of fastq files for each mate = " << readFiles.N << endl;
    
    readFiles.commandString="";
    if (readFiles.command.at(0)=="-") {
        if (readFiles.N>1)
            readFiles.commandString="cat   ";//concatenate multiple files
    } else {
        for (uint64 ii=0; ii<readFiles.command.size(); ii++) 
            readFiles.commandString+=readFiles.command.at(ii)+"   "; //concatenate into one string
    };    
    
    if (readFiles.typeN==1) {
        readNends=readFiles.names.size(); //for now the number of mates is defined by the number of input files
    } else if (readFiles.typeN==10) {//find the number of mates from the SAM file
        if (readFiles.type.size()==2 && readFiles.type.at(1)=="SE") {
            readNends=1;
        } else if (readFiles.type.size()==2 && readFiles.type.at(1)=="PE") {
            readNends=2;
        } else {
            ostringstream errOut;
            errOut <<"EXITING because of FATAL INPUT ERROR: --readFilesType SAM requires specifying SE or PE reads"<<"\n";
            errOut <<"SOLUTION: specify --readFilesType SAM SE for single-end reads or --readFilesType SAM PE for paired-end reads\n";
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
    };
    
    readNmates=readNends; //this may be changed later if one of the reads is barcode rea
};

//--readFilesType: Fastx or SAM, and SAM tags to keep
void Parameters::readFilesInitType()
{
    if (readFiles.type.at(0) == "Fastx") {
        readFiles.typeN=1;
    } else if (readFiles.type.at(0) == "SAM"){
        readFiles.typeN=10;
        readFiles.samAttrKeepAll = false;
        readFiles.samAttrKeepNone = false;
        if (readFiles.samAttrKeepIn.at(0) == "All") {
            readFiles.samAttrKeepAll = true;
        } else if (readFiles.samAttrKeepIn.at(0) == "None") {
            readFiles.samAttrKeepNone = true;
        } else {
            for (auto &tag: readFiles.samAttrKeepIn) {
                if (tag.size()!=2) {
                    exitWithError("EXITING because of FATAL PARAMETER ERROR: each SAM tags in --readFilesSAMtagsKeep should contain two letters\n\
                                  SOLUTION: specify only two-letter tags in --readFilesSAMtagsKeep.",
                                  std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
                };
                uint16_t tagn = * ( (uint16_t*) tag.c_str() );
                readFiles.samAttrKeep.insert(tagn);
            };
        };
    } else {
        ostringstream errOut;
        errOut <<"EXITING because of FATAL INPUT ERROR: unknown/unimplemented value for --readFilesType: "<<readFiles.type.at(0) <<"\n";
        errOut <<"SOLUTION: specify one of the allowed values: Fastx or SAM\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };
};

//file names from --readFilesIn, and read groups from --outSAMattrRGline
void Parameters::readFilesInitNamesFromIn()
{
    readFiles.names.resize(readFiles.in.size());
    
    for (uint32 imate=0; imate<readFiles.names.size(); imate++) {
        splitString(readFiles.in[imate], ',', readFiles.names[imate]);
        if (readFiles.names[imate].back().empty()) {//extra comma at the end
            readFiles.names[imate].pop_back();
        };
    
        if (imate>0 && readFiles.names[imate].size() != readFiles.names[imate-1].size() ) {
            ostringstream errOut;
            errOut <<"EXITING: because of fatal INPUT ERROR: number of input files for mate" << imate+1 <<"="<< readFiles.names[imate].size()  <<" is not equal to that for mate"<< imate-1 <<"="<< readFiles.names[imate-1].size() <<"\n";
            errOut <<"Make sure that the number of files in --readFilesIn is the same for both mates\n";
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
        
        for ( auto &fn : readFiles.names[imate] )
            fn = readFiles.prefixFinal + fn; //add prefix
    };

    readFiles.N = readFiles.names[0].size();

    readFilesInitReadGroups();
};

void Parameters::readFilesInitReadGroups()
{
    //read groups
    if (outSAMattrRGline.at(0)!="-") {
        string linefull;
        for (uint64 ii=0;ii<outSAMattrRGline.size(); ii++) {//concatenate into one line
            if (ii==0 || outSAMattrRGline.at(ii)==",") {//start new entry
                if (ii>0) ++ii;//skip comma
                outSAMattrRGlineSplit.push_back(outSAMattrRGline.at(ii)); //start new RG line with the first field which must be ID:xxx
                if (outSAMattrRGlineSplit.back().substr(0,3)!="ID:") {
                    ostringstream errOut;
                    errOut <<"EXITING because of FATAL INPUT ERROR: the first word of a line from --outSAMattrRGline="<<outSAMattrRGlineSplit.back()<<" does not start with ID:xxx read group identifier\n";
                    errOut <<"SOLUTION: re-run STAR with all lines in --outSAMattrRGline starting with ID:xxx\n";
                    exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
                };
                outSAMattrRG.push_back(outSAMattrRGlineSplit.back().substr(3)); //this adds the ID field
            } else {//keep adding fields to this RG line, until the next comma
                outSAMattrRGlineSplit.back()+="\t" + outSAMattrRGline.at(ii);
            };
        };
    };
    
    if (outSAMattrRG.size()>1 && outSAMattrRG.size()!=readFiles.N) {
        ostringstream errOut;
        errOut <<"EXITING: because of fatal INPUT ERROR: number of input read files: "<< readFiles.N << " does not agree with number of read group RG entries: "<< outSAMattrRG.size() <<"\n";
        errOut <<"Make sure that the number of RG lines in --outSAMattrRGline is equal to either 1, or the number of input read files in --readFilesIn\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    } else if (outSAMattrRG.size()==1) {//use the same read group for all files
        for (uint32 ifile=1; ifile<readFiles.N; ifile++) {
            outSAMattrRG.push_back(outSAMattrRG.at(0));
        };
    };           
};

//file names and read groups from --readFilesManifest
void Parameters::readFilesInitManifest()
{
    //TODO check that outSAMattrRGline and readFilesIn are not set, throw an error
    
    ifstream rfM = ifstrOpenOwned(readFiles.manifest[0], ERROR_OUT, "SOLUTION: check the path and permissions for readFilesManifest = " + readFiles.manifest[0], *this);
    inOut->logMain << "Reading input file names and read groups from readFileManifest " << readFiles.manifest[0] << endl;

    readFiles.names.resize(2);
    string rfMline;
    while (getline(rfM, rfMline)) {
    	if (rfMline.find_first_not_of(" \t")>=rfMline.size())
    		continue; //skip blank lines

        uint32 itab1=0, itab2=0;
        for (uint32 imate=0; imate<2; imate++) {//SE manifest 2nd column contains "-"
            itab2=rfMline.find('\t',itab1);
            if (itab2>=rfMline.size()) {
                ostringstream errOut;
                errOut <<"EXITING because of FATAL INPUT FILE error: readFileManifest file " << readFiles.manifest[0] <<  " has to contain at least 3 tab separated columns\n";
                errOut <<"SOLUTION: fix the formatting of the readFileManifest file: Read1 <tab> Read2 <tab> ReadGroup. For single-end reads, use - in the 2nd column.\n";
                exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_INPUT_FILES, *this);
            };
            readFiles.names[imate].push_back( readFiles.prefixFinal + rfMline.substr(itab1,itab2-itab1) );
            itab1=itab2+1;
            
            inOut->logMain << readFiles.names[imate].back() <<'\t';
        };
        
        outSAMattrRGlineSplit.push_back(rfMline.substr(itab2+1));
        
        if (outSAMattrRGlineSplit.back().substr(0,3)!="ID:")
            outSAMattrRGlineSplit.back().insert(0,"ID:");
        
        itab2=outSAMattrRGlineSplit.back().find('\t');
        outSAMattrRG.push_back(outSAMattrRGlineSplit.back().substr(3,itab2-3));
        
        inOut->logMain <<  outSAMattrRGlineSplit.back() <<'\n';
        
    };
    rfM.close();
    
    readNends = ( readFiles.names[1][0].back()=='-' ? 1 : 2);
    readFiles.names.resize(readNends);//resize if readFilesN=1
    readFiles.N = readFiles.names[0].size();
};
