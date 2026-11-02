#ifndef STAR_STITCH_CORE_BRIDGE_H
#define STAR_STITCH_CORE_BRIDGE_H
// Host-side conversion between STAR objects and the plain stitching core.
#include "StitchCore.h"
#include "IncludeDefine.h"
#include <string>
class Parameters;
class Genome;
class Transcript;
struct StitchWindowResult;
struct StitchResults;

namespace stitchcore {
Params paramsFrom(const Parameters& P);
GenomeView genomeFrom(const Genome& g);
void toCore(const Transcript& in, Tr& out);
// Writes the fields stitching can change; the rest of `out` is left as is.
void fromCore(const Tr& in, Transcript& out);
// unpackTr followed by fromCore, without the intermediate Tr: writes exactly the same fields.
void fromPacked(const TrHead& h, const TrRow* rowBase, Transcript& out);
// Empty when equal on all fields stitching can change, else a description.
std::string compare(const Tr& core, const Transcript& cpu);
// STAR_STITCH_VERIFY=1: run the core next to stitchWindowAligns on every
// window and abort on the first difference (diagnostic; doubles stitch cost).
bool verifyEnabled();
// Runs the core on one window (WA rows as passed to stitchWindowAligns, trA the
// window's initial transcript, mateBefore the maxScoreMate values before the
// CPU call) and throws std::logic_error on any difference from the CPU result.
void verifyWindow(const Parameters& P, const Genome& g, const uint64 (*WA)[WA_SIZE], uint64 nA,
                  const Transcript& trA, const char* R, uint64 Lread, const uint64* readLength,
                  uint64 mismatchLimit, const intScore* mateBefore, const intScore* mateAfter,
                  Transcript* const* wTr, uint64 nWinTr, uint64 readId);
// Compares device results for one window with the CPU stitchWindowAligns output.
void verifyDevice(const ::StitchWindowResult& w, const ::StitchResults& results, uint64 resultBase, Transcript* const* wTr,
                  uint64 nWinTr, uint64 readId);
void verifyMate(int device, intScore cpu, uint64 mate, uint64 readId);
} // namespace stitchcore
#endif
