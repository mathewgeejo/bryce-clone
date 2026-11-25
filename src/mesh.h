#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "vmath.h"

struct MeshCorner {
    uint32_t vertex = 0;
    int uv = -1, normal = -1;
};
struct PolygonMesh {
    std::vector<V3> positions, uv, normals;
    std::vector<std::vector<MeshCorner>> faces;
};

struct Mesh {
    std::vector<V3> v, n, uv;
    std::vector<uint32_t> tri;
    V3 bmin, bmax;
    struct Node { V3 bmin, bmax; int left = -1, right = -1, first = 0, count = 0; };
    std::vector<Node> nodes;
    std::vector<int> order;
    std::shared_ptr<const PolygonMesh> polygons;
    std::vector<uint32_t> vertexSources, triangleFaces;
    int triCount() const { return (int)(tri.size() / 3); }
    const std::vector<V3>& skinVertices() const { return polygons ? polygons->positions : v; }
    size_t skinVertex(size_t vertex) const { return polygons ? vertexSources[vertex] : vertex; }
    void build();
    bool intersect(V3 o, V3 d, float tmin, float tmax, float& t, V3& nrm, bool flat = false, V3* texture = nullptr) const;

private:
    int buildNode(int first, int count, int depth);
};

constexpr size_t MAX_POLYGON_CORNERS = 2048;
bool buildPolygonMesh(PolygonMesh polygons, Mesh& mesh);
PolygonMesh makePolygonBox();
PolygonMesh polygonsFromTriangles(const Mesh& mesh);
void clearPolygonNormals(PolygonMesh& mesh);
void compactPolygonPositions(PolygonMesh& mesh);
bool extrudePolygonFaces(PolygonMesh& mesh, const std::vector<size_t>& faces, float distance);
bool insetPolygonFace(PolygonMesh& mesh, size_t face, float distance);
bool cutQuadLoop(PolygonMesh& mesh, std::pair<uint32_t, uint32_t> edge, float fraction);
bool subdividePolygons(PolygonMesh& mesh);
bool deletePolygonFaces(PolygonMesh& mesh, const std::vector<size_t>& faces);
bool weldPolygonVertices(PolygonMesh& mesh, const std::vector<uint32_t>& vertices, float distance);
bool fillPolygonBoundary(PolygonMesh& mesh, const std::vector<std::pair<uint32_t, uint32_t>>& edges);
std::vector<std::pair<uint32_t, uint32_t>> expandPolygonBoundary(
    const PolygonMesh& mesh, const std::vector<std::pair<uint32_t, uint32_t>>& seeds);
bool bridgePolygonBoundaries(PolygonMesh& mesh, const std::vector<std::pair<uint32_t, uint32_t>>& edges,
                             int segments, int twist);
struct PolygonCutPoint {
    std::pair<uint32_t, uint32_t> edge;
    float fraction = .5f;
};
bool cutPolygonPath(PolygonMesh& mesh, const std::vector<PolygonCutPoint>& points);
bool bevelPolygonEdges(PolygonMesh& mesh, const std::vector<std::pair<uint32_t, uint32_t>>& edges,
                       float width, int segments);
bool symmetrizePolygons(PolygonMesh& mesh, int axis, float plane, bool positiveSource, float tolerance);
std::vector<uint32_t> matchPolygonSymmetry(const PolygonMesh& mesh, int axis, float plane, float tolerance);
void projectPolygonUV(PolygonMesh& mesh, int axis);

bool loadOBJ(const std::string& path, Mesh& m, std::string& err);
struct MeshLayer { std::string name; Mesh mesh; };
bool loadDXF(const std::string& path, std::vector<MeshLayer>& layers, float scale, bool zUp, bool split, std::string& err);
bool saveGeometry(const std::string& path, const std::vector<MeshLayer>& layers, bool dxf);

Mesh makeEditableBox();
bool subdivideMesh(Mesh& mesh);
bool extrudeMeshFaces(Mesh& mesh, const std::vector<size_t>& faces, float distance);
bool deleteMeshFaces(Mesh& mesh, const std::vector<size_t>& faces);
bool weldMeshVertices(Mesh& mesh, const std::vector<uint32_t>& vertices, float distance, bool preserveUV);
bool fillMeshBoundary(Mesh& mesh, const std::vector<std::pair<uint32_t, uint32_t>>& edges);
void projectMeshUV(Mesh& mesh, int axis);

struct TreeParams {
    float height = 1.f, spread = 38.f, trunkRadius = 0.05f, leafSize = 0.07f, leafDensity = 1.f;
    int levels = 3, branches = 4;
    uint32_t seed = 1;
    float upwardBias = .15f, taper = .7f;
    int foliageShape = 0;
};
void makeTree(const TreeParams& p, Mesh& trunk, Mesh& leaves);
