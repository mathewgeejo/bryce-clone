#include "render.h"

static const float EPS = 2e-3f;
static const int MAXD = 5;

struct Surf { V3 color; float trans; };

static Surf evalSurface(const Material& m, const Hit& h) {
    V3 p = h.lp * m.texScale;
    Surf s{m.color1, m.transparency};
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
    default: break;
    }
    return s;
}

static float bumpField(const Material& m, V3 q) {
    if (m.tex == TexType::Waves) return fbm(V3(q.x * 1.2f, q.y, q.z * 3.6f), 3);
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
    float y = d.y;
    V3 sd = k.sunDir();
    V3 c = y >= 0 ? lerp(k.horizon, k.zenith, std::pow(clampf(y, 0, 1), 0.45f)) : k.horizon * (0.8f + 0.2f * std::exp(y * 8));
    if (k.stars > 0 && y > 0) {
        V3 g = d * 400.f;
        float hs = hash3((int)std::floor(g.x), (int)std::floor(g.y), (int)std::floor(g.z));
        if (hs > 0.997f) c += V3(k.stars * (hs - 0.997f) / 0.003f) * smoothstep(0, 0.2f, y);
    }
    float cs = dot(d, sd), sunI = smoothstep(-3, 3, k.sunAlt);
    if (cs > std::cos(rad(k.sunSize))) c += k.sunColor * 3.f;
    c += k.sunColor * (std::pow(std::max(cs, 0.f), 64.f) * 0.5f + std::pow(std::max(cs, 0.f), 6.f) * 0.15f) * sunI;
    if (k.clouds > 0 && y > 0) {
        V3 q = V3(d.x / (y + 0.08f), 0, d.z / (y + 0.08f)) * k.cloudScale;
        float n = clampf(fbm(q * 1.2f, 6) * 1.1f + 0.5f, 0, 1);
        float a = smoothstep(1 - k.clouds, 1 - k.clouds + 0.3f, n) * smoothstep(0, 0.12f, y);
        V3 cc = k.cloudColor * (0.65f + 0.35f * n) + k.sunColor * (std::pow(std::max(cs, 0.f), 4.f) * 0.3f * sunI);
        c = lerp(c, cc, a * 0.95f);
    }
    return c;
}

static V3 fogBlend(const Sky& k, float dist, float frac, V3 c) {
    return lerp(c, k.fogColor, 1 - std::exp(-dist * frac * k.fog * 0.08f));
}

static V3 atmosHit(const Sky& k, V3 o, V3 p, float dist, V3 c) {
    if (k.haze > 0) c = lerp(c, k.hazeColor, 1 - std::exp(-dist * k.haze * 0.01f));
    if (k.fog > 0) {
        float lo = std::min(o.y, p.y), hi = std::max(o.y, p.y);
        float frac = hi <= k.fogHeight ? 1.f : (lo >= k.fogHeight ? 0.f : (k.fogHeight - lo) / (hi - lo));
        c = fogBlend(k, dist, frac, c);
    }
    return c;
}

static V3 atmosSky(const Sky& k, const Ray& r, V3 c) {
    if (k.haze > 0) c = lerp(c, k.hazeColor, k.haze * std::pow(1 - std::max(r.d.y, 0.f), 6.f) * 0.9f);
    if (k.fog > 0 && r.o.y < k.fogHeight) c = fogBlend(k, r.d.y > 1e-4f ? (k.fogHeight - r.o.y) / r.d.y : 1e6f, 1, c);
    return c;
}

static V3 shadowRay(const Scene& s, V3 o, V3 L, float maxd) {
    V3 att(1.f);
    for (int i = 0; i < 8; i++) {
        Hit h;
        if (!intersectScene(s, {o, L}, maxd, h)) return att;
        const Material& m = s.objs[h.obj].mat;
        if (m.transparency <= 0.01f && m.tex != TexType::Clouds) return V3(0.f);
        Surf sf = evalSurface(m, h);
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

static V3 trace(const Scene& s, const Ray& r, int depth) {
    Hit h;
    const Sky& k = s.sky;
    if (!intersectScene(s, r, 1e30f, h)) return atmosSky(k, r, skyColor(k, r.d));
    const Material& m = s.objs[h.obj].mat;
    Surf sf = evalSurface(m, h);
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
    if (sunI > 0) light(k.sunDir(), k.sunColor * sunI, 1e30f);
    for (const Object& lo : s.objs)
        if (lo.type == ObjType::Light) {
            V3 Ld = lo.pos - h.p;
            float d2 = dot(Ld, Ld), d = std::sqrt(d2);
            light(Ld / d, lo.lightColor * (lo.lightPower * 100.f / (100.f + d2)), d - EPS);
        }
    float reflF = m.reflect, transF = sf.trans;
    bool refr = m.ior > 1.001f;
    if (transF > 0 && refr) {
        float c = std::fabs(dot(r.d, nf)), r0 = (m.ior - 1) / (m.ior + 1);
        r0 *= r0;
        float kr = r0 + (1 - r0) * std::pow(1 - c, 5.f);
        reflF += (1 - reflF) * kr * transF;
        transF *= 1 - kr;
    }
    V3 col = diff * std::max(0.f, 1 - reflF - transF) + spec;
    if (depth < MAXD) {
        if (reflF > 0.01f) col += trace(s, {h.p + nf * EPS, reflect(r.d, nf)}, depth + 1) * lerp(V3(1.f), sf.color, m.metal) * reflF;
        if (transF > 0.01f) {
            Ray tr{h.p + r.d * EPS, r.d};
            V3 td;
            if (refr) tr = refractDir(r.d, nf, inside ? m.ior : 1 / m.ior, td) ? Ray{h.p - nf * EPS, td} : Ray{h.p + nf * EPS, reflect(r.d, nf)};
            col += trace(s, tr, depth + 1) * lerp(V3(1.f), sf.color, 0.5f) * transF;
        }
    }
    return atmosHit(k, r.o, h.p, h.t, col);
}

static uint32_t pack(V3 c) {
    auto q = [](float v) { return (uint32_t)(clampf(v, 0, 1) * 255.f + 0.5f); };
    return (q(c.x) << 16) | (q(c.y) << 8) | q(c.z);
}

static V3 sampleAt(const Scene& s, const ViewBasis& vb, int w, int h, float px, float py) {
    return trace(s, vb.ray((2 * px - w) / h, (h - 2 * py) / h), 0);
}

static unsigned threadCount() { return std::max(1u, std::thread::hardware_concurrency()); }

void renderImage(const Scene& sc, int w, int h, uint32_t* out, bool aa) {
    Scene s = sc.snapshot();
    s.updateAll();
    ViewBasis vb = cameraBasis(s.cam);
    std::atomic<int> next{0};
    auto work = [&] {
        for (int y; (y = next++) < h;)
            for (int x = 0; x < w; x++) {
                V3 c(0.f);
                if (aa) {
                    for (int k = 0; k < 4; k++) c += sampleAt(s, vb, w, h, x + 0.25f + 0.5f * (k & 1), y + 0.25f + 0.5f * (k >> 1));
                    c = c * 0.25f;
                } else c = sampleAt(s, vb, w, h, x + 0.5f, y + 0.5f);
                out[y * w + x] = pack(c);
            }
    };
    std::vector<std::thread> ts;
    for (unsigned i = 0; i < threadCount(); i++) ts.emplace_back(work);
    for (auto& t : ts) t.join();
}

void Renderer::start(const Scene& s, int W, int H, bool doAA) {
    cancel();
    sc = s.snapshot();
    sc.updateAll();
    W = std::max(W, 1); H = std::max(H, 1);
    if (W != w || H != h || !px) {
        w = W; h = H;
        px.reset(new std::atomic<uint32_t>[(size_t)w * h]);
        for (size_t i = 0; i < (size_t)w * h; i++) px[i] = 0x303030;
    }
    acc.assign((size_t)w * h, V3(0.f));
    aa = doAA;
    stop = false;
    prog = 0;
    running = true;
    th = std::thread(&Renderer::run, this);
}

void Renderer::cancel() {
    stop = true;
    if (th.joinable()) th.join();
    running = false;
}

void Renderer::copyTo(std::vector<uint32_t>& out) const {
    out.resize((size_t)w * h);
    for (size_t i = 0; i < out.size(); i++) out[i] = px[i].load(std::memory_order_relaxed);
}

void Renderer::run() {
    const ViewBasis vb = cameraBasis(sc.cam);
    const int steps[4] = {8, 4, 2, 1}, passes = aa ? 5 : 4;
    for (int pass = 0; pass < passes && !stop; ++pass) {
        const int s = pass < 4 ? steps[pass] : 1, rows = (h + s - 1) / s;
        std::atomic<int> next{0};
        auto work = [&] {
            for (int r; (r = next++) < rows && !stop;) {
                const int y = r * s;
                Rng rng(0x9E3779B9u * (uint32_t)(y + 1) ^ (uint32_t)pass * 7919u);
                for (int x = 0; x < w; x += s) {
                    size_t i = (size_t)y * w + x;
                    if (pass < 4) {
                        if (pass > 0 && x % (2 * s) == 0 && y % (2 * s) == 0) continue;
                        V3 c = sampleAt(sc, vb, w, h, x + 0.5f, y + 0.5f);
                        acc[i] = c;
                        uint32_t pc = pack(c);
                        for (int yy = y; yy < std::min(y + s, h); yy++)
                            for (int xx = x; xx < std::min(x + s, w); xx++) px[(size_t)yy * w + xx].store(pc, std::memory_order_relaxed);
                    } else {
                        V3 c = acc[i];
                        float con = 0;
                        if (x + 1 < w) con = std::max(con, maxc((c - acc[i + 1]) * (c - acc[i + 1])));
                        if (y + 1 < h) con = std::max(con, maxc((c - acc[i + w]) * (c - acc[i + w])));
                        if (x > 0) con = std::max(con, maxc((c - acc[i - 1]) * (c - acc[i - 1])));
                        if (y > 0) con = std::max(con, maxc((c - acc[i - w]) * (c - acc[i - w])));
                        if (con < 0.004f) continue;
                        V3 sum = c;
                        for (int k = 0; k < 4; k++)
                            sum += sampleAt(sc, vb, w, h, x + 0.25f + 0.5f * (k & 1) + (rng.f() - 0.5f) * 0.25f,
                                            y + 0.25f + 0.5f * (k >> 1) + (rng.f() - 0.5f) * 0.25f);
                        px[i].store(pack(sum * 0.2f), std::memory_order_relaxed);
                    }
                }
                prog = (pass + (r + 1) / (float)rows) / passes;
            }
        };
        std::vector<std::thread> ts;
        for (unsigned i = 0; i < threadCount(); i++) ts.emplace_back(work);
        for (auto& t : ts) t.join();
    }
    if (!stop) prog = 1;
    running = false;
}
