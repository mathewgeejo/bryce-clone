#include "scene.h"

#include <fstream>
#include <iomanip>
#include <map>

#include "terrain.h"

static const float EPS = 2e-3f;

const char* texName(TexType t) {
    static const char* n[] = {"None", "Checker", "Marble", "Wood", "Granite", "Noise", "Clouds", "Waves", "Strata",
                              "Altitude", "Spots", "Bricks", "Rust", "Lava", "Bands", "Gradient", "Picture"};
    return n[(int)t];
}
const char* mapName(TexMap m) {
    static const char* n[] = {"Object", "World", "Spherical", "Cylindrical", "Planar XZ", "Planar XY"};
    return n[(int)m];
}
const char* objTypeName(ObjType t) {
    static const char* n[] = {"Sphere", "Cube", "Cylinder", "Cone", "Pyramid", "Terrain", "Ground", "Water", "Cloud", "Light",
                              "Torus", "Disc", "Square", "Stone", "Lattice", "Mesh", "Spotlight", "Parallel", "Metaball", "Picture"};
    return n[(int)t];
}
const char* boolName(BoolMode b) {
    static const char* n[] = {"Neutral", "Positive", "Negative", "Intersect"};
    return n[(int)b];
}
const char* skyModeName(int m) {
    static const char* n[] = {"Soft Sky", "Darker Sky", "Custom Sky", "Atmosphere Off"};
    return n[std::min(std::max(m, 0), 3)];
}

Sky lerpSky(const Sky& a, const Sky& b, float t) {
    Sky s = t < 0.5f ? a : b;
    auto L = [t](float x, float y) { return lerpf(x, y, t); };
    auto C = [t](V3 x, V3 y) { return lerp(x, y, t); };
    s.zenith = C(a.zenith, b.zenith); s.horizon = C(a.horizon, b.horizon); s.sunColor = C(a.sunColor, b.sunColor);
    s.sunAlt = L(a.sunAlt, b.sunAlt); s.sunAz = L(a.sunAz, b.sunAz); s.sunSize = L(a.sunSize, b.sunSize);
    s.haze = L(a.haze, b.haze); s.hazeColor = C(a.hazeColor, b.hazeColor);
    s.fog = L(a.fog, b.fog); s.fogHeight = L(a.fogHeight, b.fogHeight); s.fogColor = C(a.fogColor, b.fogColor);
    s.clouds = L(a.clouds, b.clouds); s.stratus = L(a.stratus, b.stratus); s.cloudScale = L(a.cloudScale, b.cloudScale);
    s.cloudColor = C(a.cloudColor, b.cloudColor); s.ambient = L(a.ambient, b.ambient); s.ambientColor = C(a.ambientColor, b.ambientColor);
    s.stars = L(a.stars, b.stars); s.moonAlt = L(a.moonAlt, b.moonAlt); s.moonAz = L(a.moonAz, b.moonAz);
    s.moonPhase = L(a.moonPhase, b.moonPhase); s.moonSize = L(a.moonSize, b.moonSize); s.rainbow = L(a.rainbow, b.rainbow);
    return s;
}

void Heightfield::computeNormals() {
    n.resize(h.size());
    if (res < 2) return;
    float cs = 2.f / (res - 1);
    for (int z = 0; z < res; z++)
        for (int x = 0; x < res; x++) {
            int xl = std::max(x - 1, 0), xr = std::min(x + 1, res - 1), zu = std::max(z - 1, 0), zd = std::min(z + 1, res - 1);
            float dx = (at(xr, z) - at(xl, z)) / ((xr - xl) * cs), dz = (at(x, zd) - at(x, zu)) / ((zd - zu) * cs);
            n[z * res + x] = normalize(V3(-dx, 1, -dz));
        }
}

bool localBounds(const Object& o, V3& lo, V3& hi) {
    lo = V3(-1.f); hi = V3(1.f);
    switch (o.type) {
    case ObjType::Metaball: lo = V3(-o.metaRadius); hi = V3(o.metaRadius); break;
    case ObjType::Picture: hi = o.picture.halfExtent(); lo = -hi; break;
    case ObjType::Terrain: lo.y = 0; break;
    case ObjType::Torus: {
        float radius = o.torus.major + o.torus.minor;
        hi = {radius, o.torus.minor * o.torus.section, radius};
        lo = -hi;
        break;
    }
    case ObjType::Stone:
        if (o.stone.irregularity > 1) { hi = V3(0.8f + 0.48f * o.stone.irregularity); lo = -hi; }
        break;
    case ObjType::Disc: case ObjType::Square: lo.y = -0.01f; hi.y = 0.01f; break;
    case ObjType::Mesh:
        if (!o.mesh || o.mesh->v.empty()) return false;
        lo = o.mesh->bmin; hi = o.mesh->bmax;
        break;
    default:
        if (isInfinite(o.type) || isLight(o.type)) return false;
    }
    return true;
}

bool worldBounds(const Object& o, V3& lo, V3& hi) {
    V3 a, b;
    if (!localBounds(o, a, b)) { lo = hi = o.pos; return isLight(o.type); }
    lo = V3(1e30f); hi = V3(-1e30f);
    for (int i = 0; i < 8; i++) {
        V3 p = o.pos + o.fwd * V3((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z);
        lo = V3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = V3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    return true;
}

void Object::update(const Camera* camera) {
    M3 orientation = rotY(rad(rot.y)) * rotX(rad(rot.x));
    if (type == ObjType::Picture && picture.billboard && camera) {
        V3 n = normalize(camera->eye() - pos);
        if (length(n) < 0.5f) n = normalize(camera->eye() - camera->target);
        if (length(n) < 0.5f) n = {0, 0, 1};
        V3 r = normalize(cross(std::fabs(n.y) > 0.99f ? V3(0, 0, 1) : V3(0, 1, 0), n));
        V3 u = cross(n, r);
        for (int i = 0; i < 3; i++) {
            orientation.m[i][0] = r[i];
            orientation.m[i][1] = u[i];
            orientation.m[i][2] = n[i];
        }
    }
    fwd = orientation * rotZ(rad(rot.z)) * scaleM(scl);
    inv = fwd.inverse();
    nrm = inv.transpose();
    V3 lo, hi;
    if (!localBounds(*this, lo, hi)) { bradius = -1; return; }
    V3 c = (lo + hi) * 0.5f;
    float ms = std::max(length(fwd.col(0)), std::max(length(fwd.col(1)), length(fwd.col(2))));
    bcenter = pos + fwd * c;
    bradius = length(hi - c) * ms * 1.001f + 1e-4f;
}

void Scene::updateAll() {
    std::map<int, std::vector<int>> groups;
    std::map<int, size_t> fieldGroups;
    metaballs.clear();
    for (size_t i = 0; i < objs.size(); i++) {
        Object& o = objs[i];
        o.update(&cam);
        o.csg = false;
        if (o.mat.tex == TexType::Image && !o.mat.image.empty() && !o.mat.img) o.mat.img = loadImageCached(o.mat.image);
        for (auto& layer : o.mat.layers)
            if (layer.tex == TexType::Image && !layer.image.empty() && !layer.img) layer.img = loadImageCached(layer.image);
        if (o.type == ObjType::Picture && !o.picture.path.empty() && !o.picture.image) o.picture.image = loadImageCached(o.picture.path);
        if (o.hidden) continue;
        if (o.type == ObjType::Metaball) {
            size_t field = metaballs.size();
            if (o.group > 0) field = fieldGroups.emplace(o.group, field).first->second;
            if (field == metaballs.size()) metaballs.emplace_back();
            metaballs[field].balls.push_back((int)i);
            o.csg = true;
        } else if (o.group > 0 && isCsgCapable(o.type) && o.boolMode != BoolMode::Neutral) {
            groups[o.group].push_back((int)i);
        }
    }
    csg.clear();
    for (auto& g : groups) {
        for (int i : g.second) objs[i].csg = true;
        auto field = fieldGroups.find(g.first);
        if (field == fieldGroups.end()) csg.push_back(std::move(g.second));
        else metaballs[field->second].operands = std::move(g.second);
    }
}

int Scene::maxGroup() const {
    int m = 0;
    for (auto& o : objs) m = std::max(m, o.group);
    return m;
}

V3 Camera::eye() const {
    float p = rad(pitch), y = rad(yaw);
    return target + V3(std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y)) * dist;
}

Ray ViewBasis::ray(float nx, float ny) const {
    if (ortho) return {o + r * (nx * k) + u * (ny * k), f};
    return {o, normalize(f + r * (nx * k) + u * (ny * k))};
}

ViewBasis cameraBasis(const Camera& c) {
    ViewBasis b;
    b.o = c.eye();
    b.f = normalize(c.target - b.o);
    b.r = normalize(cross(b.f, V3(0, 1, 0)));
    b.u = cross(b.r, b.f);
    b.k = std::tan(rad(c.fov) * 0.5f);
    return b;
}

ViewBasis orthoBasis(int mode, V3 center, float halfH) {
    ViewBasis b;
    if (mode == 1) { b.f = {0, -1, 0}; b.r = {1, 0, 0}; b.u = {0, 0, -1}; }
    else if (mode == 2) { b.f = {0, 0, -1}; b.r = {1, 0, 0}; b.u = {0, 1, 0}; }
    else { b.f = {-1, 0, 0}; b.r = {0, 0, -1}; b.u = {0, 1, 0}; }
    b.o = center - b.f * 2000.f;
    b.ortho = true;
    b.k = halfH;
    return b;
}

Scene Scene::snapshot() const {
    Scene s = *this;
    for (auto& o : s.objs)
        if (o.hf) o.hf = std::make_shared<Heightfield>(*o.hf);
    return s;
}

// ---------------------------------------------------------------- animation

template <class K> static bool keySpan(const std::vector<K>& v, float f, int& i0, int& i1, float& t) {
    if (v.empty()) return false;
    i0 = i1 = 0;
    t = 0;
    if (f <= v.front().frame) return true;
    if (f >= v.back().frame) { i0 = i1 = (int)v.size() - 1; return true; }
    for (i1 = 1; v[i1].frame <= f; ++i1) {}
    i0 = i1 - 1;
    t = (f - v[i0].frame) / (float)(v[i1].frame - v[i0].frame);
    return true;
}

static float cr(float p0, float p1, float p2, float p3, float t) {
    return 0.5f * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (3 * p1 - p0 - 3 * p2 + p3) * t * t * t);
}
static V3 cr(V3 a, V3 b, V3 c, V3 d, float t) { return {cr(a.x, b.x, c.x, d.x, t), cr(a.y, b.y, c.y, d.y, t), cr(a.z, b.z, c.z, d.z, t)}; }

template <class K, class G> static auto spline(const std::vector<K>& v, int i0, int i1, float t, G get) {
    int n = (int)v.size();
    auto at = [&](int i) { return get(v[std::min(std::max(i, 0), n - 1)]); };
    return cr(at(i0 - 1), at(i0), at(i1), at(i1 + 1), t);
}

void Scene::applyFrame(int f) {
    frame = f;
    int i0, i1;
    float t;
    for (Object& o : objs)
        if (keySpan(o.keys, (float)f, i0, i1, t)) {
            o.pos = spline(o.keys, i0, i1, t, [](const Key& k) { return k.pos; });
            o.rot = spline(o.keys, i0, i1, t, [](const Key& k) { return k.rot; });
            o.scl = spline(o.keys, i0, i1, t, [](const Key& k) { return k.scl; });
            o.scl = V3(std::max(o.scl.x, 0.01f), std::max(o.scl.y, 0.01f), std::max(o.scl.z, 0.01f));
        }
    if (keySpan(camKeys, (float)f, i0, i1, t)) {
        cam.target = spline(camKeys, i0, i1, t, [](const CamKey& k) { return k.cam.target; });
        V3 a = spline(camKeys, i0, i1, t, [](const CamKey& k) { return V3(k.cam.yaw, k.cam.pitch, k.cam.dist); });
        cam.yaw = a.x; cam.pitch = clampf(a.y, -89, 89); cam.dist = std::max(a.z, 1.f);
        cam.fov = lerpf(camKeys[i0].cam.fov, camKeys[i1].cam.fov, t);
    }
    if (keySpan(skyKeys, (float)f, i0, i1, t)) sky = lerpSky(skyKeys[i0].sky, skyKeys[i1].sky, t);
    updateAll();
}

// ---------------------------------------------------------------- intersection

struct LHit { float t; V3 n; };

static const V3 cubeN[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
static const float cubeD[6] = {1, 1, 1, 1, 1, 1};
static const float s5 = 0.4472136f;
static const V3 pyrN[5] = {{0, -1, 0}, {2 * s5, s5, 0}, {-2 * s5, s5, 0}, {0, s5, 2 * s5}, {0, s5, -2 * s5}};
static const float pyrD[5] = {1, s5, s5, s5, s5};

static bool polySpan(const V3* pn, const float* pd, int cnt, V3 o, V3 d, float& tn, float& tf, V3& nn, V3& nf) {
    tn = -1e30f; tf = 1e30f;
    for (int i = 0; i < cnt; i++) {
        float dn = dot(pn[i], d), dist = pd[i] - dot(pn[i], o);
        if (std::fabs(dn) < 1e-12f) {
            if (dist < 0) return false;
            continue;
        }
        float t = dist / dn;
        if (dn < 0) { if (t > tn) { tn = t; nn = pn[i]; } }
        else if (t < tf) { tf = t; nf = pn[i]; }
    }
    return tn <= tf;
}

// Entry/exit span of a closed convex primitive along a local ray (all t, not just positive).
static bool convexSpan(ObjType type, V3 o, V3 d, float& tn, float& tf, V3& nn, V3& nf) {
    switch (type) {
    case ObjType::Sphere: {
        float a = dot(d, d), b = dot(o, d), c = dot(o, o) - 1, disc = b * b - a * c;
        if (disc < 0) return false;
        float sq = std::sqrt(disc);
        tn = (-b - sq) / a; tf = (-b + sq) / a;
        nn = o + d * tn; nf = o + d * tf;
        return true;
    }
    case ObjType::Cube: return polySpan(cubeN, cubeD, 6, o, d, tn, tf, nn, nf);
    case ObjType::Pyramid: return polySpan(pyrN, pyrD, 5, o, d, tn, tf, nn, nf);
    default: break;
    }
    // Cylinder / cone: gather boundary crossings; a convex solid has exactly two.
    float ts[4];
    V3 ns[4];
    int cnt = 0;
    auto add = [&](float t, V3 n) { if (cnt < 4) { ts[cnt] = t; ns[cnt] = n; cnt++; } };
    bool cone = type == ObjType::Cone;
    float a, b, c;
    if (cone) {
        float k = 1 - o.y;
        a = d.x * d.x + d.z * d.z - d.y * d.y * 0.25f; b = o.x * d.x + o.z * d.z + k * d.y * 0.25f; c = o.x * o.x + o.z * o.z - k * k * 0.25f;
    } else {
        a = d.x * d.x + d.z * d.z; b = o.x * d.x + o.z * d.z; c = o.x * o.x + o.z * o.z - 1;
    }
    auto side = [&](float t) {
        V3 p = o + d * t;
        if (p.y >= -1 && p.y <= 1) add(t, cone ? V3(2 * p.x, (1 - p.y) * 0.5f, 2 * p.z) : V3(p.x, 0, p.z));
    };
    if (std::fabs(a) > 1e-9f) {
        float disc = b * b - a * c;
        if (disc >= 0) { float sq = std::sqrt(disc); side((-b - sq) / a); side((-b + sq) / a); }
    } else if (cone && std::fabs(b) > 1e-12f) side(-c / (2 * b));
    if (std::fabs(d.y) > 1e-12f)
        for (float cy : {-1.f, 1.f}) {
            if (cone && cy > 0) continue;
            float t = (cy - o.y) / d.y, x = o.x + d.x * t, z = o.z + d.z * t;
            if (x * x + z * z <= 1) add(t, V3(0, cy, 0));
        }
    if (cnt < 2) return false;
    int imin = 0, imax = 0;
    for (int i = 1; i < cnt; i++) {
        if (ts[i] < ts[imin]) imin = i;
        if (ts[i] > ts[imax]) imax = i;
    }
    if (ts[imax] - ts[imin] < 1e-7f) return false;
    tn = ts[imin]; nn = ns[imin]; tf = ts[imax]; nf = ns[imax];
    return true;
}

static bool boxSpan(V3 lo, V3 hi, V3 o, V3 d, float& t0, float& t1) {
    for (int i = 0; i < 3; i++) {
        float oi = o[i], di = d[i];
        if (std::fabs(di) < 1e-12f) {
            if (oi < lo[i] || oi > hi[i]) return false;
            continue;
        }
        float ta = (lo[i] - oi) / di, tb = (hi[i] - oi) / di;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

static bool triHit(V3 o, V3 d, V3 a, V3 b, V3 c, float& t, float& u, float& v) {
    V3 e1 = b - a, e2 = c - a, p = cross(d, e2);
    float det = dot(e1, p);
    if (det == 0) return false;
    float id = 1 / det;
    V3 s = o - a;
    u = dot(s, p) * id;
    if (u < -1e-5f || u > 1 + 1e-5f) return false;
    V3 q = cross(s, e1);
    v = dot(d, q) * id;
    if (v < -1e-5f || u + v > 1 + 1e-5f) return false;
    t = dot(e2, q) * id;
    return true;
}

static bool hitTerrain(const Heightfield& f, V3 o, V3 d, LHit& h) {
    float t0 = EPS, t1 = h.t;
    if (!boxSpan(V3(-1, 0, -1), V3(1, 1, 1), o, d, t0, t1)) return false;
    const int N = f.res - 1;
    const float cs = 2.f / N, INF = 1e30f;
    V3 p = o + d * t0;
    int i = std::min(std::max((int)((p.x + 1) / cs), 0), N - 1), j = std::min(std::max((int)((p.z + 1) / cs), 0), N - 1);
    int si = d.x > 0 ? 1 : -1, sj = d.z > 0 ? 1 : -1;
    float tdx = std::fabs(d.x) > 1e-12f ? cs / std::fabs(d.x) : INF, tdz = std::fabs(d.z) > 1e-12f ? cs / std::fabs(d.z) : INF;
    float tmx = std::fabs(d.x) > 1e-12f ? ((-1 + (i + (si > 0 ? 1 : 0)) * cs) - o.x) / d.x : INF;
    float tmz = std::fabs(d.z) > 1e-12f ? ((-1 + (j + (sj > 0 ? 1 : 0)) * cs) - o.z) / d.z : INF;
    float tEnter = t0;
    while (i >= 0 && j >= 0 && i < N && j < N && tEnter <= t1) {
        float tExit = std::min(std::min(tmx, tmz), t1);
        float y0 = o.y + d.y * tEnter, y1 = o.y + d.y * tExit;
        float h00 = f.at(i, j), h10 = f.at(i + 1, j), h01 = f.at(i, j + 1), h11 = f.at(i + 1, j + 1);
        float hmax = std::max(std::max(h00, h10), std::max(h01, h11));
        if (hmax > 1e-4f && std::min(y0, y1) <= hmax + 1e-4f) {
            float x0 = -1 + i * cs, z0 = -1 + j * cs;
            V3 a(x0, h00, z0), b(x0 + cs, h10, z0), c(x0, h01, z0 + cs), e(x0 + cs, h11, z0 + cs);
            float bt = INF, bu = 0, bv = 0, t, u, v;
            int tri = -1;
            if (triHit(o, d, a, b, e, t, u, v) && t > EPS && t < bt) { bt = t; bu = u; bv = v; tri = 0; }
            if (triHit(o, d, a, e, c, t, u, v) && t > EPS && t < bt) { bt = t; bu = u; bv = v; tri = 1; }
            if (tri >= 0 && bt < h.t) {
                int r = f.res;
                V3 na = f.n[j * r + i], ne = f.n[(j + 1) * r + i + 1];
                V3 nb = tri == 0 ? f.n[j * r + i + 1] : ne, nc = tri == 0 ? ne : f.n[(j + 1) * r + i];
                h.t = bt;
                h.n = na * (1 - bu - bv) + nb * bu + nc * bv;
                return true;
            }
        }
        if (tmx < tmz) { i += si; tEnter = tmx; tmx += tdx; }
        else { j += sj; tEnter = tmz; tmz += tdz; }
    }
    return false;
}

static float sdTorus(const TorusShape& shape, V3 p, V3 start, V3 end) {
    float radial = std::sqrt(p.x * p.x + p.z * p.z) - shape.major, y = p.y / shape.section;
    float tube = std::sqrt(radial * radial + y * y) - shape.minor;
    if (shape.sweep >= 360) return tube;
    float a = dot(start, p), b = dot(end, p);
    return std::max(tube, shape.sweep <= 180 ? std::max(a, b) : std::min(a, b));
}

static float sdStone(const StoneShape& shape, V3 p, V3 off) {
    float box = maxc(V3(std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)));
    float coarse = 0.2f * fbm(p * 1.6f + off, 4);
    float grain = 0.04f * (1 - shape.smoothing) * noise3(p * 7.f + off);
    float radius = shape.irregularity == 1 ? 0.8f + coarse + grain : 0.8f + shape.irregularity * (coarse + grain);
    return lerpf(length(p), box, shape.blockiness) - radius;
}

// Sphere-traces a signed distance field in local space (handles rays starting inside).
template <class F> static bool marchSDF(F sdf, float lip, V3 lo, V3 hi, V3 o, V3 d, LHit& h) {
    float t0 = EPS, t1 = h.t;
    if (!boxSpan(lo, hi, o, d, t0, t1)) return false;
    float L = length(d);
    V3 dn = d / L;
    float s = t0 * L, se = t1 * L;
    float sign = sdf(o + dn * s) < 0 ? -1.f : 1.f;
    for (int i = 0; i < 160 && s <= se; i++) {
        float dist = sdf(o + dn * s) * sign;
        if (dist < 2e-4f) {
            V3 p = o + dn * s;
            const float e = 1e-3f;
            V3 n(sdf(p + V3(e, 0, 0)) - sdf(p - V3(e, 0, 0)), sdf(p + V3(0, e, 0)) - sdf(p - V3(0, e, 0)), sdf(p + V3(0, 0, e)) - sdf(p - V3(0, 0, e)));
            float t = s / L;
            if (t <= EPS || t >= h.t) return false;
            h.t = t;
            h.n = normalize(n);
            return true;
        }
        s += std::max(dist * lip, 1e-4f);
    }
    return false;
}

static bool hitFlat(V3 o, V3 d, bool disc, LHit& h) {
    if (std::fabs(d.y) < 1e-12f) return false;
    float t = -o.y / d.y;
    if (t <= EPS || t >= h.t) return false;
    float x = o.x + d.x * t, z = o.z + d.z * t;
    if (disc ? x * x + z * z > 1 : (std::fabs(x) > 1 || std::fabs(z) > 1)) return false;
    h.t = t; h.n = V3(0, 1, 0);
    return true;
}

static bool hitPlane(V3 o, V3 d, LHit& h) {
    if (std::fabs(d.y) < 1e-12f) return false;
    float t = -o.y / d.y;
    if (t <= EPS || t >= h.t) return false;
    h.t = t; h.n = V3(0, 1, 0);
    return true;
}

static bool sphereReject(const Object& ob, const Ray& r, float tmax) {
    if (ob.bradius <= 0) return false;
    V3 oc = r.o - ob.bcenter;
    float b = dot(oc, r.d), c = dot(oc, oc) - ob.bradius * ob.bradius;
    if (c > 0 && b > 0) return true;
    float disc = b * b - c;
    return disc < 0 || -b - std::sqrt(disc) > tmax;
}

static void fillHit(const Object& ob, const Ray& r, float t, V3 ln, Hit& h) {
    h.t = t;
    h.p = r.o + r.d * t;
    h.n = normalize(ob.nrm * ln);
    h.lr = ob.inv * (h.p - ob.pos);
    h.lp = h.lr * ob.scl;
    h.ly = h.lr.y;
}

static bool intersectObj(const Object& ob, const Ray& r, float tmax, Hit& h) {
    if (isLight(ob.type) || sphereReject(ob, r, tmax)) return false;
    V3 lo = ob.inv * (r.o - ob.pos), ld = ob.inv * r.d;
    LHit lh{tmax, {}};
    bool ok = false;
    switch (ob.type) {
    case ObjType::Sphere: case ObjType::Cube: case ObjType::Pyramid: case ObjType::Cylinder: case ObjType::Cone: {
        float tn, tf;
        V3 nn, nf;
        if (convexSpan(ob.type, lo, ld, tn, tf, nn, nf)) {
            if (tn > EPS && tn < tmax) { lh = {tn, nn}; ok = true; }
            else if (tn <= EPS && tf > EPS && tf < tmax) { lh = {tf, nf}; ok = true; }
        }
        break;
    }
    case ObjType::Metaball: {
        if (ob.metaNegative || ob.metaStrength <= 0.25f) break;
        float radius = ob.metaRadius * std::sqrt(1 - std::sqrt(0.25f / ob.metaStrength));
        float tn, tf;
        V3 nn, nf;
        if (convexSpan(ObjType::Sphere, lo / radius, ld / radius, tn, tf, nn, nf)) {
            if (tn > EPS && tn < tmax) { lh = {tn, nn}; ok = true; }
            else if (tn <= EPS && tf > EPS && tf < tmax) { lh = {tf, nf}; ok = true; }
        }
        break;
    }
    case ObjType::Picture: {
        const Picture& picture = ob.picture;
        if (!picture.image || std::fabs(ld.z) < 1e-12f) break;
        float t = -lo.z / ld.z;
        if (t <= EPS || t >= tmax) break;
        V3 p = lo + ld * t, ext = picture.halfExtent();
        if (std::fabs(p.x) > ext.x || std::fabs(p.y) > ext.y) break;
        V3 color = picture.image->sampleClamped(0.5f + 0.5f * p.x / ext.x, 0.5f - 0.5f * p.y / ext.y, picture.chroma);
        if (picture.chroma && length(color - picture.keyColor) <= picture.keyTolerance) break;
        h.pictureColor = color;
        lh = {t, {0, 0, 1}};
        ok = true;
        break;
    }
    case ObjType::Terrain: ok = ob.hf && ob.hf->res > 1 && hitTerrain(*ob.hf, lo, ld, lh); break;
    case ObjType::Lattice:
        if (ob.hf && ob.hf->res > 1) {
            if (hitTerrain(*ob.hf, lo, ld, lh)) ok = true;
            LHit m = lh;
            if (hitTerrain(*ob.hf, V3(lo.x, -lo.y, lo.z), V3(ld.x, -ld.y, ld.z), m)) { lh = {m.t, V3(m.n.x, -m.n.y, m.n.z)}; ok = true; }
        }
        break;
    case ObjType::Torus: {
        const TorusShape& shape = ob.torus;
        float a = rad(shape.start), b = rad(shape.start + shape.sweep);
        V3 start, end, min, max;
        if (shape.sweep < 360) {
            start = {std::sin(a), 0, -std::cos(a)};
            end = {-std::sin(b), 0, std::cos(b)};
        }
        localBounds(ob, min, max);
        ok = marchSDF([&](V3 p) { return sdTorus(shape, p, start, end); },
                      0.9f * std::min(1.f, shape.section), min, max, lo, ld, lh);
        break;
    }
    case ObjType::Stone: {
        V3 off((ob.seed % 97) * 3.1f, (ob.seed % 89) * 1.7f, (ob.seed % 83) * 2.3f);
        V3 min, max;
        localBounds(ob, min, max);
        ok = marchSDF([&](V3 p) { return sdStone(ob.stone, p, off); },
                      0.55f / std::max(1.f, ob.stone.irregularity), min, max, lo, ld, lh);
        break;
    }
    case ObjType::Disc: case ObjType::Square: ok = hitFlat(lo, ld, ob.type == ObjType::Disc, lh); break;
    case ObjType::Mesh: {
        float t;
        V3 n;
        if (ob.mesh && ob.mesh->intersect(lo, ld, EPS, tmax, t, n, ob.flatMesh)) { lh = {t, n}; ok = true; }
        break;
    }
    default: ok = hitPlane(lo, ld, lh); break;
    }
    if (!ok) return false;
    fillHit(ob, r, lh.t, lh.n, h);
    return true;
}

// Boolean group: (union of positives) ∩ (all intersects) minus (union of negatives).
static bool intersectCsg(const Scene& s, const std::vector<int>& g, const Ray& r, float tmax, Hit& h, int& obj) {
    struct Ev { float t; V3 n; int m; bool enter; };
    Ev ev[64];
    int ne = 0, nInter = 0, firstPos = -1;
    for (int k = 0; k < (int)g.size() && k < 32; k++) {
        const Object& ob = s.objs[g[k]];
        if (ob.boolMode == BoolMode::Intersect) nInter++;
        if (ob.boolMode == BoolMode::Positive && firstPos < 0) firstPos = k;
        V3 lo = ob.inv * (r.o - ob.pos), ld = ob.inv * r.d;
        float tn, tf;
        V3 nn, nf;
        if (!convexSpan(ob.type, lo, ld, tn, tf, nn, nf)) continue;
        ev[ne++] = {tn, nn, k, true};
        ev[ne++] = {tf, nf, k, false};
    }
    if (firstPos < 0 || ne == 0) return false;
    std::sort(ev, ev + ne, [](const Ev& a, const Ev& b) { return a.t < b.t; });
    bool in[32] = {};
    auto inside = [&] {
        int pos = 0, inter = 0, neg = 0;
        for (int k = 0; k < (int)g.size() && k < 32; k++) {
            if (!in[k]) continue;
            BoolMode b = s.objs[g[k]].boolMode;
            if (b == BoolMode::Positive) pos++;
            else if (b == BoolMode::Negative) neg++;
            else inter++;
        }
        return pos > 0 && inter == nInter && neg == 0;
    };
    bool state = false;
    for (int e = 0; e < ne; e++) {
        in[ev[e].m] = ev[e].enter;
        bool now = inside();
        if (now != state && ev[e].t > EPS) {
            if (ev[e].t >= tmax) return false;
            const Object& surf = s.objs[g[ev[e].m]];
            int mi = surf.boolMode == BoolMode::Negative ? firstPos : ev[e].m;
            fillHit(s.objs[g[mi]], r, ev[e].t, V3(0, 1, 0), h);
            V3 n = normalize(surf.nrm * ev[e].n);
            if ((dot(n, r.d) < 0) != now) n = -n;
            h.n = n;
            obj = g[mi];
            return true;
        }
        state = now;
    }
    return false;
}

struct FieldRay {
    int obj;
    double center, peak, curvature, strength;

    double kernel(double t) const {
        double d = t - center;
        double q = std::max(0.0, peak - curvature * d * d);
        return q * q;
    }
};

static double fieldAt(const std::vector<FieldRay>& parts, double t) {
    double value = -0.25;
    for (const auto& p : parts) value += p.strength * p.kernel(t);
    return value;
}

// Each kernel is monotone on either side of its closest point on the ray.
// Bound the sum on an interval so thin bridges and cavities need no fixed sampling grid.
static bool fieldCrossing(const std::vector<FieldRay>& parts, double a, double b, double fa, double fb, double& t) {
    double lo = -0.25, hi = -0.25;
    for (const auto& p : parts) {
        double v0 = p.strength * p.kernel(a), v1 = p.strength * p.kernel(b);
        double v2 = p.strength * p.kernel(std::clamp(p.center, a, b));
        lo += std::min({v0, v1, v2});
        hi += std::max({v0, v1, v2});
    }
    if (lo > 0 || hi <= 0) return false;
    double mid = (a + b) * 0.5;
    if (b - a < 0.00005 || mid == a || mid == b) {
        if ((fa > 0) == (fb > 0)) return false;
        t = mid;
        return true;
    }
    double fm = fieldAt(parts, mid);
    return fieldCrossing(parts, a, mid, fa, fm, t) || fieldCrossing(parts, mid, b, fm, fb, t);
}

static bool intersectMetaballs(const Scene& s, const MetaballGroup& g, const Ray& r, float tmax, Hit& h, int skip) {
    for (int i : g.balls) if (i == skip) return false;
    for (int i : g.operands) if (i == skip) return false;
    if (g.balls.size() == 1 && g.operands.empty()) {
        h.obj = g.balls.front();
        return intersectObj(s.objs[h.obj], r, tmax, h);
    }

    std::vector<FieldRay> parts;
    double start = tmax, end = EPS;
    int material = -1;
    for (int i : g.balls) {
        const Object& o = s.objs[i];
        if (!o.metaNegative && material < 0) material = i;
        if (sphereReject(o, r, tmax)) continue;
        V3 a = o.inv * (r.o - o.pos) / o.metaRadius, d = o.inv * r.d / o.metaRadius;
        double curvature = dot(d, d);
        if (curvature <= 0) continue;
        double center = -dot(a, d) / curvature;
        V3 closest = a + d * (float)center;
        double peak = 1 - dot(closest, closest);
        if (peak <= 0) continue;
        double span = std::sqrt(peak / curvature);
        if (center + span <= EPS || center - span >= tmax) continue;
        parts.push_back({i, center, peak, curvature, o.metaNegative ? -o.metaStrength : o.metaStrength});
        if (!o.metaNegative) {
            start = std::min(start, center - span);
            end = std::max(end, center + span);
        }
    }

    struct Event { float t; int obj; int delta; V3 normal; };
    std::vector<Event> events;
    int positive = 0, negative = 0, insideMasks = 0, masks = 0;
    auto count = [&](BoolMode mode) -> int& {
        if (mode == BoolMode::Positive) return positive;
        if (mode == BoolMode::Negative) return negative;
        return insideMasks;
    };
    for (int i : g.operands) {
        const Object& o = s.objs[i];
        if (o.boolMode == BoolMode::Positive && material < 0) material = i;
        if (o.boolMode == BoolMode::Intersect) masks++;
        float tn, tf;
        V3 nn, nf;
        if (sphereReject(o, r, tmax) || !convexSpan(o.type, o.inv * (r.o - o.pos), o.inv * r.d, tn, tf, nn, nf)) continue;
        if (tf <= EPS || tn >= tmax || tn >= tf) continue;
        if (tn <= EPS) count(o.boolMode)++;
        else events.push_back({tn, i, 1, nn});
        if (tf < tmax) events.push_back({tf, i, -1, nf});
    }
    if (material < 0) return false;
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
    auto allowed = [&] { return negative == 0 && insideMasks == masks; };
    auto ballSurface = [&](double a, double b) {
        a = std::max(a, start);
        b = std::min(b, end);
        double t;
        if (!allowed() || positive || a >= b || !fieldCrossing(parts, a, b, fieldAt(parts, a), fieldAt(parts, b), t)) return false;
        V3 point = r.o + r.d * (float)t, n;
        double strongest = 0;
        int contributor = material;
        for (const auto& p : parts) {
            const Object& o = s.objs[p.obj];
            V3 local = o.inv * (point - o.pos) / o.metaRadius;
            float q = std::max(0.f, 1 - dot(local, local));
            n += o.nrm * local * (q * (float)p.strength / o.metaRadius);
            double weight = q * q * p.strength;
            if (weight > strongest) { strongest = weight; contributor = p.obj; }
        }
        fillHit(s.objs[contributor], r, (float)t, {}, h);
        h.n = normalize(n);
        h.obj = contributor;
        return true;
    };
    float from = EPS;
    for (size_t i = 0; i < events.size();) {
        float t = events[i].t;
        if (ballSurface(from, t)) return true;
        bool ball = t > start && t < end && fieldAt(parts, t) > 0;
        auto inside = [&] { return allowed() && (positive > 0 || ball); };
        bool before = inside();
        const Event* surface = &events[i];
        do {
            const Event& e = events[i++];
            bool old = inside();
            count(s.objs[e.obj].boolMode) += e.delta;
            if (inside() != old) surface = &e;
        } while (i < events.size() && events[i].t == t);
        if (inside() != before) {
            const Object& o = s.objs[surface->obj];
            h.obj = o.boolMode == BoolMode::Negative ? material : surface->obj;
            fillHit(s.objs[h.obj], r, t, {}, h);
            h.n = normalize(o.nrm * surface->normal);
            if ((dot(h.n, r.d) < 0) != inside()) h.n = -h.n;
            return true;
        }
        from = t;
    }
    return ballSurface(from, tmax);
}

bool intersectObject(const Object& o, const Ray& r, float tmax, Hit& h) { return intersectObj(o, r, tmax, h); }

bool intersectScene(const Scene& s, const Ray& r, float tmax, Hit& h, int skip, RayPurpose purpose) {
    bool any = false;
    for (size_t i = 0; i < s.objs.size(); ++i) {
        const Object& o = s.objs[i];
        if (o.type == ObjType::Picture && ((purpose == RayPurpose::Shadow && !o.picture.shadows) ||
                                           (purpose == RayPurpose::Reflection && !o.picture.reflections))) continue;
        Hit t;
        if ((int)i != skip && !o.hidden && !o.csg && intersectObj(o, r, tmax, t)) {
            tmax = t.t;
            h = t;
            h.obj = (int)i;
            any = true;
        }
    }
    for (const auto& g : s.csg) {
        bool reject = true;
        for (int i : g) {
            if (i == skip) { reject = true; break; }
            if (!sphereReject(s.objs[i], r, tmax)) reject = false;
        }
        Hit t;
        int obj = -1;
        if (!reject && intersectCsg(s, g, r, tmax, t, obj)) {
            tmax = t.t;
            h = t;
            h.obj = obj;
            any = true;
        }
    }
    for (const auto& g : s.metaballs) {
        Hit t;
        if (intersectMetaballs(s, g, r, tmax, t, skip)) {
            tmax = t.t;
            h = t;
            any = true;
        }
    }
    return any;
}

// ---------------------------------------------------------------- wireframes

void wireframe(const Object& o, std::vector<V3>& s) {
    auto L = [&](V3 a, V3 b) {
        if (o.type == ObjType::Stone && o.stone.blockiness > 0) {
            auto shape = [&](V3 p) {
                float maxAxis = maxc(V3(std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)));
                return p * (0.9f / lerpf(0.9f, maxAxis, o.stone.blockiness));
            };
            a = shape(a); b = shape(b);
        }
        s.push_back(o.pos + o.fwd * a);
        s.push_back(o.pos + o.fwd * b);
    };
    auto ring = [&](float y, float r, int segs, float start = 0.f, float sweep = 2 * PI) {
        for (int i = 0; i < segs; i++) {
            float a0 = start + sweep * i / segs, a1 = start + sweep * (i + 1) / segs;
            L(V3(std::cos(a0) * r, y, std::sin(a0) * r), V3(std::cos(a1) * r, y, std::sin(a1) * r));
        }
    };
    auto sphere = [&](float r, int lat, int mer) {
        for (int la = -lat; la <= lat; la++) ring(std::sin(la * PI / (2 * lat + 2)) * r, std::cos(la * PI / (2 * lat + 2)) * r, 24);
        for (int m = 0; m < mer; m++) {
            float a = m * PI / mer;
            for (int i = 0; i < 16; i++) {
                float t0 = -PI / 2 + PI * i / 16, t1 = -PI / 2 + PI * (i + 1) / 16;
                L(V3(std::cos(t0) * std::cos(a), std::sin(t0), std::cos(t0) * std::sin(a)) * r, V3(std::cos(t1) * std::cos(a), std::sin(t1), std::cos(t1) * std::sin(a)) * r);
            }
        }
    };
    auto grid = [&](const Heightfield& f, float sign) {
        int step = std::max(1, (f.res - 1) / 48);
        float cs = 2.f / (f.res - 1);
        auto P = [&](int x, int z) { return V3(-1 + x * cs, f.at(x, z) * sign, -1 + z * cs); };
        auto E = [&](int x0, int z0, int x1, int z1) {
            if (f.at(x0, z0) > 1e-4f || f.at(x1, z1) > 1e-4f) L(P(x0, z0), P(x1, z1));
        };
        for (int a = 0; a < f.res; a += step)
            for (int b = 0; b + step < f.res; b += step) { E(b, a, b + step, a); E(a, b, a, b + step); }
        if (sign > 0) { L(V3(-1, 0, -1), V3(1, 0, -1)); L(V3(1, 0, -1), V3(1, 0, 1)); L(V3(1, 0, 1), V3(-1, 0, 1)); L(V3(-1, 0, 1), V3(-1, 0, -1)); }
    };
    switch (o.type) {
    case ObjType::Sphere: sphere(1, 2, 6); break;
    case ObjType::Metaball: sphere(o.metaRadius * std::sqrt(std::max(0.05f, 1 - std::sqrt(0.25f / o.metaStrength))), 2, 6); break;
    case ObjType::Picture: {
        V3 e = o.picture.halfExtent();
        V3 corners[] = {{-e.x, -e.y, 0}, {e.x, -e.y, 0}, {e.x, e.y, 0}, {-e.x, e.y, 0}};
        for (int i = 0; i < 4; i++) L(corners[i], corners[(i + 1) % 4]);
        L(corners[0], corners[2]); L(corners[1], corners[3]);
        break;
    }
    case ObjType::Stone: sphere(0.9f, 1, 4); break;
    case ObjType::Cube:
        for (int i = 0; i < 4; i++) {
            float x0 = (i & 1) ? 1.f : -1.f, z0 = (i & 2) ? 1.f : -1.f;
            L(V3(x0, -1, z0), V3(x0, 1, z0));
            L(V3(x0, -1, -1), V3(x0, -1, 1)); L(V3(x0, 1, -1), V3(x0, 1, 1));
            L(V3(-1, -1, z0), V3(1, -1, z0)); L(V3(-1, 1, z0), V3(1, 1, z0));
        }
        break;
    case ObjType::Cylinder:
        ring(-1, 1, 24); ring(1, 1, 24);
        for (int i = 0; i < 8; i++) { float a = i * PI / 4; L(V3(std::cos(a), -1, std::sin(a)), V3(std::cos(a), 1, std::sin(a))); }
        break;
    case ObjType::Cone:
        ring(-1, 1, 24);
        for (int i = 0; i < 8; i++) { float a = i * PI / 4; L(V3(std::cos(a), -1, std::sin(a)), V3(0, 1, 0)); }
        break;
    case ObjType::Pyramid: {
        V3 c[4] = {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}};
        for (int i = 0; i < 4; i++) { L(c[i], c[(i + 1) % 4]); L(c[i], V3(0, 1, 0)); }
        break;
    }
    case ObjType::Torus: {
        const TorusShape& t = o.torus;
        float start = rad(t.start), sweep = rad(t.sweep), h = t.minor * t.section;
        ring(0, t.major + t.minor, 32, start, sweep);
        ring(0, t.major - t.minor, 24, start, sweep);
        ring(h, t.major, 28, start, sweep); ring(-h, t.major, 28, start, sweep);
        int sections = t.sweep < 360 ? 13 : 12;
        for (int i = 0; i < sections; i++) {
            float a = start + sweep * i / 12;
            V3 c(std::cos(a), 0, std::sin(a));
            for (int k = 0; k < 12; k++) {
                float b0 = k * PI / 6, b1 = (k + 1) * PI / 6;
                L(c * (t.major + t.minor * std::cos(b0)) + V3(0, h * std::sin(b0), 0),
                  c * (t.major + t.minor * std::cos(b1)) + V3(0, h * std::sin(b1), 0));
            }
        }
        break;
    }
    case ObjType::Disc: ring(0, 1, 32); ring(0, 0.5f, 20); L(V3(-1, 0, 0), V3(1, 0, 0)); L(V3(0, 0, -1), V3(0, 0, 1)); break;
    case ObjType::Square:
        for (int k = -2; k <= 2; k++) { L(V3(k * 0.5f, 0, -1), V3(k * 0.5f, 0, 1)); L(V3(-1, 0, k * 0.5f), V3(1, 0, k * 0.5f)); }
        break;
    case ObjType::Terrain: if (o.hf && o.hf->res > 1) grid(*o.hf, 1); break;
    case ObjType::Lattice: if (o.hf && o.hf->res > 1) { grid(*o.hf, 1); grid(*o.hf, -1); } break;
    case ObjType::Mesh:
        if (o.mesh) {
            const Mesh& m = *o.mesh;
            int n = m.triCount(), step = std::max(1, n / 1500);
            for (int t = 0; t < n; t += step) {
                V3 a = m.v[m.tri[t * 3]], b = m.v[m.tri[t * 3 + 1]], c = m.v[m.tri[t * 3 + 2]];
                L(a, b); L(b, c); L(c, a);
            }
        }
        break;
    case ObjType::Light:
        for (int i = 0; i < 3; i++) {
            V3 a(i == 0 ? .8f : 0, i == 1 ? .8f : 0, i == 2 ? .8f : 0);
            s.push_back(o.pos - a); s.push_back(o.pos + a);
        }
        break;
    case ObjType::Spotlight: case ObjType::Parallel: {
        V3 ax = o.lightAxis(), t = std::fabs(ax.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0), u = normalize(cross(ax, t)), w = cross(ax, u);
        bool spot = o.type == ObjType::Spotlight;
        float len = spot ? 4.f : 5.f, rr = spot ? std::tan(rad(clampf(o.spotAngle, 1, 89))) * len : 1.f;
        for (int i = 0; i < 8; i++) {
            float a0 = 2 * PI * i / 8, a1 = 2 * PI * (i + 1) / 8;
            V3 p0 = (u * std::cos(a0) + w * std::sin(a0)) * rr, p1 = (u * std::cos(a1) + w * std::sin(a1)) * rr;
            V3 b0 = spot ? o.pos : o.pos + p0;
            s.push_back(b0); s.push_back(o.pos + ax * len + p0);
            s.push_back(o.pos + ax * len + p0); s.push_back(o.pos + ax * len + p1);
            if (!spot) { s.push_back(o.pos + p0); s.push_back(o.pos + p1); }
        }
        break;
    }
    default:
        for (int k = -6; k <= 6; k++) { L(V3(k * 10.f, 0, -60), V3(k * 10.f, 0, 60)); L(V3(-60, 0, k * 10.f), V3(60, 0, k * 10.f)); }
        break;
    }
}

// ---------------------------------------------------------------- factories

void regenTree(Object& o) {
    if (!o.treePart) return;
    Mesh trunk, leaves;
    makeTree(o.tree, trunk, leaves);
    o.mesh = std::make_shared<Mesh>(std::move(o.treePart == 1 ? trunk : leaves));
}

void addTree(Scene& s, const TreeParams& p, V3 pos, float size, const Material& trunk, const Material& foliage, int counter) {
    int g = s.maxGroup() + 1;
    for (int part = 1; part <= 2; part++) {
        Object o;
        o.type = ObjType::Mesh;
        o.name = "Tree " + std::to_string(counter) + (part == 1 ? " trunk" : " foliage");
        o.pos = pos;
        o.scl = V3(size);
        o.tree = p;
        o.treePart = part;
        o.group = g;
        o.mat = part == 1 ? trunk : foliage;
        regenTree(o);
        o.update();
        s.objs.push_back(o);
    }
}

Object makeObject(ObjType t, int n) {
    Object o;
    o.type = t;
    o.name = std::string(objTypeName(t)) + " " + std::to_string(n);
    o.seed = (uint32_t)n * 7919u + 17u;
    switch (t) {
    case ObjType::Sphere: case ObjType::Cube: case ObjType::Metaball: o.pos = {0, 2, 0}; o.scl = V3(2.f); break;
    case ObjType::Picture: o.pos = {0, 2, 0}; o.scl = V3(2.f); o.mat.specular = 0; break;
    case ObjType::Cylinder: o.pos = {0, 3, 0}; o.scl = {1.5f, 3, 1.5f}; break;
    case ObjType::Cone: o.pos = {0, 3, 0}; o.scl = {2, 3, 2}; break;
    case ObjType::Pyramid: o.pos = {0, 3, 0}; o.scl = V3(3.f); break;
    case ObjType::Torus: o.pos = {0, 1, 0}; o.scl = V3(3.f); break;
    case ObjType::Disc: case ObjType::Square: o.pos = {0, 0.05f, 0}; o.scl = V3(3.f); break;
    case ObjType::Stone: o.pos = {0, 1.4f, 0}; o.scl = {2, 1.5f, 2}; o.mat = findMaterial("Granite"); break;
    case ObjType::Terrain: case ObjType::Lattice:
        o.hf = std::make_shared<Heightfield>();
        if (t == ObjType::Terrain) {
            o.scl = {24, 8, 24};
            o.mat = findMaterial("Snowy Peaks");
            hfInit(*o.hf, 257);
            hfFractal(*o.hf, n * 7919u + 13u, 0.5f, false);
            hfErode(*o.hf, 60000, n);
        } else {
            o.pos = {0, 3, 0};
            o.scl = {6, 2.5f, 6};
            o.mat = findMaterial("Canyon Strata");
            hfInit(*o.hf, 129);
            hfFractal(*o.hf, n * 104729u + 7u, 0.55f, true);
        }
        break;
    case ObjType::Ground: o.mat = findMaterial("Earth"); break;
    case ObjType::Water: o.pos = {0, 1, 0}; o.mat = findMaterial("Water"); break;
    case ObjType::Cloud: o.pos = {0, 40, 0}; o.mat = findMaterial("Clouds"); break;
    case ObjType::Light: o.pos = {0, 10, 0}; break;
    case ObjType::Spotlight: o.pos = {0, 14, 0}; o.lightPower = 2; break;
    case ObjType::Parallel: o.pos = {0, 16, 0}; o.rot = {35, 0, 0}; o.lightPower = 0.6f; break;
    default: break;
    }
    o.update();
    return o;
}

Scene makeEmptyScene() {
    Scene s;
    s.sky = skyPresets()[0];
    s.objs.push_back(makeObject(ObjType::Ground, 1));
    s.cam = Camera();
    s.cam.target = {0, 2, 0};
    s.cam.dist = 30;
    s.cam.pitch = 15;
    s.updateAll();
    return s;
}

Scene makeDefaultScene() {
    Scene s;
    s.sky = skyPresets()[0];
    Object g = makeObject(ObjType::Ground, 1);
    g.mat = findMaterial("Desert Sand");
    s.objs.push_back(g);
    Object t = makeObject(ObjType::Terrain, 1);
    t.pos = {0, 0, -10};
    t.scl = {30, 9, 30};
    s.objs.push_back(t);
    Object w = makeObject(ObjType::Water, 1);
    w.pos = {0, 1.2f, 0};
    s.objs.push_back(w);
    Object sp = makeObject(ObjType::Sphere, 1);
    sp.mat = findMaterial("Chrome");
    sp.pos = {4, 4.5f, 14};
    sp.scl = V3(2.5f);
    s.objs.push_back(sp);
    s.updateAll();
    return s;
}

// ---------------------------------------------------------------- persistence

static std::string enc(const std::string& s) {
    std::string r = s.empty() ? "_" : s;
    for (char& c : r)
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') c = '_';
    return r;
}
static void wv(std::ostream& o, V3 v) { o << v.x << ' ' << v.y << ' ' << v.z << ' '; }
static void rv(std::istream& i, V3& v) { i >> v.x >> v.y >> v.z; }
static void ws(std::ostream& o, const std::string& s) { o << std::quoted(s) << ' '; }
static void rs(std::istream& i, std::string& s, int ver) {
    if (ver >= 2) { i >> std::quoted(s); return; }
    i >> s;
    for (char& c : s)
        if (c == '_') c = ' ';
}
template <class T> static void rb(std::istream& i, T& b) { int v = 0; i >> v; b = v != 0; }

static void writeCam(std::ostream& f, const Camera& c) { wv(f, c.target); f << c.yaw << ' ' << c.pitch << ' ' << c.dist << ' ' << c.fov << ' '; }
static void readCam(std::istream& f, Camera& c) { rv(f, c.target); f >> c.yaw >> c.pitch >> c.dist >> c.fov; }

static void writeSky(std::ostream& f, const Sky& k) {
    ws(f, k.name);
    wv(f, k.zenith); wv(f, k.horizon); wv(f, k.sunColor);
    f << k.sunAlt << ' ' << k.sunAz << ' ' << k.sunSize << ' ' << k.haze << ' ';
    wv(f, k.hazeColor);
    f << k.fog << ' ' << k.fogHeight << ' ';
    wv(f, k.fogColor);
    f << k.clouds << ' ' << k.cloudScale << ' ';
    wv(f, k.cloudColor);
    f << k.ambient << ' ';
    wv(f, k.ambientColor);
    f << k.stars << ' ' << (int)k.shadows << ' ';
    ws(f, k.category);
    f << k.mode << ' ' << (int)k.sunVisible << ' ' << k.stratus << ' ' << (int)k.moon << ' ' << k.moonAlt << ' ' << k.moonAz << ' '
      << k.moonPhase << ' ' << k.moonSize << ' ' << k.rainbow << ' ';
}
static void readSky(std::istream& f, Sky& k, int ver) {
    rs(f, k.name, ver);
    rv(f, k.zenith); rv(f, k.horizon); rv(f, k.sunColor);
    f >> k.sunAlt >> k.sunAz >> k.sunSize >> k.haze;
    rv(f, k.hazeColor);
    f >> k.fog >> k.fogHeight;
    rv(f, k.fogColor);
    f >> k.clouds >> k.cloudScale;
    rv(f, k.cloudColor);
    f >> k.ambient;
    rv(f, k.ambientColor);
    f >> k.stars;
    rb(f, k.shadows);
    if (ver < 2) return;
    rs(f, k.category, ver);
    f >> k.mode;
    rb(f, k.sunVisible);
    f >> k.stratus;
    rb(f, k.moon);
    f >> k.moonAlt >> k.moonAz >> k.moonPhase >> k.moonSize >> k.rainbow;
}

static void writeLayer(std::ostream& f, const TextureLayer& l) {
    f << (int)l.tex << ' ' << (int)l.map << ' ' << (int)l.blend << ' ' << (int)l.mask << ' ' << l.texScale << ' ' << l.opacity << ' ';
    wv(f, l.color1); wv(f, l.color2); wv(f, l.offset); wv(f, l.rotation);
    for (float value : l.influence) f << value << ' ';
    ws(f, l.image);
}

static bool readLayer(std::istream& f, TextureLayer& l) {
    int tex = 0, map = 0, blend = 0, mask = 0;
    if (!(f >> tex >> map >> blend >> mask >> l.texScale >> l.opacity)) return false;
    if (tex < 0 || tex >= (int)TexType::COUNT || map < 0 || map >= (int)TexMap::COUNT ||
        blend < 0 || blend >= (int)TexBlend::COUNT || mask < 0 || mask >= (int)TexMask::COUNT ||
        l.texScale < 0.01f || l.texScale > 20 || l.opacity < 0 || l.opacity > 1) return false;
    l.tex = (TexType)tex; l.map = (TexMap)map; l.blend = (TexBlend)blend; l.mask = (TexMask)mask;
    rv(f, l.color1); rv(f, l.color2); rv(f, l.offset); rv(f, l.rotation);
    for (float& value : l.influence)
        if (!(f >> value) || value < 0 || value > 1) return false;
    rs(f, l.image, 2);
    if (!f) return false;
    l.img.reset();
    return true;
}

bool TextureLayer::save(const std::string& path) const {
    std::ofstream f(path);
    f << std::setprecision(7) << "CANYONTEX 1\n";
    writeLayer(f, *this);
    f << '\n';
    return (bool)f;
}

bool TextureLayer::load(const std::string& path) {
    std::ifstream f(path);
    std::string header;
    int version = 0;
    TextureLayer layer;
    if (!(f >> header >> version) || header != "CANYONTEX" || version != 1 || !readLayer(f, layer)) return false;
    *this = std::move(layer);
    return true;
}

static void writeMat(std::ostream& f, const Material& m) {
    ws(f, m.name);
    f << (int)m.tex << ' ';
    wv(f, m.color1); wv(f, m.color2);
    f << m.diffuse << ' ' << m.ambient << ' ' << m.specular << ' ' << m.shininess << ' ' << m.reflect << ' ' << m.transparency << ' '
      << m.ior << ' ' << m.bump << ' ' << m.texScale << ' ' << m.metal << ' ';
    ws(f, m.category);
    f << (int)m.map << ' ' << m.emission << ' ' << m.blur << ' ';
    ws(f, m.image);
    f << m.layers.size() << ' ';
    for (const auto& layer : m.layers) writeLayer(f, layer);
}
static bool readMat(std::istream& f, Material& m, int ver) {
    int t = 0;
    rs(f, m.name, ver);
    f >> t;
    if (t < 0 || t >= (int)TexType::COUNT) return false;
    m.tex = (TexType)t;
    rv(f, m.color1); rv(f, m.color2);
    f >> m.diffuse >> m.ambient >> m.specular >> m.shininess >> m.reflect >> m.transparency >> m.ior >> m.bump >> m.texScale >> m.metal;
    if (ver < 2) return true;
    int mp = 0;
    rs(f, m.category, ver);
    f >> mp >> m.emission >> m.blur;
    if (mp < 0 || mp >= (int)TexMap::COUNT) return false;
    m.map = (TexMap)mp;
    rs(f, m.image, ver);
    m.img.reset();
    if (ver < 3) return (bool)f;
    int count = 0;
    if (!(f >> count) || count < 0 || count > MAX_TEXTURE_LAYERS) return false;
    m.layers.resize(count);
    for (auto& layer : m.layers)
        if (!readLayer(f, layer)) return false;
    return (bool)f;
}

bool Scene::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << std::setprecision(7) << "CANYON3D 3\ncamera ";
    writeCam(f, cam);
    f << "\nsky ";
    writeSky(f, sky);
    const RenderSettings& r = render;
    f << "\nrender " << r.width << ' ' << r.height << ' ' << r.quality << ' ' << r.samples << ' ' << r.dof << ' ' << r.panorama << ' '
      << r.softShadows << ' ' << r.blurReflections << ' ' << r.focusDist << ' ' << r.aperture << ' ' << r.gamma << ' ' << r.brightness
      << ' ' << r.contrast << ' ' << r.saturation << "\nanim " << frames << ' ' << fps << ' ' << frame << "\n";
    for (int i = 0; i < 6; i++)
        if (memorySet[i]) { f << "memory " << i << ' '; writeCam(f, memory[i]); f << "\n"; }
    for (auto& k : camKeys) { f << "camkey " << k.frame << ' '; writeCam(f, k.cam); f << "\n"; }
    for (auto& k : skyKeys) { f << "skykey " << k.frame << ' '; writeSky(f, k.sky); f << "\n"; }
    for (const Object& o : objs) {
        f << "object " << (int)o.type << ' ';
        ws(f, o.name);
        wv(f, o.pos); wv(f, o.rot); wv(f, o.scl); wv(f, o.lightColor);
        f << o.lightPower << ' ' << o.spotAngle << ' ' << o.spotSoft << ' ' << o.lightSize << ' ' << o.group << ' ' << (int)o.boolMode << ' '
          << o.hidden << ' ' << o.locked << ' ' << o.seed << ' ' << o.treePart << "\nmaterial ";
        writeMat(f, o.mat);
        f << "\n";
        if (o.type == ObjType::Metaball) f << "metaball " << o.metaRadius << ' ' << o.metaStrength << ' ' << o.metaNegative << "\n";
        if (o.type == ObjType::Picture) {
            const Picture& p = o.picture;
            f << "picture "; ws(f, p.path); wv(f, p.keyColor);
            f << p.aspect << ' ' << p.keyTolerance << ' ' << p.billboard << ' ' << p.chroma << ' ' << p.shadows << ' ' << p.reflections << "\n";
        }
        if (o.type == ObjType::Torus)
            f << "torus " << o.torus.major << ' ' << o.torus.minor << ' ' << o.torus.section << ' ' << o.torus.start << ' ' << o.torus.sweep << "\n";
        if (o.type == ObjType::Stone)
            f << "stone " << o.stone.irregularity << ' ' << o.stone.blockiness << ' ' << o.stone.smoothing << "\n";
        if (o.type == ObjType::Mesh && o.flatMesh) f << "flatmesh\n";
        if (o.treePart) {
            const TreeParams& t = o.tree;
            f << "tree " << t.height << ' ' << t.spread << ' ' << t.trunkRadius << ' ' << t.leafSize << ' ' << t.leafDensity << ' ' << t.levels
              << ' ' << t.branches << ' ' << t.seed << "\n";
        }
        for (auto& k : o.keys) { f << "key " << k.frame << ' '; wv(f, k.pos); wv(f, k.rot); wv(f, k.scl); f << "\n"; }
        if (isHeightfield(o.type) && o.hf) {
            f << "terrain " << o.hf->res << "\n";
            for (size_t i = 0; i < o.hf->h.size(); i++) f << o.hf->h[i] << ((i % 16 == 15) ? '\n' : ' ');
            f << "\n";
        }
        if (o.type == ObjType::Mesh && o.mesh && !o.treePart) {
            const Mesh& m = *o.mesh;
            f << "mesh " << m.v.size() << ' ' << m.triCount() << "\n";
            for (size_t i = 0; i < m.v.size(); i++) { wv(f, m.v[i]); wv(f, m.n[i]); f << "\n"; }
            for (size_t i = 0; i < m.tri.size(); i++) f << m.tri[i] << ((i % 3 == 2) ? '\n' : ' ');
        }
    }
    f << "end\n";
    return (bool)f;
}

bool Scene::load(const std::string& path) {
    std::ifstream f(path);
    std::string tok;
    int ver = 0;
    if (!(f >> tok >> ver) || tok != "CANYON3D" || ver < 1 || ver > 3) return false;
    Scene s;
    s.sky = skyPresets()[0];
    while (f >> tok) {
        if (tok == "camera") readCam(f, s.cam);
        else if (tok == "sky") readSky(f, s.sky, ver);
        else if (tok == "render") {
            RenderSettings& r = s.render;
            f >> r.width >> r.height >> r.quality >> r.samples;
            rb(f, r.dof); rb(f, r.panorama); rb(f, r.softShadows); rb(f, r.blurReflections);
            f >> r.focusDist >> r.aperture >> r.gamma >> r.brightness >> r.contrast >> r.saturation;
            r.width = std::min(std::max(r.width, 0), 8192); r.height = std::min(std::max(r.height, 0), 8192);
            r.quality = std::min(std::max(r.quality, 0), 2); r.samples = std::min(std::max(r.samples, 1), 256);
        } else if (tok == "anim") {
            f >> s.frames >> s.fps >> s.frame;
            s.frames = std::min(std::max(s.frames, 2), 100000); s.fps = std::min(std::max(s.fps, 1), 120);
            s.frame = std::min(std::max(s.frame, 0), s.frames - 1);
        } else if (tok == "memory") {
            int i = -1;
            f >> i;
            if (i < 0 || i >= 6) return false;
            readCam(f, s.memory[i]);
            s.memorySet[i] = true;
        } else if (tok == "camkey") {
            CamKey k;
            f >> k.frame;
            readCam(f, k.cam);
            putKey(s.camKeys, k);
        } else if (tok == "skykey") {
            SkyKey k;
            f >> k.frame;
            readSky(f, k.sky, ver);
            putKey(s.skyKeys, k);
        } else if (tok == "object") {
            Object o;
            int t = 0;
            f >> t;
            if (t < 0 || t >= (int)(ver < 3 ? ObjType::Metaball : ObjType::COUNT)) return false;
            o.type = (ObjType)t;
            rs(f, o.name, ver);
            rv(f, o.pos); rv(f, o.rot); rv(f, o.scl); rv(f, o.lightColor);
            f >> o.lightPower;
            if (ver >= 2) {
                int b = 0;
                f >> o.spotAngle >> o.spotSoft >> o.lightSize >> o.group >> b;
                if (b < 0 || b >= (int)BoolMode::COUNT) return false;
                o.boolMode = (BoolMode)b;
                rb(f, o.hidden); rb(f, o.locked);
                f >> o.seed >> o.treePart;
                if (o.treePart < 0 || o.treePart > 2 || (o.treePart && o.type != ObjType::Mesh)) return false;
            }
            s.objs.push_back(o);
        } else if (tok == "material") {
            if (s.objs.empty() || !readMat(f, s.objs.back().mat, ver)) return false;
        } else if (tok == "metaball") {
            if (ver < 3 || s.objs.empty() || s.objs.back().type != ObjType::Metaball) return false;
            Object& o = s.objs.back();
            f >> o.metaRadius >> o.metaStrength;
            rb(f, o.metaNegative);
            if (o.metaRadius < 0.2f || o.metaRadius > 4 || o.metaStrength < 0.1f || o.metaStrength > 4) return false;
        } else if (tok == "picture") {
            if (ver < 3 || s.objs.empty() || s.objs.back().type != ObjType::Picture) return false;
            Picture& p = s.objs.back().picture;
            rs(f, p.path, ver); rv(f, p.keyColor);
            f >> p.aspect >> p.keyTolerance;
            rb(f, p.billboard); rb(f, p.chroma); rb(f, p.shadows); rb(f, p.reflections);
            if (p.aspect < 1.f / 16385 || p.aspect > 16385 || p.keyTolerance < 0 || p.keyTolerance > 1) return false;
        } else if (tok == "torus") {
            if (ver < 3 || s.objs.empty() || s.objs.back().type != ObjType::Torus) return false;
            TorusShape& t = s.objs.back().torus;
            f >> t.major >> t.minor >> t.section >> t.start >> t.sweep;
            if (t.major < 0.1f || t.major > 3 || t.minor < 0.04f || t.minor >= t.major || t.section < 0.2f || t.section > 3 ||
                t.start < 0 || t.start > 360 || t.sweep < 5 || t.sweep > 360) return false;
        } else if (tok == "stone") {
            if (ver < 3 || s.objs.empty() || s.objs.back().type != ObjType::Stone) return false;
            StoneShape& shape = s.objs.back().stone;
            f >> shape.irregularity >> shape.blockiness >> shape.smoothing;
            if (shape.irregularity < 0 || shape.irregularity > 2 || shape.blockiness < 0 || shape.blockiness > 1 ||
                shape.smoothing < 0 || shape.smoothing > 1) return false;
        } else if (tok == "flatmesh") {
            if (ver < 3 || s.objs.empty() || s.objs.back().type != ObjType::Mesh) return false;
            s.objs.back().flatMesh = true;
        } else if (tok == "tree") {
            if (s.objs.empty()) return false;
            TreeParams& t = s.objs.back().tree;
            f >> t.height >> t.spread >> t.trunkRadius >> t.leafSize >> t.leafDensity >> t.levels >> t.branches >> t.seed;
            t.levels = std::min(std::max(t.levels, 1), 6); t.branches = std::min(std::max(t.branches, 1), 8);
            t.leafDensity = clampf(t.leafDensity, 0, 4);
        } else if (tok == "key") {
            if (s.objs.empty()) return false;
            Key k;
            f >> k.frame;
            rv(f, k.pos); rv(f, k.rot); rv(f, k.scl);
            putKey(s.objs.back().keys, k);
        } else if (tok == "terrain") {
            int res = 0;
            f >> res;
            if (res < 2 || res > 2049 || s.objs.empty()) return false;
            auto hf = std::make_shared<Heightfield>();
            hf->res = res;
            hf->h.resize((size_t)res * res);
            for (float& v : hf->h) f >> v;
            hf->computeNormals();
            s.objs.back().hf = hf;
        } else if (tok == "mesh") {
            size_t nv = 0, nt = 0;
            f >> nv >> nt;
            if (s.objs.empty() || nv == 0 || nv > 20000000 || nt > 20000000) return false;
            auto m = std::make_shared<Mesh>();
            m->v.resize(nv);
            m->n.resize(nv);
            for (size_t i = 0; i < nv && f; i++) { rv(f, m->v[i]); rv(f, m->n[i]); }
            m->tri.resize(nt * 3);
            for (auto& x : m->tri) {
                f >> x;
                if (x >= nv) return false;
            }
            m->build();
            s.objs.back().mesh = m;
        } else if (tok == "end") break;
        else return false;
        if (!f) return false;
    }
    for (Object& o : s.objs) {
        if (isHeightfield(o.type) && !o.hf) { o.hf = std::make_shared<Heightfield>(); hfInit(*o.hf, 129); }
        if (o.treePart) regenTree(o);
    }
    s.updateAll();
    *this = std::move(s);
    return true;
}

bool Scene::merge(const std::string& path) {
    Scene o;
    if (!o.load(path)) return false;
    int g = maxGroup();
    for (Object& x : o.objs) {
        if (x.group > 0) x.group += g;
        objs.push_back(std::move(x));
    }
    updateAll();
    return true;
}

bool Library::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << std::setprecision(7) << "CANYONLIB 3\n";
    for (auto& m : mats) { f << "material "; writeMat(f, m); f << "\n"; }
    for (auto& k : skies) { f << "sky "; writeSky(f, k); f << "\n"; }
    return (bool)f;
}

bool Library::load(const std::string& path) {
    std::ifstream f(path);
    std::string tok;
    int ver = 0;
    if (!(f >> tok >> ver) || tok != "CANYONLIB" || ver < 2 || ver > 3) return false;
    Library l;
    while (f >> tok) {
        if (tok == "material") {
            Material m;
            if (!readMat(f, m, ver)) return false;
            l.mats.push_back(m);
        } else if (tok == "sky") {
            Sky k;
            readSky(f, k, ver);
            l.skies.push_back(k);
        } else return false;
        if (!f) return false;
    }
    *this = std::move(l);
    return true;
}
