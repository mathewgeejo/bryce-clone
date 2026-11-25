#include "render.h"

static const float EPS = 2e-3f;

struct Ctx {
    const Scene& s;
    Rng& rng;
    bool soft, blur, premium;
};

struct TextureSample { V3 color; float trans, glow; };
struct Surf {
    V3 color;
    float trans, glow, specular, reflect, ambient;
    float height = 0.5f;
    bool layeredBump = false;
};

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
static V3 texCoord(const Texture& m, const Object& o, const Hit& h, float& u, float& v) {
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
    case TexMap::MeshUV:
        if (o.mesh && !o.mesh->uv.empty()) {
            u = h.uv.x; v = h.uv.y;
            return {u, v, .5f};
        }
        [[fallthrough]];
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

static TextureSample evalTexture(const Texture& m, V3 p, float u, float v, float y, float ny, float transparency, bool layer = false, int octaves = 5) {
    TextureSample s{m.color1, transparency, 1};
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
    case TexType::Noise: s.color = lerp(m.color2, m.color1, clampf(0.5f + 0.7f * fbm(p, octaves), 0, 1)); break;
    case TexType::Ridged: s.color = lerp(m.color2, m.color1, clampf(1 - turb(p, octaves), 0, 1)); break;
    case TexType::Cellular: {
        float next;
        float nearest = worley(p, next);
        s.color = lerp(m.color2, m.color1, smoothstep(0.f, .45f, next - nearest));
        break;
    }
    case TexType::Clouds: {
        float n = fbm(p, 6);
        s.trans = 1.f - smoothstep(-0.05f, 0.35f, n);
        s.color = lerp(m.color2, m.color1, smoothstep(0.f, 0.5f, n));
        break;
    }
    case TexType::Waves:
        if (layer) s.color = lerp(m.color2, m.color1, clampf(0.5f + fbm(V3(p.x * 1.2f, p.y, p.z * 3.6f), 3), 0, 1));
        break;
    case TexType::Strata:
        s.color = lerp(m.color2, m.color1, 0.5f + 0.5f * std::sin(p.y * 12.f + fbm(p * 0.8f, 3) * 4.f)) * (0.85f + 0.15f * noise3(p * 6.f));
        break;
    case TexType::Altitude: {
        float hg = y + fbm(p, 4) * 0.12f;
        float snow = smoothstep(0.5f, 0.62f, hg) * smoothstep(0.55f, 0.8f, ny);
        float grass = smoothstep(0.25f, 0.1f, hg) * smoothstep(0.75f, 0.92f, ny);
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
    case TexType::Gradient: s.color = lerp(m.color2, m.color1, clampf((y + 1) * 0.5f, 0, 1)); break;
    case TexType::Image:
        if (m.img) s.color = m.img->sample(u, 1 - v);
        break;
    default: break;
    }
    return s;
}

template <class T> static T blendTexture(T base, T texture, float weight, TexBlend mode) {
    if (mode == TexBlend::Add) return base + texture * weight;
    if (mode == TexBlend::Subtract) return base - texture * weight;
    if (mode == TexBlend::Multiply) texture = base * texture;
    if (mode == TexBlend::Screen) texture = base + texture - base * texture;
    return base + (texture - base) * weight;
}

static Surf evalSurface(const Material& m, const Object& o, const Hit& h, bool bumpOnly = false) {
    Surf s{m.color1, m.transparency, 1, m.specular, m.reflect, m.ambient};
    if (!bumpOnly) {
        if (o.type == ObjType::Picture) {
            s.color = h.pictureColor;
            s.trans = 1 - (1 - s.trans) * h.pictureOpacity;
        }
        else if (m.tex != TexType::None) {
            float u = 0, v = 0;
            V3 p = texCoord(m, o, h, u, v) * m.texScale;
            float y = m.tex == TexType::Gradient ? h.lr.y : h.ly;
            TextureSample base = evalTexture(m, p, u * m.texScale, v * m.texScale, y, h.n.y, m.transparency);
            s.color = base.color;
            s.trans = base.trans;
            s.glow = base.glow;
        }
    }
    float previous = 1;
    for (const TextureLayer& layer : m.layers) {
        float u = 0, v = 0;
        V3 p = texCoord(layer, o, h, u, v) * layer.texScale;
        M3 rot = rotY(rad(layer.rotation.y)) * rotX(rad(layer.rotation.x)) * rotZ(rad(layer.rotation.z));
        p = rot * p + layer.offset;
        if (layer.warp > 0 && layer.tex != TexType::Image)
            p += V3(noise3(p), noise3(p + V3(43)), noise3(p - V3(31))) * layer.warp;
        if (layer.tex == TexType::Image) {
            V3 uv = rot * V3((u - 0.5f) * layer.texScale, (v - 0.5f) * layer.texScale, 0) + layer.offset;
            u = uv.x + 0.5f; v = uv.y + 0.5f;
        }
        TextureSample component = evalTexture(layer, p, u, v, p.y, h.n.y, 0, true, layer.octaves);
        if (layer.contrast != 1 || layer.bias != 0) {
            auto adjust = [&](float value) { return clampf((value - .5f) * layer.contrast + .5f + layer.bias, 0, 1); };
            V3 c = component.color;
            component.color = {adjust(c.x), adjust(c.y), adjust(c.z)};
        }
        float lum = clampf(dot(component.color, V3(0.299f, 0.587f, 0.114f)), 0, 1);
        float weight = layer.opacity;
        if (layer.mask == TexMask::Luminance) weight *= lum;
        else if (layer.mask == TexMask::InverseLuminance) weight *= 1 - lum;
        else if (layer.mask == TexMask::Previous) weight *= previous;
        else if (layer.mask == TexMask::Elevation || layer.mask == TexMask::Slope) {
            float value = layer.mask == TexMask::Slope ? std::acos(clampf(std::fabs(h.n.y), 0, 1)) / (PI * .5f) :
                          clampf(isHeightfield(o.type) ? h.lr.y : (h.lr.y + 1) * .5f, 0, 1);
            float low = std::min(layer.maskLow, layer.maskHigh), high = std::max(layer.maskLow, layer.maskHigh);
            weight *= smoothstep(low - layer.maskFeather, low, value) * (1 - smoothstep(high, high + layer.maskFeather, value));
        }
        previous = lum;
        auto scalar = [&](float current, TexChannel channel, float value) {
            float amount = weight * layer.influence[channel];
            if (amount <= 0) return current;
            return clampf(blendTexture(current, value, amount, layer.blend), 0, 1);
        };
        if (layer.influence[TexBump] > 0 && layer.opacity > 0) {
            s.layeredBump = true;
            s.height = scalar(s.height, TexBump, lum);
        }
        if (bumpOnly) continue;
        s.color = blendTexture(s.color, component.color, weight * layer.influence[TexColor], layer.blend);
        s.specular = scalar(s.specular, TexSpecular, lum);
        s.reflect = scalar(s.reflect, TexReflection, lum);
        s.trans = scalar(s.trans, TexTransparency, layer.tex == TexType::Clouds ? component.trans : lum);
        s.ambient = scalar(s.ambient, TexAmbient, lum);
    }
    return s;
}

Image terrainMaterialHeightMap(const Object& terrain, int samples) {
    Image output;
    if (!terrain.hf || terrain.hf->res < 2 || samples < 2 || samples > 1025) return output;
    output.w = output.h = samples;
    output.px.resize((size_t)samples * samples);
    int last = terrain.hf->res - 1;
    for (int z = 0; z < samples; ++z) {
        for (int x = 0; x < samples; ++x) {
            int gx = x * last / (samples - 1), gz = z * last / (samples - 1);
            Hit hit;
            hit.lr = {2.f * x / (samples - 1) - 1, terrain.terrainAt(gx, gz), 2.f * z / (samples - 1) - 1};
            hit.lp = hit.lr * terrain.scl;
            hit.ly = hit.lr.y;
            hit.p = terrain.worldPos + terrain.fwd * hit.lr;
            V3 normal = terrain.hf->normal(gx, gz);
            normal.x *= terrain.terrain.height;
            normal.z *= terrain.terrain.height;
            hit.n = normalize(terrain.nrm * normal);
            Surf result = evalSurface(terrain.mat, terrain, hit);
            float height = result.layeredBump ? result.height : dot(result.color, V3(.3f, .59f, .11f));
            output.px[(size_t)z * samples + x] = V3(clampf(height, 0, 1));
        }
    }
    return output;
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

static V3 layerNormal(const Material& m, const Object& o, const Hit& h, V3 n) {
    V3 u = normalize(cross(h.n, std::fabs(h.n.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0))), v = cross(h.n, u);
    const float e = 0.01f;
    auto height = [&](V3 offset) {
        Hit sample = h;
        sample.p += offset;
        sample.lr += o.inv * offset;
        sample.lp = sample.lr * o.scl;
        sample.ly = sample.lr.y;
        return evalSurface(m, o, sample, true).height;
    };
    float du = (height(u * e) - height(u * -e)) / (2 * e);
    float dv = (height(v * e) - height(v * -e)) / (2 * e);
    return normalize(n - (u * du + v * dv) * 0.08f);
}

static V3 environmentColor(const Sky& sky, V3 d) {
    const Image& map = *sky.environmentImage;
    d = rotY(-rad(sky.environmentYaw)) * d;
    float u = std::atan2(d.x, d.z) / (2 * PI) + 0.5f;
    float v = std::acos(clampf(d.y, -1, 1)) / PI;
    return map.sample(u, clampf(v, 0.5f / map.h, 1 - 0.5f / map.h)) * sky.environmentStrength;
}

V3 skyColor(const Sky& k, V3 d) {
    if (k.environmentImage && k.environmentBackground) return environmentColor(k, d);
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
                V3 mc = k.moonColor * (1.15f + 0.25f * fbm(V3(u * 3.f, v * 3.f, 7.3f), 4));
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
        c = lerp(c, lerp(k.cloudColor * k.stratusColor, k.sunColor, 0.15f * sunI), a);
    }
    if (k.clouds > 0 && y > 0) {
        V3 q = V3(d.x / (y + 0.08f), 0, d.z / (y + 0.08f)) * (k.cloudScale * k.cloudAltitude / 12);
        float low = fbm(q * 1.2f, 6), high = fbm(q * 1.35f + V3(0, k.cloudThickness * 0.7f, 0), 4);
        float n = clampf(lerpf(low, high, std::min(k.cloudThickness * 0.09f, 0.5f)) * 1.1f + 0.5f, 0, 1);
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

static bool volumeSpan(const Object& ob, const Ray& ray, float limit, float& near, float& far) {
    Hit hit;
    if (!intersectObject(ob, ray, 1e30f, hit)) return false;
    bool inside = dot(hit.n, ray.d) > 0;
    near = inside ? 0.f : hit.t;
    if (near >= limit) return false;
    far = hit.t;
    if (!inside) {
        float offset = near + EPS;
        if (!intersectObject(ob, {ray.o + ray.d * offset, ray.d}, 1e30f, hit)) return false;
        far = offset + hit.t;
    }
    far = std::min(far, limit);
    return far > near;
}

static float volumeDensity(const Object& ob, V3 point) {
    if (ob.mat.volumeNoise <= 0) return ob.mat.volumeDensity;
    V3 p = ob.inv * (point - ob.worldPos) * std::max(0.1f, ob.mat.texScale);
    float n = clampf(fbm(p * 3.f, 4) * 0.85f + 0.55f, 0, 1);
    return ob.mat.volumeDensity * lerpf(1, n, ob.mat.volumeNoise);
}

static float volumeShadow(const Scene& s, const Ray& ray, float limit) {
    float optical = 0;
    for (int i : s.volumes) {
        const Object& ob = s.objs[i];
        float a, b;
        if (!volumeSpan(ob, ray, limit, a, b)) continue;
        for (int j = 0; j < 4; ++j) {
            V3 p = ray.o + ray.d * lerpf(a, b, (j + 0.5f) / 4);
            optical += volumeDensity(ob, p) * (b - a) * 0.25f * (1 + ob.mat.absorption);
        }
    }
    return std::exp(-optical);
}

static V3 compositeVolumes(const Scene& s, const Ray& ray, float limit, V3 behind, bool premium) {
    if (s.volumes.empty()) return behind;
    struct Span { int index; float a, b; };
    std::vector<Span> spans;
    for (int i : s.volumes) {
        float a, b;
        if (volumeSpan(s.objs[i], ray, limit, a, b)) spans.push_back({i, a, b});
    }
    std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) { return a.a < b.a; });
    float trans = 1;
    V3 color, sun = s.sky.sunDir();
    int steps = premium ? 24 : 12;
    for (const auto& span : spans) {
        const auto& o = s.objs[span.index];
        float step = (span.b - span.a) / steps;
        for (int j = 0; j < steps && trans > 0.001f; ++j) {
            V3 p = ray.o + ray.d * (span.a + (j + 0.5f) * step);
            float density = volumeDensity(o, p);
            float light = smoothstep(-3, 3, s.sky.sunAlt) * volumeShadow(s, {p, sun}, 1e30f);
            V3 incident = s.sky.ambientColor * s.sky.ambient + s.sky.sunColor * (light * (0.5f + 0.3f * dot(ray.d, sun)));
            float absorb = 1 - std::exp(-density * step * (1 + o.mat.absorption));
            color += incident * o.mat.color1 * (trans * absorb * o.mat.volumeScatter);
            trans *= 1 - absorb;
        }
    }
    return color + behind * trans;
}

static V3 shadowRay(const Scene& s, V3 o, V3 L, float maxd) {
    V3 att(volumeShadow(s, {o, L}, maxd));
    for (int i = 0; i < 8; i++) {
        Hit h;
        if (!intersectScene(s, {o, L}, maxd, h, -1, RayPurpose::Shadow)) return att;
        if (!s.render.transparentShadows) return {};
        const Object& ob = s.objs[h.obj];
        const Material& m = ob.mat;
        if (m.transparency <= 0.01f && m.tex != TexType::Clouds && m.layers.empty() && h.pictureOpacity >= 0.999f) return V3(0.f);
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

struct Medium {
    std::array<int, 16> layers{};
    int count = 0;
    int top() const { return count ? layers[count - 1] : -1; }
};

static V3 trace(Ctx& c, const Ray& r, int depth, RayPurpose purpose = RayPurpose::Surface, Medium media = {}) {
    const Scene& s = c.s;
    const Sky& k = s.sky;
    Hit h;
    bool surface = intersectScene(s, r, 1e30f, h, -1, purpose);
    float maximum = surface ? h.t : 1e30f;
    V3 lamp;
    bool emitted = false;
    for (int i : s.lights) {
        const Object& l = s.objs[i];
        if (!l.lighting.visible || l.type == ObjType::Parallel) continue;
        V3 to = r.o - l.worldPos;
        float radius = std::max(0.14f, l.lightSize), b = dot(to, r.d);
        float disc = b * b - dot(to, to) + radius * radius;
        if (disc < 0) continue;
        float t = -b - std::sqrt(disc);
        if (t <= EPS || t >= maximum) continue;
        maximum = t; emitted = true; lamp = l.lightColor * l.lightPower;
    }
    if (emitted) return compositeVolumes(s, r, maximum, atmosHit(k, r.o, r.o + r.d * maximum, maximum, lamp), c.premium);
    if (!surface) {
        bool environment = k.environmentImage && (k.environmentBackground || purpose == RayPurpose::Reflection);
        V3 background = environment ? environmentColor(k, r.d) : atmosSky(k, r, skyColor(k, r.d));
        return compositeVolumes(s, r, maximum, background, c.premium);
    }
    const Object& ob = s.objs[h.obj];
    const Material& m = ob.mat;
    Surf sf = evalSurface(m, ob, h);
    bool inside = dot(h.n, r.d) > 0;
    V3 nf = bumpNormal(m, h, inside ? -h.n : h.n);
    if (sf.layeredBump) nf = layerNormal(m, ob, h, nf);
    if (ob.type == ObjType::Water && ob.directionalWater) {
        float frequency = 2 * PI / ob.waveLength, angle = rad(ob.waveAngle);
        V3 direction(std::cos(angle), 0, std::sin(angle));
        V3 crosswind(-direction.z, 0, direction.x);
        float phase = frequency * dot(h.p, direction) + s.frame * .12f;
        float ripple = 2.7f * frequency * dot(h.p, crosswind) + s.frame * .07f;
        V3 gradient = direction * (frequency * std::cos(phase)) + crosswind * (.6f * frequency * std::cos(ripple));
        nf = normalize(nf - gradient * (inside ? -ob.waveHeight : ob.waveHeight));
    }
    V3 diff = sf.color * k.ambientColor * (k.ambient * sf.ambient * 2.f), spec(0.f);
    if (k.skylight > 0) diff += sf.color * skyColor(k, nf) * (m.diffuse * k.skylight * 0.38f);
    auto light = [&](V3 L, V3 li, float maxd, bool casts = true, float diffuse = 1, float specular = 1) {
        float ndl = m.tex == TexType::Clouds && ob.type != ObjType::Picture ? std::fabs(dot(h.n, L)) * 0.8f + 0.2f : dot(nf, L);
        if (ndl <= 0) return;
        if (k.shadows && casts) li = li * shadowRay(s, h.p + nf * EPS, L, maxd);
        if (maxc(li) <= 0) return;
        diff += sf.color * li * (m.diffuse * ndl * diffuse);
        if (sf.specular > 0) spec += li * (specular * sf.specular * std::pow(std::max(0.f, dot(nf, normalize(L - r.d))), m.shininess));
    };
    if (k.environmentImage && k.environmentStrength > 0) {
        const Image& map = *k.environmentImage;
        const auto& cdf = map.lightCdf;
        M3 rotate = rotY(rad(k.environmentYaw));
        int samples = c.premium ? k.environmentSamples : std::min(4, k.environmentSamples);
        if (!cdf.empty() && cdf.back() > 0) {
            for (int sample = 0; sample < samples; ++sample) {
                double pick = cdf.back() * (sample + (double)c.rng.f()) / samples;
                auto pixel = std::upper_bound(cdf.begin(), cdf.end(), pick);
                if (pixel == cdf.end()) continue;
                size_t index = pixel - cdf.begin();
                int y = (int)(index / map.w);
                float cosTop = std::cos(PI * y / map.h), cosBottom = std::cos(PI * (y + 1) / map.h);
                float cosTheta = lerpf(cosTop, cosBottom, c.rng.f());
                float sinTheta = std::sqrt(std::max(0.f, 1 - cosTheta * cosTheta));
                float phi = ((index % map.w + c.rng.f()) / map.w - 0.5f) * 2 * PI;
                V3 direction = rotate * V3(std::sin(phi) * sinTheta, cosTheta, std::cos(phi) * sinTheta);
                double probability = (*pixel - (index ? cdf[index - 1] : 0.0)) / cdf.back();
                double areaOverPi = 2.0 * (cosTop - cosBottom) / map.w;
                if (probability > 0)
                    light(direction, map.px[index] * (float)(areaOverPi * k.environmentStrength / (samples * probability)), 1e30f);
            }
        }
    }
    float sunI = smoothstep(-3, 3, k.sunAlt);
    V3 sun = k.sunDir();
    if (sunI > 0) {
        if (k.cloudShadow > 0 && k.clouds > 0 && sun.y > 0.01f) {
            V3 q = h.p + sun * (std::max(0.f, k.cloudAltitude - h.p.y) / sun.y);
            float density = fbm(q * (0.065f * k.cloudScale), 4) + 0.55f;
            sunI *= 1 - k.cloudShadow * smoothstep(1 - k.clouds, 1 - k.clouds + 0.25f, density);
        }
        light(c.soft ? jitterDir(sun, std::tan(rad(std::max(k.sunSize, 0.3f) * 1.5f)), c.rng) : sun, k.sunColor * sunI, 1e30f);
    }
    for (int i : s.lights) {
        const Object& lo = s.objs[i];
        const auto& profile = lo.lighting;
        if (profile.targetGroup > 0 && ((ob.group == profile.targetGroup) == profile.excludeGroup)) continue;
        if (lo.type == ObjType::Parallel) {
            V3 L = -lo.lightAxis();
            if (c.soft && lo.lightSize > 0) L = jitterDir(L, lo.lightSize * 0.05f, c.rng);
            light(L, lo.lightColor * lo.lightPower, 1e30f, profile.casts, profile.diffuseStrength, profile.specularStrength);
            continue;
        }
        int samples = c.soft && lo.lightSize > 0 ? profile.samples : 1;
        for (int sample = 0; sample < samples; ++sample) {
        V3 lp = lo.worldPos;
        if (c.soft && lo.lightSize > 0) {
            if (profile.disk) {
                float angle = (sample + c.rng.f()) * 2 * PI / samples, radius = std::sqrt(c.rng.f()) * lo.lightSize;
                lp += normalize(lo.fwd.col(0)) * (std::cos(angle) * radius) + normalize(lo.fwd.col(2)) * (std::sin(angle) * radius);
            } else lp += inSphere(c.rng) * lo.lightSize;
        }
        V3 Ld = lp - h.p;
        float d2 = std::max(dot(Ld, Ld), 1e-6f), d = std::sqrt(d2);
        float falloff = 100.f / (100.f + d2);
        if (profile.falloff == Falloff::None) falloff = 1;
        else if (profile.falloff == Falloff::Linear) falloff = std::max(0.f, 1 - d / profile.range);
        else if (profile.falloff == Falloff::InverseSquare) falloff = std::min(1.f, profile.range * profile.range / d2);
        V3 L = Ld / d, li = lo.lightColor * (lo.lightPower * falloff / samples);
        if (lo.type == ObjType::Spotlight) {
            float outer = std::cos(rad(clampf(lo.spotAngle, 1, 89))), inner = std::cos(rad(clampf(lo.spotAngle, 1, 89) * (1 - clampf(lo.spotSoft, 0, 1))));
            li = li * smoothstep(outer, std::max(inner, outer + 1e-3f), dot(-L, lo.lightAxis()));
            if (profile.gelImage) {
                V3 projected = lo.inv * -L;
                float depth = std::max(-projected.y, 1e-6f) * std::tan(rad(clampf(lo.spotAngle, 1, 89)));
                li = li * profile.gelImage->sampleClamped(0.5f + 0.5f * projected.x / depth, 0.5f + 0.5f * projected.z / depth, false);
            }
        }
        light(L, li, d - EPS, profile.casts, profile.diffuseStrength, profile.specularStrength);
        }
    }
    float reflF = sf.reflect, transF = sf.trans;
    bool refr = m.ior > 1.001f && ob.type != ObjType::Picture && !isInfinite(ob.type);
    Medium after = media;
    V3 td = r.d;
    if (transF > 0 && refr) {
        float n1 = media.top() >= 0 ? s.objs[media.top()].mat.ior : (inside ? m.ior : 1.f);
        if (inside) {
            for (int i = after.count - 1; i >= 0; --i) {
                const Object& previous = s.objs[after.layers[i]];
                if (after.layers[i] != h.obj && !(ob.csg && previous.csg && previous.group == ob.group && ob.group > 0)) continue;
                for (int j = i + 1; j < after.count; ++j) after.layers[j - 1] = after.layers[j];
                --after.count;
                break;
            }
        } else if (after.count < (int)after.layers.size()) after.layers[after.count++] = h.obj;
        float n2 = after.top() >= 0 ? s.objs[after.top()].mat.ior : 1.f;
        float cc = clampf(std::fabs(dot(r.d, nf)), 0, 1), r0 = (n1 - n2) / (n1 + n2);
        r0 *= r0;
        float kr = refractDir(r.d, nf, n1 / n2, td) ? r0 + (1 - r0) * std::pow(1 - cc, 5.f) : 1.f;
        reflF += (1 - reflF) * kr * transF;
        transF *= 1 - kr;
    }
    int maxDepth = s.render.bounces > 0 ? s.render.bounces : (c.premium ? 6 : 5);
    if (depth == 0 && k.indirectStrength > 0 && m.diffuse > 0 && reflF + transF < 0.99f) {
        int samples = c.premium ? k.indirectSamples : 1;
        V3 right = normalize(cross(nf, std::fabs(nf.y) > .95f ? V3(0, 0, 1) : V3(0, 1, 0)));
        V3 forward = cross(nf, right), bounced;
        for (int i = 0; i < samples; ++i) {
            float radius2 = (i + c.rng.f()) / samples, angle = 2 * PI * c.rng.f();
            float radius = std::sqrt(radius2);
            V3 direction = nf * std::sqrt(1 - radius2) + (right * std::cos(angle) + forward * std::sin(angle)) * radius;
            bounced += trace(c, {h.p + nf * EPS, direction}, maxDepth + 1);
        }
        diff += bounced * sf.color * (k.indirectStrength * m.diffuse / samples);
    }
    V3 col = diff * std::max(0.f, 1 - reflF - transF) + spec + sf.color * (m.emission * sf.glow);
    bool blur = c.blur && m.blur > 0;
    if (depth < maxDepth) {
        if (reflF > 0.01f) {
            V3 rd = reflect(r.d, nf);
            if (blur) {
                rd = jitterDir(rd, m.blur * 0.35f, c.rng);
                if (dot(rd, nf) < 0) rd = reflect(rd, nf);
            }
            col += trace(c, {h.p + nf * EPS, rd}, depth + 1, RayPurpose::Reflection, media) * lerp(V3(1.f), sf.color, m.metal) * reflF;
        }
        if (transF > 0.01f) {
            Ray tr{h.p + r.d * EPS, r.d};
            if (refr) tr = {h.p - nf * EPS, td};
            if (blur) tr.d = jitterDir(tr.d, m.blur * 0.25f, c.rng);
            V3 tint = m.absorption > 0 ? V3(1) : lerp(V3(1.f), sf.color, 0.5f);
            col += trace(c, tr, depth + 1, purpose, after) * tint * transF;
        }
    }
    if (media.top() >= 0) {
        const Material& medium = s.objs[media.top()].mat;
        V3 a = (V3(1) - medium.color1) * (-medium.absorption * h.t);
        col = col * V3(std::exp(a.x), std::exp(a.y), std::exp(a.z));
    }
    return compositeVolumes(s, r, h.t, atmosHit(k, r.o, h.p, h.t, col), c.premium);
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
    if (!vb.ortho && lens && rs.dof && rs.aperture > 0) {
        V3 focus = r.o + r.d * (rs.focusDist / std::max(dot(r.d, vb.f), 1e-3f));
        float a = lens->f() * 2 * PI, rr = std::sqrt(lens->f()) * rs.aperture * 0.5f;
        V3 o = r.o + vb.r * (std::cos(a) * rr) + vb.u * (std::sin(a) * rr);
        return {o, normalize(focus - o)};
    }
    return r;
}

static bool inRenderRegion(const RenderSettings& r, int w, int h, int x, int y) {
    if (!r.region) return true;
    return x >= (int)(r.cropX * w) && y >= (int)(r.cropY * h) &&
           x < std::min(w, (int)std::ceil((r.cropX + r.cropW) * w)) &&
           y < std::min(h, (int)std::ceil((r.cropY + r.cropH) * h));
}

void renderOutputPass(const Scene& scene, int w, int h, std::vector<uint32_t>& pixels, RenderPass pass) {
    size_t count = (size_t)w * h;
    if (pass != RenderPass::Alpha) pixels.assign(count, 0);
    else pixels.resize(count);
    ViewBasis basis = cameraBasis(scene.viewCamera());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto& pixel = pixels[(size_t)y * w + x];
            if (!inRenderRegion(scene.render, w, h, x, y)) { pixel = 0; continue; }
            Ray ray = cameraRay(scene, basis, w, h, x + 0.5f, y + 0.5f, nullptr);
            Hit hit;
            bool found = intersectScene(scene, ray, 1e30f, hit, -1, RayPurpose::Surface);
            if (pass == RenderPass::ObjectId) {
                if (found) pixel = (uint32_t)hit.obj + 1;
            } else if (pass == RenderPass::Depth) {
                if (found) {
                    auto v = (uint32_t)(255 * (1 - clampf(hit.t / std::max(1.f, scene.cam.dist * 4), 0, 1)));
                    pixel = (v << 16) | (v << 8) | v;
                }
            } else {
                float trans = volumeShadow(scene, ray, 1e30f);
                for (int i = 0; found && i < 8 && trans > 0.004f; ++i) {
                    const auto& o = scene.objs[hit.obj];
                    trans *= clampf(evalSurface(o.mat, o, hit).trans, 0, 1);
                    ray.o = hit.p + ray.d * EPS;
                    found = trans > 0.004f && intersectScene(scene, ray, 1e30f, hit, -1, RayPurpose::Surface);
                }
                pixel = (pixel & 0xffffff) | ((uint32_t)(255 * (1 - trans) + 0.5f) << 24);
            }
        }
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

void Renderer::start(const Scene& s, int W, int H, int q, const ViewBasis* previewView) {
    cancel();
    sc = s.snapshot();
    overrideView = previewView != nullptr;
    if (previewView) view = *previewView;
    sc.updateAll();
    quality = q < 0 ? std::min(std::max(sc.render.quality, 0), 2) : std::min(q, 2);
    W = std::max(W, 1); H = std::max(H, 1);
    if (W != w || H != h || !px) {
        w = W; h = H;
        px.reset(new std::atomic<uint32_t>[(size_t)w * h]);
        for (size_t i = 0; i < (size_t)w * h; i++) px[i] = 0x303030;
    }
    acc.assign((size_t)w * h, V3(0.f));
    variance.clear();
    if (quality == 2 && sc.render.adaptive) variance.resize((size_t)w * h);
    completedRows.clear();
    nextPass = 0;
    filtered = false;
    for (size_t i = 0; i < acc.size(); ++i)
        px[i].store(sc.render.region ? 0 : 0x303030, std::memory_order_relaxed);
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

bool Renderer::resume() {
    if (!resumable()) return false;
    wait();
    stop = false;
    running = true;
    th = std::thread(&Renderer::run, this);
    return true;
}

void Renderer::wait() {
    if (th.joinable()) th.join();
}

void Renderer::copyTo(std::vector<uint32_t>& out) const {
    out.resize((size_t)w * h);
    for (size_t i = 0; i < out.size(); i++) out[i] = px[i].load(std::memory_order_relaxed);
}

void Renderer::run() {
    const ViewBasis vb = overrideView ? view : cameraBasis(sc.viewCamera());
    const RenderSettings& rs = sc.render;
    const bool prem = quality == 2;
    const int samples = prem ? std::max(1, rs.samples) : 0, steps[4] = {8, 4, 2, 1};
    const int passes = 4 + (quality == 1 ? 1 : 0) + samples;
    int x0 = rs.region ? std::min(w - 1, (int)(w * rs.cropX)) : 0;
    int y0 = rs.region ? std::min(h - 1, (int)(h * rs.cropY)) : 0;
    int x1 = rs.region ? std::clamp((int)std::ceil(w * (rs.cropX + rs.cropW)), x0 + 1, w) : w;
    int y1 = rs.region ? std::clamp((int)std::ceil(h * (rs.cropY + rs.cropH)), y0 + 1, h) : h;
    for (; nextPass < passes && !stop; ++nextPass) {
        const int pass = nextPass;
        const int s = pass < 4 ? steps[pass] : 1, rows = (y1 - y0 + s - 1) / s;
        if (completedRows.empty()) completedRows.resize(rows);
        std::atomic<int> next{0};
        std::atomic<int> finishedRows{(int)std::count(completedRows.begin(), completedRows.end(), 1)};
        auto work = [&] {
            for (int r; (r = next++) < rows && !stop;) {
                if (completedRows[r]) continue;
                const int y = y0 + r * s;
                Rng rng(0x9E3779B9u * (uint32_t)(y + 1) ^ (uint32_t)(pass + 1) * 7919u ^ (uint32_t)(rs.seed - 1));
                Ctx base{sc, rng, false, false, false}, fancy{sc, rng, rs.softShadows, rs.blurReflections, true};
                for (int x = x0; x < x1; x += s) {
                    size_t i = (size_t)y * w + x;
                    if (pass < 4) {
                        if (pass > 0 && (x - x0) % (2 * s) == 0 && (y - y0) % (2 * s) == 0) continue;
                        V3 c = trace(base, cameraRay(sc, vb, w, h, x + 0.5f, y + 0.5f, nullptr), 0);
                        acc[i] = c;
                        uint32_t pc = pack(c, rs);
                        for (int yy = y; yy < std::min(y + s, y1); yy++)
                            for (int xx = x; xx < std::min(x + s, x1); xx++) px[(size_t)yy * w + xx].store(pc, std::memory_order_relaxed);
                    } else if (!prem) {
                        V3 c = acc[i];
                        float con = 0;
                        auto diff = [&](size_t j) { V3 d = c - acc[j]; con = std::max(con, maxc(d * d)); };
                        if (x + 1 < x1) diff(i + 1);
                        if (y + 1 < y1) diff(i + w);
                        if (x > x0) diff(i - 1);
                        if (y > y0) diff(i - w);
                        if (con < 0.004f) continue;
                        V3 sum = c;
                        for (int k = 0; k < 4; k++)
                            sum += trace(base, cameraRay(sc, vb, w, h, x + 0.25f + 0.5f * (k & 1) + (rng.f() - 0.5f) * 0.25f, y + 0.25f + 0.5f * (k >> 1) + (rng.f() - 0.5f) * 0.25f, nullptr), 0);
                        px[i].store(pack(sum * 0.2f, rs), std::memory_order_relaxed);
                    } else {
                        const int j = pass - 4;
                        if (!variance.empty() && variance[i] < 0) continue;
                        float jx = (j == 0) ? 0.5f : rng.f(), jy = (j == 0) ? 0.5f : rng.f();
                        V3 c = trace(fancy, cameraRay(sc, vb, w, h, x + jx, y + jy, &rng), 0);
                        acc[i] = j == 0 ? c : acc[i] + c;
                        if (!variance.empty()) {
                            const V3 weights(0.299f, 0.587f, 0.114f);
                            float l = dot(c, weights);
                            variance[i] += l * l;
                            if (j >= 7) {
                                float mean = dot(acc[i], weights) / (j + 1);
                                float error = (variance[i] / (j + 1) - mean * mean) / j;
                                if (error < 0.00004f * (1 + mean * mean)) variance[i] = -1;
                            }
                        }
                        px[i].store(pack(acc[i] / (float)(j + 1), rs), std::memory_order_relaxed);
                    }
                }
                completedRows[r] = 1;
                float p = (pass + (++finishedRows) / (float)rows) / passes;
                float old = prog.load();
                while (p > old && !prog.compare_exchange_weak(old, p)) {}
            }
        };
        std::vector<std::thread> ts;
        for (unsigned i = 0; i < std::min({threadCount(), (unsigned)rows, 32u}); i++) ts.emplace_back(work);
        for (auto& t : ts) t.join();
        if (finishedRows != rows) break;
        completedRows.clear();
    }
    if (!stop && rs.denoise && !filtered) {
        std::vector<uint32_t> src;
        copyTo(src);
        auto rgb = [](uint32_t p) { return V3((float)((p >> 16) & 255), (float)((p >> 8) & 255), (float)(p & 255)); };
        for (int y = y0 + 1; y + 1 < y1; ++y)
            for (int x = x0 + 1; x + 1 < x1; ++x) {
                size_t i = (size_t)y * w + x;
                V3 center = rgb(src[i]), sum = center * 2;
                float total = 2;
                for (int j : {-w, -1, 1, w}) {
                    V3 c = rgb(src[i + j]);
                    float weight = 1 / (1 + dot(c - center, c - center) / 144.f);
                    sum += c * weight; total += weight;
                }
                sum = sum / total;
                px[i].store(((uint32_t)(sum.x + 0.5f) << 16) | ((uint32_t)(sum.y + 0.5f) << 8) | (uint32_t)(sum.z + 0.5f));
            }
        filtered = true;
    }
    if (!stop) { prog = 1; done = true; }
    running = false;
}
