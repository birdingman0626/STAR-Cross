# C++17 checkpoint and migration boundary

The published `4a079e7` release passed its exact-SHA main, release and CodeQL
workflows (38343513694, 38343515991, 38343513698); all six binary archives exist.
This is verified coverage, not a claim that all defects or security alerts are gone.

An additional pre-migration check reproduced a coordinate-sort deadlock at an
exact memory cap: the fixture needs 4356 bytes, preflight accepts that value, but
the old admission predicate requires strictly less. The published Windows binary
exceeded a 12-second watchdog after starting sorting. Evidence is retained under
`star-sort-exact-cap-before-bnced82s` in the local test temporary directory.

Admission now accepts equality and uses subtraction to avoid addition overflow.
The worker sleeps only after unsuccessful admission. The scientific fixture
derives the exact cap from its own preflight log, reruns sorting and compares BAM
records with the generous-cap run. It must pass on each release platform.

Local Windows: 124/124 CTest cases and the complete miniature scientific suite
pass, including exact-cap sorting and comparison against released `4a079e7`.
Linux optimized ASan/UBSan also passes the full scientific suite against released
`4a079e7` (sparse SA=3, leak detection and halt-on-error enabled), including the
exact-cap and oversized-FASTA fatal-worker fixtures. CI contract and watchdog
tests pass (Windows skips the Unix process-group test). Hosted release gates must
pass before the new checkpoint can be described as published/qualified.

Known boundaries remain explicit: pathological external read producers can block
FIFO startup/teardown; exhaustive parser fuzzing and every legacy mode are not
qualified. The remaining 18 CodeQL alerts have dispositions, not blanket waivers.
See `SECURITY_SCAN_REVIEW_20261009.md`. C++20 work does not erase these boundaries.

The requested next stage is a separate C++20 migration branch. Keep the shipping
default at 17 until the changed source has passed all supported target gates,
including s390x, and the host/device CUDA combination has been checked separately.
Start with a bounded borrowed seed-batch interface; do not combine language
migration with an alignment algorithm or dependency upgrade.
