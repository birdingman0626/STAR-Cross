#include "BAMfunctions.h"
#include "BAMlimits.h"
#include "htslib/htslib/kstring.h"
#include "samAux.h"
#include "bamEndian.h"
#include "ErrorWarning.h"
using namespace std;

string bam_cigarString (bam1_t *b) {//output CIGAR string
    const bam1_core_t *c = &b->core;

    string cigarString("");
    if ( c->n_cigar > 0 ) {
      uint32_t *cigar = bam_get_cigar(b);
      for (uint32_t i = 0; i < c->n_cigar; ++i) {
        cigarString+=to_string((uint64)bam_cigar_oplen(cigar[i]))+bam_cigar_opchr(cigar[i]);
      };
    };


    return cigarString;
};

int bam_read1_fromArray(char *bamChar, bam1_t *b) //modified from samtools bam_read1 to assign BAM record in mmemry to bam structure
{
	bam1_core_t *c = &b->core;
	int32_t block_len; //, ret, i;
 	uint32_t *x;

    uint32_t *bamU32=(uint32_t*) bamChar;
    block_len=bamU32[0];

    x=bamU32+1;

	c->tid = x[0]; c->pos = x[1];
	c->bin = x[2]>>16; c->qual = x[2]>>8&0xff; c->l_qname = x[2]&0xff;
	c->flag = x[3]>>16; c->n_cigar = x[3]&0xffff;
	c->l_qseq = x[4];
	c->mtid = x[5]; c->mpos = x[6]; c->isize = x[7];
	b->l_data = block_len - 32;
	if (b->l_data < 0 || c->l_qseq < 0) return -4;
	if ((char *)bam_get_aux(b) - (char *)b->data > b->l_data)
		return -4;
	if (b->m_data < (uint32_t) b->l_data) {//l_data>=0 checked above
		b->m_data = b->l_data;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4146) // htslib kroundup32 negates an unsigned on purpose
#endif
		kroundup32(b->m_data);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
// no need to realloc b->data, because it is overwritten later with bamChar
	}
    b->data=(uint8_t*) bamChar+4*9;

	return 4 + block_len;
}


bool outBAMwriteHeader (BGZF* fp, const string &samh, const vector <string> &chrn, const vector <uint64> &chrl) {
    bool ok = bgzf_write(fp,"BAM\001",4)==4;
    int32 hlen=samh.size();
    ok = ok && bamWriteInt32LE(fp,hlen)==4;
    ok = ok && bgzf_write(fp,samh.c_str(),hlen)==hlen;
    int32 nchr=static_cast<int32>(chrn.size()); //reference count and lengths were checked by requireBAMreferenceLengths
    ok = ok && bamWriteInt32LE(fp,nchr)==4;
    for (int32 ii=0; ok && ii<nchr; ii++) {
        int32 rlen = static_cast<int32>(chrn.at(ii).size()+1); //names are short header strings
        int32 slen = static_cast<int32>(chrl[ii]);
        ok = ok && bamWriteInt32LE(fp,rlen)==4;
        ok = ok && bgzf_write(fp,chrn.at(ii).data(),rlen)==rlen; //this includes \0 at the end of the string
        ok = ok && bamWriteInt32LE(fp,slen)==4;
    };
    return ok && bgzf_flush(fp)==0;
};

void requireBAMreferenceLengths(const vector <string> &chrn, const vector <uint64> &chrl, const string &bamName, Parameters &P) {
    if (chrn.size() > BAM_MAX_REFERENCE_LENGTH)
        exitWithError("EXITING because of fatal PARAMETER error: too many references (" + to_string(chrn.size()) + ") for BAM output " + bamName +
                      "\nSOLUTION: use --outSAMtype SAM.\n", std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
    const size_t ii = firstOversizedBAMreference(chrl);
    if (ii < chrl.size())
        exitWithError("EXITING because of fatal PARAMETER error: reference " + chrn.at(ii) + " is " + to_string(chrl[ii]) +
                      " bases long; BAM output " + bamName + " supports at most " + to_string(BAM_MAX_REFERENCE_LENGTH) + " bases per reference" +
                      "\nSOLUTION: use --outSAMtype SAM, or split the reference into shorter sequences.\n",
                      std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
};

void exitBAMwriteError(const string &what, Parameters &P) {
    exitWithError("EXITING because of fatal OUTPUT error: could not write " + what +
                  "\nSOLUTION: check free disk space and write permissions.\n", std::cerr, P.inOut->logMain, EXIT_CODE_FILE_WRITE, P);
};

int bamAttrArrayWrite(int32 attr, const char* tagName, char* attrArray ) {
    attrArray[0]=tagName[0];attrArray[1]=tagName[1];
    attrArray[2]='i';
    i32_to_le(attr, reinterpret_cast<uint8_t*>(attrArray+3));
    return 3+sizeof(int32);
};
int bamAttrArrayWrite(float attr, const char* tagName, char* attrArray ) {
    attrArray[0]=tagName[0];attrArray[1]=tagName[1];
    attrArray[2]='f';
    float_to_le(attr, reinterpret_cast<uint8_t*>(attrArray+3));
    return 3+sizeof(int32);
};
int bamAttrArrayWrite(char attr, const char* tagName, char* attrArray ) {
    attrArray[0]=tagName[0];attrArray[1]=tagName[1];
    attrArray[2]='A';
    attrArray[3]=attr;
    return 3+sizeof(char);
};
int bamAttrArrayWrite(const string &attr, const char* tagName, char* attrArray ) {
    attrArray[0]=tagName[0];attrArray[1]=tagName[1];
    attrArray[2]='Z';
    memcpy(attrArray+3,attr.c_str(),attr.size()+1);//copy string data including \0
    return 3+attr.size()+1;
};
int bamAttrArrayWrite(const vector<char> &attr, const char* tagName, char* attrArray ) {
    attrArray[0]=tagName[0];attrArray[1]=tagName[1];
    attrArray[2]='B';
    attrArray[3]='c';
    const int32 size = static_cast<int32>(attr.size());
    i32_to_le(size, reinterpret_cast<uint8_t*>(attrArray+4));
    memcpy(attrArray+4+sizeof(int32),attr.data(),attr.size());//copy array data
    return 4+sizeof(int32)+attr.size();
};
int bamAttrArrayWrite(const vector<int32> &attr, const char* tagName, char* attrArray ) {
    attrArray[0]=tagName[0];attrArray[1]=tagName[1];
    attrArray[2]='B';
    attrArray[3]='i';
    const int32 size = static_cast<int32>(attr.size());
    i32_to_le(size, reinterpret_cast<uint8_t*>(attrArray+4));
    for (size_t i=0; i<attr.size(); ++i)
        i32_to_le(attr[i], reinterpret_cast<uint8_t*>(attrArray+8+4*i));
    return 4+sizeof(int32)+sizeof(int32)*attr.size();
};

int bamAttrArrayWriteSAMtags(string &attrStr, char *attrArray, size_t capacity, Parameters &P) {
    size_t pos1=0, pos2=0;
    string selected;
    do {//cycle over multiple tags separated by tab
        pos2 = attrStr.find('\t',pos1);
        string attr1 = attrStr.substr(pos1, pos2-pos1); //substring containing one tag
        pos1=pos2+1; //for the next search

        if (attr1.empty())
            continue; //extra tab at the beginning, or consecutive tabs
            
        if (attr1.size()<5 || attr1[2]!=':' || attr1[4]!=':')
            exitWithError("EXITING: malformed SAM auxiliary field\n", std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
        uint16_t tagn;
        memcpy(&tagn, attr1.data(), sizeof(tagn));
        if ( !P.readFiles.samAttrKeepAll && P.readFiles.samAttrKeep.count(tagn)==0 )
            continue; //skip tags not contained the list            

        if (!selected.empty()) selected += '\t';
        selected += attr1;
    } while (pos2 != string::npos);

    try {
        const auto bytes = samAuxBytes(selected, capacity);
        if (!bytes.empty()) memcpy(attrArray, bytes.data(), bytes.size());
        return static_cast<int>(bytes.size());
    } catch (const std::exception &error) {
        exitWithError(string("EXITING: ")+error.what()+"\n", std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
        return 0;
    }
};


