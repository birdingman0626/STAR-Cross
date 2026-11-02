#ifndef STAR_DIRECTIONAL_UMI_H
#define STAR_DIRECTIONAL_UMI_H
#include <cstdint>
#include <cstdlib>
#include <vector>

// UMI-tools directional reachability (v1.1.6 network.py). Records contain
// packed UMI, read count, corrected UMI. Ties use sequence order deterministically.
inline uint32_t collapseDirectionalUMIs(uint32_t* records, uint32_t count, uint32_t stride) {
    if (!count) return 0;
    std::qsort(records, count, stride*sizeof(uint32_t), [](const void* left, const void* right) {
        const auto* a=static_cast<const uint32_t*>(left);
        const auto* b=static_cast<const uint32_t*>(right);
        if (a[1]!=b[1]) return a[1]>b[1] ? -1:1;
        return a[0]<b[0] ? -1 : a[0]>b[0] ? 1:0;
    });
    std::vector<unsigned char> assigned(count,0);
    std::vector<uint32_t> queue;
    queue.reserve(count);
    uint32_t groups=0;
    for (uint32_t root=0;root<count;++root) {
        if (assigned[root]) continue;
        ++groups;queue.clear();queue.push_back(root);assigned[root]=1;
        records[root*stride+2]=records[root*stride];
        for (size_t head=0;head<queue.size();++head) {
            const auto* parent=records+queue[head]*stride;
            for (uint32_t child=0;child<count;++child) {
                if (assigned[child]) continue;
                auto* target=records+child*stride;
                if (uint64_t(parent[1])+1<2*uint64_t(target[1])) continue;
                uint32_t difference=parent[0]^target[0];
                // Fold each two-bit base to one bit; exactly one changed base.
                difference=(difference | (difference>>1)) & 0x55555555u;
                if (!difference || (difference & (difference-1))) continue;
                assigned[child]=1;target[2]=records[root*stride];queue.push_back(child);
            }
        }
    }
    return groups;
}
#endif
