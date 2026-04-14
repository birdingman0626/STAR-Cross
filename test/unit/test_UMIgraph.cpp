#include "doctest/doctest.h"
#include "UMIgraph.h"
// UMIgraph.h uses only <vector>/<array>/<cstdint>/<algorithm> — no STAR headers needed.

using u32 = uint32_t;
using EdgeList = std::vector<std::array<u32, 2>>;

TEST_CASE("graphNumberOfConnectedComponents - N=0, empty V returns 0") {
    std::vector<u32> color;
    CHECK(graphNumberOfConnectedComponents(0, {}, color) == 0);
    CHECK(color.empty());
}

TEST_CASE("graphNumberOfConnectedComponents - all isolated: N components") {
    std::vector<u32> color;
    // 3 nodes, no edges → 3 components
    CHECK(graphNumberOfConnectedComponents(3, {}, color) == 3);
    // isolated nodes have nodeColor == (uint32_t)-1
    REQUIRE(color.size() == 3);
    CHECK(color[0] == (u32)-1);
    CHECK(color[1] == (u32)-1);
    CHECK(color[2] == (u32)-1);
}

TEST_CASE("graphNumberOfConnectedComponents - single edge: 1 component") {
    std::vector<u32> color;
    EdgeList edges = {{0, 1}};
    CHECK(graphNumberOfConnectedComponents(2, edges, color) == 1);
    // Both nodes are connected: nodeColor is their common root
    REQUIRE(color.size() == 2);
    CHECK(color[0] == color[1]);
    CHECK(color[0] != (u32)-1);
}

TEST_CASE("graphNumberOfConnectedComponents - chain 0-1-2: 1 component") {
    std::vector<u32> color;
    EdgeList edges = {{0, 1}, {1, 2}};
    CHECK(graphNumberOfConnectedComponents(3, edges, color) == 1);
    REQUIRE(color.size() == 3);
    CHECK(color[0] == color[1]);
    CHECK(color[1] == color[2]);
}

TEST_CASE("graphNumberOfConnectedComponents - two disconnected pairs") {
    // Nodes 0-1 connected, nodes 2-3 connected, no cross edge
    std::vector<u32> color;
    EdgeList edges = {{0, 1}, {2, 3}};
    CHECK(graphNumberOfConnectedComponents(4, edges, color) == 2);
    REQUIRE(color.size() == 4);
    CHECK(color[0] == color[1]);
    CHECK(color[2] == color[3]);
    CHECK(color[0] != color[2]); // different components
}

TEST_CASE("graphNumberOfConnectedComponents - mixed: 2 connected + 2 isolated") {
    // N=5: nodes {0,1} connected, nodes {2,3,4} isolated
    std::vector<u32> color;
    EdgeList edges = {{0, 1}};
    CHECK(graphNumberOfConnectedComponents(5, edges, color) == 4); // 1 + 3 isolated
    REQUIRE(color.size() == 5);
    CHECK(color[0] == color[1]);        // connected pair
    CHECK(color[2] == (u32)-1);         // isolated
    CHECK(color[3] == (u32)-1);
    CHECK(color[4] == (u32)-1);
}

TEST_CASE("graphNumberOfConnectedComponents - self-loop edge (same node both ends)") {
    // Edge {0,0}: ufUnite(0,0) is a no-op, but hasEdge[0]=true → node 0 is "connected"
    // Expected: 1 component (node 0 forms its own connected component)
    std::vector<u32> color;
    EdgeList edges = {{0, 0}};
    CHECK(graphNumberOfConnectedComponents(1, edges, color) == 1);
    REQUIRE(color.size() == 1);
    CHECK(color[0] != (u32)-1);
}

TEST_CASE("graphNumberOfConnectedComponents - duplicate edges do not create extra components") {
    std::vector<u32> color;
    // Same edge repeated three times
    EdgeList edges = {{0, 1}, {0, 1}, {0, 1}};
    CHECK(graphNumberOfConnectedComponents(2, edges, color) == 1);
    REQUIRE(color.size() == 2);
    CHECK(color[0] == color[1]);
}

TEST_CASE("graphNumberOfConnectedComponents - star topology (hub + leaves)") {
    // Node 0 connects to nodes 1,2,3,4 — all in one component
    std::vector<u32> color;
    EdgeList edges = {{0,1},{0,2},{0,3},{0,4}};
    CHECK(graphNumberOfConnectedComponents(5, edges, color) == 1);
    REQUIRE(color.size() == 5);
    u32 root = color[0];
    for (int i = 1; i < 5; i++) CHECK(color[i] == root);
}

TEST_CASE("graphNumberOfConnectedComponents - long chain does not stack overflow") {
    // 10000-node chain: 0-1-2-...-9999
    // Verifies the iterative Union-Find handles long chains without recursion depth issues
    const u32 N = 10000;
    std::vector<u32> color;
    EdgeList edges;
    edges.reserve(N - 1);
    for (u32 i = 0; i + 1 < N; i++)
        edges.push_back({i, i + 1});
    CHECK(graphNumberOfConnectedComponents(N, edges, color) == 1);
    REQUIRE(color.size() == N);
    u32 root = color[0];
    for (u32 i = 1; i < N; i++) CHECK(color[i] == root);
}

TEST_CASE("graphNumberOfConnectedComponents - nodeColor unchanged for isolated node after merge nearby") {
    // N=4: edge {0,1}; nodes 2 and 3 isolated
    std::vector<u32> color;
    EdgeList edges = {{0, 1}};
    graphNumberOfConnectedComponents(4, edges, color);
    CHECK(color[2] == (u32)-1);
    CHECK(color[3] == (u32)-1);
}
