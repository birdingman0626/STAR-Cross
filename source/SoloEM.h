#ifndef STAR_SOLO_EM_H
#define STAR_SOLO_EM_H
#include <vector>

inline double soloEMThreshold(double value) { return value<0.01 ? 0.0:value; }

// Preserve the ordinary truncated EM update exactly. If truncation removes all
// support for this molecule, use its pre-truncation weights, without changing
// the weights used by other molecules. An already-zero set receives one uniform count.
template<class GeneSet>
void soloEMDistribute(const GeneSet& genes,const std::vector<double>& previous,
                      std::vector<double>& next) {
    double norm=0;
    for (const auto gene:genes) norm+=soloEMThreshold(previous[gene]);
    const bool truncated=norm>0;
    if (!truncated) for (const auto gene:genes) norm+=previous[gene];
    if (norm==0) {
        const double uniform=1.0/genes.size();
        for (const auto gene:genes) next[gene]+=uniform;
        return;
    }
    const double inverse=1.0/norm;
    for (const auto gene:genes)
        next[gene]+=(truncated ? soloEMThreshold(previous[gene]):previous[gene])*inverse;
}
#endif
