#include "BAMfunctions.h"
#include "htslib/htslib/kstring.h"
#include "samAux.h"
#include "bamEndian.h"
#include "ErrorWarning.h"


string bam_cigarString (bam1_t *b) {//output CIGAR string
//    kstring_t strK;
//    kstring_t *str=&strK;
    const bam1_core_t *c = &b->core;

    string cigarString("");
    if ( c->n_cigar > 0 ) {
      uint32_t *cigar = bam_get_cigar(b);
      for (int i = 0; i < c->n_cigar; ++i) {
        cigarString+=to_string((uint)bam_cigar_oplen(cigar[i]))+bam_cigar_opchr(cigar[i]);
      };
    };


//	if (c->n_cigar) { // cigar
//		for (int i = 0; i < c->n_cigar; ++i) {
//			kputw(bam_cigar_oplen(cigar[i]), str);
//			kputc(bam_cigar_opchr(cigar[i]), str);
//		}
//	} else kputc('*', str);
//
//    string cigarString (str->s,str->l);
    return cigarString;
};

int bam_read1_fromArray(char *bamChar, bam1_t *b) //modified from samtools bam_read1 to assign BAM record in mmemry to bam structure
{
	bam1_core_t *c = &b->core;
	int32_t block_len; //, ret, i;
// // 	uint32_t x[8];
// // 	if ((ret = bgzf_read(fp, &block_len, 4)) != 4) {
// // 		if (ret == 0) return -1; // normal end-of-file
// // 		else return -2; // truncated
// // 	}
 	uint32_t *x;

    uint32_t *bamU32=(uint32_t*) bamChar;
    block_len=bamU32[0];

// // 	if (bgzf_read(fp, x, 32) != 32) return -3;
// // 	if (fp->is_be) {
// // 		ed_swap_4p(&block_len);
// // 		for (i = 0; i < 8; ++i) ed_swap_4p(x + i);
// // 	}
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
	if (b->m_data < b->l_data) {
		b->m_data = b->l_data;
		kroundup32(b->m_data);
// no need to realloc b->data, because it is overwritten later with bamChar
//		b->data = (uint8_t*)realloc(b->data, b->m_data);
//		if (!b->data)
//			return -4;
	}
// // 	if (bgzf_read(fp, b->data, b->l_data) != b->l_data) return -4;
// // 	//b->l_aux = b->l_data - c->n_cigar * 4 - c->l_qname - c->l_qseq - (c->l_qseq+1)/2;
// // 	if (fp->is_be) swap_data(c, b->l_data, b->data, 0);
    b->data=(uint8_t*) bamChar+4*9;

	return 4 + block_len;
}


void outBAMwriteHeader (BGZF* fp, const string &samh, const vector <string> &chrn, const vector <uint> &chrl) {
    bgzf_write(fp,"BAM\001",4);
    int32 hlen=samh.size();
    bamWriteInt32LE(fp,hlen);
    bgzf_write(fp,samh.c_str(),hlen);
    int32 nchr=(int32) chrn.size();
    bamWriteInt32LE(fp,nchr);
    for (int32 ii=0;ii<nchr;ii++) {
        int32 rlen = (int32) (chrn.at(ii).size()+1);
        int32 slen = (int32) chrl[ii];
        bamWriteInt32LE(fp,rlen);
        bgzf_write(fp,chrn.at(ii).data(),rlen); //this includes \0 at the end of the string
        bamWriteInt32LE(fp,slen);
    };
    bgzf_flush(fp);
};

// calculate bin given an alignment covering [beg,end) (zero-based, half-close-half-open)
int reg2bin(int beg, int end)
{
    --end;
    if (beg>>14 == end>>14) return ((1<<15)-1)/7 + (beg>>14);
    if (beg>>17 == end>>17) return ((1<<12)-1)/7 + (beg>>17);
    if (beg>>20 == end>>20) return ((1<<9)-1)/7 + (beg>>20);
    if (beg>>23 == end>>23) return ((1<<6)-1)/7 + (beg>>23);
    if (beg>>26 == end>>26) return ((1<<3)-1)/7 + (beg>>26);
    return 0;
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
int bamAttrArrayWrite(string &attr, const char* tagName, char* attrArray ) {
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


