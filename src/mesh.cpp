#define _CRT_SECURE_NO_WARNINGS
#include "mesh.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>

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

bool Mesh::intersect(V3 o, V3 d, float tmin, float tmax, float& tOut, V3& nOut) const {
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
                nOut = n[ia] * (1 - u - w) + n[ib] * u + n[ic] * w;
                hit = true;
            }
        } else if (sp + 2 <= 96) {
            stack[sp++] = nd.left;
            stack[sp++] = nd.right;
        }
    }
    return hit;
}

bool loadOBJ(const std::string& path, Mesh& m, std::string& err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open file"; return false; }
    std::vector<V3> pos, nrm;
    std::map<std::pair<int, int>, uint32_t> remap;
    Mesh out;
    bool allNormals = true;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'v' && line[1] == ' ') {
            V3 p;
            if (sscanf(line + 2, "%f %f %f", &p.x, &p.y, &p.z) == 3) pos.push_back(p);
        } else if (line[0] == 'v' && line[1] == 'n') {
            V3 p;
            if (sscanf(line + 3, "%f %f %f", &p.x, &p.y, &p.z) == 3) nrm.push_back(normalize(p));
        } else if (line[0] == 'f' && line[1] == ' ') {
            std::vector<uint32_t> poly;
            char* tok = strtok(line + 2, " \t\r\n");
            while (tok) {
                int vi = 0, ti = 0, ni = 0;
                if (sscanf(tok, "%d/%d/%d", &vi, &ti, &ni) != 3 && sscanf(tok, "%d//%d", &vi, &ni) != 2) {
                    ni = 0;
                    sscanf(tok, "%d", &vi);
                }
                if (vi < 0) vi = (int)pos.size() + vi + 1;
                if (ni < 0) ni = (int)nrm.size() + ni + 1;
                if (vi < 1 || vi > (int)pos.size()) { fclose(f); err = "bad vertex index"; return false; }
                if (ni < 1 || ni > (int)nrm.size()) { ni = 0; allNormals = false; }
                auto key = std::make_pair(vi, ni);
                auto it = remap.find(key);
                if (it == remap.end()) {
                    it = remap.emplace(key, (uint32_t)out.v.size()).first;
                    out.v.push_back(pos[vi - 1]);
                    out.n.push_back(ni ? nrm[ni - 1] : V3(0, 1, 0));
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
    V3 lo(1e30f), hi(-1e30f);
    for (V3 p : out.v) growBox(lo, hi, p);
    V3 c = (lo + hi) * 0.5f;
    float s = 2.f / std::max(1e-6f, maxc(hi - lo));
    for (V3& p : out.v) p = (p - c) * s;
    out.build();
    m = std::move(out);
    return true;
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
        for (V3 q : {c - u - w, c + u - w, c + u + w, c - u + w}) { leaves.v.push_back(q); leaves.n.push_back(nn); }
        leaves.tri.insert(leaves.tri.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    };
    std::function<void(V3, V3, float, float, int)> branch;
    branch = [&](V3 base, V3 dir, float len, float radius, int level) {
        V3 tip = base + dir * len;
        tube(trunk, base, tip, radius, radius * 0.7f);
        if (level >= p.levels) {
            int cnt = std::max(1, (int)(12 * p.leafDensity));
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
            V3 nd = normalize(dir * std::cos(sp) + perp * std::sin(sp) + V3(0, 0.15f, 0));
            branch(lerp(base, tip, at), nd, len * (0.55f + 0.2f * rng.f()), radius * 0.6f, level + 1);
        }
        branch(tip, normalize(dir + V3(rng.f() - 0.5f, 0.3f, rng.f() - 0.5f) * 0.25f), len * 0.65f, radius * 0.7f, level + 1);
    };
    branch(V3(0, 0, 0), V3(0, 1, 0), p.height * 0.42f, p.trunkRadius * p.height, 0);
    trunk.build();
    leaves.build();
}
