#include "scene.h"

#include <fstream>
#include <initializer_list>

#include "terrain.h"

static const float EPS = 2e-3f;

const char* texName(TexType t) {
    static const char* n[] = {"None", "Checker", "Marble", "Wood", "Granite", "Noise", "Clouds", "Waves", "Strata", "Altitude"};
    return n[(int)t];
}

const char* objTypeName(ObjType t) {
    static const char* n[] = {"Sphere", "Cube", "Cylinder", "Cone", "Pyramid", "Terrain", "Ground", "Water", "Cloud", "Light"};
    return n[(int)t];
}

static Material M(const char* n, TexType t, V3 a, V3 b, float diff, float amb, float spec, float shin, float refl, float trans,
                  float ior, float bump, float scale, float metal) {
    Material m;
    m.name = n; m.tex = t; m.color1 = a; m.color2 = b; m.diffuse = diff; m.ambient = amb; m.specular = spec; m.shininess = shin;
    m.reflect = refl; m.transparency = trans; m.ior = ior; m.bump = bump; m.texScale = scale; m.metal = metal;
    return m;
}

const std::vector<Material>& materialPresets() {
    using T = TexType;
    static const std::vector<Material> p = {
        M("Default Gray", T::None, {.72f, .72f, .72f}, {.3f, .3f, .3f}, .85f, .3f, .25f, 30, 0, 0, 1, 0, 1, 0),
        M("Chrome", T::None, {.9f, .9f, .95f}, {0, 0, 0}, .08f, .1f, 1, 150, .9f, 0, 1, 0, 1, 0),
        M("Gold", T::None, {1, .76f, .3f}, {0, 0, 0}, .3f, .15f, .9f, 90, .65f, 0, 1, 0, 1, 1),
        M("Glass", T::None, {.95f, .98f, 1}, {0, 0, 0}, .02f, .05f, 1, 200, .05f, .92f, 1.5f, 0, 1, 0),
        M("Red Plastic", T::None, {.8f, .08f, .06f}, {0, 0, 0}, .85f, .3f, .6f, 60, .05f, 0, 1, 0, 1, 0),
        M("Water", T::Waves, {.12f, .32f, .42f}, {0, 0, 0}, .25f, .3f, .9f, 120, .35f, .55f, 1.33f, .5f, .6f, 0),
        M("Snowy Peaks", T::Altitude, {.96f, .97f, 1}, {.45f, .38f, .32f}, .9f, .3f, .1f, 10, 0, 0, 1, .6f, 3, 0),
        M("Canyon Strata", T::Strata, {.86f, .55f, .34f}, {.58f, .28f, .17f}, .9f, .3f, .05f, 10, 0, 0, 1, .5f, 1, 0),
        M("Desert Sand", T::Noise, {.93f, .8f, .56f}, {.78f, .62f, .4f}, .9f, .35f, .05f, 10, 0, 0, 1, .25f, .6f, 0),
        M("Grass", T::Noise, {.32f, .5f, .16f}, {.18f, .32f, .09f}, .9f, .3f, .05f, 10, 0, 0, 1, .3f, .8f, 0),
        M("Earth", T::Noise, {.5f, .43f, .34f}, {.33f, .28f, .22f}, .9f, .3f, .05f, 10, 0, 0, 1, .2f, .4f, 0),
        M("Granite", T::Granite, {.62f, .6f, .57f}, {.27f, .26f, .24f}, .85f, .3f, .3f, 40, 0, 0, 1, .3f, 1.5f, 0),
        M("Marble", T::Marble, {.95f, .94f, .92f}, {.3f, .32f, .38f}, .8f, .3f, .7f, 80, .12f, 0, 1, 0, .8f, 0),
        M("Wood", T::Wood, {.78f, .52f, .27f}, {.45f, .25f, .1f}, .85f, .3f, .3f, 30, 0, 0, 1, .1f, 1, 0),
        M("Checker", T::Checker, {.9f, .9f, .9f}, {.12f, .12f, .12f}, .85f, .3f, .2f, 30, 0, 0, 1, 0, 1, 0),
        M("Clouds", T::Clouds, {1, 1, 1}, {.72f, .75f, .8f}, .8f, .7f, 0, 10, 0, 0, 1, 0, .035f, 0),
    };
    return p;
}

Material findMaterial(const std::string& name) {
    for (auto& m : materialPresets())
        if (m.name == name) return m;
    return Material();
}

static Sky S(const char* n, V3 zen, V3 hor, V3 sun, float alt, float az, float haze, V3 hazeC, float fog, float fogH, V3 fogC,
             float clouds, V3 cloudC, float amb, V3 ambC, float stars, bool shadows) {
    Sky s;
    s.name = n; s.zenith = zen; s.horizon = hor; s.sunColor = sun; s.sunAlt = alt; s.sunAz = az; s.haze = haze; s.hazeColor = hazeC;
    s.fog = fog; s.fogHeight = fogH; s.fogColor = fogC; s.clouds = clouds; s.cloudColor = cloudC; s.ambient = amb; s.ambientColor = ambC;
    s.stars = stars; s.shadows = shadows;
    return s;
}

const std::vector<Sky>& skyPresets() {
    static const std::vector<Sky> p = {
        S("Clear Day", {.2f, .4f, .85f}, {.72f, .8f, .95f}, {1, .97f, .9f}, 45, 135, .15f, {.75f, .82f, .92f}, 0, 3, {.85f, .85f, .88f},
          .25f, {1, 1, 1}, .5f, {.55f, .65f, .85f}, 0, true),
        S("Sunset", {.15f, .2f, .45f}, {1, .55f, .25f}, {1, .6f, .3f}, 6, 200, .35f, {.95f, .6f, .4f}, 0, 3, {.9f, .7f, .6f},
          .35f, {1, .7f, .5f}, .35f, {.6f, .45f, .5f}, 0, true),
        S("Overcast", {.55f, .58f, .62f}, {.75f, .77f, .8f}, {.6f, .6f, .6f}, 60, 135, .3f, {.75f, .77f, .8f}, 0, 3, {.8f, .8f, .82f},
          .95f, {.85f, .86f, .88f}, .8f, {.7f, .72f, .75f}, 0, false),
        S("Night", {.01f, .02f, .08f}, {.08f, .1f, .25f}, {.5f, .55f, .8f}, 35, 100, .1f, {.1f, .12f, .25f}, 0, 3, {.2f, .2f, .3f},
          .1f, {.3f, .32f, .4f}, .3f, {.15f, .2f, .4f}, 1, true),
        S("Misty Morning", {.5f, .6f, .75f}, {.85f, .85f, .85f}, {1, .9f, .75f}, 15, 110, .4f, {.85f, .85f, .87f}, .5f, 4, {.85f, .85f, .88f},
          .2f, {1, .95f, .9f}, .55f, {.7f, .72f, .78f}, 0, true),
        S("Alien", {.1f, .3f, .15f}, {.8f, .9f, .4f}, {.9f, 1, .6f}, 25, 160, .3f, {.6f, .8f, .3f}, 0, 3, {.6f, .8f, .4f},
          .4f, {.9f, 1, .7f}, .45f, {.5f, .7f, .4f}, 0, true),
    };
    return p;
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

void Object::update() {
    fwd = rotY(rad(rot.y)) * rotX(rad(rot.x)) * rotZ(rad(rot.z)) * scaleM(scl);
    inv = fwd.inverse();
    nrm = inv.transpose();
    V3 lc(0.f);
    float lr;
    switch (type) {
    case ObjType::Sphere: lr = 1; break;
    case ObjType::Cube: case ObjType::Pyramid: lr = 1.7321f; break;
    case ObjType::Cylinder: case ObjType::Cone: lr = 1.4143f; break;
    case ObjType::Terrain: lc = V3(0, .5f, 0); lr = 1.5f; break;
    default: bradius = -1; return;
    }
    float ms = std::max(length(fwd.col(0)), std::max(length(fwd.col(1)), length(fwd.col(2))));
    bcenter = pos + fwd * lc;
    bradius = lr * ms * 1.001f;
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

// ---------------------------------------------------------------- intersection

struct LHit { float t; V3 n; };

static inline void consider(float t, V3 n, LHit& b) {
    if (t > EPS && t < b.t) { b.t = t; b.n = n; }
}

static bool hitSphere(V3 o, V3 d, LHit& h) {
    float a = dot(d, d), b = dot(o, d), c = dot(o, o) - 1, disc = b * b - a * c;
    if (disc < 0) return false;
    float sq = std::sqrt(disc), t = (-b - sq) / a;
    if (t <= EPS) t = (-b + sq) / a;
    if (t <= EPS || t >= h.t) return false;
    h.t = t;
    h.n = o + d * t;
    return true;
}

static bool hitConvex(const V3* pn, const float* pd, int cnt, V3 o, V3 d, LHit& h) {
    float tn = -1e30f, tf = 1e30f;
    V3 nn, nf;
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
    if (tn > tf) return false;
    if (tn > EPS) {
        if (tn >= h.t) return false;
        h.t = tn; h.n = nn;
        return true;
    }
    if (tf > EPS && tf < h.t) { h.t = tf; h.n = nf; return true; }
    return false;
}

static const V3 cubeN[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
static const float cubeD[6] = {1, 1, 1, 1, 1, 1};
static const float s5 = 0.4472136f;
static const V3 pyrN[5] = {{0, -1, 0}, {2 * s5, s5, 0}, {-2 * s5, s5, 0}, {0, s5, 2 * s5}, {0, s5, -2 * s5}};
static const float pyrD[5] = {1, s5, s5, s5, s5};

static bool hitCylinder(V3 o, V3 d, LHit& h) {
    LHit b = h;
    float a = d.x * d.x + d.z * d.z;
    if (a > 1e-12f) {
        float bb = o.x * d.x + o.z * d.z, c = o.x * o.x + o.z * o.z - 1, disc = bb * bb - a * c;
        if (disc >= 0) {
            float sq = std::sqrt(disc);
            for (float t : {(-bb - sq) / a, (-bb + sq) / a}) {
                float y = o.y + d.y * t;
                if (y >= -1 && y <= 1) consider(t, V3(o.x + d.x * t, 0, o.z + d.z * t), b);
            }
        }
    }
    if (std::fabs(d.y) > 1e-12f)
        for (float cy : {-1.f, 1.f}) {
            float t = (cy - o.y) / d.y, x = o.x + d.x * t, z = o.z + d.z * t;
            if (x * x + z * z <= 1) consider(t, V3(0, cy, 0), b);
        }
    if (b.t < h.t) { h = b; return true; }
    return false;
}

static bool hitCone(V3 o, V3 d, LHit& h) {
    LHit b = h;
    float k = 1 - o.y;
    float a = d.x * d.x + d.z * d.z - d.y * d.y * 0.25f, bb = o.x * d.x + o.z * d.z + k * d.y * 0.25f, c = o.x * o.x + o.z * o.z - k * k * 0.25f;
    auto side = [&](float t) {
        V3 p = o + d * t;
        if (p.y >= -1 && p.y <= 1) consider(t, V3(2 * p.x, (1 - p.y) * 0.5f, 2 * p.z), b);
    };
    if (std::fabs(a) > 1e-9f) {
        float disc = bb * bb - a * c;
        if (disc >= 0) { float sq = std::sqrt(disc); side((-bb - sq) / a); side((-bb + sq) / a); }
    } else if (std::fabs(bb) > 1e-12f) side(-c / (2 * bb));
    if (std::fabs(d.y) > 1e-12f) {
        float t = (-1 - o.y) / d.y;
        V3 p = o + d * t;
        if (p.x * p.x + p.z * p.z <= 1) consider(t, V3(0, -1, 0), b);
    }
    if (b.t < h.t) { h = b; return true; }
    return false;
}

static bool hitPlane(V3 o, V3 d, LHit& h) {
    if (std::fabs(d.y) < 1e-12f) return false;
    float t = -o.y / d.y;
    if (t <= EPS || t >= h.t) return false;
    h.t = t; h.n = V3(0, 1, 0);
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
    const V3 bmin(-1, 0, -1), bmax(1, 1, 1);
    for (int i = 0; i < 3; i++) {
        float oi = o[i], di = d[i];
        if (std::fabs(di) < 1e-12f) {
            if (oi < bmin[i] || oi > bmax[i]) return false;
            continue;
        }
        float ta = (bmin[i] - oi) / di, tb = (bmax[i] - oi) / di;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
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
        if (std::min(y0, y1) <= std::max(std::max(h00, h10), std::max(h01, h11)) + 1e-4f) {
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

static bool intersectObj(const Object& ob, const Ray& r, float tmax, Hit& h) {
    if (ob.type == ObjType::Light) return false;
    if (ob.bradius > 0) {
        V3 oc = r.o - ob.bcenter;
        float b = dot(oc, r.d), c = dot(oc, oc) - ob.bradius * ob.bradius;
        if (c > 0 && b > 0) return false;
        float disc = b * b - c;
        if (disc < 0 || -b - std::sqrt(disc) > tmax) return false;
    }
    V3 lo = ob.inv * (r.o - ob.pos), ld = ob.inv * r.d;
    LHit lh{tmax, {}};
    bool ok = false;
    switch (ob.type) {
    case ObjType::Sphere: ok = hitSphere(lo, ld, lh); break;
    case ObjType::Cube: ok = hitConvex(cubeN, cubeD, 6, lo, ld, lh); break;
    case ObjType::Pyramid: ok = hitConvex(pyrN, pyrD, 5, lo, ld, lh); break;
    case ObjType::Cylinder: ok = hitCylinder(lo, ld, lh); break;
    case ObjType::Cone: ok = hitCone(lo, ld, lh); break;
    case ObjType::Terrain: ok = ob.hf && ob.hf->res > 1 && hitTerrain(*ob.hf, lo, ld, lh); break;
    default: ok = hitPlane(lo, ld, lh); break;
    }
    if (!ok) return false;
    V3 lp = lo + ld * lh.t;
    h.t = lh.t;
    h.p = r.o + r.d * lh.t;
    h.n = normalize(ob.nrm * lh.n);
    h.lp = lp * ob.scl;
    h.ly = lp.y;
    return true;
}

bool intersectScene(const Scene& s, const Ray& r, float tmax, Hit& h, int skip) {
    bool any = false;
    for (size_t i = 0; i < s.objs.size(); ++i) {
        Hit t;
        if ((int)i != skip && intersectObj(s.objs[i], r, tmax, t)) {
            tmax = t.t;
            h = t;
            h.obj = (int)i;
            any = true;
        }
    }
    return any;
}

// ---------------------------------------------------------------- wireframes

void wireframe(const Object& o, std::vector<V3>& s) {
    auto L = [&](V3 a, V3 b) { s.push_back(o.pos + o.fwd * a); s.push_back(o.pos + o.fwd * b); };
    auto ring = [&](float y, float r, int segs) {
        for (int i = 0; i < segs; i++) {
            float a0 = 2 * PI * i / segs, a1 = 2 * PI * (i + 1) / segs;
            L(V3(std::cos(a0) * r, y, std::sin(a0) * r), V3(std::cos(a1) * r, y, std::sin(a1) * r));
        }
    };
    switch (o.type) {
    case ObjType::Sphere:
        for (int la = -2; la <= 2; la++) ring(std::sin(la * PI / 6), std::cos(la * PI / 6), 24);
        for (int m = 0; m < 6; m++) {
            float a = m * PI / 6;
            for (int i = 0; i < 16; i++) {
                float t0 = -PI / 2 + PI * i / 16, t1 = -PI / 2 + PI * (i + 1) / 16;
                L(V3(std::cos(t0) * std::cos(a), std::sin(t0), std::cos(t0) * std::sin(a)), V3(std::cos(t1) * std::cos(a), std::sin(t1), std::cos(t1) * std::sin(a)));
            }
        }
        break;
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
    case ObjType::Terrain: {
        if (!o.hf || o.hf->res < 2) break;
        const Heightfield& f = *o.hf;
        int step = std::max(1, (f.res - 1) / 32);
        float cs = 2.f / (f.res - 1);
        auto P = [&](int x, int z) { return V3(-1 + x * cs, f.at(x, z), -1 + z * cs); };
        for (int a = 0; a < f.res; a += step)
            for (int b = 0; b + step < f.res; b += step) { L(P(b, a), P(b + step, a)); L(P(a, b), P(a, b + step)); }
        break;
    }
    case ObjType::Light:
        for (int i = 0; i < 3; i++) {
            V3 a(i == 0 ? .8f : 0, i == 1 ? .8f : 0, i == 2 ? .8f : 0);
            s.push_back(o.pos - a); s.push_back(o.pos + a);
        }
        break;
    default:
        for (int k = -6; k <= 6; k++) { L(V3(k * 10.f, 0, -60), V3(k * 10.f, 0, 60)); L(V3(-60, 0, k * 10.f), V3(60, 0, k * 10.f)); }
        break;
    }
}

// ---------------------------------------------------------------- factories

Object makeObject(ObjType t, int n) {
    Object o;
    o.type = t;
    o.name = std::string(objTypeName(t)) + " " + std::to_string(n);
    switch (t) {
    case ObjType::Sphere: case ObjType::Cube: o.pos = {0, 2, 0}; o.scl = V3(2.f); break;
    case ObjType::Cylinder: o.pos = {0, 3, 0}; o.scl = {1.5f, 3, 1.5f}; break;
    case ObjType::Cone: o.pos = {0, 3, 0}; o.scl = {2, 3, 2}; break;
    case ObjType::Pyramid: o.pos = {0, 3, 0}; o.scl = V3(3.f); break;
    case ObjType::Terrain:
        o.scl = {24, 8, 24};
        o.mat = findMaterial("Snowy Peaks");
        o.hf = std::make_shared<Heightfield>();
        hfInit(*o.hf, 257);
        hfFractal(*o.hf, n * 7919u + 13u, 0.5f, false);
        hfErode(*o.hf, 60000, n);
        break;
    case ObjType::Ground: o.mat = findMaterial("Earth"); break;
    case ObjType::Water: o.pos = {0, 1, 0}; o.mat = findMaterial("Water"); break;
    case ObjType::Cloud: o.pos = {0, 40, 0}; o.mat = findMaterial("Clouds"); break;
    case ObjType::Light: o.pos = {0, 10, 0}; break;
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
static std::string dec(std::string s) {
    for (char& c : s)
        if (c == '_') c = ' ';
    return s;
}
static void wv(std::ostream& o, V3 v) { o << v.x << ' ' << v.y << ' ' << v.z << ' '; }
static void rv(std::istream& i, V3& v) { i >> v.x >> v.y >> v.z; }

bool Scene::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "CANYON3D 1\ncamera ";
    wv(f, cam.target);
    f << cam.yaw << ' ' << cam.pitch << ' ' << cam.dist << ' ' << cam.fov << "\n";
    const Sky& k = sky;
    f << "sky " << enc(k.name) << ' ';
    wv(f, k.zenith); wv(f, k.horizon); wv(f, k.sunColor);
    f << k.sunAlt << ' ' << k.sunAz << ' ' << k.sunSize << ' ' << k.haze << ' ';
    wv(f, k.hazeColor);
    f << k.fog << ' ' << k.fogHeight << ' ';
    wv(f, k.fogColor);
    f << k.clouds << ' ' << k.cloudScale << ' ';
    wv(f, k.cloudColor);
    f << k.ambient << ' ';
    wv(f, k.ambientColor);
    f << k.stars << ' ' << (int)k.shadows << "\n";
    for (const Object& o : objs) {
        f << "object " << (int)o.type << ' ' << enc(o.name) << ' ';
        wv(f, o.pos); wv(f, o.rot); wv(f, o.scl); wv(f, o.lightColor);
        f << o.lightPower << "\n";
        const Material& m = o.mat;
        f << "material " << enc(m.name) << ' ' << (int)m.tex << ' ';
        wv(f, m.color1); wv(f, m.color2);
        f << m.diffuse << ' ' << m.ambient << ' ' << m.specular << ' ' << m.shininess << ' ' << m.reflect << ' ' << m.transparency << ' '
          << m.ior << ' ' << m.bump << ' ' << m.texScale << ' ' << m.metal << "\n";
        if (o.type == ObjType::Terrain && o.hf) {
            f << "terrain " << o.hf->res << "\n";
            for (size_t i = 0; i < o.hf->h.size(); i++) f << o.hf->h[i] << ((i % 16 == 15) ? '\n' : ' ');
            f << "\n";
        }
    }
    f << "end\n";
    return (bool)f;
}

bool Scene::load(const std::string& path) {
    std::ifstream f(path);
    std::string tok;
    int ver = 0;
    if (!(f >> tok >> ver) || tok != "CANYON3D") return false;
    Scene s;
    while (f >> tok) {
        if (tok == "camera") {
            rv(f, s.cam.target);
            f >> s.cam.yaw >> s.cam.pitch >> s.cam.dist >> s.cam.fov;
        } else if (tok == "sky") {
            Sky& k = s.sky;
            int sh = 1;
            f >> k.name;
            k.name = dec(k.name);
            rv(f, k.zenith); rv(f, k.horizon); rv(f, k.sunColor);
            f >> k.sunAlt >> k.sunAz >> k.sunSize >> k.haze;
            rv(f, k.hazeColor);
            f >> k.fog >> k.fogHeight;
            rv(f, k.fogColor);
            f >> k.clouds >> k.cloudScale;
            rv(f, k.cloudColor);
            f >> k.ambient;
            rv(f, k.ambientColor);
            f >> k.stars >> sh;
            k.shadows = sh != 0;
        } else if (tok == "object") {
            Object o;
            int t = 0;
            f >> t >> o.name;
            if (t < 0 || t >= (int)ObjType::COUNT) return false;
            o.type = (ObjType)t;
            o.name = dec(o.name);
            rv(f, o.pos); rv(f, o.rot); rv(f, o.scl); rv(f, o.lightColor);
            f >> o.lightPower;
            s.objs.push_back(o);
        } else if (tok == "material") {
            if (s.objs.empty()) return false;
            Material& m = s.objs.back().mat;
            int t = 0;
            f >> m.name >> t;
            if (t < 0 || t >= (int)TexType::COUNT) return false;
            m.name = dec(m.name);
            m.tex = (TexType)t;
            rv(f, m.color1); rv(f, m.color2);
            f >> m.diffuse >> m.ambient >> m.specular >> m.shininess >> m.reflect >> m.transparency >> m.ior >> m.bump >> m.texScale >> m.metal;
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
        } else if (tok == "end") {
            break;
        } else {
            return false;
        }
        if (!f) return false;
    }
    for (Object& o : s.objs)
        if (o.type == ObjType::Terrain && !o.hf) { o.hf = std::make_shared<Heightfield>(); hfInit(*o.hf, 129); }
    s.updateAll();
    *this = std::move(s);
    return true;
}
