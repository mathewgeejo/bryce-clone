#define _CRT_SECURE_NO_WARNINGS
#include "mesh.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <fstream>
#include <locale>
#include <map>
#include <sstream>
#include <utility>

static void growBox(V3& lo, V3& hi, V3 p) {
    lo = V3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
    hi = V3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
}

void Mesh::build() {
    if (n.size() != v.size()) {
        n.assign(v.size(), V3(0.f));
        for (size_t i = 0; i + 2 < tri.size(); i += 3) {
            V3 a = v[tri[i]], b = v[tri[i + 1]], c = v[tri[i + 2]], fn = cross(b - a, c - a);
            for (int k = 0; k < 3; k++) n[tri[i + k]] += fn;
        }
        for (V3& x : n) x = length(x) > 0 ? normalize(x) : V3(0, 1, 0);
    }
    bmin = V3(1e30f);
    bmax = V3(-1e30f);
    for (V3 p : v) growBox(bmin, bmax, p);
    if (v.empty()) bmin = bmax = V3(0.f);
    order.resize(triCount());
    for (int i = 0; i < triCount(); i++) order[i] = i;
    nodes.clear();
    nodes.reserve(triCount() * 2 / 3 + 1);
    if (triCount() > 0) buildNode(0, triCount(), 0);
}

Mesh makeEditableBox() {
    Mesh mesh;
    mesh.v = {{-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
              {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1}};
    mesh.tri = {0,2,1, 0,3,2, 4,5,6, 4,6,7, 0,1,5, 0,5,4,
                3,7,6, 3,6,2, 0,4,7, 0,7,3, 1,2,6, 1,6,5};
    mesh.build();
    return mesh;
}

namespace {
using MeshEdge = std::pair<uint32_t, uint32_t>;
MeshEdge edgeKey(uint32_t a, uint32_t b) { return std::minmax(a, b); }

void compactMesh(Mesh& mesh) {
    std::vector<uint32_t> remap(mesh.v.size(), UINT32_MAX);
    std::vector<V3> vertices, uv;
    for (auto& index : mesh.tri) {
        auto& mapped = remap[index];
        if (mapped == UINT32_MAX) {
            mapped = (uint32_t)vertices.size();
            vertices.push_back(mesh.v[index]);
            if (!mesh.uv.empty()) uv.push_back(mesh.uv[index]);
        }
        index = mapped;
    }
    mesh.v = std::move(vertices);
    mesh.uv = std::move(uv);
}

void rebuildEditedMesh(Mesh& mesh) {
    mesh.n.clear();
    mesh.build();
}
}  // namespace

bool subdivideMesh(Mesh& mesh) {
    if (mesh.tri.empty() || mesh.triCount() > 500000 || mesh.v.size() > 2000000) return false;
    std::map<MeshEdge, uint32_t> middles;
    auto middle = [&](uint32_t a, uint32_t b) {
        auto [it, inserted] = middles.emplace(edgeKey(a, b), (uint32_t)mesh.v.size());
        if (inserted) {
            mesh.v.push_back((mesh.v[a] + mesh.v[b]) * .5f);
            if (!mesh.uv.empty()) mesh.uv.push_back((mesh.uv[a] + mesh.uv[b]) * .5f);
        }
        return it->second;
    };
    std::vector<uint32_t> triangles;
    triangles.reserve(mesh.tri.size() * 4);
    for (size_t i = 0; i < mesh.tri.size(); i += 3) {
        uint32_t a = mesh.tri[i], b = mesh.tri[i + 1], c = mesh.tri[i + 2];
        uint32_t ab = middle(a, b), bc = middle(b, c), ca = middle(c, a);
        triangles.insert(triangles.end(), {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca});
    }
    mesh.tri = std::move(triangles);
    rebuildEditedMesh(mesh);
    return true;
}

bool extrudeMeshFaces(Mesh& mesh, const std::vector<size_t>& faces, float distance) {
    if (faces.empty() || !std::isfinite(distance) || std::fabs(distance) < 1e-6f) return false;
    size_t count = (size_t)mesh.triCount();
    if (count > 2000000 || faces.size() > (2000000 - count) / 6 || mesh.v.size() > 2000000) return false;
    struct EdgeUse { uint32_t a, b; int selected = 0, total = 0; };
    std::map<MeshEdge, EdgeUse> edges;
    std::vector<bool> selected(count);
    V3 normal;
    for (size_t face : faces) {
        if (face >= count || selected[face]) return false;
        selected[face] = true;
        const uint32_t* v = &mesh.tri[3 * face];
        normal += cross(mesh.v[v[1]] - mesh.v[v[0]], mesh.v[v[2]] - mesh.v[v[0]]);
        for (int i = 0; i < 3; ++i) {
            uint32_t a = v[i], b = v[(i + 1) % 3];
            auto [entry, added] = edges.emplace(edgeKey(a, b), EdgeUse{a, b});
            auto& use = entry->second;
            if (!added && (use.selected != 1 || use.a == a)) return false;
            ++use.selected;
        }
    }
    if (length(normal) < 1e-8f) return false;
    for (size_t i = 0; i < mesh.tri.size(); i += 3) {
        for (int j = 0; j < 3; ++j) {
            auto it = edges.find(edgeKey(mesh.tri[i + j], mesh.tri[i + (j + 1) % 3]));
            if (it != edges.end() && ++it->second.total > 2) return false;
        }
    }
    V3 offset = normalize(normal) * distance;
    std::map<uint32_t, uint32_t> cap;
    for (size_t face : faces) {
        for (int i = 0; i < 3; ++i) {
            auto& index = mesh.tri[face * 3 + i];
            auto [entry, added] = cap.emplace(index, (uint32_t)mesh.v.size());
            if (added) {
                mesh.v.push_back(mesh.v[index] + offset);
                if (!mesh.uv.empty()) {
                    V3 texture = mesh.uv[index];
                    mesh.uv.push_back(texture);
                }
            }
            index = entry->second;
        }
    }
    for (const auto& entry : edges) {
        const auto& edge = entry.second;
        if (edge.selected != 1) continue;
        uint32_t a = cap.at(edge.a), b = cap.at(edge.b);
        mesh.tri.insert(mesh.tri.end(), {edge.a, edge.b, b, edge.a, b, a});
    }
    // Keep cap face indices stable for repeated extrusion in the editor.
    compactMesh(mesh);
    rebuildEditedMesh(mesh);
    return true;
}

bool deleteMeshFaces(Mesh& mesh, const std::vector<size_t>& faces) {
    if (faces.empty()) return false;
    size_t count = (size_t)mesh.triCount();
    std::vector<bool> removed(count);
    for (size_t face : faces) {
        if (face >= count || removed[face]) return false;
        removed[face] = true;
    }
    if (faces.size() == count) return false;
    std::vector<uint32_t> triangles;
    triangles.reserve((count - faces.size()) * 3);
    for (size_t i = 0; i < count; ++i)
        if (!removed[i]) triangles.insert(triangles.end(), mesh.tri.begin() + i * 3, mesh.tri.begin() + i * 3 + 3);
    mesh.tri = std::move(triangles);
    compactMesh(mesh);
    rebuildEditedMesh(mesh);
    return true;
}

void projectMeshUV(Mesh& mesh, int axis) {
    if (axis < 0 || axis > 2) return;
    int u = (axis + 1) % 3, v = (axis + 2) % 3;
    V3 extent = mesh.bmax - mesh.bmin;
    float width = std::max(extent[u], 1e-8f), height = std::max(extent[v], 1e-8f);
    mesh.uv.resize(mesh.v.size());
    for (size_t i = 0; i < mesh.v.size(); ++i) {
        V3 local = mesh.v[i] - mesh.bmin;
        mesh.uv[i] = {local[u] / width, local[v] / height, 0};
    }
}

int Mesh::buildNode(int first, int count, int depth) {
    int idx = (int)nodes.size();
    nodes.push_back(Node());
    V3 lo(1e30f), hi(-1e30f), clo(1e30f), chi(-1e30f);
    auto centroid = [&](int t) { return (v[tri[t * 3]] + v[tri[t * 3 + 1]] + v[tri[t * 3 + 2]]) / 3.f; };
    for (int i = first; i < first + count; i++) {
        int t = order[i];
        for (int k = 0; k < 3; k++) growBox(lo, hi, v[tri[t * 3 + k]]);
        growBox(clo, chi, centroid(t));
    }
    nodes[idx].bmin = lo;
    nodes[idx].bmax = hi;
    if (count <= 4 || depth > 40) {
        nodes[idx].first = first;
        nodes[idx].count = count;
        return idx;
    }
    V3 ext = chi - clo;
    int axis = ext.x > ext.y && ext.x > ext.z ? 0 : (ext.y > ext.z ? 1 : 2);
    int half = count / 2;
    std::nth_element(order.begin() + first, order.begin() + first + half, order.begin() + first + count,
                     [&](int a, int b) { return centroid(a)[axis] < centroid(b)[axis]; });
    int l = buildNode(first, half, depth + 1);
    int r = buildNode(first + half, count - half, depth + 1);
    nodes[idx].left = l;
    nodes[idx].right = r;
    return idx;
}

static bool boxHit(V3 lo, V3 hi, V3 o, V3 inv, float tmin, float tmax) {
    for (int i = 0; i < 3; i++) {
        float t0 = (lo[i] - o[i]) * inv[i], t1 = (hi[i] - o[i]) * inv[i];
        if (t0 > t1) std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        if (tmin > tmax) return false;
    }
    return true;
}

bool Mesh::intersect(V3 o, V3 d, float tmin, float tmax, float& tOut, V3& nOut, bool flat, V3* texture) const {
    if (nodes.empty()) return false;
    V3 inv(1.f / (d.x != 0 ? d.x : 1e-30f), 1.f / (d.y != 0 ? d.y : 1e-30f), 1.f / (d.z != 0 ? d.z : 1e-30f));
    int stack[96], sp = 0;
    stack[sp++] = 0;
    bool hit = false;
    while (sp) {
        const Node& nd = nodes[stack[--sp]];
        if (!boxHit(nd.bmin, nd.bmax, o, inv, tmin, tmax)) continue;
        if (nd.left < 0) {
            for (int i = nd.first; i < nd.first + nd.count; i++) {
                int t = order[i];
                uint32_t ia = tri[t * 3], ib = tri[t * 3 + 1], ic = tri[t * 3 + 2];
                V3 a = v[ia], e1 = v[ib] - a, e2 = v[ic] - a, p = cross(d, e2);
                float det = dot(e1, p);
                if (det == 0) continue;
                float id = 1 / det;
                V3 s = o - a;
                float u = dot(s, p) * id;
                if (u < 0 || u > 1) continue;
                V3 q = cross(s, e1);
                float w = dot(d, q) * id;
                if (w < 0 || u + w > 1) continue;
                float tt = dot(e2, q) * id;
                if (tt <= tmin || tt >= tmax) continue;
                tmax = tt;
                tOut = tt;
                nOut = flat ? cross(e1, e2) : n[ia] * (1 - u - w) + n[ib] * u + n[ic] * w;
                if (texture && !uv.empty()) *texture = uv[ia] * (1 - u - w) + uv[ib] * u + uv[ic] * w;
                hit = true;
            }
        } else if (sp + 2 <= 96) {
            const auto& a = nodes[nd.left];
            const auto& b = nodes[nd.right];
            bool leftFirst = dot(a.bmin + a.bmax - b.bmin - b.bmax, d) < 0;
            stack[sp++] = leftFirst ? nd.right : nd.left;
            stack[sp++] = leftFirst ? nd.left : nd.right;
        }
    }
    return hit;
}

bool loadOBJ(const std::string& path, Mesh& m, std::string& err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open file"; return false; }
    std::vector<V3> pos, nrm, uv;
    std::map<std::array<int, 3>, uint32_t> remap;
    Mesh out;
    bool allNormals = true, allUVs = true;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'v' && line[1] == ' ') {
            V3 p;
            if (sscanf(line + 2, "%f %f %f", &p.x, &p.y, &p.z) == 3) pos.push_back(p);
        } else if (line[0] == 'v' && line[1] == 'n') {
            V3 p;
            if (sscanf(line + 3, "%f %f %f", &p.x, &p.y, &p.z) == 3) nrm.push_back(normalize(p));
        } else if (line[0] == 'v' && line[1] == 't') {
            V3 p;
            if (sscanf(line + 3, "%f %f", &p.x, &p.y) == 2) uv.push_back(p);
        } else if (line[0] == 'f' && line[1] == ' ') {
            std::vector<uint32_t> poly;
            char* tok = strtok(line + 2, " \t\r\n");
            while (tok && tok[0] != '#') {
                int vi = 0, ti = 0, ni = 0;
                if (sscanf(tok, "%d/%d/%d", &vi, &ti, &ni) < 2) sscanf(tok, "%d//%d", &vi, &ni);
                if (vi < 0) vi = (int)pos.size() + vi + 1;
                if (ni < 0) ni = (int)nrm.size() + ni + 1;
                if (ti < 0) ti = (int)uv.size() + ti + 1;
                if (vi < 1 || vi > (int)pos.size()) { fclose(f); err = "bad vertex index"; return false; }
                if (ni < 1 || ni > (int)nrm.size()) { ni = 0; allNormals = false; }
                if (ti < 1 || ti > (int)uv.size()) { ti = 0; allUVs = false; }
                std::array<int, 3> key{vi, ni, ti};
                auto it = remap.find(key);
                if (it == remap.end()) {
                    it = remap.emplace(key, (uint32_t)out.v.size()).first;
                    out.v.push_back(pos[vi - 1]);
                    out.n.push_back(ni ? nrm[ni - 1] : V3(0, 1, 0));
                    out.uv.push_back(ti ? uv[ti - 1] : V3());
                }
                poly.push_back(it->second);
                tok = strtok(nullptr, " \t\r\n");
            }
            for (size_t i = 2; i < poly.size(); i++) {
                out.tri.push_back(poly[0]);
                out.tri.push_back(poly[i - 1]);
                out.tri.push_back(poly[i]);
            }
        }
    }
    fclose(f);
    if (out.tri.empty()) { err = "no faces found"; return false; }
    if (!allNormals) out.n.clear();
    if (!allUVs) out.uv.clear();
    V3 lo(1e30f), hi(-1e30f);
    for (V3 p : out.v) growBox(lo, hi, p);
    V3 c = (lo + hi) * 0.5f;
    float s = 2.f / std::max(1e-6f, maxc(hi - lo));
    for (V3& p : out.v) p = (p - c) * s;
    out.build();
    m = std::move(out);
    return true;
}

namespace {

struct DxfValue { int code; std::string value; };

std::string trimDxf(std::string value) {
    size_t a = value.find_first_not_of(" \r\t"), b = value.find_last_not_of(" \r\t");
    return a == std::string::npos ? std::string() : value.substr(a, b - a + 1);
}

template <class T> bool dxfNumber(const std::string& value, T& result) {
    std::istringstream in(value);
    in.imbue(std::locale::classic());
    return (in >> result) && in.peek() == EOF;
}

void dxfFace(Mesh& mesh, const std::vector<V3>& vertices) {
    if (vertices.size() < 3) return;
    uint32_t start = (uint32_t)mesh.v.size();
    mesh.v.insert(mesh.v.end(), vertices.begin(), vertices.end());
    for (uint32_t i = 2; i < vertices.size(); ++i)
        if (length(cross(vertices[i - 1] - vertices[0], vertices[i] - vertices[0])) > 0)
            mesh.tri.insert(mesh.tri.end(), {start, start + i - 1, start + i});
}

}  // namespace

bool loadDXF(const std::string& path, std::vector<MeshLayer>& layers, float scale, bool zUp, bool split, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open DXF"; return false; }
    if (!(scale > 0 && scale <= 1e6f)) { err = "invalid import scale"; return false; }
    std::map<std::string, Mesh> meshes;
    std::vector<DxfValue> record;
    std::string entity, parentLayer;
    bool active = true, polyface = false, eof = false;
    std::vector<V3> polyVertices;
    std::vector<std::array<int, 4>> polyFaces;
    int line = 0;
    auto at = [&](int code) {
        for (const auto& value : record) if (value.code == code) return value.value;
        return std::string();
    };
    auto layer = [&](const std::string& name) -> Mesh& {
        return meshes[split ? (name.empty() ? "0" : name) : "DXF"];
    };
    auto vertex = [&](int offset, V3& p) {
        if (!dxfNumber(at(10 + offset), p.x) || !dxfNumber(at(20 + offset), p.y)) return false;
        std::string z = at(30 + offset);
        if (!z.empty() && !dxfNumber(z, p.z)) return false;
        if (!(maxc({std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)}) <= 1e12f)) return false;
        p = (zUp ? V3(p.x, p.z, -p.y) : p) * scale;
        return true;
    };
    auto finishPolyline = [&] {
        if (!polyface) return true;
        auto& mesh = layer(parentLayer);
        for (const auto& indices : polyFaces) {
            std::vector<V3> face;
            for (int index : indices) {
                if (!index) break;
                if (index < 0 || (size_t)index > polyVertices.size()) return false;
                face.push_back(polyVertices[index - 1]);
            }
            if (face.size() < 3) return false;
            dxfFace(mesh, face);
        }
        polyface = false;
        polyVertices.clear();
        polyFaces.clear();
        return true;
    };
    auto consume = [&] {
        if (entity == "SECTION") { active = at(2) == "ENTITIES"; return true; }
        if (!active) return true;
        if (entity == "3DFACE") {
            std::vector<V3> vertices(3);
            for (int i = 0; i < 3; ++i) if (!vertex(i, vertices[i])) return false;
            if (!at(13).empty()) {
                V3 last;
                if (!vertex(3, last)) return false;
                if (length(last - vertices.back()) > 0) vertices.push_back(last);
            }
            dxfFace(layer(at(8)), vertices);
        } else if (entity == "POLYLINE") {
            if (!finishPolyline()) return false;
            int flags = 0;
            dxfNumber(at(70), flags);
            polyface = (flags & 64) != 0;
            parentLayer = at(8);
        } else if (entity == "VERTEX" && polyface) {
            int flags = 0;
            dxfNumber(at(70), flags);
            if ((flags & 128) && !(flags & 64)) {
                std::array<int, 4> face{};
                for (int i = 0; i < 4; ++i) {
                    auto value = at(71 + i);
                    int index = 0;
                    if (!value.empty() && (!dxfNumber(value, index) || index < -2000000 || index > 2000000)) return false;
                    face[i] = std::abs(index);
                }
                polyFaces.push_back(face);
            } else {
                V3 p;
                if (!vertex(0, p)) return false;
                polyVertices.push_back(p);
            }
        } else if (entity == "SEQEND" || entity == "ENDSEC") {
            if (!finishPolyline()) return false;
            if (entity == "ENDSEC") active = false;
        }
        return true;
    };
    std::string code, value;
    while (std::getline(in, code)) {
        ++line;
        int group = 0;
        if (line > 16000000 || !dxfNumber(trimDxf(code), group) || !std::getline(in, value)) {
            err = "invalid or truncated ASCII DXF at line " + std::to_string(line);
            return false;
        }
        ++line;
        value = trimDxf(std::move(value));
        if (group == 0) {
            if (!consume()) { err = "malformed " + entity + " before line " + std::to_string(line - 1); return false; }
            if (value == "EOF") { eof = true; break; }
            entity = value;
            record.clear();
        } else {
            record.push_back({group, std::move(value)});
        }
    }
    if (!eof || !finishPolyline()) { err = "incomplete DXF or missing EOF"; return false; }
    std::vector<MeshLayer> result;
    for (auto& [name, mesh] : meshes) {
        if (mesh.tri.empty()) continue;
        if (mesh.v.size() > 2000000 || mesh.triCount() > 2000000) { err = "DXF layer is too large"; return false; }
        mesh.build();
        result.push_back({name, std::move(mesh)});
    }
    if (result.empty()) { err = "no 3DFACE or polyface geometry in the ENTITIES section"; return false; }
    layers = std::move(result);
    return true;
}

bool saveGeometry(const std::string& path, const std::vector<MeshLayer>& layers, bool dxf) {
    std::ofstream out(path);
    if (!out) return false;
    out.imbue(std::locale::classic());
    out.precision(9);
    if (dxf) out << "0\nSECTION\n2\nENTITIES\n";
    else out << "# Brycescape geometry\n";
    size_t base = 1, uvBase = 1, normalsBase = 1;
    for (const auto& [name, mesh] : layers) {
        std::string label = name;
        for (char& c : label) if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') &&
                                  !(c >= '0' && c <= '9') && c != '-') c = '_';
        if (label.empty()) label = "Object";
        bool hasUV = mesh.v.size() == mesh.uv.size() && !mesh.uv.empty();
        bool hasNormals = mesh.v.size() == mesh.n.size() && !mesh.n.empty();
        if (!dxf) {
            out << "o " << label << '\n';
            for (V3 p : mesh.v) out << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
            if (hasUV) for (V3 p : mesh.uv) out << "vt " << p.x << ' ' << p.y << '\n';
            if (hasNormals) for (V3 p : mesh.n) out << "vn " << p.x << ' ' << p.y << ' ' << p.z << '\n';
        }
        for (size_t i = 0; i < mesh.tri.size(); i += 3) {
            if (!dxf) {
                out << "f";
                for (int k = 0; k < 3; ++k) {
                    uint32_t index = mesh.tri[i + k];
                    out << ' ' << base + index;
                    if (hasUV || hasNormals) out << '/';
                    if (hasUV) out << uvBase + index;
                    if (hasNormals) out << '/' << normalsBase + index;
                }
                out << '\n';
                continue;
            }
            out << "0\n3DFACE\n8\n" << label << '\n';
            for (int k = 0; k < 4; ++k) {
                V3 p = mesh.v[mesh.tri[i + std::min(k, 2)]];
                out << 10 + k << '\n' << p.x << '\n' << 20 + k << '\n' << -p.z << '\n' << 30 + k << '\n' << p.y << '\n';
            }
        }
        base += mesh.v.size();
        if (hasUV) uvBase += mesh.uv.size();
        if (hasNormals) normalsBase += mesh.n.size();
    }
    if (dxf) out << "0\nENDSEC\n0\nEOF\n";
    out.close();
    return (bool)out;
}

static void tube(Mesh& m, V3 a, V3 b, float ra, float rb) {
    V3 ax = normalize(b - a), t = std::fabs(ax.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0);
    V3 u = normalize(cross(ax, t)), w = cross(ax, u);
    const int S = 6;
    uint32_t base = (uint32_t)m.v.size();
    for (int i = 0; i < S; i++) {
        float ang = 2 * PI * i / S;
        V3 dir = u * std::cos(ang) + w * std::sin(ang);
        m.v.push_back(a + dir * ra); m.n.push_back(dir);
        m.v.push_back(b + dir * rb); m.n.push_back(dir);
    }
    for (int i = 0; i < S; i++) {
        uint32_t i0 = base + i * 2, i1 = base + ((i + 1) % S) * 2;
        m.tri.insert(m.tri.end(), {i0, i1, i0 + 1, i1, i1 + 1, i0 + 1});
    }
}

void makeTree(const TreeParams& p, Mesh& trunk, Mesh& leaves) {
    trunk = Mesh();
    leaves = Mesh();
    Rng rng(p.seed * 2246822519u + 3);
    auto leaf = [&](V3 c, float s) {
        V3 nn = normalize(V3(rng.f() - 0.5f, rng.f() * 0.8f + 0.2f, rng.f() - 0.5f));
        V3 t = std::fabs(nn.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0), u = normalize(cross(nn, t)) * s, w = normalize(cross(nn, u)) * s;
        uint32_t b = (uint32_t)leaves.v.size();
        if (p.foliageShape != 0) {
            float width = p.foliageShape == 1 ? 0.68f : 0.17f;
            for (V3 q : {c - w, c + u * width, c + w, c - u * width}) { leaves.v.push_back(q); leaves.n.push_back(nn); }
            leaves.tri.insert(leaves.tri.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
            return;
        }
        for (V3 q : {c - u - w, c + u - w, c + u + w, c - u + w}) { leaves.v.push_back(q); leaves.n.push_back(nn); }
        leaves.tri.insert(leaves.tri.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    };
    std::function<void(V3, V3, float, float, int)> branch;
    branch = [&](V3 base, V3 dir, float len, float radius, int level) {
        V3 tip = base + dir * len;
        tube(trunk, base, tip, radius, radius * p.taper);
        if (level >= p.levels) {
            int cnt = (int)std::lround(12 * p.leafDensity);
            for (int i = 0; i < cnt; i++)
                leaf(lerp(base, tip, 0.3f + 0.7f * rng.f()) + V3(rng.f() - 0.5f, rng.f() - 0.5f, rng.f() - 0.5f) * (len * 0.6f),
                     p.leafSize * (0.7f + 0.6f * rng.f()));
            return;
        }
        V3 t = std::fabs(dir.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0), u = normalize(cross(dir, t)), w = cross(dir, u);
        float phase = rng.f() * 2 * PI;
        for (int b = 0; b < p.branches; b++) {
            float at = 0.4f + 0.55f * (b + rng.f()) / p.branches, ang = phase + 2 * PI * b / p.branches + (rng.f() - 0.5f);
            float sp = rad(p.spread * (0.75f + 0.5f * rng.f()));
            V3 perp = u * std::cos(ang) + w * std::sin(ang);
            V3 nd = normalize(dir * std::cos(sp) + perp * std::sin(sp) + V3(0, p.upwardBias, 0));
            branch(lerp(base, tip, at), nd, len * (0.55f + 0.2f * rng.f()), radius * 0.6f, level + 1);
        }
        branch(tip, normalize(dir + V3(rng.f() - 0.5f, 0.3f, rng.f() - 0.5f) * 0.25f), len * 0.65f, radius * 0.7f, level + 1);
    };
    branch(V3(0, 0, 0), V3(0, 1, 0), p.height * 0.42f, p.trunkRadius * p.height, 0);
    trunk.build();
    leaves.build();
}
