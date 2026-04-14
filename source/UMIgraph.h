// UMIgraph.h — Union-Find + connected-component logic for UMI graph collapsing.
// Extracted from SoloFeature_collapseUMI_Graph.cpp so it can be tested independently.
// Uses only <vector>/<array>/<cstdint>/<algorithm> — no STAR-specific types required.
#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <algorithm>

// Path-halving Union-Find (iterative, no recursion, no stack-overflow risk on long chains)
static inline uint32_t ufFind(std::vector<uint32_t> &parent, uint32_t x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]]; // path compression (halving)
        x = parent[x];
    }
    return x;
}

static inline void ufUnite(std::vector<uint32_t> &parent, std::vector<uint32_t> &rank,
                            uint32_t a, uint32_t b) {
    a = ufFind(parent, a);
    b = ufFind(parent, b);
    if (a == b) return;
    if (rank[a] < rank[b]) std::swap(a, b);
    parent[b] = a;
    if (rank[a] == rank[b]) rank[a]++;
}

// Count connected components in an undirected graph with N nodes and edge list V.
// nodeColor[i] is set to the root node index for connected nodes, left as (uint32_t)-1
// for isolated nodes (no edges).
// Returns the total number of connected components (isolated + non-isolated groups).
inline uint32_t graphNumberOfConnectedComponents(
        uint32_t N,
        std::vector<std::array<uint32_t, 2>> V,
        std::vector<uint32_t> &nodeColor) {
    nodeColor.resize(N, (uint32_t)-1);

    if (V.empty())
        return N;

    std::vector<uint32_t> parent(N), ufRank(N, 0);
    for (uint32_t i = 0; i < N; i++) parent[i] = i;

    std::vector<bool> hasEdge(N, false);
    for (uint32_t ii = 0; ii < V.size(); ii++) {
        ufUnite(parent, ufRank, V[ii][0], V[ii][1]);
        hasEdge[V[ii][0]] = true;
        hasEdge[V[ii][1]] = true;
    }

    // Count isolated nodes and assign colors to connected nodes
    uint32_t nConnComp = 0;
    for (uint32_t ii = 0; ii < N; ii++) {
        if (!hasEdge[ii]) {
            ++nConnComp; // isolated node counts as its own component
        } else {
            nodeColor[ii] = ufFind(parent, ii);
        }
    }

    // Count distinct roots among connected nodes
    std::vector<bool> rootSeen(N, false);
    for (uint32_t ii = 0; ii < N; ii++) {
        if (hasEdge[ii]) {
            uint32_t root = ufFind(parent, ii);
            if (!rootSeen[root]) {
                rootSeen[root] = true;
                ++nConnComp;
            }
        }
    }

    return nConnComp;
}
