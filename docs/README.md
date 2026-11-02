# Documentation

Start with the document for the task. Dated result files are historical evidence
for their recorded revision, not current release certification.

| Task | Entry point |
| --- | --- |
| Install and run | [Project README](../README.md), [build](BUILD.md), [STARsolo](STARsolo.md), [web UI](webui/README.md) |
| Validate a change | [Qualification plan](QUALIFICATION_PLAN.md), [CPU validation](CPU_VALIDATION.md), [local CI](LOCAL_CI.md) |
| Understand recorded checks | [Qualification results](QUALIFICATION_RESULTS_20261007.md) |
| Work on GPU acceleration | [Execution plan](GPU_FULL_PIPELINE_PLAN.md), [CLI and supported scope](GPU_BATCHED_SEEDING.md), [measurements](GPU_RESULTS.md) |
| Review GPU approaches | [Reference projects](GPU_MIGRATION_REFERENCE_REVIEW.md), [earlier experiments](GPU_EXPERIMENT.md) |
| Review scientific semantics | [Literature review](LITERATURE_ALIGNMENT_REVIEW.md), [references](REFERENCES.md) |
| Optimize CPU performance | [Optimization plan](OPTIMIZATION_PLAN.md), [C++20 backlog](CXX20_OPTIMIZATION_BACKLOG.md) |
| Check language/toolchain status | [C++20 status](CXX20_UPGRADE_PLAN.md), [performance RCA](CXX20_PERFORMANCE_RCA.md) |
| Change ownership or concurrency | [Ownership audit](OWNERSHIP_AUDIT.md), [shared-memory admission](SHARED_MEMORY_ADMISSION.md), [thread sanitizer](THREAD_SANITIZER_AUDIT.md) |
| Update dependencies/security | [Dependencies](DEPENDENCIES.md), [security review](SECURITY_SCAN_REVIEW_20261009.md), [reporting policy](../SECURITY.md) |

Historical architecture/maintenance and optimization plans/results retain their
original filenames for existing links. Consult git history for superseded
implementation chronology. Read current build, CLI and qualification contracts
before reusing commands from an older record.
