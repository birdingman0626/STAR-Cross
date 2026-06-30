#include "IncludeDefine.h"
#include "Parameters.h"
#include "ErrorWarning.h"
#include "streamFuns.h"

#define PAR_NAME_PRINT_WIDTH 30

// Load default parameters, parse command-line arguments, open the log file,
// load parameter files, re-scan CLI for final values, and print the effective
// command line.  On return, commandLineFull is populated and the log is open.
void Parameters::inputParameters_parseSources(int argInN, char* argIn[]) {

    //hard-coded parameters
    runRestart.type=0;

///////// Default parameters

    #include "parametersDefault.xxd"
    string parString( (const char*) parametersDefault,parametersDefault_len);
    stringstream parStream (parString);

    scanAllLines(parStream, 0, -1);
    for (uint ii=0; ii<parArray.size(); ii++) {
        if (parArray[ii]->inputLevel<0) {
            ostringstream errOut;
            errOut <<"BUG: DEFAULT parameter value not defined: "<<parArray[ii]->nameString;
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
    };

///////// Initial parameters from Command Line

    commandLine="";
    string commandLineFile="";

    if (argInN>1) {//scan parameters from command line
        commandLine += string(argIn[0]);
        for (int iarg=1; iarg<argInN; iarg++) {
            string oneArg=string(argIn[iarg]);

            if (oneArg=="--version") {//print version and exit
                std::cout << STAR_VERSION <<std::endl;
                exit(0);
            };

            size_t found = oneArg.find("=");
            if (found!=string::npos && oneArg.substr(0,2)=="--") {// --parameter=value
                string key = oneArg.substr(2, found - 2);
                string val = oneArg.substr(found + 1);
                if (val.find_first_of(" \t")!=std::string::npos) {//there is white space in the argument, put "" around
                    val ='\"' + val + '\"';
                };
                commandLineFile += '\n' + key + ' ' + val;
            } else if (oneArg.substr(0,2)=="--") {//parameter name, cut --
                string paramName = oneArg.substr(2);
                commandLineFile +='\n' + paramName;
                // Boolean flags: no value expected; inject "True" when flag stands alone.
                // Add new boolean flag names here as needed.
                static const string boolFlags[] = {"legacy", "webuiMetrics"};
                for (const auto &bf : boolFlags) {
                    if (paramName == bf) {
                        bool nextIsValue = (iarg + 1 < argInN) && string(argIn[iarg+1]).substr(0,2) != "--";
                        if (!nextIsValue) commandLineFile += " True";
                        break;
                    }
                }
            } else {//parameter value
                if (oneArg.find_first_of(" \t")!=std::string::npos) {//there is white space in the argument, put "" around
                    oneArg ='\"'  + oneArg +'\"';
                };
                commandLineFile +=' ' + oneArg;
            };
            commandLine += ' ' + oneArg;
        };
        istringstream parStreamCommandLine(commandLineFile);
        scanAllLines(parStreamCommandLine, 1, 2); //read only initial Command Line parameters
    };

	createDirectory(outFileNamePrefix, S_IRWXU, "--outFileNamePrefix", *this); //TODO: runDirPerm is hard-coded now. Need to load it from command-line

    outLogFileName=outFileNamePrefix + "Log.out";
    inOut->logMain.open(outLogFileName.c_str());
    if (inOut->logMain.fail()) {
        ostringstream errOut;
        errOut <<"EXITING because of FATAL ERROR: could not create output file: "<<outFileNamePrefix + "Log.out"<<"\n";
        errOut <<"SOLUTION: check if the path " << outFileNamePrefix << " exists and you have permissions to write there\n";
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    inOut->logMain << "STAR version=" << STAR_VERSION << "\n";
    inOut->logMain << "STAR compilation time,server,dir=" << COMPILATION_TIME_PLACE << "\n";
    inOut->logMain << "STAR git: " << GIT_BRANCH_COMMIT_DIFF << "\n";
    #ifdef COMPILE_FOR_LONG_READS
           inOut->logMain << "Compiled for LONG reads" << "\n";
    #endif

    //define what goes to cout
    if (outStd=="Log") {
        inOut->logStdOut=& std::cout;
    } else if (outStd=="SAM" || outStd=="BAM_Unsorted" || outStd=="BAM_SortedByCoordinate" || outStd=="BAM_Quant") {
        inOut->logStdOutFile.open((outFileNamePrefix + "Log.std.out").c_str());
        inOut->logStdOut= & inOut->logStdOutFile;
    } else {
        ostringstream errOut;
        errOut <<"EXITING because of FATAL PARAMETER error: outStd="<<outStd <<" is not a valid value of the parameter\n";
        errOut <<"SOLUTION: provide a valid value fot outStd: Log / SAM / BAM_Unsorted / BAM_SortedByCoordinate";
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    /*
    inOut->logMain << "##### DEFAULT parameters:\n" <<flush;
    for (uint ii=0; ii<parArray.size(); ii++) {
        if (parArray[ii]->inputLevel==0) {
            inOut->logMain << setw(PAR_NAME_PRINT_WIDTH) << parArray[ii]->nameString <<"    "<< *(parArray[ii]) << endl;
        };
    };
    */

    inOut->logMain <<"##### Command Line:\n"<<commandLine <<endl ;

    inOut->logMain << "##### Initial USER parameters from Command Line:\n";
    for (uint ii=0; ii<parArray.size(); ii++) {
        if (parArray[ii]->inputLevel==1) {
            inOut->logMain << setw(PAR_NAME_PRINT_WIDTH) << parArray[ii]->nameString <<"    "<< *(parArray[ii]) << endl;
        };
    };

///////// Parameters files

    if (parametersFiles.at(0) != "-") {//read parameters from a user-defined file
        for (uint ii=0; ii<parametersFiles.size(); ii++) {
            parameterInputName.push_back(parametersFiles.at(ii));
            ifstream parFile(parametersFiles.at(ii).c_str());
            if (parFile.good()) {
                inOut->logMain << "##### USER parameters from user-defined parameters file " <<parametersFiles.at(ii)<< ":\n" <<flush;
                scanAllLines(parFile, parameterInputName.size()-1, -1);
                parFile.close();
            } else {
                ostringstream errOut;
                errOut <<"EXITING because of fatal input ERROR: could not open user-defined parameters file " <<parametersFiles.at(ii)<< "\n" <<flush;
                exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
            };
        };
    };

///////// Command Line Final

    if (argInN>1) {//scan all parameters from command line and override previous values
        inOut->logMain << "###### All USER parameters from Command Line:\n" <<flush;
        istringstream parStreamCommandLine(commandLineFile);
        scanAllLines(parStreamCommandLine, 2, -1);
    };

    inOut->logMain << "##### Finished reading parameters from all sources\n\n" << flush;

    inOut->logMain << "##### Final user re-defined parameters-----------------:\n" << flush;

    ostringstream clFull;
    clFull << argIn[0];
    for (uint ii=0; ii<parArray.size(); ii++) {
        if (parArray[ii]->inputLevel>0) {
            inOut->logMain << setw(PAR_NAME_PRINT_WIDTH) << parArray[ii]->nameString <<"    "<< *(parArray[ii]) << endl;
            if (parArray[ii]->nameString != "parametersFiles" ) {
                clFull << "   --" << parArray[ii]->nameString << " " << *(parArray[ii]);
            };
        };
    };
    commandLineFull=clFull.str();
    inOut->logMain << "\n-------------------------------\n##### Final effective command line:\n" <<  clFull.str() << "\n";

    /*
    //     parOut.close();
    inOut->logMain << "\n##### Final parameters after user input--------------------------------:\n" << flush;
    //     parOut.open("Parameters.all.out");
    for (uint ii=0; ii<parArray.size(); ii++) {
        inOut->logMain << setw(PAR_NAME_PRINT_WIDTH) << parArray[ii]->nameString <<"    "<< *(parArray[ii]) << endl;
    };
    //     parOut.close();
    */
    inOut->logMain << "----------------------------------------\n\n" << flush;
}
