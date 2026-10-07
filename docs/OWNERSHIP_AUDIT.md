# Ownership inventory and migration boundaries

Audited 2026-10-07. This file distinguishes implemented storage ownership from
unverified whole-program teardown. C++17 remains sufficient for these changes.

| Resource | Owner / borrowers | Current release boundary | Status |
| --- | --- | --- | --- |
| PackedArray allocation | `storage` owns allocation base; `charArray` is a mutable view, potentially offset or rebound | Explicit idempotent `deallocateArray()` or automatic destruction | Implemented; copying borrows, moving transfers an owner; owner-copy assignment rejects |
| Genome suffix-array aliases | SA, SAinsert, SApass1, SApass2 may point inside another reservation | Genome snapshots in STAR/pass1 must not outlive original storage | PackedArray no longer copies release authority; broader Genome migration pending |
| Genome G/G1 and metadata | Heap or shared storage depending on `gLoad`; G is commonly an interior pointer | Existing `freeMemory()` and explicit sharedMemory deletion before Parameters streams | Not migrated; must distinguish attach/detach/remove before replacing cleanup |
| BAM buffers/bin arrays | BAMoutput unique owners; binStart elements borrow offsets in bamArray | After mapping, Solo, sorting and other BAM consumers finish | Implemented without zero-filling the large buffer |
| BAM temporary streams | BAMoutput owns streams returned by `ofstrOpen()` | Checked `finalize()`, before deleting temporary files | Implemented; destructor closes best-effort only; deferred disk-full regression added |
| BAM BGZF | Parameters owns open handle; BAMoutput borrows | Checked flush/close in STAR; borrower destruction never closes it | Implemented for unsorted/transcriptome handles; other standalone BAM writers still need a separate audit |
| ReadAlign arenas | Per-thread allocations; borrows Genome/Transcriptome/Parameters | Currently process lifetime | Not migrated; optional WASP/merged reads and chimeric stream aliases need explicit owner/view design |
| Chunk input/output and Transcriptome | Chunk owns input streams/buffers and local quantifications; metadata shallow-copied | All worker threads must join before release | BAM writers migrated; remaining chunk resources pending |
| CUDA allocation | Existing DeviceBuffer/Buffer scoped owners | Synchronizing adapter execution followed by cudaFree | Actual adapter/seed checks and memcheck; stream/event/global ownership not broadened |
| SharedMemory | Noncopyable named IPC attachment and counter; external log stream borrowed | Detach both mappings; explicit Remove or verified last-user policy removes names | SysV/POSIX two-process lifecycle and failure fixtures pass on Linux; full Genome ownership and concurrent admission remain pending |

`PackedArray` view copies intentionally do not extend the source lifetime. They
are not deep copies or reference-counted ownership. Moving a borrowed view into
an owner keeps its reservation alive, avoiding a self-alias use-after-free.
Borrowers must never read after owner release. A const/span-only public interface
is a later API migration, not established by this change.

`exitWithError()` calls `exit()`: normal automatic-stack unwinding is not
guaranteed on fatal paths. Do not remove the full-CLI leak exception or claim
complete exception/cancellation cleanup from these scoped migrations.

Next stages require independent fixtures for pass2 growth, concurrent IPC admission,
shared-memory load/keep/remove modes, partial construction, long-read/chimeric
and worker teardown. No blanket shared_ptr or zero-filled large vectors.

## Shared-memory continuation

The small IPC fixtures now cover keep/reopen, cross-process payload visibility,
first-owner detach without removing a live borrower's segment, last-user removal,
counter attachment balance, repeated explicit cleanup and allocation overflow.
They refuse pre-existing keys and keep the temporary key-file inode reserved;
parallel executions choose an unused project key instead of deleting collisions.
On platforms exposing `SHM_DEST`, an invalidated counter must not authorize
automatic data removal. Cleanup errors detach best-effort, not unconditional
`Clean()`. The exception's error-only constructor initializes all its fields.

Mapping size comes from kernel metadata, not an uninitialized shared header.
The existing header/payload offset remains reserved, preserving genome layout.
Unix SysV and POSIX fixtures ran under ASan/UBSan with leak detection; real STAR
SysV load/keep/remove modes were compared against NoSharedMemory. Windows still
supports only NoSharedMemory. These tests do not prove atomic last-user decisions
against concurrent new admissions, all syscall-failure paths, or orphan-free
constructor failure. Do not mark all of O3 complete from this bounded work.
