#pragma once
#include <string>
#include <vector>

#include "vmath.h"

struct Mesh {
    std::vector<V3> v, n;
    std::vector<uint32_t> tri;
    V3 bmin, bmax;
    struct Node { V3 bmin, bmax; int left = -1, right = -1, first = 0, count = 0; };
    std::vector<Node> nodes;
    std::vector<int> order;
    int triCount() const { return (int)(tri.size() / 3); }
    void build();
    bool intersect(V3 o, V3 d, float tmin, float tmax, float& t, V3& nrm, bool flat = false) const;

private:
    int buildNode(int first, int count, int depth);
};

bool loadOBJ(const std::string& path, Mesh& m, std::string& err);

struct TreeParams {
    float height = 1.f, spread = 38.f, trunkRadius = 0.05f, leafSize = 0.07f, leafDensity = 1.f;
    int levels = 3, branches = 4;
    uint32_t seed = 1;
};
void makeTree(const TreeParams& p, Mesh& trunk, Mesh& leaves);
