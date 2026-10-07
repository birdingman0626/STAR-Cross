// Tiny IPC-only fixture: no genome allocation, no global IPC cleanup.
#include "SharedMemory.h"
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

static_assert(!std::is_copy_constructible<SharedMemory>::value, "IPC handles must not copy");
static_assert(!std::is_move_constructible<SharedMemory>::value, "IPC handles must not move implicitly");

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static bool absent(key_t key) {
#ifdef POSIX_SHARED_MEM
    const auto name = "/" + std::to_string(key);
    const int fd = shm_open(name.c_str(), O_RDWR, 0);
    if (fd >= 0) { close(fd); return false; }
#else
    if (shmget(key, 0, 0) >= 0) return false;
#endif
    return errno == ENOENT;
}
static void signalByte(int fd) {
    const char byte = 'x';
    require(write(fd, &byte, 1) == 1, "pipe write");
}
static void waitByte(int fd) {
    char byte;
    require(read(fd, &byte, 1) == 1, "pipe read");
}

int main(int argc, char** argv) {
    try {
        if (argc == 5) {
            const key_t key = static_cast<key_t>(std::stol(argv[2]));
            {
                SharedMemory borrower(key, true);
                require(!borrower.NeedsAllocation() && !borrower.IsAllocator(), "child attaches");
                require(borrower.GetSize() >= 128, "attached capacity covers payload");
                require(*static_cast<int*>(borrower.GetMapped()) == 42, "shared payload");
                signalByte(std::stoi(argv[3]));
                waitByte(std::stoi(argv[4]));
                require(*static_cast<int*>(borrower.GetMapped()) == 43, "survives owner detach");
                if (std::string(argv[1]) == "abrupt-child") _exit(77);
            }
            return 0;
        }
        // ftok is not collision-free. Refuse a pre-existing data OR counter key;
        // never remove a resource that this fixture did not create.
        char path[] = "/tmp/star-ipc-XXXXXX";
        const int temp = mkstemp(path);
        require(temp >= 0, "temporary key file");
        struct KeyFile {
            const char* path;
            ~KeyFile() { unlink(path); }
        } keyFile{path}; // Keep inode reserved while parallel backends execute.
        key_t key = -1;
        for (int attempt = 0; attempt < 255; ++attempt) {
            const int project = (getpid() + attempt) % 255 + 1;
            const key_t candidate = ftok(path, project);
            if (candidate != -1 && candidate != std::numeric_limits<key_t>::max()
                && absent(candidate) && shmget(candidate + 1, 0, 0) < 0 && errno == ENOENT) {
                key = candidate;
                break;
            }
        }
        close(temp);
        require(key != -1 && key != std::numeric_limits<key_t>::max(), "valid key");
        require(absent(key) && shmget(key + 1, 0, 0) < 0 && errno == ENOENT, "unused keys");
        {
            SharedMemory kept(key, false);
            require(kept.GetMapped() == nullptr, "unallocated mapping");
            bool overflow = false;
            try { kept.Allocate(std::numeric_limits<size_t>::max()); }
            catch (const SharedMemoryException& e) { overflow = e.GetErrorDetail() == EOVERFLOW; }
            require(overflow, "allocation overflow rejected");
            kept.Allocate(128);
            // POSIX shm ftruncate/fstat may expose page-rounded capacity on macOS.
            require(kept.GetSize() >= 128 && kept.IsAllocator(), "allocator capacity");
            *static_cast<int*>(kept.GetMapped()) = 42;
            bool duplicate = false;
            try { kept.Allocate(128); }
            catch (const SharedMemoryException& e) { duplicate = e.GetErrorCode() == EALREADYALLOCATED; }
            require(duplicate, "duplicate allocation rejected");
        }
        require(!absent(key), "keep retains segment");
        struct shmid_ds counterInfo;
        const int counterId = shmget(key + 1, 0, 0);
        require(counterId >= 0 && shmctl(counterId, IPC_STAT, &counterInfo) == 0,
                "kept counter accessible");
        require(counterInfo.shm_nattch == 0, "keep detaches counter without removing it");
        for (bool abrupt : {false, true}) {
            auto owner = std::make_unique<SharedMemory>(key, true);
            if (owner->NeedsAllocation()) {
                owner->Allocate(128);
                *static_cast<int*>(owner->GetMapped()) = 42;
            }
            int ready[2], done[2];
            require(pipe(ready) == 0 && pipe(done) == 0, "pipes");
            const pid_t child = fork();
            require(child >= 0, "fork");
            if (child == 0) {
                close(ready[0]); close(done[1]);
                const auto keyText = std::to_string(key);
                const auto readyText = std::to_string(ready[1]);
                const auto doneText = std::to_string(done[0]);
                execl(argv[0], argv[0], abrupt ? "abrupt-child" : "child", keyText.c_str(), readyText.c_str(), doneText.c_str(), nullptr);
                _exit(127);
            }
            close(ready[1]); close(done[0]);
            waitByte(ready[0]);
            *static_cast<int*>(owner->GetMapped()) = 43;
            owner.reset();
            require(!absent(key), "do not remove while child attached");
            signalByte(done[1]);
            close(ready[0]); close(done[1]);
            int status = 0;
            require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == (abrupt ? 77 : 0), "child exit");
            if (abrupt) {
                require(!absent(key), "abrupt exit does not unlink a named resource");
                SharedMemory recovery(key, true);
                require(*static_cast<int*>(recovery.GetMapped()) == 43, "recovery retains payload");
            }
            require(absent(key), "last detach removes segment");
            require(shmget(key + 1, 0, 0) < 0 && errno == ENOENT, "last detach removes counter");
        }
#if defined(__linux__) && !defined(STAR_TEST_SKIP_ALLOCATION_LIMIT)
        {
            auto failed = std::make_unique<SharedMemory>(key, true);
            struct rlimit original;
            require(getrlimit(RLIMIT_AS, &original) == 0, "read address-space limit");
            struct rlimit limited = original;
            limited.rlim_cur = 0; // deny mapping, without allocating real RAM
            require(setrlimit(RLIMIT_AS, &limited) == 0, "inject mapping failure");
            bool rejected = false;
            try { failed->Allocate(128); }
            catch (const SharedMemoryException&) { rejected = true; }
            catch (...) { setrlimit(RLIMIT_AS, &original); throw; }
            require(setrlimit(RLIMIT_AS, &original) == 0, "restore address-space limit");
            failed.reset();
            const bool cleaned = absent(key);
            if (!cleaned) { SharedMemory recovery(key, false); recovery.Clean(); }
            require(rejected && cleaned, "failed allocation removes its own partial segment");
        }
#endif
        {
            SharedMemory removed(key, false);
            removed.Allocate(128);
            removed.Clean();
            removed.Clean();
            require(removed.GetMapped() == nullptr && removed.NeedsAllocation(), "idempotent clean");
        }
        require(absent(key) && shmget(key + 1, 0, 0) < 0 && errno == ENOENT, "no destructor counter recreation");
#ifdef SHM_DEST
        // Invalidate this fixture's counter. IPC_STAT can still succeed until
        // detach, but a removed counter cannot certify absence of other users.
        {
            auto failed = std::make_unique<SharedMemory>(key, true);
            failed->Allocate(128);
            const int ownCounter = shmget(key + 1, 0, 0);
            require(ownCounter >= 0 && shmctl(ownCounter, IPC_RMID, nullptr) == 0,
                    "inject missing fixture counter");
            failed.reset();
            require(!absent(key), "failed usage query must not remove data");
            SharedMemory explicitRemover(key, false);
            explicitRemover.Clean();
        }
        require(absent(key) && shmget(key + 1, 0, 0) < 0 && errno == ENOENT,
                "failure fixture explicitly cleaned");
#endif
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "IPC lifecycle failed: " << e.what() << '\n';
        return 1;
    }
}
