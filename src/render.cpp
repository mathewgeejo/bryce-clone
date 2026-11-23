#include "render.h"

static const float EPS = 2e-3f;

struct Ctx {
    const Scene& s;
    Rng& rng;
    bool soft, blur, premium;
};

struct Surf { V3 color; float trans, glow; };

static V3 inSphere(Rng& r) {
    for (;;) {
        V3 p(r.f() * 2 - 1, r.f() * 2 - 1, r.f() * 2 - 1);
        if (dot(p, p) <= 1) return p;
    }
}

static float worley(V3 p, float& f2) {
    float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z), f1 = 9;
    f2 = 9;
    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int cx = (int)fx + dx, cy = (int)fy + dy, cz = (int)fz + dz;
                V3 fp(cx + hash3(cx, cy, cz), cy + hash3(cx + 17, cy + 5, cz + 3), cz + hash3(cx + 31, cy + 11, cz + 7));
                float d = dot(fp - p, fp - p);
                if (d < f1) { f2 = f1; f1 = d; }
                else if (d < f2) f2 = d;
            }
    f2 = std::sqrt(f2);
    return std::sqrt(f1);
}

static V3 spectrum(float t) {
    float h = (1 - t) * 0.75f * 6;
    return {clampf(std::fabs(h - 3) - 1, 0, 1), clampf(2 - std::fabs(h - 2), 0, 1), clampf(2 - std::fabs(h - 4), 0, 1)};
}

// Texture-space point for procedural textures plus (u, v) for picture textures.
static V3 texCoord(const Material& m, const Object& o, const Hit& h, float& u, float& v) {
    V3 lr = h.lr;
    auto ang = [&] { return std::atan2(lr.z, lr.x) / (2 * PI) + 0.5f; };
    switch (m.map) {
    case TexMap::World: u = h.p.x * 0.1f; v = h.p.z * 0.1f; return h.p * 0.25f;
    case TexMap::Spherical: {
        float l = std::max(length(lr), 1e-6f);
        u = ang(); v = 1 - std::acos(clampf(lr.y / l, -1, 1)) / PI;
        return {u * 8, v * 4, 0.5f};
    }
    case TexMap::Cylindrical: u = ang(); v = (lr.y + 1) * 0.5f; return {u * 8, lr.y * 2, 0.5f};
    case TexMap::PlanarXZ: u = (lr.x + 1) * 0.5f; v = (lr.z + 1) * 0.5f; return {h.lp.x, 0.5f, h.lp.z};
    case TexMap::PlanarXY: u = (lr.x + 1) * 0.5f; v = (lr.y + 1) * 0.5f; return {h.lp.x, h.lp.y, 0.5f};
    default: {
        V3 ln = o.fwd.transpose() * h.n;
        float ax = std::fabs(ln.x), ay = std::fabs(ln.y), az = std::fabs(ln.z);
        if (ax >= ay && ax >= az) { u = (lr.z + 1) * 0.5f; v = (lr.y + 1) * 0.5f; }
        else if (ay >= az) { u = (lr.x + 1) * 0.5f; v = (lr.z + 1) * 0.5f; }
        else { u = (lr.x + 1) * 0.5f; v = (lr.y + 1) * 0.5f; }
        return h.lp;
    }
    }
}

static Surf evalSurface(const Material& m, const Object& o, const Hit& h) {
    float u = 0, v = 0;
    V3 p = texCoord(m, o, h, u, v) * m.texScale;
    Surf s{m.color1, m.transparency, 1};
    switch (m.tex) {
    case TexType::Checker: {
        int c = (int)std::floor(p.x) + (int)std::floor(p.y + 1e-3f) + (int)std::floor(p.z);
        s.color = (c & 1) ? m.color2 : m.color1;
        break;
    }
    case TexType::Marble: s.color = lerp(m.color2, m.color1, std::pow(0.5f + 0.5f * std::sin(p.x * 2.f + turb(p, 5) * 5.f), 0.6f)); break;
    case TexType::Wood: {
        float r = std::sqrt(p.x * p.x + p.z * p.z) * 4.f + noise3(p * 0.8f) * 1.5f, t = r - std::floor(r);
        s.color = lerp(m.color1, m.color2, t * t * t);
        break;
    }
    case TexType::Granite:
        s.color = lerp(m.color2, m.color1, smoothstep(-0.25f, 0.3f, fbm(p * 3.f, 4)));
        if (noise3(p * 25.f) > 0.42f) s.color = s.color * 0.55f;
        break;
    case TexType::Noise: s.color = lerp(m.color2, m.color1, clampf(0.5f + 0.7f * fbm(p, 5), 0, 1)); break;
    case TexType::Clouds: {
        float n = fbm(p, 6);
        s.trans = 1.f - smoothstep(-0.05f, 0.35f, n);
        s.color = lerp(m.color2, m.color1, smoothstep(0.f, 0.5f, n));
        break;
    }
    case TexType::Strata:
        s.color = lerp(m.color2, m.color1, 0.5f + 0.5f * std::sin(p.y * 12.f + fbm(p * 0.8f, 3) * 4.f)) * (0.85f + 0.15f * noise3(p * 6.f));
        break;
    case TexType::Altitude: {
        float hg = h.ly + fbm(p, 4) * 0.12f;
        float snow = smoothstep(0.5f, 0.62f, hg) * smoothstep(0.55f, 0.8f, h.n.y);
        float grass = smoothstep(0.25f, 0.1f, hg) * smoothstep(0.75f, 0.92f, h.n.y);
        V3 rock = m.color2 * (0.75f + 0.35f * (0.5f + 0.5f * fbm(p * 2.3f, 3)));
        s.color = lerp(lerp(rock, V3(0.3f, 0.42f, 0.18f), grass), m.color1, snow);
        break;
    }
    case TexType::Spots: {
        float f2, f1 = worley(p * 2.f, f2);
        s.color = lerp(m.color2, m.color1, smoothstep(0.28f, 0.42f, f1 + noise3(p * 6.f) * 0.06f));
        break;
    }
    case TexType::Bricks: {
        const float bh = 0.5f;
        float row = std::floor(p.y / bh), x = p.x + p.z + (((int)row & 1) ? 0.5f : 0.f), col = std::floor(x);
        float fx = x - col, fy = p.y / bh - row, edge = std::min(std::min(fx, 1 - fx), std::min(fy, 1 - fy) * bh);
        V3 brick = m.color1 * (0.82f + 0.3f * hash3((int)col, (int)row, 3)) * (0.9f + 0.1f * noise3(p * 9.f));
        s.color = lerp(m.color2, brick, smoothstep(0.02f, 0.035f, edge));
        break;
    }
    case TexType::Rust: {
        float t = smoothstep(-0.05f, 0.2f, fbm(p * 1.5f + V3(3.1f), 5) + 0.15f * noise3(p * 12.f));
        s.color = lerp(m.color1 * (0.9f + 0.1f * noise3(p * 20.f)), m.color2 * (0.7f + 0.5f * (0.5f + 0.5f * fbm(p * 8.f, 3))), t);
        break;
    }
    case TexType::Lava: {
        float f2, f1 = worley(p * 1.5f, f2);
        float hot = clampf(1 - smoothstep(0.f, 0.14f, f2 - f1) + turb(p * 2.f, 4) * 0.5f - 0.15f, 0, 1);
        s.color = lerp(m.color2, m.color1, hot);
        s.glow = hot * hot;
        break;
    }
    case TexType::Rings: s.color = lerp(m.color2, m.color1, 0.5f + 0.5f * std::sin(p.y * 9.f + fbm(p * 1.5f, 4) * 3.f)); break;
    case TexType::Gradient: s.color = lerp(m.color2, m.color1, clampf((h.lr.y + 1) * 0.5f, 0, 1)); break;
    case TexType::Image:
        if (m.img) s.color = m.img->sample(u * m.texScale, 1 - v * m.texScale);
        break;
    default: break;
    }
    return s;
}

static float bumpField(const Material& m, V3 q) {
    if (m.tex == TexType::Waves) return fbm(V3(q.x * 1.2f, q.y, q.z * 3.6f), 3);
    if (m.tex == TexType::Spots || m.tex == TexType::Lava) { float f2; return worley(q * 2.f, f2) * 0.5f + fbm(q * 4.f, 2) * 0.3f; }
    return fbm(q * 4.f, 3);
}

static V3 bumpNormal(const Material& m, const Hit& h, V3 n) {
    if (m.bump <= 0) return n;
    V3 q = h.lp * m.texScale;
    const float e = 0.01f;
    V3 g(bumpField(m, q + V3(e, 0, 0)) - bumpField(m, q - V3(e, 0, 0)), bumpField(m, q + V3(0, e, 0)) - bumpField(m, q - V3(0, e, 0)),
         bumpField(m, q + V3(0, 0, e)) - bumpField(m, q - V3(0, 0, e)));
    g = g / (2 * e);
    g -= n * dot(g, n);
    return normalize(n - g * (m.bump * 0.08f));
}

V3 skyColor(const Sky& k, V3 d) {
    if (k.mode == 3) return k.zenith;
    float y = d.y, ex = k.mode == 0 ? 0.8f : (k.mode == 1 ? 0.3f : 0.45f);
    V3 zen = k.mode == 1 ? k.zenith * 0.6f : k.zenith, hor = k.mode == 0 ? lerp(k.horizon, V3(1.f), 0.15f) : k.horizon;
    V3 sd = k.sunDir();
    V3 c = y >= 0 ? lerp(hor, zen, std::pow(clampf(y, 0, 1), ex)) : hor * (0.8f + 0.2f * std::exp(y * 8));
    if (k.stars > 0 && y > 0) {
        V3 g = d * 400.f;
        float hs = hash3((int)std::floor(g.x), (int)std::floor(g.y), (int)std::floor(g.z));
        if (hs > 0.997f) c += V3(k.stars * (hs - 0.997f) / 0.003f) * smoothstep(0, 0.2f, y);
    }
    float cs = dot(d, sd), sunI = smoothstep(-3, 3, k.sunAlt);
    if (k.moon) {
        V3 md = k.moonDir();
        float cm = dot(d, md), th = k.moonPhase * 2 * PI, sr = std::sin(rad(k.moonSize));
        if (cm > std::cos(rad(k.moonSize))) {
            V3 rt = normalize(cross(md, std::fabs(md.y) < 0.99f ? V3(0, 1, 0) : V3(1, 0, 0))), up = cross(rt, md);
            float u = dot(d, rt) / sr, v = dot(d, up) / sr, r2 = u * u + v * v;
            if (r2 < 1) {
                V3 n(u, v, std::sqrt(1 - r2));
                float lit = smoothstep(-0.05f, 0.12f, dot(n, V3(std::sin(th), 0, -std::cos(th))));
                V3 mc = V3(0.95f, 0.93f, 0.88f) * (1.15f + 0.25f * fbm(V3(u * 3.f, v * 3.f, 7.3f), 4));
                c = lerp(c, lerp(c * 0.7f, mc, lit), smoothstep(1.f, 0.9f, r2));
            }
        }
        c += V3(0.75f, 0.8f, 0.95f) * (std::pow(std::max(cm, 0.f), 300.f) * 0.25f * (0.5f - 0.5f * std::cos(th)));
    }
    if (k.sunVisible) {
        if (cs > std::cos(rad(k.sunSize))) c += k.sunColor * 3.f;
        c += k.sunColor * (std::pow(std::max(cs, 0.f), 64.f) * 0.5f + std::pow(std::max(cs, 0.f), 6.f) * 0.15f) * sunI;
    }
    if (k.stratus > 0 && y > 0) {
        V3 q = V3(d.x / (y + 0.15f), 0, d.z / (y + 0.15f)) * (k.cloudScale * 0.6f);
        float n = fbm(V3(q.x * 0.35f, 3.7f, q.z * 2.2f), 5) * 1.2f + 0.5f;
        float a = smoothstep(1 - k.stratus, 1 - k.stratus + 0.45f, n) * smoothstep(0, 0.15f, y) * 0.8f;
        c = lerp(c, lerp(k.cloudColor, k.sunColor, 0.15f * sunI), a);
    }
    if (k.clouds > 0 && y > 0) {
        V3 q = V3(d.x / (y + 0.08f), 0, d.z / (y + 0.08f)) * k.cloudScale;
        float n = clampf(fbm(q * 1.2f, 6) * 1.1f + 0.5f, 0, 1);
        float a = smoothstep(1 - k.clouds, 1 - k.clouds + 0.3f, n) * smoothstep(0, 0.12f, y);
        V3 cc = k.cloudColor * (0.65f + 0.35f * n) + k.sunColor * (std::pow(std::max(cs, 0.f), 4.f) * 0.3f * sunI);
        c = lerp(c, cc, a * 0.95f);
    }
    if (k.rainbow > 0 && y > 0 && k.sunAlt < 42) {
        float ang = std::acos(clampf(dot(d, -sd), -1, 1)) * 180 / PI, t = (ang - 40.3f) / 2.4f;
        if (t > 0 && t < 1) c += spectrum(t) * (std::sin(t * PI) * k.rainbow * 0.35f * smoothstep(0, 0.1f, y));
    }
    return c;
}

static V3 fogBlend(const Sky& k, float dist, float frac, V3 c) {
    return lerp(c, k.fogColor, 1 - std::exp(-dist * frac * k.fog * 0.08f));
}

static V3 atmosHit(const Sky& k, V3 o, V3 p, float dist, V3 c) {
    if (k.mode == 3) return c;
    if (k.haze > 0) c = lerp(c, k.hazeColor, 1 - std::exp(-dist * k.haze * 0.01f));
    if (k.fog > 0) {
        float lo = std::min(o.y, p.y), hi = std::max(o.y, p.y);
        float frac = hi <= k.fogHeight ? 1.f : (lo >= k.fogHeight ? 0.f : (k.fogHeight - lo) / (hi - lo));
        c = fogBlend(k, dist, frac, c);
    }
    return c;
}

static V3 atmosSky(const Sky& k, const Ray& r, V3 c) {
    if (k.mode == 3) return c;
    if (k.haze > 0) c = lerp(c, k.hazeColor, k.haze * std::pow(1 - std::max(r.d.y, 0.f), 6.f) * 0.9f);
    if (k.fog > 0 && r.o.y < k.fogHeight) c = fogBlend(k, r.d.y > 1e-4f ? (k.fogHeight - r.o.y) / r.d.y : 1e6f, 1, c);
    return c;
}

static V3 shadowRay(const Scene& s, V3 o, V3 L, float maxd) {
    V3 att(1.f);
    for (int i = 0; i < 8; i++) {
        Hit h;
        if (!intersectScene(s, {o, L}, maxd, h)) return att;
        const Object& ob = s.objs[h.obj];
        const Material& m = ob.mat;
        if (m.transparency <= 0.01f && m.tex != TexType::Clouds) return V3(0.f);
        Surf sf = evalSurface(m, ob, h);
        if (sf.trans <= 0.01f) return V3(0.f);
        att = att * lerp(V3(1.f), sf.color, 0.5f) * sf.trans;
        if (maxc(att) < 0.01f) return V3(0.f);
        o = h.p + L * EPS;
        maxd -= h.t;
    }
    return att;
}

static bool refractDir(V3 d, V3 n, float eta, V3& t) {
    float c = -dot(d, n), k = 1 - eta * eta * (1 - c * c);
    if (k < 0) return false;
    t = d * eta + n * (eta * c - std::sqrt(k));
    return true;
}

static V3 jitterDir(V3 d, float amount, Rng& r) { return normalize(d + inSphere(r) * amount); }

static V3 trace(Ctx& c, const Ray& r, int depth) {
    const Scene& s = c.s;
    const Sky& k = s.sky;
    Hit h;
    if (!intersectScene(s, r, 1e30f, h)) return atmosSky(k, r, skyColor(k, r.d));
    const Object& ob = s.objs[h.obj];
    const Material& m = ob.mat;
    Surf sf = evalSurface(m, ob, h);
    bool inside = dot(h.n, r.d) > 0;
    V3 nf = bumpNormal(m, h, inside ? -h.n : h.n);
    V3 diff = sf.color * k.ambientColor * (k.ambient * m.ambient * 2.f), spec(0.f);
    auto light = [&](V3 L, V3 li, float maxd) {
        float ndl = m.tex == TexType::Clouds ? std::fabs(dot(h.n, L)) * 0.8f + 0.2f : dot(nf, L);
        if (ndl <= 0) return;
        if (k.shadows) li = li * shadowRay(s, h.p + nf * EPS, L, maxd);
        if (maxc(li) <= 0) return;
        diff += sf.color * li * (m.diffuse * ndl);
        if (m.specular > 0) spec += li * (m.specular * std::pow(std::max(0.f, dot(nf, normalize(L - r.d))), m.shininess));
    };
    float sunI = smoothstep(-3, 3, k.sunAlt);
    if (sunI > 0) light(c.soft ? jitterDir(k.sunDir(), std::tan(rad(std::max(k.sunSize, 0.3f) * 1.5f)), c.rng) : k.sunDir(), k.sunColor * sunI, 1e30f);
    for (const Object& lo : s.objs) {
        if (!isLight(lo.type) || lo.hidden) continue;
        if (lo.type == ObjType::Parallel) {
            V3 L = -lo.lightAxis();
            if (c.soft && lo.lightSize > 0) L = jitterDir(L, lo.lightSize * 0.05f, c.rng);
            light(L, lo.lightColor * lo.lightPower, 1e30f);
            continue;
        }
        V3 lp = c.soft && lo.lightSize > 0 ? lo.pos + inSphere(c.rng) * lo.lightSize : lo.pos;
        V3 Ld = lp - h.p;
        float d2 = std::max(dot(Ld, Ld), 1e-6f), d = std::sqrt(d2);
        V3 L = Ld / d, li = lo.lightColor * (lo.lightPower * 100.f / (100.f + d2));
        if (lo.type == ObjType::Spotlight) {
            float outer = std::cos(rad(clampf(lo.spotAngle, 1, 89))), inner = std::cos(rad(clampf(lo.spotAngle, 1, 89) * (1 - clampf(lo.spotSoft, 0, 1))));
            li = li * smoothstep(outer, std::max(inner, outer + 1e-3f), dot(-L, lo.lightAxis()));
        }
        light(L, li, d - EPS);
    }
    float reflF = m.reflect, transF = sf.trans;
    bool refr = m.ior > 1.001f;
    if (transF > 0 && refr) {
        float cc = std::fabs(dot(r.d, nf)), r0 = (m.ior - 1) / (m.ior + 1);
        r0 *= r0;
        float kr = r0 + (1 - r0) * std::pow(1 - cc, 5.f);
        reflF += (1 - reflF) * kr * transF;
        transF *= 1 - kr;
    }
    V3 col = diff * std::max(0.f, 1 - reflF - transF) + spec + sf.color * (m.emission * sf.glow);
    int maxDepth = c.premium ? 6 : 5;
    bool blur = c.blur && m.blur > 0;
    if (depth < maxDepth) {
        if (reflF > 0.01f) {
            V3 rd = reflect(r.d, nf);
            if (blur) {
                rd = jitterDir(rd, m.blur * 0.35f, c.rng);
                if (dot(rd, nf) < 0) rd = reflect(rd, nf);
            }
            col += trace(c, {h.p + nf * EPS, rd}, depth + 1) * lerp(V3(1.f), sf.color, m.metal) * reflF;
        }
        if (transF > 0.01f) {
            Ray tr{h.p + r.d * EPS, r.d};
            V3 td;
            if (refr) tr = refractDir(r.d, nf, inside ? m.ior : 1 / m.ior, td) ? Ray{h.p - nf * EPS, td} : Ray{h.p + nf * EPS, reflect(r.d, nf)};
            if (blur) tr.d = jitterDir(tr.d, m.blur * 0.25f, c.rng);
            col += trace(c, tr, depth + 1) * lerp(V3(1.f), sf.color, 0.5f) * transF;
        }
    }
    return atmosHit(k, r.o, h.p, h.t, col);
}

static uint32_t pack(V3 c, const RenderSettings& rs) {
    c = (c + V3(rs.brightness) - V3(0.5f)) * rs.contrast + V3(0.5f);
    float l = c.x * 0.299f + c.y * 0.587f + c.z * 0.114f;
    c = lerp(V3(l), c, rs.saturation);
    float ig = 1.f / std::max(rs.gamma, 0.05f);
    auto q = [ig](float v) { return (uint32_t)(std::pow(clampf(v, 0, 1), ig) * 255.f + 0.5f); };
    return (q(c.x) << 16) | (q(c.y) << 8) | q(c.z);
}

static Ray cameraRay(const Scene& s, const ViewBasis& vb, int w, int h, float px, float py, Rng* lens) {
    const RenderSettings& rs = s.render;
    if (rs.panorama) {
        float lon = (px / w - 0.5f) * 2 * PI, lat = (0.5f - py / h) * PI;
        V3 fh = normalize(V3(vb.f.x, 0, vb.f.z)), rr = normalize(cross(fh, V3(0, 1, 0)));
        return {vb.o, fh * (std::cos(lat) * std::cos(lon)) + rr * (std::cos(lat) * std::sin(lon)) + V3(0, std::sin(lat), 0)};
    }
    Ray r = vb.ray((2 * px - w) / h, (h - 2 * py) / h);
    if (lens && rs.dof && rs.aperture > 0) {
        V3 focus = r.o + r.d * (rs.focusDist / std::max(dot(r.d, vb.f), 1e-3f));
        float a = lens->f() * 2 * PI, rr = std::sqrt(lens->f()) * rs.aperture * 0.5f;
        V3 o = r.o + vb.r * (std::cos(a) * rr) + vb.u * (std::sin(a) * rr);
        return {o, normalize(focus - o)};
    }
    return r;
}

static unsigned threadCount() { return std::max(1u, std::thread::hardware_concurrency()); }

void renderImage(const Scene& s, int w, int h, uint32_t* out, int quality) {
    Renderer r;
    r.start(s, w, h, quality);
    r.wait();
    std::vector<uint32_t> px;
    r.copyTo(px);
    std::copy(px.begin(), px.end(), out);
}

void Renderer::start(const Scene& s, int W, int H, int q) {
    cancel();
    sc = s.snapshot();
    sc.updateAll();
    quality = q < 0 ? std::min(std::max(sc.render.quality, 0), 2) : std::min(q, 2);
    W = std::max(W, 1); H = std::max(H, 1);
    if (W != w || H != h || !px) {
        w = W; h = H;
        px.reset(new std::atomic<uint32_t>[(size_t)w * h]);
        for (size_t i = 0; i < (size_t)w * h; i++) px[i] = 0x303030;
    }
    acc.assign((size_t)w * h, V3(0.f));
    stop = false;
    done = false;
    prog = 0;
    running = true;
    th = std::thread(&Renderer::run, this);
}

void Renderer::cancel() {
    stop = true;
    wait();
    running = false;
}

void Renderer::wait() {
    if (th.joinable()) th.join();
}

void Renderer::copyTo(std::vector<uint32_t>& out) const {
    out.resize((size_t)w * h);
    for (size_t i = 0; i < out.size(); i++) out[i] = px[i].load(std::memory_order_relaxed);
}

void Renderer::run() {
    const ViewBasis vb = cameraBasis(sc.cam);
    const RenderSettings& rs = sc.render;
    const bool prem = quality == 2;
    const int samples = prem ? std::max(1, rs.samples) : 0, steps[4] = {8, 4, 2, 1};
    const int passes = 4 + (quality == 1 ? 1 : 0) + samples;
    for (int pass = 0; pass < passes && !stop; ++pass) {
        const int s = pass < 4 ? steps[pass] : 1, rows = (h + s - 1) / s;
        std::atomic<int> next{0};
        auto work = [&] {
            for (int r; (r = next++) < rows && !stop;) {
                const int y = r * s;
                Rng rng(0x9E3779B9u * (uint32_t)(y + 1) ^ (uint32_t)(pass + 1) * 7919u);
                Ctx base{sc, rng, false, false, false}, fancy{sc, rng, rs.softShadows, rs.blurReflections, true};
                for (int x = 0; x < w; x += s) {
                    size_t i = (size_t)y * w + x;
                    if (pass < 4) {
                        if (pass > 0 && x % (2 * s) == 0 && y % (2 * s) == 0) continue;
                        V3 c = trace(base, cameraRay(sc, vb, w, h, x + 0.5f, y + 0.5f, nullptr), 0);
                        acc[i] = c;
                        uint32_t pc = pack(c, rs);
                        for (int yy = y; yy < std::min(y + s, h); yy++)
                            for (int xx = x; xx < std::min(x + s, w); xx++) px[(size_t)yy * w + xx].store(pc, std::memory_order_relaxed);
                    } else if (!prem) {
                        V3 c = acc[i];
                        float con = 0;
                        auto diff = [&](size_t j) { V3 d = c - acc[j]; con = std::max(con, maxc(d * d)); };
                        if (x + 1 < w) diff(i + 1);
                        if (y + 1 < h) diff(i + w);
                        if (x > 0) diff(i - 1);
                        if (y > 0) diff(i - w);
                        if (con < 0.004f) continue;
                        V3 sum = c;
                        for (int k = 0; k < 4; k++)
                            sum += trace(base, cameraRay(sc, vb, w, h, x + 0.25f + 0.5f * (k & 1) + (rng.f() - 0.5f) * 0.25f, y + 0.25f + 0.5f * (k >> 1) + (rng.f() - 0.5f) * 0.25f, nullptr), 0);
                        px[i].store(pack(sum * 0.2f, rs), std::memory_order_relaxed);
                    } else {
                        const int j = pass - 4;
                        float jx = (j == 0) ? 0.5f : rng.f(), jy = (j == 0) ? 0.5f : rng.f();
                        V3 c = trace(fancy, cameraRay(sc, vb, w, h, x + jx, y + jy, &rng), 0);
                        acc[i] = j == 0 ? c : acc[i] + c;
                        px[i].store(pack(acc[i] / (float)(j + 1), rs), std::memory_order_relaxed);
                    }
                }
                prog = (pass + (r + 1) / (float)rows) / passes;
            }
        };
        std::vector<std::thread> ts;
        for (unsigned i = 0; i < threadCount(); i++) ts.emplace_back(work);
        for (auto& t : ts) t.join();
    }
    if (!stop) { prog = 1; done = true; }
    running = false;
}
