# Failure-path qualification (2026-10-07)

## Decisions and changes

- Keep C++17 as the default and published language level. C++20 CI is compatibility
  coverage only; it is not a promotion.
- Reject failed POSIX read preprocessors before final statistics, Solo counting or
  `ALL DONE!`. Generated command scripts stop at the first failed input command.
  Deliberately bounded consumers still terminate/reap their producer.
- Roll back a shared-memory object created by a failed allocation attempt, never a
  pre-existing object. Preserve the original exception if rollback itself fails.
- Treat shared-memory `GetSize()` as usable capacity, not the exact requested
  length. The preceding macOS CI failed this test's equality assertion; the fix
  does not change the mapping layout or payload format.
- Test normal and abrupt child exit separately. An abrupt exit does not promise
  automatic POSIX name removal; the fixture explicitly validates recovery cleanup.
- Inject Linux mapping failure with a restored `RLIMIT_AS`, without reserving a
  large allocation. Skip that injection under ASan because its virtual address
  reservations conflict with the limit; retain the other IPC checks.
- Add a positive EmptyDrops synthetic oracle: rescue one signal barcode below the
  knee, exclude the specified ambient barcodes, and verify exact retained counts.
  This does not establish empirical FDR on a real library.
- Run the existing s390x release validation harness in ordinary CI too. No new
  release tag or publication is needed to exercise that platform.

## Observed verification

- Native Windows C++17 Release: 98/98 CTest tests, miniature alignment/count/input
  failure integration, frozen-reference comparisons and positive EmptyDrops pass.
- Native Windows CUDA C++17: 98/98 CTest tests; required GPU junction-remap
  equivalence and resident-seed checks pass. CUDA remains experimental/optional.
- Python validation-harness unit checks: 17 discovered, 13 passed and four
  platform-specific skips on Windows.
- Linux Release and ASan integration passed the preprocessor failure fix before
  the final allocation-rollback fixture was added. Do not substitute that older
  result for final IPC validation.
- Final Linux Release rebuild passes 100/100 CTest tests, standalone cell filtering
  and miniature frozen-reference alignment/count integration. The final Windows
  input fixture additionally checks positive execution and intentionally bounded
  consumption; hosted Linux CI runs the same expanded fixture.
- Final local Linux IPC verification encountered WSL startup timeouts. Retrying
  via root allowed compilation to start, but overlapping retries damaged only the
  ignored build's object-generation step; reconfigure and retry serially. No WSL
  shutdown or other workloads were terminated. Serial reconfiguration recovered:
  final SysV and POSIX lifecycle tests both pass, including mapping-failure
  rollback and abrupt-exit recovery. Both also pass under ASan with leak detection
  enabled (the explicitly documented address-limit injection is skipped there).
- macOS and s390x require successful hosted CI for the new commit. A submitted job
  or prior green build is not proof of this candidate's acceptance.

## Remaining acceptance boundaries

- The earlier five-pair A/A run exceeded the 2% noise gate (median 4.02%). No new
  speedup is claimed. At inspection, approximately 32.5 GiB physical RAM was free;
  the full index needs about 31.5 GiB before adequate workload headroom. Defer the
  calibrated A/A and ten paired A/B measurements until resources are available.
- Existing 100k/1M prefix fixtures are synchronized prefixes, not representative
  full-library samples. Larger/full-library validation, long-read scientific
  oracles and independent ambiguous-transcript estimator validation remain open.
- Custom preprocessors that close stdout but continue indefinitely, and complete
  CLI cancellation ownership, are not universally qualified by these checks.
- Reuse existing tests/workflows instead of adding a parallel framework. Do not
  ignore maintained source or scientific outputs to make a dirty tree look clean.

Local receipts live in the ignored validation directory. Original user data and
prior scientific results remain untouched.
