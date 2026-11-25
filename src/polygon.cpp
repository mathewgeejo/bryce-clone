#include "mesh.h"

#include <array>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace {
using Face = std::vector<MeshCorner>;
using Edge = std::pair<uint32_t, uint32_t>;
using Triangle = std::array<size_t, 3>;

Edge edgeKey(uint32_t a, uint32_t b) { return std::minmax(a, b); }
bool finite(V3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

V3 faceNormal(const PolygonMesh& mesh, const Face& face) {
    V3 origin = mesh.positions[face.front().vertex], normal;
    for (size_t i = 1; i + 1 < face.size(); ++i)
        normal += cross(mesh.positions[face[i].vertex] - origin, mesh.positions[face[i + 1].vertex] - origin);
    return normal;
}

struct Point { double x, y; };
double turn(Point a, Point b, Point c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool segmentsMeet(Point a, Point b, Point c, Point d, double epsilon) {
    if (std::max(a.x, b.x) < std::min(c.x, d.x) || std::max(c.x, d.x) < std::min(a.x, b.x) ||
        std::max(a.y, b.y) < std::min(c.y, d.y) || std::max(c.y, d.y) < std::min(a.y, b.y)) return false;
    auto side = [&](Point p, Point q, Point r) {
        double area = turn(p, q, r);
        return area > epsilon ? 1 : area < -epsilon ? -1 : 0;
    };
    return side(a, b, c) * side(a, b, d) <= 0 && side(c, d, a) * side(c, d, b) <= 0;
}

bool triangulate(const PolygonMesh& mesh, const Face& face, V3 normal, std::vector<Triangle>& triangles) {
    float magnitude = maxc({std::fabs(normal.x), std::fabs(normal.y), std::fabs(normal.z)});
    if (!finite(normal) || magnitude == 0) return false;
    triangles.clear();
    if (face.size() == 3) { triangles.push_back({0, 1, 2}); return true; }
    // Project onto the face's average plane. An axis-aligned projection can collapse
    // an edge of a twisted quad even when the face has a valid triangulation.
    int least = 0;
    for (int i = 1; i < 3; ++i)
        if (std::fabs(normal[i]) < std::fabs(normal[least])) least = i;
    const V3 axes[] = {{1,0,0}, {0,1,0}, {0,0,1}};
    V3 unit = normalize(normal / magnitude);
    V3 x = normalize(cross(axes[least], unit)), y = cross(unit, x);
    V3 origin = mesh.positions[face.front().vertex];
    double scale = 0;
    std::vector<Point> points;
    for (const auto& corner : face) {
        V3 p = mesh.positions[corner.vertex];
        double dx = (double)p.x - origin.x, dy = (double)p.y - origin.y, dz = (double)p.z - origin.z;
        Point projected{dx * x.x + dy * x.y + dz * x.z, dx * y.x + dy * y.y + dz * y.z};
        scale = std::max({scale, std::fabs(projected.x), std::fabs(projected.y)});
        points.push_back(projected);
    }
    double epsilon = scale * scale * 1e-12;
    for (size_t i = 0; i < points.size(); ++i) {
        size_t next = (i + 1) % points.size();
        Point a = points[i], b = points[next];
        if (a.x == b.x && a.y == b.y) return false;
        for (size_t j = i + 2; j < points.size(); ++j) {
            size_t end = (j + 1) % points.size();
            if (end != i && segmentsMeet(a, b, points[j], points[end], epsilon)) return false;
        }
    }
    std::vector<size_t> ring(points.size());
    std::iota(ring.begin(), ring.end(), 0);
    while (ring.size() > 3) {
        bool clipped = false;
        for (size_t i = 0; i < ring.size(); ++i) {
            size_t a = ring[(i + ring.size() - 1) % ring.size()], b = ring[i], c = ring[(i + 1) % ring.size()];
            if (turn(points[a], points[b], points[c]) <= epsilon) continue;
            bool occupied = std::any_of(ring.begin(), ring.end(), [&](size_t p) {
                return p != a && p != b && p != c &&
                       turn(points[a], points[b], points[p]) >= -epsilon &&
                       turn(points[b], points[c], points[p]) >= -epsilon &&
                       turn(points[c], points[a], points[p]) >= -epsilon;
            });
            if (occupied) continue;
            triangles.push_back({a, b, c});
            ring.erase(ring.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) return false;
    }
    if (turn(points[ring[0]], points[ring[1]], points[ring[2]]) <= epsilon) return false;
    triangles.push_back({ring[0], ring[1], ring[2]});
    return true;
}

bool hasIndex(int index, size_t count) {
    return index == -1 || (index >= 0 && (size_t)index < count);
}

struct EdgeUse { size_t face, corner; };
using EdgeUses = std::map<Edge, std::vector<EdgeUse>>;
EdgeUses gatherEdges(const PolygonMesh& mesh) {
    EdgeUses uses;
    for (size_t i = 0; i < mesh.faces.size(); ++i) {
        const auto& face = mesh.faces[i];
        for (size_t j = 0; j < face.size(); ++j)
            uses[edgeKey(face[j].vertex, face[(j + 1) % face.size()].vertex)].push_back({i, j});
    }
    return uses;
}

int addUV(PolygonMesh& mesh, V3 uv) {
    mesh.uv.push_back(uv);
    return (int)mesh.uv.size() - 1;
}

using SplitAttributes = std::map<std::tuple<uint32_t, int, int>, int>;
int splitAttribute(std::vector<V3>& values, SplitAttributes& indices, const MeshCorner& a, const MeshCorner& b,
                   int from, int to, uint32_t vertex, float t) {
    if (from < 0 || to < 0) return -1;
    auto key = a.vertex < b.vertex ? std::make_tuple(vertex, from, to) : std::make_tuple(vertex, to, from);
    auto [entry, added] = indices.emplace(key, (int)values.size());
    if (added) values.push_back(lerp(values[from], values[to], t));
    return entry->second;
}

MeshCorner splitCorner(PolygonMesh& mesh, SplitAttributes& uvs, const MeshCorner& a, const MeshCorner& b,
                       uint32_t vertex, float t) {
    return {vertex, splitAttribute(mesh.uv, uvs, a, b, a.uv, b.uv, vertex, t)};
}

bool convex(const PolygonMesh& mesh, const Face& face, V3 normal) {
    for (size_t i = 0; i < face.size(); ++i) {
        V3 a = mesh.positions[face[i].vertex], b = mesh.positions[face[(i + 1) % face.size()].vertex];
        V3 c = mesh.positions[face[(i + 2) % face.size()].vertex];
        if (!(dot(cross(b - a, c - b), normal) > 0)) return false;
    }
    return true;
}

bool planar(const PolygonMesh& mesh, const Face& face, V3 normal) {
    V3 origin = mesh.positions[face.front().vertex];
    float span = 0, maxDeviation = 0;
    for (const auto& corner : face) {
        V3 delta = mesh.positions[corner.vertex] - origin;
        span = std::max(span, length(delta));
        maxDeviation = std::max(maxDeviation, std::fabs(dot(normal, delta)));
    }
    return span > 0 && maxDeviation <= span * 1e-5f;
}

struct BoundaryPath { std::vector<uint32_t> points; bool closed = false; };
std::vector<BoundaryPath> boundaryPaths(const PolygonMesh& mesh, const EdgeUses& uses,
                                        const std::vector<Edge>& selected) {
    std::map<uint32_t, uint32_t> next;
    std::set<uint32_t> ends;
    for (auto [a, b] : selected) {
        auto entry = uses.find(edgeKey(a, b));
        if (entry == uses.end() || entry->second.size() != 1) return {};
        auto [face, corner] = entry->second.front();
        uint32_t to = mesh.faces[face][corner].vertex;
        uint32_t from = to == a ? b : a;
        // A new polygon follows each boundary edge in the opposite direction.
        if (!next.emplace(from, to).second || !ends.insert(to).second) return {};
    }
    std::vector<uint32_t> starts;
    for (const auto& [from, to] : next) if (!ends.count(from)) starts.push_back(from);
    auto walk = [&](uint32_t start) {
        BoundaryPath path{{start}};
        uint32_t vertex = start;
        for (auto edge = next.find(vertex); edge != next.end(); edge = next.find(vertex)) {
            vertex = edge->second;
            next.erase(edge);
            if (vertex == start) { path.closed = true; break; }
            path.points.push_back(vertex);
        }
        return path;
    };
    std::vector<BoundaryPath> paths;
    for (uint32_t start : starts) paths.push_back(walk(start));
    while (!next.empty()) paths.push_back(walk(next.begin()->first));
    return paths;
}

bool splitFaceAlongChord(const PolygonMesh& mesh, const Face& face, uint32_t a, uint32_t b,
                         std::array<Face, 2>& split) {
    auto at = [&](uint32_t v) {
        return (size_t)(std::find_if(face.begin(), face.end(), [&](const auto& c) { return c.vertex == v; }) - face.begin());
    };
    size_t first = at(a), last = at(b), count = face.size();
    if (first == count || last == count || first == last ||
        (first + 1) % count == last || (last + 1) % count == first) return false;
    V3 sourceNormal = faceNormal(mesh, face);
    std::vector<Triangle> triangles;
    for (Face& part : split) {
        part.clear();
        for (size_t i = first; i != last; i = (i + 1) % count) part.push_back(face[i]);
        part.push_back(face[last]);
        V3 normal = faceNormal(mesh, part);
        if (!(dot(sourceNormal, normal) > 0) || !triangulate(mesh, part, normal, triangles)) return false;
        std::swap(first, last);
    }
    return true;
}
}  // namespace

bool buildPolygonMesh(PolygonMesh polygons, Mesh& mesh) {
    auto validBuffer = [](const std::vector<V3>& buffer) {
        return buffer.size() <= 20000000 && std::all_of(buffer.begin(), buffer.end(), [](V3 p) { return finite(p); });
    };
    if (polygons.faces.empty() || polygons.faces.size() > 20000000 ||
        !validBuffer(polygons.positions) || !validBuffer(polygons.uv) || !validBuffer(polygons.normals)) return false;
    Mesh out;
    std::vector<V3> normals(polygons.positions.size());
    std::map<std::tuple<uint32_t, int, int>, uint32_t> renderVertices;
    std::vector<Triangle> triangles;
    std::vector<uint32_t> indices;
    std::vector<int> normalIndices;
    bool usesUV = false;
    for (size_t i = 0; i < polygons.faces.size(); ++i) {
        const auto& face = polygons.faces[i];
        if (face.size() < 3 || face.size() > MAX_POLYGON_CORNERS ||
            out.triangleFaces.size() + face.size() - 2 > 20000000) return false;
        for (const auto& corner : face) {
            if (corner.vertex >= polygons.positions.size() || !hasIndex(corner.uv, polygons.uv.size()) ||
                !hasIndex(corner.normal, polygons.normals.size())) return false;
        }
        V3 normal = faceNormal(polygons, face);
        if (!triangulate(polygons, face, normal, triangles)) return false;
        indices.clear();
        for (const auto& corner : face) {
            normals[corner.vertex] += normal;
            auto [entry, added] = renderVertices.emplace(std::make_tuple(corner.vertex, corner.uv, corner.normal),
                                                         (uint32_t)out.v.size());
            if (added) {
                out.v.push_back(polygons.positions[corner.vertex]);
                out.vertexSources.push_back(corner.vertex);
                normalIndices.push_back(corner.normal);
                out.uv.push_back(corner.uv >= 0 ? polygons.uv[corner.uv] : V3{});
                usesUV |= corner.uv >= 0;
            }
            indices.push_back(entry->second);
        }
        for (const auto& triangle : triangles) {
            for (size_t corner : triangle) out.tri.push_back(indices[corner]);
            out.triangleFaces.push_back((uint32_t)i);
        }
    }
    if (!usesUV) out.uv.clear();
    out.n.reserve(out.v.size());
    for (size_t i = 0; i < out.v.size(); ++i) {
        V3 n = normalIndices[i] >= 0 ? polygons.normals[normalIndices[i]] : normals[out.vertexSources[i]];
        out.n.push_back(finite(n) && length(n) > 0 ? normalize(n) : V3(0, 1, 0));
    }
    out.polygons = std::make_shared<PolygonMesh>(std::move(polygons));
    out.build();
    mesh = std::move(out);
    return true;
}

PolygonMesh makePolygonBox() {
    PolygonMesh box;
    box.positions = {{-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
                     {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1}};
    box.faces = {{{0},{3},{2},{1}}, {{4},{5},{6},{7}}, {{0},{1},{5},{4}},
                 {{3},{7},{6},{2}}, {{0},{4},{7},{3}}, {{1},{2},{6},{5}}};
    return box;
}

PolygonMesh polygonsFromTriangles(const Mesh& mesh) {
    if (mesh.polygons) return *mesh.polygons;
    PolygonMesh out;
    out.positions = mesh.v;
    if (mesh.uv.size() == mesh.v.size()) out.uv = mesh.uv;
    if (mesh.n.size() == mesh.v.size()) out.normals = mesh.n;
    for (size_t i = 0; i < mesh.tri.size(); i += 3) {
        Face face;
        for (size_t j = 0; j < 3; ++j) {
            auto v = mesh.tri[i + j];
            face.push_back({v, out.uv.empty() ? -1 : (int)v, out.normals.empty() ? -1 : (int)v});
        }
        out.faces.push_back(std::move(face));
    }
    return out;
}

void clearPolygonNormals(PolygonMesh& mesh) {
    mesh.normals.clear();
    for (auto& face : mesh.faces) for (auto& corner : face) corner.normal = -1;
}

void compactPolygonPositions(PolygonMesh& mesh) {
    std::vector<bool> used(mesh.positions.size());
    for (const auto& face : mesh.faces) for (const auto& corner : face) used[corner.vertex] = true;
    std::vector<uint32_t> remap(mesh.positions.size());
    size_t count = 0;
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        if (!used[i]) continue;
        remap[i] = (uint32_t)count;
        mesh.positions[count++] = mesh.positions[i];
    }
    mesh.positions.resize(count);
    for (auto& face : mesh.faces) for (auto& corner : face) corner.vertex = remap[corner.vertex];
}

bool extrudePolygonFaces(PolygonMesh& mesh, const std::vector<size_t>& faces, float distance) {
    if (faces.empty() || !std::isfinite(distance) || std::fabs(distance) < 1e-6f) return false;
    std::vector<bool> selected(mesh.faces.size());
    for (size_t face : faces) {
        if (face >= mesh.faces.size() || selected[face]) return false;
        selected[face] = true;
    }
    auto edges = gatherEdges(mesh);
    std::vector<std::vector<size_t>> neighbors(mesh.faces.size());
    std::vector<std::vector<size_t>> boundary(mesh.faces.size());
    for (const auto& [edge, uses] : edges) {
        for (const auto& use : uses) {
            if (!selected[use.face]) continue;
            if (uses.size() > 2) return false;
            auto other = uses.size() == 2 ? uses[0].face == use.face ? uses[1] : uses[0] : use;
            if (uses.size() == 2 && selected[other.face]) {
                if (mesh.faces[use.face][use.corner].vertex == mesh.faces[other.face][other.corner].vertex) return false;
                neighbors[use.face].push_back(other.face);
            } else boundary[use.face].push_back(use.corner);
        }
    }
    PolygonMesh edited = mesh;
    std::vector<bool> visited(mesh.faces.size());
    for (size_t start : faces) {
        if (visited[start]) continue;
        std::vector<size_t> component{start};
        visited[start] = true;
        V3 normal;
        size_t border = 0;
        for (size_t i = 0; i < component.size(); ++i) {
            size_t face = component[i];
            normal += faceNormal(mesh, mesh.faces[face]);
            border += boundary[face].size();
            for (size_t neighbor : neighbors[face]) {
                if (visited[neighbor]) continue;
                visited[neighbor] = true;
                component.push_back(neighbor);
            }
        }
        if (!border || length(normal) == 0) return false;
        V3 offset = normalize(normal) * distance;
        std::map<uint32_t, uint32_t> newVertices;
        for (size_t index : component) {
            const auto& face = mesh.faces[index];
            auto& cap = edited.faces[index];
            for (auto& corner : cap) {
                auto [entry, added] = newVertices.emplace(corner.vertex, (uint32_t)edited.positions.size());
                if (added) edited.positions.push_back(mesh.positions[corner.vertex] + offset);
                corner.vertex = entry->second;
            }
            for (size_t i : boundary[index]) {
                size_t j = (i + 1) % face.size();
                Face side{face[i], face[j], edited.faces[index][j], edited.faces[index][i]};
                if (face[i].uv >= 0 && face[j].uv >= 0) {
                    constexpr V3 sideUVs[] = {{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}};
                    for (size_t k = 0; k < side.size(); ++k) side[k].uv = addUV(edited, sideUVs[k]);
                }
                edited.faces.push_back(std::move(side));
            }
        }
    }
    compactPolygonPositions(edited);
    clearPolygonNormals(edited);
    mesh = std::move(edited);
    return true;
}

bool insetPolygonFace(PolygonMesh& mesh, size_t index, float distance) {
    if (index >= mesh.faces.size() || !(distance > 0) || !std::isfinite(distance)) return false;
    Face face = mesh.faces[index], cap = face;
    V3 normal = normalize(faceNormal(mesh, face));
    if (!planar(mesh, face, normal) || !convex(mesh, face, normal)) return false;
    std::vector<V3> inside, inset;
    for (size_t i = 0; i < face.size(); ++i) {
        V3 a = mesh.positions[face[i].vertex], b = mesh.positions[face[(i + 1) % face.size()].vertex];
        inside.push_back(normalize(cross(normal, b - a)));
    }
    for (size_t i = 0; i < face.size(); ++i) {
        V3 a = inside[(i + face.size() - 1) % face.size()], b = inside[i];
        float divisor = 1 + dot(a, b);
        if (divisor < 1e-5f) return false;
        inset.push_back(mesh.positions[face[i].vertex] + (a + b) * (distance / divisor));
    }
    for (size_t i = 0; i < face.size(); ++i) {
        size_t j = (i + 1) % face.size();
        if (!(dot(inset[j] - inset[i], mesh.positions[face[j].vertex] - mesh.positions[face[i].vertex]) > 0))
            return false;
    }
    std::vector<Triangle> triangles;
    if (!triangulate(mesh, face, normal, triangles)) return false;
    for (size_t i = 0; i < cap.size(); ++i) {
        cap[i] = {(uint32_t)mesh.positions.size()};
        mesh.positions.push_back(inset[i]);
        for (const auto& t : triangles) {
            V3 a = mesh.positions[face[t[0]].vertex], b = mesh.positions[face[t[1]].vertex];
            V3 c = mesh.positions[face[t[2]].vertex], p = inset[i];
            float area = dot(normal, cross(b - a, c - a));
            float u = dot(normal, cross(p - a, c - a)) / area;
            float v = dot(normal, cross(b - a, p - a)) / area;
            if (u < -1e-5f || v < -1e-5f || u + v > 1.00001f) continue;
            if (face[t[0]].uv >= 0 && face[t[1]].uv >= 0 && face[t[2]].uv >= 0)
                cap[i].uv = addUV(mesh, mesh.uv[face[t[0]].uv] * (1 - u - v) +
                                         mesh.uv[face[t[1]].uv] * u + mesh.uv[face[t[2]].uv] * v);
            break;
        }
    }
    mesh.faces[index] = cap;
    for (size_t i = 0; i < face.size(); ++i) {
        size_t j = (i + 1) % face.size();
        mesh.faces.push_back({face[i], face[j], cap[j], cap[i]});
    }
    clearPolygonNormals(mesh);
    return true;
}

bool cutQuadLoop(PolygonMesh& mesh, Edge edge, float fraction) {
    if (!(fraction > .001f && fraction < .999f)) return false;
    auto edges = gatherEdges(mesh);
    if (!edges.count(edgeKey(edge.first, edge.second))) return false;
    struct Cut { float fraction; uint32_t vertex = 0; };
    std::map<Edge, Cut> cuts;
    std::vector<Edge> queue;
    auto addCut = [&](uint32_t a, uint32_t b, float t) {
        Edge key = edgeKey(a, b);
        float canonical = a == key.first ? t : 1 - t;
        auto [entry, added] = cuts.emplace(key, Cut{canonical});
        if (added) queue.push_back(key);
        return std::fabs(entry->second.fraction - canonical) < 1e-5f;
    };
    addCut(edge.first, edge.second, fraction);
    std::map<size_t, size_t> crossed;
    std::map<size_t, std::vector<size_t>> terminations;
    for (size_t i = 0; i < queue.size(); ++i) {
        Edge key = queue[i];
        const auto& uses = edges.at(key);
        if (uses.size() > 2) return false;
        for (auto [faceIndex, cornerIndex] : uses) {
            const auto& face = mesh.faces[faceIndex];
            if (face.size() != 4) { terminations[faceIndex].push_back(cornerIndex); continue; }
            auto [entry, added] = crossed.emplace(faceIndex, cornerIndex);
            if (!added && entry->second % 2 != cornerIndex % 2) return false;
            float t = cuts.at(key).fraction;
            if (face[cornerIndex].vertex != key.first) t = 1 - t;
            if (!addCut(face[(cornerIndex + 3) % 4].vertex, face[(cornerIndex + 2) % 4].vertex, t)) return false;
        }
    }
    if (crossed.empty()) return false;
    for (auto& [key, cut] : cuts) {
        cut.vertex = (uint32_t)mesh.positions.size();
        mesh.positions.push_back(lerp(mesh.positions[key.first], mesh.positions[key.second], cut.fraction));
    }
    SplitAttributes uvs;
    auto midpoint = [&](const MeshCorner& a, const MeshCorner& b) {
        Edge key = edgeKey(a.vertex, b.vertex);
        const auto& cut = cuts.at(key);
        return splitCorner(mesh, uvs, a, b, cut.vertex, a.vertex == key.first ? cut.fraction : 1 - cut.fraction);
    };
    for (auto [index, i] : crossed) {
        const auto& face = mesh.faces[index];
        auto a = face[i], b = face[(i + 1) % 4], c = face[(i + 2) % 4], d = face[(i + 3) % 4];
        auto ab = midpoint(a, b), cd = midpoint(c, d);
        mesh.faces[index] = {a, ab, cd, d};
        mesh.faces.push_back({ab, b, c, cd});
    }
    for (auto& [index, corners] : terminations) {
        const auto& face = mesh.faces[index];
        std::sort(corners.begin(), corners.end());
        Face updated;
        for (size_t i = 0; i < face.size(); ++i) {
            updated.push_back(face[i]);
            if (std::binary_search(corners.begin(), corners.end(), i))
                updated.push_back(midpoint(face[i], face[(i + 1) % face.size()]));
        }
        mesh.faces[index] = std::move(updated);
    }
    clearPolygonNormals(mesh);
    return true;
}

bool subdividePolygons(PolygonMesh& mesh) {
    for (const auto& face : mesh.faces)
        if (!convex(mesh, face, faceNormal(mesh, face))) return false;
    std::map<Edge, uint32_t> midpoints;
    SplitAttributes uvs;
    std::vector<Face> faces;
    for (const auto& face : mesh.faces) {
        V3 center, uv;
        bool allUV = true;
        std::vector<MeshCorner> middle;
        for (size_t i = 0; i < face.size(); ++i) {
            const auto& a = face[i];
            const auto& b = face[(i + 1) % face.size()];
            center += mesh.positions[a.vertex];
            if (a.uv >= 0) uv += mesh.uv[a.uv];
            else allUV = false;
            auto [it, added] = midpoints.emplace(edgeKey(a.vertex, b.vertex), (uint32_t)mesh.positions.size());
            if (added) mesh.positions.push_back(lerp(mesh.positions[a.vertex], mesh.positions[b.vertex], .5f));
            middle.push_back(splitCorner(mesh, uvs, a, b, it->second, .5f));
        }
        MeshCorner c{(uint32_t)mesh.positions.size()};
        mesh.positions.push_back(center / (float)face.size());
        if (allUV) c.uv = addUV(mesh, uv / (float)face.size());
        for (size_t i = 0; i < face.size(); ++i)
            faces.push_back({face[i], middle[i], c, middle[(i + face.size() - 1) % face.size()]});
    }
    mesh.faces = std::move(faces);
    clearPolygonNormals(mesh);
    return true;
}

bool deletePolygonFaces(PolygonMesh& mesh, const std::vector<size_t>& faces) {
    if (faces.empty() || faces.size() >= mesh.faces.size()) return false;
    std::vector<bool> deleted(mesh.faces.size());
    for (size_t i : faces) {
        if (i >= deleted.size() || deleted[i]) return false;
        deleted[i] = true;
    }
    size_t kept = 0;
    for (size_t i = 0; i < deleted.size(); ++i) {
        if (deleted[i]) continue;
        if (i != kept) mesh.faces[kept] = std::move(mesh.faces[i]);
        ++kept;
    }
    mesh.faces.resize(kept);
    compactPolygonPositions(mesh);
    return true;
}

bool weldPolygonVertices(PolygonMesh& mesh, const std::vector<uint32_t>& vertices, float distance) {
    if (vertices.size() < 2 || !(distance > 0) || !std::isfinite(distance)) return false;
    std::vector<uint32_t> order = vertices, remap(mesh.positions.size());
    std::sort(order.begin(), order.end());
    if (order.back() >= remap.size() || std::adjacent_find(order.begin(), order.end()) != order.end()) return false;
    std::iota(remap.begin(), remap.end(), 0);
    using Cell = std::array<double, 3>;
    std::map<Cell, std::vector<uint32_t>> cells;
    bool joined = false;
    for (uint32_t v : order) {
        V3 p = mesh.positions[v];
        Cell cell{std::floor((double)p.x / distance), std::floor((double)p.y / distance),
                  std::floor((double)p.z / distance)};
        for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y) for (int z = -1; z <= 1; ++z) {
            auto near = cells.find({cell[0] + x, cell[1] + y, cell[2] + z});
            if (near == cells.end()) continue;
            for (uint32_t other : near->second)
                if (other < remap[v] && length(p - mesh.positions[other]) <= distance) remap[v] = other;
        }
        if (remap[v] == v) cells[cell].push_back(v);
        else joined = true;
    }
    if (!joined) return false;
    std::vector<Face> faces;
    for (auto face : mesh.faces) {
        for (auto& corner : face) corner.vertex = remap[corner.vertex];
        face.erase(std::unique(face.begin(), face.end(), [](const MeshCorner& a, const MeshCorner& b) {
            return a.vertex == b.vertex;
        }), face.end());
        if (face.size() > 1 && face.front().vertex == face.back().vertex) face.pop_back();
        if (face.size() >= 3) faces.push_back(std::move(face));
    }
    if (faces.empty()) return false;
    mesh.faces = std::move(faces);
    compactPolygonPositions(mesh);
    clearPolygonNormals(mesh);
    return true;
}

bool fillPolygonBoundary(PolygonMesh& mesh, const std::vector<Edge>& edges) {
    if (edges.size() < 3 || edges.size() > MAX_POLYGON_CORNERS) return false;
    auto paths = boundaryPaths(mesh, gatherEdges(mesh), edges);
    if (paths.size() != 1 || !paths.front().closed) return false;
    Face face;
    for (uint32_t vertex : paths.front().points) face.push_back({vertex});
    V3 normal = faceNormal(mesh, face);
    std::vector<Triangle> triangles;
    if (!planar(mesh, face, normalize(normal)) || !triangulate(mesh, face, normal, triangles)) return false;
    mesh.faces.push_back(std::move(face));
    return true;
}

std::vector<Edge> expandPolygonBoundary(const PolygonMesh& mesh, const std::vector<Edge>& seeds) {
    const auto edges = gatherEdges(mesh);
    std::map<uint32_t, std::vector<Edge>> atVertex;
    std::vector<Edge> todo;
    std::set<Edge> visited;
    for (const auto& [edge, uses] : edges) {
        if (uses.size() != 1) continue;
        atVertex[edge.first].push_back(edge);
        atVertex[edge.second].push_back(edge);
    }
    auto visit = [&](Edge edge) { if (visited.insert(edge).second) todo.push_back(edge); };
    for (auto [a, b] : seeds) {
        Edge edge = edgeKey(a, b);
        auto entry = edges.find(edge);
        if (entry != edges.end() && entry->second.size() == 1) visit(edge);
    }
    for (size_t i = 0; i < todo.size(); ++i) {
        auto [a, b] = todo[i];
        for (uint32_t vertex : {a, b})
            if (atVertex.at(vertex).size() == 2)
                for (Edge edge : atVertex.at(vertex)) visit(edge);
    }
    return todo;
}

bool bridgePolygonBoundaries(PolygonMesh& mesh, const std::vector<Edge>& selected, int segments, int twist) {
    if (segments < 1 || segments > 32 || selected.size() > MAX_POLYGON_CORNERS * 2) return false;
    auto edges = gatherEdges(mesh);
    auto paths = boundaryPaths(mesh, edges, selected);
    if (paths.size() != 2 || paths[0].closed != paths[1].closed) return false;
    auto& a = paths[0].points;
    auto& b = paths[1].points;
    if (a.size() != b.size() || a.size() > MAX_POLYGON_CORNERS) return false;
    bool closed = paths[0].closed;
    size_t columns = a.size(), spans = closed ? columns : columns - 1;
    std::reverse(b.begin(), b.end());
    if (closed) {
        size_t bestOffset = 0;
        double bestCost = std::numeric_limits<double>::infinity();
        for (size_t offset = 0; offset < columns; ++offset) {
            double cost = 0;
            for (size_t i = 0; i < columns; ++i) {
                V3 delta = mesh.positions[a[i]] - mesh.positions[b[(i + offset) % columns]];
                cost += dot(delta, delta);
            }
            if (cost < bestCost) { bestOffset = offset; bestCost = cost; }
        }
        int count = (int)columns;
        int offset = ((int)bestOffset + count + twist % count) % count;
        std::rotate(b.begin(), b.begin() + offset, b.end());
    }
    for (size_t i = 0; i < columns; ++i) {
        auto at = edges.find(edgeKey(a[i], b[i]));
        if (at == edges.end()) continue;
        bool end = !closed && (i == 0 || i == columns - 1);
        if (!end || segments > 1 || at->second.size() != 1) return false;
        auto [face, corner] = at->second.front();
        uint32_t from = mesh.faces[face][corner].vertex;
        if (from != (i == 0 ? a[i] : b[i])) return false;
        if (spans == 1 && edges.at(edgeKey(a[0], a[1])).front().face == face &&
            edges.at(edgeKey(b[0], b[1])).front().face == face &&
            edges.count(edgeKey(a[1 - i], b[1 - i]))) return false;
    }
    PolygonMesh edited = mesh;
    std::vector<std::vector<uint32_t>> rows((size_t)segments + 1);
    rows.front() = a;
    rows.back() = b;
    for (int row = 1; row < segments; ++row) {
        for (size_t i = 0; i < columns; ++i) {
            rows[row].push_back((uint32_t)edited.positions.size());
            edited.positions.push_back(lerp(mesh.positions[a[i]], mesh.positions[b[i]], (float)row / segments));
        }
    }
    std::vector<float> distance(spans + 1);
    for (size_t i = 0; i < spans; ++i) {
        size_t j = (i + 1) % columns;
        distance[i + 1] = distance[i] + length(mesh.positions[a[i]] - mesh.positions[a[j]]) +
                          length(mesh.positions[b[i]] - mesh.positions[b[j]]);
    }
    if (!(distance.back() > 0)) return false;
    std::vector<std::vector<int>> uv(rows.size());
    for (size_t r = 0; r < rows.size(); ++r)
        for (float d : distance) uv[r].push_back(addUV(edited, {d / distance.back(), (float)r / segments, 0}));
    std::vector<Triangle> triangles;
    for (int r = 0; r < segments; ++r) {
        for (size_t i = 0; i < spans; ++i) {
            size_t j = (i + 1) % columns;
            Face face{{rows[r][i], uv[r][i]}, {rows[r][j], uv[r][i + 1]},
                      {rows[r + 1][j], uv[r + 1][i + 1]}, {rows[r + 1][i], uv[r + 1][i]}};
            if (!triangulate(edited, face, faceNormal(edited, face), triangles)) return false;
            edited.faces.push_back(std::move(face));
        }
    }
    mesh = std::move(edited);
    return true;
}

bool cutPolygonPath(PolygonMesh& mesh, const std::vector<PolygonCutPoint>& points) {
    if (points.size() < 2 || points.size() > MAX_POLYGON_CORNERS) return false;
    const auto edges = gatherEdges(mesh);
    struct AlongEdge { float fraction; uint32_t vertex; };
    struct Anchor { uint32_t vertex; std::vector<size_t> faces; };
    std::map<Edge, std::vector<AlongEdge>> splits;
    std::map<uint32_t, std::vector<size_t>> facesAtVertex;
    std::vector<Anchor> anchors;
    for (const auto& point : points) {
        auto [a, b] = point.edge;
        float t = point.fraction;
        if (a >= mesh.positions.size() || b >= mesh.positions.size() || !(t >= 0 && t <= 1)) return false;
        if (a == b || t == 0 || t == 1) facesAtVertex.try_emplace(t == 1 ? b : a);
    }
    if (!facesAtVertex.empty()) {
        for (size_t i = 0; i < mesh.faces.size(); ++i) {
            for (const auto& corner : mesh.faces[i]) {
                auto at = facesAtVertex.find(corner.vertex);
                if (at != facesAtVertex.end()) at->second.push_back(i);
            }
        }
    }
    PolygonMesh edited = mesh;
    for (const auto& point : points) {
        auto [a, b] = point.edge;
        float fraction = point.fraction;
        if (a == b || fraction == 0 || fraction == 1) {
            uint32_t vertex = fraction == 1 ? b : a;
            anchors.push_back({vertex, facesAtVertex.at(vertex)});
            continue;
        }
        Edge edge = edgeKey(a, b);
        auto at = edges.find(edge);
        if (at == edges.end() || at->second.size() > 2) return false;
        if (a != edge.first) fraction = 1 - fraction;
        auto& alongEdge = splits[edge];
        auto entry = std::find_if(alongEdge.begin(), alongEdge.end(), [&](const AlongEdge& cut) {
            return std::fabs(cut.fraction - fraction) < 1e-5f;
        });
        uint32_t vertex = entry != alongEdge.end() ? entry->vertex : (uint32_t)edited.positions.size();
        if (entry == alongEdge.end()) {
            alongEdge.push_back({fraction, vertex});
            edited.positions.push_back(lerp(mesh.positions[edge.first], mesh.positions[edge.second], fraction));
        }
        Anchor anchor{vertex, {}};
        for (const auto& use : at->second) anchor.faces.push_back(use.face);
        anchors.push_back(std::move(anchor));
    }

    std::set<size_t> expanded;
    for (auto& [edge, alongEdge] : splits) {
        std::sort(alongEdge.begin(), alongEdge.end(), [](AlongEdge a, AlongEdge b) { return a.fraction < b.fraction; });
        for (const auto& use : edges.at(edge)) expanded.insert(use.face);
    }
    SplitAttributes uvs, normals;
    std::vector<Triangle> triangles;
    for (size_t index : expanded) {
        const Face& face = mesh.faces[index];
        Face stepped;
        for (size_t i = 0; i < face.size(); ++i) {
            const auto& a = face[i];
            const auto& b = face[(i + 1) % face.size()];
            stepped.push_back(a);
            Edge edge = edgeKey(a.vertex, b.vertex);
            auto at = splits.find(edge);
            if (at == splits.end()) continue;
            const auto& alongEdge = at->second;
            bool forward = a.vertex == edge.first;
            for (size_t j = 0; j < alongEdge.size(); ++j) {
                auto [fraction, vertex] = alongEdge[forward ? j : alongEdge.size() - 1 - j];
                float t = forward ? fraction : 1 - fraction;
                MeshCorner corner = splitCorner(edited, uvs, a, b, vertex, t);
                corner.normal = splitAttribute(edited.normals, normals, a, b, a.normal, b.normal, vertex, t);
                stepped.push_back(corner);
            }
        }
        edited.faces[index] = std::move(stepped);
    }
    std::map<size_t, std::vector<size_t>> extraParts;
    for (size_t i = 1; i < anchors.size(); ++i) {
        const auto& a = anchors[i - 1];
        const auto& b = anchors[i];
        std::vector<size_t> common;
        std::set_intersection(a.faces.begin(), a.faces.end(), b.faces.begin(), b.faces.end(), std::back_inserter(common));
        size_t original = SIZE_MAX, match = SIZE_MAX;
        std::array<Face, 2> split, candidate;
        for (size_t index : common) {
            const auto& parts = extraParts[index];
            for (size_t j = 0; j <= parts.size(); ++j) {
                size_t part = j == parts.size() ? index : parts[j];
                if (!splitFaceAlongChord(edited, edited.faces[part], a.vertex, b.vertex, candidate)) continue;
                if (match != SIZE_MAX) return false;
                original = index;
                match = part;
                split = std::move(candidate);
            }
        }
        if (match == SIZE_MAX) return false;
        edited.faces[match] = std::move(split[0]);
        extraParts[original].push_back(edited.faces.size());
        edited.faces.push_back(std::move(split[1]));
    }
    for (size_t index : expanded) {
        const auto& face = edited.faces[index];
        if (face.size() > MAX_POLYGON_CORNERS || !triangulate(edited, face, faceNormal(edited, face), triangles)) return false;
    }
    for (const auto& [source, parts] : extraParts)
        for (size_t part : parts) if (edited.faces[part].size() > MAX_POLYGON_CORNERS) return false;
    mesh = std::move(edited);
    return true;
}

void projectPolygonUV(PolygonMesh& mesh, int axis) {
    if (mesh.positions.empty() || axis < 0 || axis > 2) return;
    int x = (axis + 1) % 3, y = (axis + 2) % 3;
    float minX = mesh.positions.front()[x], maxX = minX, minY = mesh.positions.front()[y], maxY = minY;
    for (V3 p : mesh.positions) {
        minX = std::min(minX, p[x]); maxX = std::max(maxX, p[x]);
        minY = std::min(minY, p[y]); maxY = std::max(maxY, p[y]);
    }
    mesh.uv.clear();
    for (V3 p : mesh.positions)
        mesh.uv.push_back({(p[x] - minX) / std::max(1e-6f, maxX - minX),
                           (p[y] - minY) / std::max(1e-6f, maxY - minY), 0});
    for (auto& face : mesh.faces) for (auto& corner : face) corner.uv = (int)corner.vertex;
}
