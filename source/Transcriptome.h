#ifndef CODE_Transcriptome
#define CODE_Transcriptome

#include <set>

#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "Quantifications.h"
#include "AlignVsTranscript.h"
#include "ReadAnnotations.h"

class Transcriptome {
public:
    string trInfoDir;

    vector <string> trID, geID, geName, geBiotype; //transcript/gene IDs
    uint32 nTr=0, nGe=0; //number of transcript/genes

    uint *trS=nullptr, *trE=nullptr, *trEmax=nullptr; //transcripts start,end,end-max

    uint32 nEx=0; //number of exons
    uint16 *trExN=nullptr; //number of exons per transcript
    uint32 *trExI=nullptr; //index of the first exon for each transcript in exSE
    uint8 *trStr=nullptr; //transcript strand
    uint32 *exSE=nullptr; //exons start/end
    uint32 *exLenCum=nullptr; //cumulative length of previous exons
    uint32 *trGene=nullptr; //transcript to gene correspondence
    uint32 *trLen=nullptr; //transcript lengths

    struct {//exon-gene structure for GeneCounts
       uint64 nEx;//number of exons/genes
       uint64 *s,*e, *eMax;  //exon start/end
       uint8  *str;   //strand
       uint32 *g, *t; //gene/transcript IDs
    } exG{};

    struct {//geneFull structure
        uint64 *s, *e, *eMax;
        uint8 *str;
        uint32 *g;
    } geneFull{};

    Quantifications *quants=nullptr; //metadata copies borrow; chunk owns its newly allocated counts

    //methods:
    Transcriptome (Parameters &Pin, bool load=true); //load=false creates a valid inactive placeholder
    uint32 quantAlign (Transcript &aG, Transcript *aTall);//transform coordinates for all aligns from genomic in RA to transcriptomic in RAtr
    void geneCountsAddAlign(uint nA, Transcript **aAll, vector<int32> &gene1); //add one alignment to gene counts
    void quantsAllocate(); //allocate quants structure
    void quantsOutput(); //output quantification files
    void geneFullAlignOverlap(uint nA, Transcript **aAll, int32 strandType, ReadAnnotFeature &annFeat);
    void geneFullAlignOverlap_ExonOverIntron(uint nA, Transcript **aAll, int32 strandType, ReadAnnotFeature &annFeat, ReadAnnotFeature &annFeatGeneConcordant);
    //void geneFullAlignOverlap_CR(uint nA, Transcript **aAll, int32 strandType, ReadAnnotations &readAnnot);
    void classifyAlign(Transcript **alignG, uint64 nAlignG, ReadAnnotations &readAnnot);
    void alignExonOverlap(uint nA, Transcript **aAll, int32 strandType, ReadAnnotFeature &annFeat);

private:
    Parameters &P; //normal "genomic" parameters

};

#endif
