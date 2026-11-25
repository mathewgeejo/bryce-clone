#pragma once
#include <array>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "image.h"
#include "mesh.h"
#include "vmath.h"

enum class TexType { None, Checker, Marble, Wood, Granite, Noise, Clouds, Waves, Strata, Altitude, Spots, Bricks, Rust, Lava, Rings, Gradient, Image, Ridged, Cellular, COUNT };
const char* texName(TexType t);
enum class TexMap { Object, World, Spherical, Cylindrical, PlanarXZ, PlanarXY, MeshUV, COUNT };
const char* mapName(TexMap m);

struct Texture {
    TexType tex = TexType::None;
    TexMap map = TexMap::Object;
    V3 color1{0.72f, 0.72f, 0.72f}, color2{0.3f, 0.3f, 0.3f};
    float texScale = 1.f;
    std::string image;
    std::shared_ptr<const Image> img;
};

enum class TexBlend { Mix, Multiply, Add, Subtract, Screen, COUNT };
enum class TexMask { None, Luminance, InverseLuminance, Previous, Elevation, Slope, COUNT };
enum class TexOutput { Default, Luminance, Alpha, Red, Green, Blue, COUNT };
enum TexChannel { TexColor, TexBump, TexSpecular, TexReflection, TexTransparency, TexAmbient, TexChannelCount };
constexpr int MAX_TEXTURE_LAYERS = 16;

struct TextureLayer : Texture {
    V3 offset, rotation;
    TexBlend blend = TexBlend::Mix;
    TexMask mask = TexMask::None;
    float opacity = 1.f;
    float warp = 0, contrast = 1, bias = 0;
    float maskLow = 0.25f, maskHigh = 0.75f, maskFeather = 0.1f;
    int octaves = 5;
    TexOutput heightOutput = TexOutput::Default, transparencyOutput = TexOutput::Default;
    std::array<float, TexChannelCount> influence{1};
    bool save(const std::string& path) const;
    bool load(const std::string& path);
};

struct Material : Texture {
    std::string name = "Default Gray", category = "Basic";
    float diffuse = 0.85f, ambient = 0.3f, specular = 0.25f, shininess = 30.f;
    float reflect = 0.f, transparency = 0.f, ior = 1.f, bump = 0.f, metal = 0.f, emission = 0.f, blur = 0.f;
    float absorption = 0, volumeDensity = 0, volumeScatter = 0.8f, volumeNoise = 0;
    std::vector<TextureLayer> layers;
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
    float cloudAltitude = 12, cloudThickness = 2, cloudShadow = 0, skylight = 0;
    V3 stratusColor{1, 1, 1}, moonColor{0.95f, 0.93f, 0.88f};
    std::string environment;
    std::shared_ptr<const Image> environmentImage;
    float environmentYaw = 0, environmentStrength = 1;
    int environmentSamples = 8;
    bool environmentBackground = true;
    float indirectStrength = 0;
    int indirectSamples = 4;
    int indirectBounces = 1;
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

enum class ObjType { Sphere, Cube, Cylinder, Cone, Pyramid, Terrain, Ground, Water, Cloud, Light, Torus, Disc, Square, Stone, Lattice, Mesh, Spotlight, Parallel, Metaball, Picture, COUNT };
const char* objTypeName(ObjType t);
inline bool isInfinite(ObjType t) { return t == ObjType::Ground || t == ObjType::Water || t == ObjType::Cloud; }
inline bool isLight(ObjType t) { return t == ObjType::Light || t == ObjType::Spotlight || t == ObjType::Parallel; }
inline bool isHeightfield(ObjType t) { return t == ObjType::Terrain || t == ObjType::Lattice; }
inline bool isCsgCapable(ObjType t) { return t <= ObjType::Pyramid || t == ObjType::Lattice; }
inline bool canVolume(ObjType t) { return t <= ObjType::Pyramid; }

enum class Falloff { Soft, None, Linear, InverseSquare, COUNT };
struct LightSettings {
    Falloff falloff = Falloff::Soft;
    float range = 20;
    int samples = 1;
    bool casts = true, disk = false, enabled = true, visible = false;
    float diffuseStrength = 1, specularStrength = 1;
    int targetGroup = 0;
    bool excludeGroup = false;
    std::string gel;
    std::shared_ptr<const Image> gelImage;
};

enum class BoolMode { Neutral, Positive, Negative, Intersect, COUNT };
const char* boolName(BoolMode b);

struct Heightfield {
    int res = 0;
    std::vector<float> h;
    std::vector<V3> n;
    float at(int x, int z) const { return h[z * res + x]; }
    V3 normal(int x, int z) const;
    V3 calculateNormal(int x, int z) const;
    void computeNormals(int x0 = 0, int z0 = 0, int x1 = -1, int z1 = -1);
};
constexpr int MAX_TERRAIN_RESOLUTION = 4097;

enum class Ease { Spline, Linear, Smooth, Hold, COUNT };
const char* easeName(Ease ease);
struct Key { int frame = 0; V3 pos, rot, scl{1, 1, 1}; Ease ease = Ease::Spline; };
Key sampleTransformTrack(const std::vector<Key>& keys, float frame, const Key& fallback);
constexpr int MAX_RIG_JOINTS = 256;
constexpr size_t MAX_SKIN_VERTICES = 1000000;
constexpr int MAX_JOINT_KEYS = 10000;

struct Joint {
    std::string name;
    int parent = -1;
    V3 head, tail{0, 1, 0};
    Key pose;
    std::vector<Key> keys;
};
struct SkinWeights {
    std::array<uint16_t, 4> joint{};
    std::array<float, 4> weight{};
};
struct IKKey {
    int frame = 0;
    V3 target, pole;
    float influence = 1;
    Ease ease = Ease::Linear;
};
struct RigIK {
    int end = -1;
    IKKey settings;
    std::vector<IKKey> keys;
};
constexpr int MAX_RIG_ACTIONS = 64;
constexpr int MAX_RIG_CLIPS = 256;
constexpr int MAX_ACTION_FRAMES = 1200;
constexpr size_t MAX_ACTION_SAMPLES = 1000000;
struct JointActionTrack { int joint = -1; std::vector<Key> keys; };
struct IKActionTrack { int end = -1; std::vector<IKKey> keys; };
struct RigAction {
    std::string name;
    int frames = 0;
    std::vector<JointActionTrack> joints;
    std::vector<IKActionTrack> ik;
};
struct RigClip {
    int action = -1;
    int start = 0, repeats = 1;
    float speed = 1, influence = 1;
    int fadeIn = 0, fadeOut = 0;
};
struct Rig {
    std::vector<Joint> joints;
    std::vector<SkinWeights> weights;
    std::vector<RigIK> ik;
    std::vector<std::shared_ptr<const RigAction>> actions;
    std::vector<RigClip> clips;
};
struct PosedJoint {
    V3 head, tail;
    M3 basis;
    M3 inverseBind;
};
bool validRig(const Rig& rig, size_t vertices);
IKKey sampleIKTrack(const std::vector<IKKey>& keys, float frame, const IKKey& fallback);
inline IKKey sampleIK(const RigIK& ik, float frame) { return sampleIKTrack(ik.keys, frame, ik.settings); }
bool validRigMotion(const Rig& rig);
int rigClipFrames(const RigAction& action, const RigClip& clip);
std::shared_ptr<const RigAction> captureRigAction(const Rig& rig, int first, int last, const std::string& name);
std::shared_ptr<const RigAction> transferRigAction(const Rig& source, int action, const Rig& destination);
void removeRigAction(Rig& rig, int action);
void remapRigActionTracks(Rig& rig, int removedJoint, int removedIK);
bool bakeRigClips(Rig& rig, int frames);
std::vector<PosedJoint> poseRig(const Rig& rig, int frame);
bool bindRig(Rig& rig, const Mesh& mesh);
bool setSkinWeight(SkinWeights& weights, int joint, float weight, size_t jointCount);
std::shared_ptr<const Mesh> deformRig(const Rig& rig, const Mesh& mesh, int frame);
struct TerrainKey { int frame = 0; std::shared_ptr<const Heightfield> field; Ease ease = Ease::Linear; };
struct Camera;

struct Picture {
    std::string path;
    std::shared_ptr<const Image> image;
    float aspect = 1.f, keyTolerance = 0.08f;
    V3 keyColor{1, 0, 1};
    bool billboard = false, chroma = false, shadows = true, reflections = true;
    V3 halfExtent() const { return {std::min(aspect, 1.f), std::min(1.f / aspect, 1.f), 0.01f}; }
};

struct TorusShape {
    float major = 0.7f, minor = 0.3f, section = 1.f;
    float start = 0.f, sweep = 360.f;
};

struct StoneShape {
    float irregularity = 1.f, blockiness = 0.f, smoothing = 0.f;
};

struct TerrainShape { float height = 1, clip = 0, thickness = 0, smoothing = 0; };

struct AppearanceKey {
    int frame = 0;
    Material material;
    Picture picture;
    TerrainShape terrain;
    V3 lightColor;
    std::array<float, 4> light{};
    bool hidden = false;
    Ease ease = Ease::Linear;
    std::optional<LightSettings> lighting;
};

struct Object {
    ObjType type = ObjType::Sphere;
    std::string name;
    V3 pos, rot, scl{1, 1, 1};
    Material mat;
    V3 lightColor{1, 1, 1};
    float lightPower = 1, spotAngle = 30, spotSoft = 0.3f, lightSize = 0;
    LightSettings lighting;
    int group = 0;
    int parent = -1;
    int instanceSource = -1;
    bool terrainAnchor = false;
    V3 anchorPoint;
    V3 anchorNormal;
    M3 bind;
    V3 bindOffset;
    bool hasBind = false;
    BoolMode boolMode = BoolMode::Neutral;
    bool hidden = false, locked = false;
    float metaRadius = 1.4f, metaStrength = 1.f;
    bool metaNegative = false;
    Picture picture;
    TorusShape torus;
    StoneShape stone;
    bool directionalWater = false;
    float waveAngle = 35, waveLength = 2, waveHeight = .03f;
    TerrainShape terrain;
    bool flatMesh = false;
    uint32_t seed = 1;
    int treePart = 0;  // 0 none, 1 trunk, 2 foliage (mesh regenerated from `tree`)
    TreeParams tree;
    std::shared_ptr<Heightfield> hf;
    std::shared_ptr<const Mesh> mesh;
    std::shared_ptr<const Rig> rig;
    std::vector<Key> keys;
    std::vector<AppearanceKey> appearances;
    std::vector<TerrainKey> terrainKeys;
    // derived by update()
    std::shared_ptr<const Heightfield> morphFrom, morphTo;
    std::shared_ptr<const Mesh> posedMesh;
    std::weak_ptr<const Mesh> skinSource;
    std::weak_ptr<const Rig> skinRig;
    int skinFrame = -1;
    float morphWeight = 0;
    M3 fwd, inv, nrm, parentSpace, parentInv;
    V3 bcenter, worldPos;
    float bradius = -1;
    bool csg = false;
    void update(const Camera* camera = nullptr, const Object* ancestor = nullptr);
    const Mesh* visibleMesh() const { return posedMesh ? posedMesh.get() : mesh.get(); }
    void moveWorld(V3 delta) { pos += parentInv * delta; }
    Key transformAt(int frame) const { return sampleTransform((float)frame); }
    Key sampleTransform(float frame) const;
    float rawTerrainAt(int x, int z) const;
    float terrainAt(int x, int z) const {
        float value = rawTerrainAt(x, z);
        if (terrain.smoothing > 0) {
            int limit = hf->res - 1;
            float neighbors = rawTerrainAt(std::max(0, x - 1), z) + rawTerrainAt(std::min(limit, x + 1), z) +
                              rawTerrainAt(x, std::max(0, z - 1)) + rawTerrainAt(x, std::min(limit, z + 1));
            value = lerpf(value, neighbors * .25f, terrain.smoothing);
        }
        return std::max(terrain.clip, value) * terrain.height;
    }
    AppearanceKey appearance(int frame) const;
    V3 lightAxis() const { return normalize(fwd * V3(0, -1, 0)); }
};

struct Camera {
    V3 target{0, 4, 0};
    float yaw = 20, pitch = 8, dist = 48, fov = 50;
    bool orthographic = false;
    float orthoScale = 20;
    int lookAt = -1;
    V3 eye() const;
};
struct CamKey { int frame = 0; Camera cam; Ease ease = Ease::Spline; };
struct NamedCamera { std::string name; Camera view; std::vector<CamKey> keys; };
struct SkyKey { int frame = 0; Sky sky; };

struct RenderSettings {
    int width = 0, height = 0;  // 0 = match the viewport
    int quality = 1;            // 0 draft, 1 normal, 2 premium
    int samples = 16;
    bool dof = false, panorama = false, softShadows = true, blurReflections = true;
    float focusDist = 40, aperture = 0.4f, gamma = 1, brightness = 0, contrast = 1, saturation = 1;
    int bounces = 0, seed = 1;
    bool transparentShadows = true, adaptive = false, denoise = false, region = false;
    bool alpha = false, depthOutput = false, idOutput = false;
    float cropX = 0, cropY = 0, cropW = 0.5f, cropH = 0.5f;
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
    V3 p, n, lp, lr, pictureColor, uv;
    float pictureOpacity = 1;
    float ly = 0;
    int obj = -1;
};

struct MetaballGroup {
    std::vector<int> balls, operands;
    V3 lower{1e30f}, upper{-1e30f};
    bool nested = false;
};

struct BooleanLink { int child, parent; BoolMode mode; };
struct BooleanNode {
    int field = -1;
    bool nested = false;
    std::vector<int> parts;
    std::vector<std::pair<int, BoolMode>> children;
};
struct SceneNode { V3 lower, upper; int left = -1, right = -1, first = 0, count = 0; };

struct Scene {
    std::vector<Object> objs;
    std::string title, description;
    std::map<std::string, std::shared_ptr<const Image>> embeddedImages;
    Sky sky;
    Camera cam;
    RenderSettings render;
    int frames = 120, fps = 15, frame = 0;
    std::vector<CamKey> camKeys;
    std::vector<NamedCamera> cameras;
    int activeCamera = -1;
    std::vector<SkyKey> skyKeys;
    Camera memory[6];
    bool memorySet[6] = {};
    std::vector<std::vector<int>> csg;  // derived: boolean groups
    std::vector<MetaballGroup> metaballs;
    std::vector<BooleanLink> booleanLinks;
    std::vector<BooleanNode> booleanNodes;
    std::vector<int> lights, volumes;
    std::vector<SceneNode> rayNodes;
    std::vector<int> rayOrder, unbounded;
    bool cutterMaterial = false;
    void updateAll();
    Scene snapshot() const;
    void applyFrame(int f);
    int maxGroup() const;
    bool save(const std::string& path) const;
    bool load(const std::string& path);
    bool merge(const std::string& path);
    bool package(const std::string& path, std::string& error) const;
    bool embedAssets(std::string& error);
    std::vector<std::string> missingAssets() const;
    bool relinkAsset(const std::string& source, const std::string& replacement);
    void reloadAssets();
    void append(Scene other);
    Camera viewCamera() const;
    Camera cameraAt(float frame) const;
    void selectCamera(int index);
    bool setParent(int child, int newParent);
    bool setBooleanParent(int child, int parent, BoolMode mode);
    int scatter(const std::vector<int>& sources, int surface, const std::vector<Ray>& rays,
                float minScale, float maxScale, float yawRange, bool align, Rng& rng);
    Scene subset(const std::vector<int>& indices) const;
    int duplicateMirrored(const std::vector<int>& indices, int axis, V3 pivot);
private:
    bool writeFile(const std::string& path) const;
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
enum class RayPurpose { View, Surface, Shadow, Reflection };
bool intersectScene(const Scene& s, const Ray& r, float tmax, Hit& h, int skip = -1, RayPurpose purpose = RayPurpose::View);
bool intersectObject(const Object& o, const Ray& r, float tmax, Hit& h);
void wireframe(const Object& o, std::vector<V3>& segs);
bool localBounds(const Object& o, V3& lo, V3& hi);
bool worldBounds(const Object& o, V3& lo, V3& hi);
std::vector<MeshLayer> meshSelection(const Scene& scene, const std::vector<int>& indices, int resolution);
void regenTree(Object& o);
void addTree(Scene& s, const TreeParams& p, V3 pos, float size, const Material& trunk, const Material& foliage, int counter);
const std::vector<std::string>& exampleNames();
const std::vector<std::string>& exampleDescriptions();
Scene makeExample(int i);
