#pragma once
#include <memory>
#include <string>
#include <vector>

#include "vmath.h"

enum class TexType { None, Checker, Marble, Wood, Granite, Noise, Clouds, Waves, Strata, Altitude, COUNT };
const char* texName(TexType t);

struct Material {
    std::string name = "Default Gray";
    TexType tex = TexType::None;
    V3 color1{0.72f, 0.72f, 0.72f}, color2{0.3f, 0.3f, 0.3f};
    float diffuse = 0.85f, ambient = 0.3f, specular = 0.25f, shininess = 30.f;
    float reflect = 0.f, transparency = 0.f, ior = 1.f, bump = 0.f, texScale = 1.f, metal = 0.f;
};
const std::vector<Material>& materialPresets();
Material findMaterial(const std::string& name);

struct Sky {
    std::string name = "Clear Day";
    V3 zenith{0.2f, 0.4f, 0.85f}, horizon{0.72f, 0.8f, 0.95f}, sunColor{1.f, 0.97f, 0.9f};
    float sunAlt = 45, sunAz = 135, sunSize = 1.2f;
    float haze = 0.15f;
    V3 hazeColor{0.75f, 0.82f, 0.92f};
    float fog = 0, fogHeight = 3;
    V3 fogColor{0.85f, 0.85f, 0.88f};
    float clouds = 0.25f, cloudScale = 1;
    V3 cloudColor{1, 1, 1};
    float ambient = 0.5f;
    V3 ambientColor{0.55f, 0.65f, 0.85f};
    float stars = 0;
    bool shadows = true;
    V3 sunDir() const {
        float a = rad(sunAlt), z = rad(sunAz);
        return {std::cos(a) * std::sin(z), std::sin(a), std::cos(a) * std::cos(z)};
    }
};
const std::vector<Sky>& skyPresets();

enum class ObjType { Sphere, Cube, Cylinder, Cone, Pyramid, Terrain, Ground, Water, Cloud, Light, COUNT };
const char* objTypeName(ObjType t);
inline bool isInfinite(ObjType t) { return t == ObjType::Ground || t == ObjType::Water || t == ObjType::Cloud; }

struct Heightfield {
    int res = 0;
    std::vector<float> h;
    std::vector<V3> n;
    float at(int x, int z) const { return h[z * res + x]; }
    void computeNormals();
};

struct Object {
    ObjType type = ObjType::Sphere;
    std::string name;
    V3 pos, rot, scl{1, 1, 1};
    Material mat;
    V3 lightColor{1, 1, 1};
    float lightPower = 1;
    std::shared_ptr<Heightfield> hf;
    M3 fwd, inv, nrm;
    V3 bcenter;
    float bradius = -1;
    void update();
};

struct Camera {
    V3 target{0, 4, 0};
    float yaw = 20, pitch = 8, dist = 48, fov = 50;
    V3 eye() const;
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
    V3 p, n, lp;
    float ly = 0;
    int obj = -1;
};

struct Scene {
    std::vector<Object> objs;
    Sky sky;
    Camera cam;
    void updateAll() { for (auto& o : objs) o.update(); }
    Scene snapshot() const;
    bool save(const std::string& path) const;
    bool load(const std::string& path);
};

Object makeObject(ObjType t, int counter);
Scene makeDefaultScene();
Scene makeEmptyScene();
bool intersectScene(const Scene& s, const Ray& r, float tmax, Hit& h, int skip = -1);
void wireframe(const Object& o, std::vector<V3>& segs);
