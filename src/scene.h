#pragma once
#include <memory>
#include <string>
#include <vector>

#include "image.h"
#include "mesh.h"
#include "vmath.h"

enum class TexType { None, Checker, Marble, Wood, Granite, Noise, Clouds, Waves, Strata, Altitude, Spots, Bricks, Rust, Lava, Rings, Gradient, Image, COUNT };
const char* texName(TexType t);
enum class TexMap { Object, World, Spherical, Cylindrical, PlanarXZ, PlanarXY, COUNT };
const char* mapName(TexMap m);

struct Material {
    std::string name = "Default Gray", category = "Basic";
    TexType tex = TexType::None;
    TexMap map = TexMap::Object;
    V3 color1{0.72f, 0.72f, 0.72f}, color2{0.3f, 0.3f, 0.3f};
    float diffuse = 0.85f, ambient = 0.3f, specular = 0.25f, shininess = 30.f;
    float reflect = 0.f, transparency = 0.f, ior = 1.f, bump = 0.f, texScale = 1.f, metal = 0.f, emission = 0.f, blur = 0.f;
    std::string image;
    std::shared_ptr<const Image> img;
};
const std::vector<Material>& materialPresets();
Material findMaterial(const std::string& name);

struct Sky {
    std::string name = "Clear Day", category = "Day";
    int mode = 2;  // 0 soft, 1 darker, 2 custom, 3 atmosphere off
    V3 zenith{0.2f, 0.4f, 0.85f}, horizon{0.72f, 0.8f, 0.95f}, sunColor{1.f, 0.97f, 0.9f};
    float sunAlt = 45, sunAz = 135, sunSize = 1.2f;
    bool sunVisible = true;
    float haze = 0.15f;
    V3 hazeColor{0.75f, 0.82f, 0.92f};
    float fog = 0, fogHeight = 3;
    V3 fogColor{0.85f, 0.85f, 0.88f};
    float clouds = 0.25f, stratus = 0, cloudScale = 1;
    V3 cloudColor{1, 1, 1};
    float ambient = 0.5f;
    V3 ambientColor{0.55f, 0.65f, 0.85f};
    float stars = 0;
    bool shadows = true;
    bool moon = false;
    float moonAlt = 30, moonAz = 300, moonPhase = 0.5f, moonSize = 1.6f;
    float rainbow = 0;
    V3 sunDir() const { return dirFrom(sunAlt, sunAz); }
    V3 moonDir() const { return dirFrom(moonAlt, moonAz); }
    static V3 dirFrom(float alt, float az) {
        float a = rad(alt), z = rad(az);
        return {std::cos(a) * std::sin(z), std::sin(a), std::cos(a) * std::cos(z)};
    }
};
const std::vector<Sky>& skyPresets();
const char* skyModeName(int m);
Sky lerpSky(const Sky& a, const Sky& b, float t);

enum class ObjType { Sphere, Cube, Cylinder, Cone, Pyramid, Terrain, Ground, Water, Cloud, Light, Torus, Disc, Square, Stone, Lattice, Mesh, Spotlight, Parallel, COUNT };
const char* objTypeName(ObjType t);
inline bool isInfinite(ObjType t) { return t == ObjType::Ground || t == ObjType::Water || t == ObjType::Cloud; }
inline bool isLight(ObjType t) { return t == ObjType::Light || t == ObjType::Spotlight || t == ObjType::Parallel; }
inline bool isHeightfield(ObjType t) { return t == ObjType::Terrain || t == ObjType::Lattice; }
inline bool isCsgCapable(ObjType t) { return t <= ObjType::Pyramid; }

enum class BoolMode { Neutral, Positive, Negative, Intersect, COUNT };
const char* boolName(BoolMode b);

struct Heightfield {
    int res = 0;
    std::vector<float> h;
    std::vector<V3> n;
    float at(int x, int z) const { return h[z * res + x]; }
    void computeNormals();
};

struct Key { int frame = 0; V3 pos, rot, scl{1, 1, 1}; };

struct Object {
    ObjType type = ObjType::Sphere;
    std::string name;
    V3 pos, rot, scl{1, 1, 1};
    Material mat;
    V3 lightColor{1, 1, 1};
    float lightPower = 1, spotAngle = 30, spotSoft = 0.3f, lightSize = 0;
    int group = 0;
    BoolMode boolMode = BoolMode::Neutral;
    bool hidden = false, locked = false;
    uint32_t seed = 1;
    int treePart = 0;  // 0 none, 1 trunk, 2 foliage (mesh regenerated from `tree`)
    TreeParams tree;
    std::shared_ptr<Heightfield> hf;
    std::shared_ptr<const Mesh> mesh;
    std::vector<Key> keys;
    // derived by update()
    M3 fwd, inv, nrm;
    V3 bcenter;
    float bradius = -1;
    bool csg = false;
    void update();
    V3 lightAxis() const { return normalize(fwd * V3(0, -1, 0)); }
};

struct Camera {
    V3 target{0, 4, 0};
    float yaw = 20, pitch = 8, dist = 48, fov = 50;
    V3 eye() const;
};
struct CamKey { int frame = 0; Camera cam; };
struct SkyKey { int frame = 0; Sky sky; };

struct RenderSettings {
    int width = 0, height = 0;  // 0 = match the viewport
    int quality = 1;            // 0 draft, 1 normal, 2 premium
    int samples = 16;
    bool dof = false, panorama = false, softShadows = true, blurReflections = true;
    float focusDist = 40, aperture = 0.4f, gamma = 1, brightness = 0, contrast = 1, saturation = 1;
};

struct ViewBasis {
    V3 o, f, r, u;
    bool ortho = false;
    float k = 1;
    Ray ray(float nx, float ny) const;
    V3 toCam(V3 p) const { V3 q = p - o; return {dot(q, r), dot(q, u), dot(q, f)}; }
};
ViewBasis cameraBasis(const Camera& c);
ViewBasis orthoBasis(int mode, V3 center, float halfH);

struct Hit {
    float t = 0;
    V3 p, n, lp, lr;
    float ly = 0;
    int obj = -1;
};

struct Scene {
    std::vector<Object> objs;
    Sky sky;
    Camera cam;
    RenderSettings render;
    int frames = 120, fps = 15, frame = 0;
    std::vector<CamKey> camKeys;
    std::vector<SkyKey> skyKeys;
    Camera memory[6];
    bool memorySet[6] = {};
    std::vector<std::vector<int>> csg;  // derived: boolean groups
    void updateAll();
    Scene snapshot() const;
    void applyFrame(int f);
    int maxGroup() const;
    bool save(const std::string& path) const;
    bool load(const std::string& path);
    bool merge(const std::string& path);
};

template <class K> void putKey(std::vector<K>& v, const K& k) {
    auto it = v.begin();
    while (it != v.end() && it->frame < k.frame) ++it;
    if (it != v.end() && it->frame == k.frame) *it = k;
    else v.insert(it, k);
}
template <class K> bool eraseKey(std::vector<K>& v, int frame) {
    for (auto it = v.begin(); it != v.end(); ++it)
        if (it->frame == frame) { v.erase(it); return true; }
    return false;
}

struct Library {
    std::vector<Material> mats;
    std::vector<Sky> skies;
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

Object makeObject(ObjType t, int counter);
Scene makeDefaultScene();
Scene makeEmptyScene();
bool intersectScene(const Scene& s, const Ray& r, float tmax, Hit& h, int skip = -1);
bool intersectObject(const Object& o, const Ray& r, float tmax, Hit& h);
void wireframe(const Object& o, std::vector<V3>& segs);
bool localBounds(const Object& o, V3& lo, V3& hi);
bool worldBounds(const Object& o, V3& lo, V3& hi);
void regenTree(Object& o);
void addTree(Scene& s, const TreeParams& p, V3 pos, float size, const Material& trunk, const Material& foliage, int counter);
const std::vector<std::string>& exampleNames();
const std::vector<std::string>& exampleDescriptions();
Scene makeExample(int i);
