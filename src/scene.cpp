#include "scene.h"

#include <fstream>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <tuple>

#include "terrain.h"

static const float EPS = 2e-3f;

const char* texName(TexType t) {
    static const char* n[] = {"None", "Checker", "Marble", "Wood", "Granite", "Noise", "Clouds", "Waves", "Strata",
                              "Altitude", "Spots", "Bricks", "Rust", "Lava", "Bands", "Gradient", "Picture", "Ridged", "Cellular"};
    return n[(int)t];
}
const char* mapName(TexMap m) {
    static const char* n[] = {"Object", "World", "Spherical", "Cylindrical", "Planar XZ", "Planar XY", "Mesh UV"};
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
    s.cloudAltitude = L(a.cloudAltitude, b.cloudAltitude); s.cloudThickness = L(a.cloudThickness, b.cloudThickness);
    s.cloudShadow = L(a.cloudShadow, b.cloudShadow); s.skylight = L(a.skylight, b.skylight);
    s.stratusColor = C(a.stratusColor, b.stratusColor); s.moonColor = C(a.moonColor, b.moonColor);
    s.environmentYaw = L(a.environmentYaw, b.environmentYaw);
    s.environmentStrength = L(a.environmentStrength, b.environmentStrength);
    s.indirectStrength = L(a.indirectStrength, b.indirectStrength);
    return s;
}

V3 Heightfield::calculateNormal(int x, int z) const {
    int xl = std::max(x - 1, 0), xr = std::min(x + 1, res - 1), zu = std::max(z - 1, 0), zd = std::min(z + 1, res - 1);
    float cs = 2.f / (res - 1);
    float dx = (at(xr, z) - at(xl, z)) / ((xr - xl) * cs), dz = (at(x, zd) - at(x, zu)) / ((zd - zu) * cs);
    return normalize(V3(-dx, 1, -dz));
}

V3 Heightfield::normal(int x, int z) const {
    return n.empty() ? calculateNormal(x, z) : n[z * res + x];
}

void Heightfield::computeNormals(int x0, int z0, int x1, int z1) {
    if (res > 2049) {
        std::vector<V3>().swap(n);
        return;
    }
    if (n.size() != h.size()) { x0 = z0 = 0; x1 = z1 = -1; }
    n.resize(h.size());
    if (res < 2) return;
    for (int z = std::max(0, z0); z <= (z1 < 0 ? res - 1 : std::min(res - 1, z1)); z++)
        for (int x = std::max(0, x0); x <= (x1 < 0 ? res - 1 : std::min(res - 1, x1)); x++)
            n[z * res + x] = calculateNormal(x, z);
}

bool localBounds(const Object& o, V3& lo, V3& hi) {
    lo = V3(-1.f); hi = V3(1.f);
    switch (o.type) {
    case ObjType::Metaball: lo = V3(-o.metaRadius); hi = V3(o.metaRadius); break;
    case ObjType::Picture: hi = o.picture.halfExtent(); lo = -hi; break;
    case ObjType::Terrain: lo.y = -o.terrain.thickness; hi.y = o.terrain.height; break;
    case ObjType::Lattice: lo.y = -o.terrain.height; hi.y = o.terrain.height; break;
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
    case ObjType::Mesh: {
        const Mesh* mesh = o.visibleMesh();
        if (!mesh || mesh->v.empty()) return false;
        lo = mesh->bmin; hi = mesh->bmax;
        break;
    }
    default:
        if (isInfinite(o.type) || isLight(o.type)) return false;
    }
    return true;
}

float Object::rawTerrainAt(int x, int z) const {
    if (!morphFrom) return hf->at(x, z);
    auto sample = [&](const Heightfield& field) {
        if (field.res == hf->res) return field.at(x, z);
        float u = x * (field.res - 1.f) / (hf->res - 1), v = z * (field.res - 1.f) / (hf->res - 1);
        int i = std::min((int)u, field.res - 2), j = std::min((int)v, field.res - 2);
        return lerpf(lerpf(field.at(i, j), field.at(i + 1, j), u - i),
                     lerpf(field.at(i, j + 1), field.at(i + 1, j + 1), u - i), v - j);
    };
    float value = sample(*morphFrom);
    return morphTo && morphWeight > 0 ? lerpf(value, sample(*morphTo), morphWeight) : value;
}

bool worldBounds(const Object& o, V3& lo, V3& hi) {
    V3 a, b;
    if (!localBounds(o, a, b)) { lo = hi = o.worldPos; return isLight(o.type); }
    lo = V3(1e30f); hi = V3(-1e30f);
    for (int i = 0; i < 8; i++) {
        V3 p = o.worldPos + o.fwd * V3((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z);
        lo = V3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = V3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    return true;
}

static V3 terrainSurface(const Object& terrain, V3 point, float& height) {
    int cells = terrain.hf->res - 1;
    float x = clampf((point.x + 1) * .5f, 0, 1) * cells;
    float z = clampf((point.z + 1) * .5f, 0, 1) * cells;
    int ix = std::min((int)x, cells - 1), iz = std::min((int)z, cells - 1);
    float u = x - ix, v = z - iz;
    float a = terrain.terrainAt(ix, iz), e = terrain.terrainAt(ix + 1, iz + 1);
    float dx, dz;
    if (u >= v) {
        float b = terrain.terrainAt(ix + 1, iz);
        dx = b - a;
        dz = e - b;
    } else {
        float c = terrain.terrainAt(ix, iz + 1);
        dx = e - c;
        dz = c - a;
    }
    height = a + dx * u + dz * v;
    return normalize(V3(-dx, 2.f / cells, -dz));
}

void Object::update(const Camera* camera, const Object* ancestor) {
    parentSpace = ancestor ? ancestor->fwd * bind : bind;
    V3 offset = bindOffset;
    if (terrainAnchor && ancestor && ancestor->type == ObjType::Terrain && ancestor->hf && ancestor->hf->res >= 2) {
        float localY;
        V3 normal = terrainSurface(*ancestor, anchorPoint, localY);
        if (dot(anchorNormal, anchorNormal) > .5f && length(normal - anchorNormal) > .0001f) {
            V3 from = normalize(ancestor->nrm * anchorNormal), to = normalize(ancestor->nrm * normal);
            V3 axis = cross(from, to);
            float factor = 1 + dot(from, to);
            if (factor > .001f) {
                auto rotate = [&](V3 v) { return v + cross(axis, v) + cross(axis, cross(axis, v)) / factor; };
                for (int column = 0; column < 3; ++column) {
                    V3 v = rotate(parentSpace.col(column));
                    for (int row = 0; row < 3; ++row) parentSpace.m[row][column] = v[row];
                }
                offset = anchorPoint + ancestor->inv * rotate(ancestor->fwd * (offset - anchorPoint));
            }
        }
        offset.y += localY - anchorPoint.y;
    }
    parentInv = parentSpace.inverse();
    worldPos = parentSpace * pos + (ancestor ? ancestor->worldPos + ancestor->fwd * offset : offset);
    M3 orientation = rotY(rad(rot.y)) * rotX(rad(rot.x));
    bool facing = type == ObjType::Picture && picture.billboard && camera;
    if (facing) {
        V3 n = normalize(camera->eye() - worldPos);
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
    fwd = (facing ? orientation : parentSpace * orientation) * rotZ(rad(rot.z)) * scaleM(scl);
    inv = fwd.inverse();
    nrm = inv.transpose();
    V3 lo, hi;
    if (!localBounds(*this, lo, hi)) { bradius = -1; return; }
    V3 c = (lo + hi) * 0.5f;
    float ms = std::max(length(fwd.col(0)), std::max(length(fwd.col(1)), length(fwd.col(2))));
    bcenter = worldPos + fwd * c;
    bradius = length(hi - c) * ms * 1.001f + 1e-4f;
    if (ancestor || hasBind) {
        bradius = 0;
        for (int i = 0; i < 8; ++i)
            bradius = std::max(bradius, length(fwd * (V3((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z) - c)));
        bradius += 1e-4f;
    }
}

static bool hierarchyOrder(const std::vector<Object>& objects, std::vector<int>& order) {
    int count = (int)objects.size();
    std::vector<uint8_t> state(count);
    std::vector<int> chain;
    for (int start = 0; start < count; ++start) {
        int i = start;
        chain.clear();
        while (i >= 0 && i < count && state[i] == 0) {
            state[i] = 1;
            chain.push_back(i);
            i = objects[i].parent;
        }
        if (i >= count || i < -1 || (i >= 0 && state[i] == 1)) return false;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            state[*it] = 2;
            order.push_back(*it);
        }
    }
    return true;
}

static void updateTerrainMorph(Object& object, int frame);

void Scene::updateAll() {
    for (auto& object : objs) updateTerrainMorph(object, frame);
    for (auto& o : objs) {
        if (o.instanceSource >= 0) continue;
        if (!o.rig || !o.mesh || o.rig->weights.empty()) {
            o.posedMesh.reset();
            o.skinRig.reset();
            continue;
        }
        bool animated = !o.rig->clips.empty() ||
                        std::any_of(o.rig->joints.begin(), o.rig->joints.end(), [](const Joint& j) { return j.animatedTransform(); }) ||
                        std::any_of(o.rig->ik.begin(), o.rig->ik.end(), [](const RigIK& chain) { return !chain.keys.empty(); });
        int poseFrame = animated ? frame : 0;
        if (o.skinFrame != poseFrame || o.skinSource.lock() != o.mesh || o.skinRig.lock() != o.rig) {
            o.posedMesh = deformRig(*o.rig, *o.mesh, poseFrame);
            o.skinSource = o.mesh;
            o.skinRig = o.rig;
            o.skinFrame = poseFrame;
        }
    }
    for (Object& o : objs) if (o.instanceSource >= 0 && o.instanceSource < (int)objs.size()) {
        const Object& source = objs[o.instanceSource];
        o.mesh = source.mesh;
        o.posedMesh = source.posedMesh;
        o.hf = source.hf;
        o.terrain = source.terrain;
        o.morphFrom = source.morphFrom;
        o.morphTo = source.morphTo;
        o.morphWeight = source.morphWeight;
        o.stone = source.stone;
        o.torus = source.torus;
        o.seed = source.seed;
        o.flatMesh = source.flatMesh;
    }
    std::vector<int> order;
    if (hierarchyOrder(objs, order)) {
        for (int i : order) {
            Object& o = objs[i];
            o.update(&cam, o.parent < 0 ? nullptr : &objs[o.parent]);
        }
    } else {
        for (Object& o : objs) o.update(&cam);
    }
    std::map<int, std::vector<int>> groups;
    std::map<int, size_t> fieldGroups;
    metaballs.clear();
    lights.clear();
    volumes.clear();
    auto image = [&](const std::string& key) {
        auto found = embeddedImages.find(key);
        return found == embeddedImages.end() ? loadImageCached(key) : found->second;
    };
    if (!sky.environment.empty() && !sky.environmentImage) sky.environmentImage = image(sky.environment);
    for (auto& key : skyKeys)
        if (!key.sky.environment.empty() && !key.sky.environmentImage) key.sky.environmentImage = image(key.sky.environment);
    for (size_t i = 0; i < objs.size(); i++) {
        Object& o = objs[i];
        o.csg = false;
        if (o.mat.tex == TexType::Image && !o.mat.image.empty() && !o.mat.img) o.mat.img = image(o.mat.image);
        for (auto& layer : o.mat.layers)
            if (layer.tex == TexType::Image && !layer.image.empty() && !layer.img) layer.img = image(layer.image);
        if (o.type == ObjType::Picture && !o.picture.path.empty() && !o.picture.image) o.picture.image = image(o.picture.path);
        if (isLight(o.type) && !o.lighting.gel.empty() && !o.lighting.gelImage) o.lighting.gelImage = image(o.lighting.gel);
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
    booleanNodes.clear();
    std::map<int, int> nested;
    auto node = [&](int id) {
        auto [it, inserted] = nested.emplace(id, (int)booleanNodes.size());
        if (inserted) booleanNodes.emplace_back();
        return it->second;
    };
    for (const auto& link : booleanLinks) { node(link.child); node(link.parent); }
    for (const auto& [id, members] : groups)
        for (int i : members) if (objs[i].type == ObjType::Lattice) { node(id); break; }
    for (auto& g : groups) {
        for (int i : g.second) objs[i].csg = true;
        auto parent = nested.find(g.first);
        if (parent != nested.end()) {
            booleanNodes[parent->second].parts = std::move(g.second);
            continue;
        }
        auto field = fieldGroups.find(g.first);
        if (field == fieldGroups.end()) csg.push_back(std::move(g.second));
        else metaballs[field->second].operands = std::move(g.second);
    }
    for (const auto& [id, index] : nested) {
        auto field = fieldGroups.find(id);
        if (field == fieldGroups.end()) continue;
        booleanNodes[index].field = (int)field->second;
        metaballs[field->second].nested = true;
    }
    for (auto& field : metaballs) {
        auto include = [&](int i) {
            V3 low, high;
            if (!worldBounds(objs[i], low, high)) return;
            auto& a = field.lower;
            auto& b = field.upper;
            a = {std::min(a.x, low.x), std::min(a.y, low.y), std::min(a.z, low.z)};
            b = {std::max(b.x, high.x), std::max(b.y, high.y), std::max(b.z, high.z)};
        };
        for (int i : field.balls) if (!objs[i].metaNegative) include(i);
        for (int i : field.operands) if (objs[i].boolMode == BoolMode::Positive) include(i);
    }
    for (const auto& link : booleanLinks) {
        int child = nested[link.child];
        booleanNodes[nested[link.parent]].children.emplace_back(child, link.mode);
        booleanNodes[child].nested = true;
    }
    for (int i = 0; i < (int)objs.size(); ++i) {
        const auto& o = objs[i];
        if (o.hidden) continue;
        if (isLight(o.type) && o.lighting.enabled) lights.push_back(i);
        if (!o.csg && canVolume(o.type) && o.mat.volumeDensity > 0) volumes.push_back(i);
    }
    rayNodes.clear(); rayOrder.clear(); unbounded.clear();
    if (objs.size() < 16) return;
    std::vector<std::pair<V3, V3>> boxes(objs.size());
    for (int i = 0; i < (int)objs.size(); ++i) {
        const auto& o = objs[i];
        if (o.hidden || o.csg || isLight(o.type)) continue;
        auto& [lo, hi] = boxes[i];
        if (worldBounds(o, lo, hi)) rayOrder.push_back(i);
        else unbounded.push_back(i);
    }
    if (rayOrder.empty()) return;
    auto build = [&](const auto& self, int first, int last) -> int {
        V3 lo(1e30f), hi(-1e30f);
        for (int i = first; i < last; ++i) {
            auto& [a, b] = boxes[rayOrder[i]];
            lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)};
            hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
        }
        int id = (int)rayNodes.size();
        rayNodes.push_back({lo - V3(1e-4f), hi + V3(1e-4f)});
        if (last - first <= 4) {
            rayNodes[id].first = first;
            rayNodes[id].count = last - first;
            return id;
        }
        V3 size = hi - lo;
        int axis = size.x > size.y && size.x > size.z ? 0 : (size.y > size.z ? 1 : 2);
        int mid = first + (last - first) / 2;
        std::nth_element(rayOrder.begin() + first, rayOrder.begin() + mid, rayOrder.begin() + last, [&](int a, int b) {
            return boxes[a].first[axis] + boxes[a].second[axis] < boxes[b].first[axis] + boxes[b].second[axis];
        });
        int left = self(self, first, mid), right = self(self, mid, last);
        rayNodes[id].left = left; rayNodes[id].right = right;
        return id;
    };
    build(build, 0, (int)rayOrder.size());
}

bool Scene::setBooleanParent(int child, int parent, BoolMode mode) {
    if (child <= 0 || parent < 0 || (parent && mode == BoolMode::Neutral)) return false;
    int depth = 0;
    for (int i = parent; i > 0;) {
        if (i == child || ++depth > 32) return false;
        auto it = std::find_if(booleanLinks.begin(), booleanLinks.end(), [=](const BooleanLink& l) { return l.child == i; });
        i = it != booleanLinks.end() ? it->parent : 0;
    }
    booleanLinks.erase(std::remove_if(booleanLinks.begin(), booleanLinks.end(), [=](const BooleanLink& l) { return l.child == child; }), booleanLinks.end());
    if (parent) booleanLinks.push_back({child, parent, mode});
    return true;
}

static void bindParent(Object& object, int index, const Object* parent) {
    V3 origin = object.worldPos - object.parentSpace * object.pos;
    object.bind = parent ? parent->inv * object.parentSpace : object.parentSpace;
    object.bindOffset = parent ? parent->inv * (origin - parent->worldPos) : origin;
    object.parent = index;
    object.terrainAnchor = false;
    object.hasBind = true;
}

bool Scene::setParent(int child, int newParent) {
    if (child < 0 || child >= (int)objs.size() || newParent < -1 || newParent >= (int)objs.size()) return false;
    for (int i = newParent; i >= 0; i = objs[i].parent) if (i == child) return false;
    if (newParent >= 0 && length(objs[newParent].inv * V3(1, 0, 0)) == 0) return false;
    if (objs[child].parent != newParent) {
        bindParent(objs[child], newParent, newParent < 0 ? nullptr : &objs[newParent]);
        updateAll();
    }
    return true;
}

Scene Scene::subset(const std::vector<int>& indices) const {
    Scene result = *this;
    result.objs.clear();
    std::vector<int> remap(objs.size(), -1);
    for (int i : indices) {
        if (i < 0 || i >= (int)objs.size() || remap[i] >= 0) continue;
        remap[i] = (int)result.objs.size();
        result.objs.push_back(objs[i]);
    }
    for (Object& o : result.objs) {
        if (o.instanceSource >= 0) {
            int original = o.instanceSource;
            o.instanceSource = remap[original];
            if (o.instanceSource < 0) o.rig = objs[original].rig;
        }
        if (o.parent < 0) continue;
        int newParent = remap[o.parent];
        if (newParent < 0) bindParent(o, -1, nullptr);
        else o.parent = newParent;
    }
    auto fixCamera = [&](Camera& c) {
        if (c.lookAt >= 0) c.lookAt = c.lookAt < (int)remap.size() ? remap[c.lookAt] : -1;
    };
    fixCamera(result.cam);
    for (auto& key : result.camKeys) fixCamera(key.cam);
    for (auto& camera : result.cameras) {
        fixCamera(camera.view);
        for (auto& key : camera.keys) fixCamera(key.cam);
    }
    for (auto& camera : result.memory) fixCamera(camera);
    std::set<int> used;
    for (const Object& o : result.objs) if (o.group) used.insert(o.group);
    result.booleanLinks.erase(std::remove_if(result.booleanLinks.begin(), result.booleanLinks.end(), [&](const BooleanLink& l) {
        return !used.count(l.child) || !used.count(l.parent);
    }), result.booleanLinks.end());
    result.updateAll();
    return result;
}

int Scene::duplicateMirrored(const std::vector<int>& indices, int axis, V3 pivot) {
    if (axis < 0 || axis > 2) return 0;
    std::vector<int> visible;
    for (int index : indices) {
        if (index < 0 || index >= (int)objs.size()) continue;
        const auto& obj = objs[index];
        if (!obj.hidden && !obj.locked && !isInfinite(obj.type)) visible.push_back(index);
    }
    if (visible.empty()) return 0;
    Scene copy = subset(visible);
    M3 reflection;
    reflection.m[axis][axis] = -1;
    std::map<const Heightfield*, std::shared_ptr<Heightfield>> heightCopies;
    for (auto& obj : copy.objs) {
        obj.name += " mirror";
        if (obj.hf) {
            auto [entry, inserted] = heightCopies.try_emplace(obj.hf.get());
            if (inserted) entry->second = std::make_shared<Heightfield>(*obj.hf);
            obj.hf = entry->second;
        }
        if (obj.parent < 0) {
            obj.bind = reflection * obj.bind;
            obj.bindOffset = pivot + reflection * (obj.bindOffset - pivot);
            obj.hasBind = true;
        }
    }
    int count = (int)copy.objs.size();
    append(std::move(copy));
    return count;
}

int Scene::maxGroup() const {
    int m = 0;
    for (auto& o : objs) m = std::max(m, o.group);
    for (auto& link : booleanLinks) m = std::max({m, link.parent, link.child});
    return m;
}

int Scene::scatter(const std::vector<int>& sources, int surface, const std::vector<Ray>& rays,
                   float minScale, float maxScale, float yawRange, bool align, Rng& rng) {
    if (surface < 0 || surface >= (int)objs.size() || objs[surface].hidden || objs[surface].csg || isLight(objs[surface].type)) return 0;
    std::vector<Object> prototypes;
    V3 lo(1e30f), hi(-1e30f);
    for (int index : sources) {
        if (index < 0 || index >= (int)objs.size() || index == surface) continue;
        const Object& o = objs[index];
        V3 lower, upper;
        if (o.hidden || o.csg || isLight(o.type) || o.type == ObjType::Metaball || !worldBounds(o, lower, upper)) continue;
        Object copy = o;
        copy.instanceSource = o.instanceSource < 0 ? index : o.instanceSource;
        copy.rig.reset();
        copy.name += " instance";
        copy.parent = surface;
        copy.terrainAnchor = false;
        copy.treePart = 0;
        copy.hasBind = true;
        copy.pos = copy.rot = {};
        copy.scl = V3(1);
        copy.keys.clear();
        copy.curves.reset();
        copy.appearances.clear();
        copy.terrainKeys.clear();
        prototypes.push_back(std::move(copy));
        lo = {std::min(lo.x, lower.x), std::min(lo.y, lower.y), std::min(lo.z, lower.z)};
        hi = {std::max(hi.x, upper.x), std::max(hi.y, upper.y), std::max(hi.z, upper.z)};
    }
    if (prototypes.empty()) return 0;
    V3 pivot((lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f);
    int group = prototypes.size() > 1 ? maxGroup() : 0, created = 0;
    for (const Ray& ray : rays) {
        if (objs.size() + prototypes.size() > 20000 || group == (std::numeric_limits<int>::max)()) break;
        Hit hit;
        const Object& receiver = objs[surface];
        if (!intersectObject(receiver, ray, 1e30f, hit)) continue;
        M3 receiverInv = receiver.inv;
        V3 receiverOrigin = receiver.worldPos;
        V3 anchor = receiverInv * (hit.p - receiverOrigin);
        bool terrainTop = receiver.type == ObjType::Terrain && dot(hit.n, receiver.nrm * V3(0, 1, 0)) > 0;
        float anchorHeight;
        V3 anchorSlope = terrainTop && align ? terrainSurface(receiver, anchor, anchorHeight) : V3();
        V3 up = align ? hit.n : V3(0, 1, 0);
        V3 right = normalize(cross(up, std::fabs(up.z) < 0.95f ? V3(0, 0, 1) : V3(-1, 0, 0)));
        V3 forward = cross(right, up);
        M3 basis;
        for (int i = 0; i < 3; ++i) {
            basis.m[i][0] = right[i];
            basis.m[i][1] = up[i];
            basis.m[i][2] = forward[i];
        }
        M3 transform = basis * rotY(rad(yawRange * (rng.f() - .5f))) * scaleM(V3(lerpf(minScale, maxScale, rng.f())));
        if (prototypes.size() > 1) ++group;
        for (const Object& base : prototypes) {
            Object copy = base;
            copy.group = group;
            copy.boolMode = BoolMode::Neutral;
            copy.locked = false;
            copy.bind = receiverInv * transform * base.fwd;
            copy.bindOffset = receiverInv * (hit.p + transform * (base.worldPos - pivot) - receiverOrigin);
            copy.terrainAnchor = terrainTop;
            copy.anchorPoint = anchor;
            copy.anchorNormal = anchorSlope;
            objs.push_back(std::move(copy));
            ++created;
        }
    }
    return created;
}

V3 Camera::eye() const {
    float p = rad(pitch), y = rad(yaw);
    return target + V3(std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y)) * dist;
}

Camera Scene::viewCamera() const {
    Camera result = cam;
    if (cam.lookAt < 0 || cam.lookAt >= (int)objs.size()) return result;
    V3 target = objs[cam.lookAt].worldPos, delta = cam.eye() - target;
    float distance = length(delta);
    if (distance < 0.001f) return result;
    result.target = target;
    result.dist = distance;
    result.yaw = std::atan2(delta.x, delta.z) * 180 / PI;
    result.pitch = std::asin(clampf(delta.y / distance, -0.99999f, 0.99999f)) * 180 / PI;
    return result;
}

void Scene::selectCamera(int index) {
    if (index < 0 || index >= (int)cameras.size() || index == activeCamera) return;
    if (activeCamera >= 0 && activeCamera < (int)cameras.size()) {
        cameras[activeCamera].view = cam;
        cameras[activeCamera].keys = camKeys;
    }
    cam = cameras[index].view;
    camKeys = cameras[index].keys;
    activeCamera = index;
    updateAll();
}

Ray ViewBasis::ray(float nx, float ny) const {
    if (ortho) return {o + r * (nx * k) + u * (ny * k), f};
    return {o, normalize(f + r * (nx * k) + u * (ny * k))};
}

ViewBasis cameraBasis(const Camera& c) {
    ViewBasis b;
    b.o = c.eye();
    float yaw = rad(c.yaw), pitch = rad(c.pitch);
    b.f = {-std::cos(pitch) * std::sin(yaw), -std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
    b.r = {std::cos(yaw), 0, -std::sin(yaw)};
    b.u = cross(b.r, b.f);
    b.ortho = c.orthographic;
    b.k = c.orthographic ? c.orthoScale : std::tan(rad(c.fov) * 0.5f);
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
    std::map<const Heightfield*, std::shared_ptr<Heightfield>> copies;
    for (auto& o : s.objs) if (o.hf) {
        auto [at, added] = copies.emplace(o.hf.get(), nullptr);
        if (added) at->second = std::make_shared<Heightfield>(*o.hf);
        o.hf = at->second;
    }
    return s;
}

// ---------------------------------------------------------------- animation

template <class K> static bool keySpan(const std::vector<K>& v, float f, int& i0, int& i1, float& t) {
    if (v.empty()) return false;
    i0 = i1 = 0;
    t = 0;
    if (f <= v.front().frame) return true;
    if (f >= v.back().frame) { i0 = i1 = (int)v.size() - 1; return true; }
    i1 = (int)(std::upper_bound(v.begin(), v.end(), f, [](float time, const K& key) {
        return time < key.frame;
    }) - v.begin());
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

const char* easeName(Ease ease) {
    const char* names[] = {"Spline", "Linear", "Smooth", "Hold"};
    return names[(int)ease];
}

static float easeTime(Ease ease, float t) {
    if (ease == Ease::Hold) return 0;
    if (ease == Ease::Smooth) return t * t * (3 - 2 * t);
    return t;
}

static void updateTerrainMorph(Object& object, int frame) {
    int first, next;
    float t;
    if (!keySpan(object.terrainKeys, (float)frame, first, next, t)) {
        object.morphFrom.reset();
        object.morphTo.reset();
        return;
    }
    const auto& a = object.terrainKeys[first];
    object.morphFrom = a.field;
    object.morphTo = object.terrainKeys[next].field;
    object.morphWeight = a.ease == Ease::Spline ? easeTime(Ease::Smooth, t) : easeTime(a.ease, t);
}

template <class K, class G> static V3 motion(const std::vector<K>& v, int a, int b, float t, G get) {
    return v[a].ease == Ease::Spline ? spline(v, a, b, t, get) : lerp(get(v[a]), get(v[b]), easeTime(v[a].ease, t));
}

Key sampleTransformTrack(const std::vector<Key>& keys, float frame, const Key& fallback) {
    int first, next;
    float t;
    if (!keySpan(keys, frame, first, next, t)) {
        Key value = fallback;
        value.frame = (int)frame;
        return value;
    }
    V3 scale = motion(keys, first, next, t, [](const Key& k) { return k.scl; });
    return {(int)frame, motion(keys, first, next, t, [](const Key& k) { return k.pos; }),
            motion(keys, first, next, t, [](const Key& k) { return k.rot; }),
            {std::max(scale.x, .01f), std::max(scale.y, .01f), std::max(scale.z, .01f)}, keys[first].ease};
}

namespace {
static_assert((int)Ease::COUNT == (int)CurveEase::Bezier);
constexpr size_t MAX_OBJECT_CURVE_KEYS = 100000;

float& transformChannelSlot(Key& key, int channel) {
    V3& values = channel < 3 ? key.pos : channel < 6 ? key.rot : key.scl;
    int axis = channel % 3;
    return axis == 0 ? values.x : axis == 1 ? values.y : values.z;
}

double cubic(double a, double b, double c, double d, double t) {
    double u = 1 - t;
    return u*u*u*a + 3*u*u*t*b + 3*u*t*t*c + t*t*t*d;
}

double curveParameter(double time, const std::array<CurveHandle, 2>& handles) {
    double lo = 0, hi = 1;
    for (int i = 0; i < 26; ++i) {
        double t = (lo + hi) * .5;
        if (cubic(0, handles[0].fraction, 1 - handles[1].fraction, 1, t) < time) lo = t;
        else hi = t;
    }
    return (lo + hi) * .5;
}

CurveKey toCurveKey(const Key& key, int channel) {
    return {key.frame, transformChannelValue(key, channel), (CurveEase)key.ease};
}

bool validCurveTrack(const CurveTrack& track, int channel, bool joint) {
    if (track.size() > (joint ? (size_t)MAX_JOINT_KEYS : MAX_OBJECT_CURVE_KEYS)) return false;
    auto handle = [](const CurveHandle& h) {
        return h.fraction >= 0 && h.fraction <= 1 && std::fabs(h.offset) <= 2e6f;
    };
    int last = -1;
    for (const auto& key : track) {
        if (key.frame <= last || key.frame >= 100000 || !(std::fabs(key.value) <= 1e6f) ||
            (joint && channel >= 6 && !(key.value >= .01f && key.value <= 100.f)) ||
            key.ease < CurveEase::Spline || key.ease >= CurveEase::COUNT || !handle(key.in) || !handle(key.out)) return false;
        last = key.frame;
    }
    return true;
}

bool hasCurveKeys(const TransformCurves& curves) {
    return std::any_of(curves.channels.begin(), curves.channels.end(), [](const auto& track) { return !track.empty(); });
}
}  // namespace

const char* curveEaseName(CurveEase ease) {
    return ease == CurveEase::Bezier ? "Bezier" : easeName((Ease)ease);
}

float transformChannelValue(const Key& key, int channel) {
    return (channel < 3 ? key.pos : channel < 6 ? key.rot : key.scl)[channel % 3];
}

std::array<CurveHandle, 2> bezierHandles(const CurveKey& first, const CurveKey& last) {
    std::array<CurveHandle, 2> handles{first.out, last.in};
    float extent = handles[0].fraction + handles[1].fraction;
    if (extent > 1) {
        for (auto& h : handles) { h.fraction /= extent; h.offset /= extent; }
    }
    return handles;
}

float sampleCurve(const CurveTrack& keys, float frame, float fallback) {
    int first, next;
    float t;
    if (!keySpan(keys, frame, first, next, t)) return fallback;
    const auto& a = keys[first];
    const auto& b = keys[next];
    if (first == next || t == 0 || a.ease == CurveEase::Hold) return a.value;
    if (a.ease == CurveEase::Bezier) {
        auto handles = bezierHandles(a, b);
        double u = curveParameter(t, handles);
        return (float)cubic(a.value, (double)a.value + handles[0].offset,
                           (double)b.value + handles[1].offset, b.value, u);
    }
    if (a.ease == CurveEase::Spline) return spline(keys, first, next, t, [](const auto& k) { return k.value; });
    return lerpf(a.value, b.value, easeTime((Ease)a.ease, t));
}

bool insertCurveSample(CurveTrack& keys, int frame, float fallback) {
    auto at = std::lower_bound(keys.begin(), keys.end(), frame, [](const CurveKey& key, int time) { return key.frame < time; });
    if (at != keys.end() && at->frame == frame) return false;
    CurveKey key{frame, sampleCurve(keys, (float)frame, fallback)};
    if (at != keys.begin() && at != keys.end()) {
        auto& a = *(at - 1);
        auto& b = *at;
        key.ease = a.ease;
        if (a.ease == CurveEase::Bezier) {
            auto handles = bezierHandles(a, b);
            double atTime = (double)(frame - a.frame) / (b.frame - a.frame);
            double t = curveParameter(atTime, handles);
            struct Point { double x, y; };
            auto mix = [t](Point p, Point q) { return Point{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t}; };
            Point p{0, a.value}, q{handles[0].fraction, (double)a.value + handles[0].offset};
            Point r{1 - handles[1].fraction, (double)b.value + handles[1].offset}, s{1, b.value};
            Point pq = mix(p,q), qr = mix(q,r), rs = mix(r,s);
            Point l = mix(pq,qr), h = mix(qr,rs), mid = mix(l,h);
            key.value = (float)mid.y;
            a.out = {clampf((float)(pq.x / atTime), 0, 1), (float)(pq.y - p.y)};
            key.in = {clampf((float)((atTime - l.x) / atTime), 0, 1), (float)(l.y - mid.y)};
            key.out = {clampf((float)((h.x - atTime) / (1 - atTime)), 0, 1), (float)(h.y - mid.y)};
            b.in = {clampf((float)((1 - rs.x) / (1 - atTime)), 0, 1), (float)(rs.y - s.y)};
        }
    }
    keys.insert(at, key);
    return true;
}

CurveTrack splitTransformChannel(const std::vector<Key>& keys, int channel) {
    CurveTrack track;
    track.reserve(keys.size());
    for (const auto& key : keys) track.push_back(toCurveKey(key, channel));
    return track;
}

Key sampleTransformAnimation(const std::vector<Key>& keys, const TransformCurves* curves, float frame,
                             const Key& fallback, bool joint) {
    if (!curves) return sampleTransformTrack(keys, frame, fallback);
    Key pose = fallback;
    pose.frame = (int)frame;
    for (int channel = 0; channel < TRANSFORM_CHANNELS; ++channel) {
        const auto& track = curves->channels[channel];
        if (track.empty()) continue;
        float& v = transformChannelSlot(pose, channel);
        v = sampleCurve(track, frame, v);
        if (channel >= 6) v = clampf(v, .01f, joint ? 100.f : 1e6f);
    }
    return pose;
}

bool validTransformCurves(const TransformCurves& curves, bool joint) {
    for (int channel = 0; channel < TRANSFORM_CHANNELS; ++channel)
        if (!validCurveTrack(curves.channels[channel], channel, joint)) return false;
    return hasCurveKeys(curves);
}

bool replaceTransformChannel(std::vector<Key>& legacy, std::shared_ptr<const TransformCurves>& curves,
                             int channel, CurveTrack replacement, bool joint) {
    if (channel < 0 || channel >= TRANSFORM_CHANNELS || !validCurveTrack(replacement, channel, joint)) return false;
    auto edited = curves ? std::make_shared<TransformCurves>(*curves) : std::make_shared<TransformCurves>();
    if (!curves) {
        for (int i = 0; i < TRANSFORM_CHANNELS; ++i) {
            if (i == channel) continue;
            edited->channels[i] = splitTransformChannel(legacy, i);
            if (!validCurveTrack(edited->channels[i], i, joint)) return false;
        }
    }
    edited->channels[channel] = std::move(replacement);
    curves = hasCurveKeys(*edited) ? std::move(edited) : nullptr;
    legacy.clear();
    return true;
}

bool setTransformKey(std::vector<Key>& legacy, std::shared_ptr<const TransformCurves>& curves, const Key& key,
                     bool joint, unsigned channels) {
    if (!curves) { putKey(legacy, key); return true; }
    auto edited = std::make_shared<TransformCurves>(*curves);
    for (int channel = 0; channel < TRANSFORM_CHANNELS; ++channel) {
        if (!(channels & (1u << channel))) continue;
        auto& track = edited->channels[channel];
        auto at = std::lower_bound(track.begin(), track.end(), key.frame, [](const CurveKey& k, int time) { return k.frame < time; });
        if (at != track.end() && at->frame == key.frame) at->value = transformChannelValue(key, channel);
        else track.insert(at, toCurveKey(key, channel));
        if (!validCurveTrack(track, channel, joint)) return false;
    }
    curves = std::move(edited);
    return true;
}

bool removeTransformKeys(std::vector<Key>& legacy, std::shared_ptr<const TransformCurves>& curves, int frame) {
    if (!curves) return eraseKey(legacy, frame);
    auto edited = std::make_shared<TransformCurves>(*curves);
    bool changed = false;
    for (auto& track : edited->channels) changed |= eraseKey(track, frame);
    if (changed) curves = hasCurveKeys(*edited) ? std::move(edited) : nullptr;
    return changed;
}

bool retimeTransformKeys(std::shared_ptr<const TransformCurves>& curves, int from, int to) {
    if (!curves || from == to || to < 0 || to >= 100000) return false;
    auto edited = std::make_shared<TransformCurves>(*curves);
    bool changed = false;
    for (auto& track : edited->channels) {
        auto at = std::lower_bound(track.begin(), track.end(), from, [](const CurveKey& k, int time) { return k.frame < time; });
        if (at == track.end() || at->frame != from) continue;
        if (std::any_of(track.begin(), track.end(), [to](const auto& k) { return k.frame == to; })) return false;
        CurveKey moved = *at;
        moved.frame = to;
        track.erase(at);
        putKey(track, moved);
        changed = true;
    }
    if (changed) curves = std::move(edited);
    return changed;
}

std::vector<int> transformKeyFrames(const std::vector<Key>& legacy, const TransformCurves* curves) {
    std::vector<int> frames;
    if (!curves) {
        frames.reserve(legacy.size());
        for (const auto& key : legacy) frames.push_back(key.frame);
    } else {
        for (const auto& track : curves->channels) for (const auto& key : track) frames.push_back(key.frame);
        std::sort(frames.begin(), frames.end());
        frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    }
    return frames;
}

std::shared_ptr<const TransformCurves> bakeTransformCurves(const TransformCurves& original, const std::vector<Key>& samples,
                                                          const Key& fallback, bool guardBoundaries, bool joint) {
    if (samples.empty()) return {};
    auto result = std::make_shared<TransformCurves>();
    int start = samples.front().frame, end = samples.back().frame + 1;
    for (int channel = 0; channel < TRANSFORM_CHANNELS; ++channel) {
        const auto& source = original.channels[channel];
        auto lower = [&](int frame) {
            return std::lower_bound(source.begin(), source.end(), frame, [](const CurveKey& key, int f) { return key.frame < f; });
        };
        auto channelFallback = transformChannelValue(fallback, channel);
        auto& track = result->channels[channel];
        track.assign(source.begin(), lower(start));
        if (guardBoundaries && start && (track.empty() || track.back().frame < start - 1))
            track.push_back({start - 1, sampleCurve(source, (float)(start - 1), channelFallback), CurveEase::Hold});
        for (const auto& sample : samples) track.push_back(toCurveKey(sample, channel));
        auto after = lower(end);
        if (guardBoundaries && after != source.end() && after->frame > end) {
            auto bridge = source;
            insertCurveSample(bridge, end, channelFallback);
            auto cut = std::lower_bound(bridge.begin(), bridge.end(), end, [](const CurveKey& key, int f) { return key.frame < f; });
            track.insert(track.end(), cut, bridge.end());
        } else track.insert(track.end(), after, source.end());
        if (!validCurveTrack(track, channel, joint)) return {};
    }
    return result;
}

Key Object::sampleTransform(float frame) const {
    return sampleTransformAnimation(keys, curves.get(), frame, {0, pos, rot, scl});
}

IKKey sampleIKTrack(const std::vector<IKKey>& keys, float frame, const IKKey& fallback) {
    int first, next;
    float t;
    if (!keySpan(keys, frame, first, next, t)) {
        IKKey goal = fallback;
        goal.frame = (int)frame;
        return goal;
    }
    const IKKey& a = keys[first];
    const IKKey& b = keys[next];
    float blend = a.ease == Ease::Spline ?
                  spline(keys, first, next, t, [](const IKKey& key) { return key.influence; }) :
                  lerpf(a.influence, b.influence, easeTime(a.ease, t));
    return {(int)frame, motion(keys, first, next, t, [](const IKKey& key) { return key.target; }),
            motion(keys, first, next, t, [](const IKKey& key) { return key.pole; }),
            clampf(blend, 0, 1), a.ease};
}

AppearanceKey Object::appearance(int f) const {
    AppearanceKey key{f, mat, picture, terrain, lightColor, {lightPower, spotAngle, spotSoft, lightSize}, hidden};
    if (isLight(type)) key.lighting = lighting;
    return key;
}

static void blendAppearance(Object& o, const AppearanceKey& a, const AppearanceKey& b, float t) {
    t = easeTime(a.ease, t);
    o.mat = a.material;
    o.mat.color1 = lerp(a.material.color1, b.material.color1, t);
    o.mat.color2 = lerp(a.material.color2, b.material.color2, t);
    for (auto property : {&Material::diffuse, &Material::ambient, &Material::specular, &Material::shininess,
                          &Material::reflect, &Material::transparency, &Material::ior, &Material::bump,
                          &Material::metal, &Material::emission, &Material::blur, &Material::absorption,
                          &Material::volumeDensity, &Material::volumeScatter, &Material::volumeNoise})
        o.mat.*property = lerpf(a.material.*property, b.material.*property, t);
    o.mat.texScale = lerpf(a.material.texScale, b.material.texScale, t);
    for (size_t i = 0; i < o.mat.layers.size() && i < b.material.layers.size(); ++i) {
        auto& layer = o.mat.layers[i];
        const auto& other = b.material.layers[i];
        layer.color1 = lerp(layer.color1, other.color1, t); layer.color2 = lerp(layer.color2, other.color2, t);
        layer.offset = lerp(layer.offset, other.offset, t); layer.rotation = lerp(layer.rotation, other.rotation, t);
        layer.texScale = lerpf(layer.texScale, other.texScale, t); layer.opacity = lerpf(layer.opacity, other.opacity, t);
        for (auto property : {&TextureLayer::warp, &TextureLayer::contrast, &TextureLayer::bias,
                              &TextureLayer::maskLow, &TextureLayer::maskHigh, &TextureLayer::maskFeather})
            layer.*property = lerpf(layer.*property, other.*property, t);
        for (int j = 0; j < TexChannelCount; ++j) layer.influence[j] = lerpf(layer.influence[j], other.influence[j], t);
    }
    o.lightColor = lerp(a.lightColor, b.lightColor, t);
    o.lightPower = lerpf(a.light[0], b.light[0], t);
    o.spotAngle = lerpf(a.light[1], b.light[1], t);
    o.spotSoft = lerpf(a.light[2], b.light[2], t);
    o.lightSize = lerpf(a.light[3], b.light[3], t);
    if (a.lighting) {
        o.lighting = *a.lighting;
        if (b.lighting) {
            o.lighting.range = lerpf(a.lighting->range, b.lighting->range, t);
            o.lighting.diffuseStrength = lerpf(a.lighting->diffuseStrength, b.lighting->diffuseStrength, t);
            o.lighting.specularStrength = lerpf(a.lighting->specularStrength, b.lighting->specularStrength, t);
        }
    }
    o.terrain = {lerpf(a.terrain.height, b.terrain.height, t), lerpf(a.terrain.clip, b.terrain.clip, t),
                 lerpf(a.terrain.thickness, b.terrain.thickness, t), lerpf(a.terrain.smoothing, b.terrain.smoothing, t)};
    o.picture = a.picture;
    o.picture.keyTolerance = lerpf(a.picture.keyTolerance, b.picture.keyTolerance, t);
    o.hidden = a.hidden;
}

void Scene::applyFrame(int f) {
    frame = f;
    int i0, i1;
    float t;
    for (Object& o : objs) {
        if (o.animatedTransform()) {
            Key pose = o.transformAt(f);
            o.pos = pose.pos;
            o.rot = pose.rot;
            o.scl = pose.scl;
        }
        if (keySpan(o.appearances, (float)f, i0, i1, t)) blendAppearance(o, o.appearances[i0], o.appearances[i1], t);
    }
    cam = cameraAt((float)f);
    if (keySpan(skyKeys, (float)f, i0, i1, t)) sky = lerpSky(skyKeys[i0].sky, skyKeys[i1].sky, t);
    updateAll();
}

Camera Scene::cameraAt(float frame) const {
    int first, next;
    float t;
    if (!keySpan(camKeys, frame, first, next, t)) return cam;
    Camera camera = camKeys[first].cam;
    camera.target = motion(camKeys, first, next, t, [](const CamKey& k) { return k.cam.target; });
    V3 pose = motion(camKeys, first, next, t, [](const CamKey& k) { return V3(k.cam.yaw, k.cam.pitch, k.cam.dist); });
    camera.yaw = pose.x; camera.pitch = clampf(pose.y, -90, 90); camera.dist = std::max(pose.z, 1.f);
    float eased = easeTime(camKeys[first].ease, t);
    camera.fov = lerpf(camera.fov, camKeys[next].cam.fov, eased);
    camera.orthoScale = lerpf(camera.orthoScale, camKeys[next].cam.orthoScale, eased);
    return camera;
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

static bool hitTerrain(const Object& terrain, V3 o, V3 d, LHit& h) {
    const Heightfield& f = *terrain.hf;
    float t0 = EPS, t1 = h.t;
    if (!boxSpan(V3(-1, 0, -1), V3(1, terrain.terrain.height, 1), o, d, t0, t1)) return false;
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
        float h00 = terrain.terrainAt(i, j), h10 = terrain.terrainAt(i + 1, j);
        float h01 = terrain.terrainAt(i, j + 1), h11 = terrain.terrainAt(i + 1, j + 1);
        float hmax = std::max(std::max(h00, h10), std::max(h01, h11));
        float hmin = std::min(std::min(h00, h10), std::min(h01, h11));
        if ((hmax > 1e-4f || terrain.terrain.thickness > 0) &&
            std::min(y0, y1) <= hmax + 1e-4f && std::max(y0, y1) >= hmin - 1e-4f) {
            float x0 = -1 + i * cs, z0 = -1 + j * cs;
            V3 a(x0, h00, z0), b(x0 + cs, h10, z0), c(x0, h01, z0 + cs), e(x0 + cs, h11, z0 + cs);
            float bt = INF, bu = 0, bv = 0, t, u, v;
            int tri = -1;
            if (triHit(o, d, a, b, e, t, u, v) && t > EPS && t < bt) { bt = t; bu = u; bv = v; tri = 0; }
            if (triHit(o, d, a, e, c, t, u, v) && t > EPS && t < bt) { bt = t; bu = u; bv = v; tri = 1; }
            if (tri >= 0 && bt < h.t) {
                V3 na = f.normal(i, j), ne = f.normal(i + 1, j + 1);
                V3 nb = tri == 0 ? f.normal(i + 1, j) : ne, nc = tri == 0 ? ne : f.normal(i, j + 1);
                h.t = bt;
                V3 n = na * (1 - bu - bv) + nb * bu + nc * bv;
                h.n = terrain.morphFrom || terrain.terrain.clip > 0 || terrain.terrain.smoothing > 0 ? normalize(tri == 0 ? cross(e - a, b - a) : cross(c - a, e - a)) :
                    normalize({n.x * terrain.terrain.height, n.y, n.z * terrain.terrain.height});
                return true;
            }
        }
        if (tmx < tmz) { i += si; tEnter = tmx; tmx += tdx; }
        else { j += sj; tEnter = tmz; tmz += tdz; }
    }
    return false;
}

static bool hitTerrainBase(const Object& ob, V3 o, V3 d, LHit& h) {
    bool lattice = ob.type == ObjType::Lattice;
    if (!lattice && ob.terrain.thickness <= 0) return false;
    float floor = -ob.terrain.thickness;
    bool found = false;
    auto face = [&](int axis, float plane, V3 normal) {
        if (std::fabs(d[axis]) < 1e-12f) return;
        float t = (plane - o[axis]) / d[axis];
        if (t <= EPS || t >= h.t) return;
        V3 p = o + d * t;
        if (axis == 1) {
            if (std::fabs(p.x) > 1 || std::fabs(p.z) > 1) return;
        } else {
            float u = axis == 0 ? p.z : p.x;
            if (std::fabs(u) > 1 || (!lattice && p.y < floor)) return;
            int n = ob.hf->res - 1, k = plane < 0 ? 0 : n;
            float s = (u + 1) * 0.5f * n;
            int i = std::min((int)s, n - 1);
            float a = axis == 0 ? ob.terrainAt(k, i) : ob.terrainAt(i, k);
            float b = axis == 0 ? ob.terrainAt(k, i + 1) : ob.terrainAt(i + 1, k);
            float top = lerpf(a, b, s - i);
            if (p.y > top || (lattice && p.y < -top)) return;
        }
        h = {t, normal};
        found = true;
    };
    if (!lattice) face(1, floor, {0, -1, 0});
    for (float sign : {-1.f, 1.f}) { face(0, sign, {sign, 0, 0}); face(2, sign, {0, 0, sign}); }
    return found;
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
    h.lr = ob.inv * (h.p - ob.worldPos);
    h.lp = h.lr * ob.scl;
    h.ly = h.lr.y;
}

static bool intersectObj(const Object& ob, const Ray& r, float tmax, Hit& h) {
    if (isLight(ob.type) || sphereReject(ob, r, tmax)) return false;
    V3 lo = ob.inv * (r.o - ob.worldPos), ld = ob.inv * r.d;
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
        float u = 0.5f + 0.5f * p.x / ext.x, v = 0.5f - 0.5f * p.y / ext.y;
        float alpha = picture.image->opacity(u, v);
        V3 color = picture.image->sampleClamped(u, v, picture.chroma || !picture.image->alpha.empty());
        if (alpha < 0.004f || (picture.chroma && length(color - picture.keyColor) <= picture.keyTolerance)) break;
        h.pictureColor = color;
        h.pictureOpacity = alpha;
        lh = {t, {0, 0, 1}};
        ok = true;
        break;
    }
    case ObjType::Terrain:
        if (ob.hf && ob.hf->res > 1) {
            ok = hitTerrain(ob, lo, ld, lh);
            if (hitTerrainBase(ob, lo, ld, lh)) ok = true;
        }
        break;
    case ObjType::Lattice:
        if (ob.hf && ob.hf->res > 1) {
            if (hitTerrain(ob, lo, ld, lh)) ok = true;
            LHit m = lh;
            if (hitTerrain(ob, V3(lo.x, -lo.y, lo.z), V3(ld.x, -ld.y, ld.z), m)) { lh = {m.t, V3(m.n.x, -m.n.y, m.n.z)}; ok = true; }
            if (ob.csg && hitTerrainBase(ob, lo, ld, lh)) ok = true;
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
        const Mesh* mesh = ob.visibleMesh();
        if (mesh && mesh->intersect(lo, ld, EPS, tmax, t, n, ob.flatMesh, &h.uv)) { lh = {t, n}; ok = true; }
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
        V3 lo = ob.inv * (r.o - ob.worldPos), ld = ob.inv * r.d;
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
            int mi = surf.boolMode == BoolMode::Negative && !s.cutterMaterial ? firstPos : ev[e].m;
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
    float near = EPS, far = tmax;
    if (g.lower.x > g.upper.x || !boxSpan(g.lower, g.upper, r.o, r.d, near, far)) return false;
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
        V3 a = o.inv * (r.o - o.worldPos) / o.metaRadius, d = o.inv * r.d / o.metaRadius;
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
        if (sphereReject(o, r, tmax) || !convexSpan(o.type, o.inv * (r.o - o.worldPos), o.inv * r.d, tn, tf, nn, nf)) continue;
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
            V3 local = o.inv * (point - o.worldPos) / o.metaRadius;
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
            h.obj = o.boolMode == BoolMode::Negative && !s.cutterMaterial ? material : surface->obj;
            fillHit(s.objs[h.obj], r, t, {}, h);
            h.n = normalize(o.nrm * surface->normal);
            if ((dot(h.n, r.d) < 0) != inside()) h.n = -h.n;
            return true;
        }
        from = t;
    }
    return ballSurface(from, tmax);
}

static bool intersectNested(const Scene& s, int root, const Ray& ray, float limit, Hit& h, int skip, int depth = 0) {
    if (depth >= 32) return false;
    const auto& node = s.booleanNodes[root];
    struct Operand { int index, kind; BoolMode role; Hit hit; bool available = false, inside = false; };
    std::vector<Operand> items;
    for (int i : node.parts) { if (i == skip) return false; items.push_back({i, 0, s.objs[i].boolMode, {}}); }
    for (auto [i, mode] : node.children) items.push_back({i, 1, mode, {}});
    if (node.field >= 0) items.push_back({node.field, 2, BoolMode::Positive, {}});
    auto advance = [&](Operand& in, float start) {
        Ray r{ray.o + ray.d * start, ray.d};
        if (in.kind == 0) {
            in.available = intersectObj(s.objs[in.index], r, 1e30f, in.hit);
            in.hit.obj = in.index;
        } else if (in.kind == 1) {
            in.available = intersectNested(s, in.index, r, 1e30f, in.hit, skip, depth + 1);
        } else {
            in.available = intersectMetaballs(s, s.metaballs[in.index], r, 1e30f, in.hit, skip);
        }
        if (in.available) in.hit.t += start;
    };
    for (auto& in : items) {
        advance(in, 0);
        in.inside = in.available && dot(in.hit.n, ray.d) > 0;
    }
    auto inside = [&] {
        bool any = false;
        for (const auto& in : items) {
            if ((in.role == BoolMode::Negative && in.inside) || (in.role == BoolMode::Intersect && !in.inside)) return false;
            if (in.role == BoolMode::Positive && in.inside) any = true;
        }
        return any;
    };
    for (int iteration = 0; iteration < 512; ++iteration) {
        float best = limit;
        for (const auto& in : items) if (in.available) best = std::min(best, in.hit.t);
        if (best >= limit) return false;
        bool before = inside();
        Hit face;
        BoolMode role = BoolMode::Neutral;
        for (auto& in : items) {
            if (!in.available || in.hit.t > best + 1e-5f) continue;
            bool old = inside();
            in.inside = dot(in.hit.n, ray.d) < 0;
            if (old != inside()) { face = in.hit; role = in.role; }
            advance(in, in.hit.t);
        }
        if (before == inside()) continue;
        h = face;
        if ((dot(h.n, ray.d) < 0) != inside()) h.n = -h.n;
        if (role == BoolMode::Negative && !s.cutterMaterial) {
            for (const auto& in : items) if (in.role == BoolMode::Positive && in.inside && in.hit.obj >= 0) {
                h.obj = in.hit.obj;
                const Object& m = s.objs[h.obj];
                h.lr = m.inv * (h.p - m.worldPos);
                h.lp = h.lr * m.scl;
                h.ly = h.lr.y;
                break;
            }
        }
        return true;
    }
    return false;
}

bool intersectObject(const Object& o, const Ray& r, float tmax, Hit& h) { return intersectObj(o, r, tmax, h); }

bool intersectScene(const Scene& s, const Ray& r, float tmax, Hit& h, int skip, RayPurpose purpose) {
    bool any = false;
    auto test = [&](int i) {
        const Object& o = s.objs[i];
        if (purpose != RayPurpose::View && canVolume(o.type) && o.mat.volumeDensity > 0 && !o.csg) return;
        if (o.type == ObjType::Picture && ((purpose == RayPurpose::Shadow && !o.picture.shadows) ||
                                           (purpose == RayPurpose::Reflection && !o.picture.reflections))) return;
        Hit t;
        if (i != skip && !o.hidden && !o.csg && intersectObj(o, r, any ? tmax + 1e-6f : tmax, t) &&
            (t.t < tmax || (any && t.t == tmax && i < h.obj))) {
            tmax = t.t;
            h = t;
            h.obj = i;
            any = true;
        }
    };
    if (s.rayNodes.empty()) {
        for (int i = 0; i < (int)s.objs.size(); ++i) test(i);
    } else {
        for (int i : s.unbounded) test(i);
        int stack[64], next = 0;
        stack[next++] = 0;
        while (next) {
            const auto& node = s.rayNodes[stack[--next]];
            float near = EPS, far = tmax;
            if (!boxSpan(node.lower, node.upper, r.o, r.d, near, far)) continue;
            if (node.left < 0) {
                for (int i = 0; i < node.count; ++i) test(s.rayOrder[node.first + i]);
            } else {
                float a = EPS, az = tmax, b = EPS, bz = tmax;
                const auto& l = s.rayNodes[node.left]; const auto& rt = s.rayNodes[node.right];
                bool left = boxSpan(l.lower, l.upper, r.o, r.d, a, az);
                bool right = boxSpan(rt.lower, rt.upper, r.o, r.d, b, bz);
                if (left && right) {
                    stack[next++] = a < b ? node.right : node.left;
                    stack[next++] = a < b ? node.left : node.right;
                } else if (left || right) stack[next++] = left ? node.left : node.right;
            }
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
        if (g.nested) continue;
        Hit t;
        if (intersectMetaballs(s, g, r, tmax, t, skip)) {
            tmax = t.t;
            h = t;
            any = true;
        }
    }
    for (int i = 0; i < (int)s.booleanNodes.size(); ++i) {
        if (s.booleanNodes[i].nested) continue;
        Hit t;
        if (intersectNested(s, i, r, tmax, t, skip)) { h = t; tmax = t.t; any = true; }
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
        s.push_back(o.worldPos + o.fwd * a);
        s.push_back(o.worldPos + o.fwd * b);
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
        auto P = [&](int x, int z) { return V3(-1 + x * cs, o.terrainAt(x, z) * sign, -1 + z * cs); };
        auto E = [&](int x0, int z0, int x1, int z1) {
            if (o.terrainAt(x0, z0) > 1e-4f || o.terrainAt(x1, z1) > 1e-4f) L(P(x0, z0), P(x1, z1));
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
        if (const Mesh* mesh = o.visibleMesh()) {
            const Mesh& m = *mesh;
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
            s.push_back(o.worldPos - a); s.push_back(o.worldPos + a);
        }
        break;
    case ObjType::Spotlight: case ObjType::Parallel: {
        V3 ax = o.lightAxis(), t = std::fabs(ax.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0), u = normalize(cross(ax, t)), w = cross(ax, u);
        bool spot = o.type == ObjType::Spotlight;
        float len = spot ? 4.f : 5.f, rr = spot ? std::tan(rad(clampf(o.spotAngle, 1, 89))) * len : 1.f;
        for (int i = 0; i < 8; i++) {
            float a0 = 2 * PI * i / 8, a1 = 2 * PI * (i + 1) / 8;
            V3 p0 = (u * std::cos(a0) + w * std::sin(a0)) * rr, p1 = (u * std::cos(a1) + w * std::sin(a1)) * rr;
            V3 b0 = spot ? o.worldPos : o.worldPos + p0;
            s.push_back(b0); s.push_back(o.worldPos + ax * len + p0);
            s.push_back(o.worldPos + ax * len + p0); s.push_back(o.worldPos + ax * len + p1);
            if (!spot) { s.push_back(o.worldPos + p0); s.push_back(o.worldPos + p1); }
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

static void writeCam(std::ostream& f, const Camera& c) {
    wv(f, c.target);
    f << c.yaw << ' ' << c.pitch << ' ' << c.dist << ' ' << c.fov << ' ' << c.orthographic << ' ' << c.orthoScale << ' ' << c.lookAt << ' ';
}
static void readCam(std::istream& f, Camera& c, int ver) {
    rv(f, c.target);
    f >> c.yaw >> c.pitch >> c.dist >> c.fov;
    if (ver >= 4) { rb(f, c.orthographic); f >> c.orthoScale >> c.lookAt; }
    c.orthoScale = clampf(c.orthoScale, 0.1f, 5000);
}

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
    f << k.cloudAltitude << ' ' << k.cloudThickness << ' ' << k.cloudShadow << ' ' << k.skylight << ' ';
    wv(f, k.stratusColor); wv(f, k.moonColor);
    ws(f, k.environment);
    f << k.environmentYaw << ' ' << k.environmentStrength << ' ' << k.environmentSamples << ' ' << k.environmentBackground << ' '
      << k.indirectStrength << ' ' << k.indirectSamples << ' ' << k.indirectBounces << ' ';
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
    if (ver >= 4) {
        f >> k.cloudAltitude >> k.cloudThickness >> k.cloudShadow >> k.skylight;
        rv(f, k.stratusColor); rv(f, k.moonColor);
        k.cloudAltitude = clampf(k.cloudAltitude, 1, 200);
        k.cloudThickness = clampf(k.cloudThickness, 0.1f, 30);
    }
    if (ver >= 5) {
        rs(f, k.environment, ver);
        f >> k.environmentYaw >> k.environmentStrength >> k.environmentSamples;
        rb(f, k.environmentBackground);
        f >> k.indirectStrength >> k.indirectSamples;
        k.environmentStrength = clampf(k.environmentStrength, 0, 64);
        k.environmentSamples = std::clamp(k.environmentSamples, 1, 64);
        k.indirectStrength = clampf(k.indirectStrength, 0, 2);
        k.indirectSamples = std::clamp(k.indirectSamples, 1, 16);
    }
    if (ver >= 6) {
        f >> k.indirectBounces;
        k.indirectBounces = std::clamp(k.indirectBounces, 1, 4);
    }
}

static void writeLayer(std::ostream& f, const TextureLayer& l) {
    f << (int)l.tex << ' ' << (int)l.map << ' ' << (int)l.blend << ' ' << (int)l.mask << ' ' << l.texScale << ' ' << l.opacity << ' ';
    wv(f, l.color1); wv(f, l.color2); wv(f, l.offset); wv(f, l.rotation);
    for (float value : l.influence) f << value << ' ';
    ws(f, l.image);
    f << l.warp << ' ' << l.contrast << ' ' << l.bias << ' ' << l.maskLow << ' ' << l.maskHigh << ' ' << l.maskFeather << ' ' << l.octaves << ' ';
    f << (int)l.heightOutput << ' ' << (int)l.transparencyOutput << ' ';
}

static bool readLayer(std::istream& f, TextureLayer& l, int version) {
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
    if (version >= 2) {
        f >> l.warp >> l.contrast >> l.bias >> l.maskLow >> l.maskHigh >> l.maskFeather >> l.octaves;
        l.warp = clampf(l.warp, 0, 4);
        l.contrast = clampf(l.contrast, 0, 4);
        l.bias = clampf(l.bias, -1, 1);
        l.maskLow = clampf(l.maskLow, 0, 1);
        l.maskHigh = clampf(l.maskHigh, 0, 1);
        l.maskFeather = clampf(l.maskFeather, 0.001f, 0.5f);
        l.octaves = std::clamp(l.octaves, 1, 10);
    }
    if (version >= 3) {
        int height, transparency;
        if (!(f >> height >> transparency) || height < 0 || height >= (int)TexOutput::COUNT
            || transparency < 0 || transparency >= (int)TexOutput::COUNT) return false;
        l.heightOutput = (TexOutput)height;
        l.transparencyOutput = (TexOutput)transparency;
    }
    if (!f) return false;
    l.img.reset();
    return true;
}

bool TextureLayer::save(const std::string& path) const {
    std::ofstream f(path);
    f << std::setprecision(7) << "CANYONTEX 3\n";
    writeLayer(f, *this);
    f << '\n';
    return (bool)f;
}

bool TextureLayer::load(const std::string& path) {
    std::ifstream f(path);
    std::string header;
    int version = 0;
    TextureLayer layer;
    if (!(f >> header >> version) || header != "CANYONTEX" || version < 1 || version > 3 || !readLayer(f, layer, version)) return false;
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
    f << m.absorption << ' ' << m.volumeDensity << ' ' << m.volumeScatter << ' ' << m.volumeNoise << ' ';
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
        if (!readLayer(f, layer, ver >= 6 ? 3 : (ver >= 5 ? 2 : 1))) return false;
    if (ver >= 4) {
        f >> m.absorption >> m.volumeDensity >> m.volumeScatter >> m.volumeNoise;
        m.absorption = clampf(m.absorption, 0, 10);
        m.volumeDensity = clampf(m.volumeDensity, 0, 8);
        m.volumeScatter = clampf(m.volumeScatter, 0, 1);
        m.volumeNoise = clampf(m.volumeNoise, 0, 1);
    }
    return (bool)f;
}

static bool readEase(std::istream& f, Ease& ease) {
    int value = 0;
    if (!(f >> value) || value < 0 || value >= (int)Ease::COUNT) return false;
    ease = (Ease)value;
    return true;
}

static void writePicture(std::ostream& f, const Picture& p) {
    ws(f, p.path); wv(f, p.keyColor);
    f << p.aspect << ' ' << p.keyTolerance << ' ' << p.billboard << ' ' << p.chroma << ' ' << p.shadows << ' ' << p.reflections << ' ';
}

static bool readPicture(std::istream& f, Picture& p, int ver) {
    rs(f, p.path, ver); rv(f, p.keyColor);
    f >> p.aspect >> p.keyTolerance;
    rb(f, p.billboard); rb(f, p.chroma); rb(f, p.shadows); rb(f, p.reflections);
    return f && p.aspect >= 1.f / 16385 && p.aspect <= 16385 && p.keyTolerance >= 0 && p.keyTolerance <= 1;
}

static bool readTerrainShape(std::istream& f, TerrainShape& t, int ver) {
    f >> t.height >> t.clip >> t.thickness;
    if (ver >= 5) f >> t.smoothing;
    return f && t.height >= 0.05f && t.height <= 5 && t.clip >= 0 && t.clip <= 1 &&
           t.thickness >= 0 && t.thickness <= 2 && t.smoothing >= 0 && t.smoothing <= 1;
}

static void writeLightSettings(std::ostream& f, const LightSettings& l) {
    f << (int)l.falloff << ' ' << l.range << ' ' << l.samples << ' ' << l.casts << ' ' << l.disk << ' '
      << l.enabled << ' ' << l.visible << ' ';
    ws(f, l.gel);
    f << l.diffuseStrength << ' ' << l.specularStrength << ' ' << l.targetGroup << ' ' << l.excludeGroup << ' ';
}

static bool readLightSettings(std::istream& f, LightSettings& l, int ver) {
    int falloff = 0;
    if (!(f >> falloff >> l.range >> l.samples) || falloff < 0 || falloff >= (int)Falloff::COUNT) return false;
    l.falloff = (Falloff)falloff;
    l.range = clampf(l.range, 0.05f, 10000);
    l.samples = std::clamp(l.samples, 1, 16);
    rb(f, l.casts); rb(f, l.disk); rb(f, l.enabled); rb(f, l.visible);
    rs(f, l.gel, ver);
    if (ver >= 5) {
        f >> l.diffuseStrength >> l.specularStrength >> l.targetGroup;
        rb(f, l.excludeGroup);
        l.diffuseStrength = clampf(l.diffuseStrength, 0, 2);
        l.specularStrength = clampf(l.specularStrength, 0, 2);
        if (l.targetGroup < 0) return false;
    }
    return (bool)f;
}

template <class S, class F> static void sceneAssets(S& scene, F visit) {
    visit(scene.sky.environment, scene.sky.environmentImage);
    for (auto& key : scene.skyKeys) visit(key.sky.environment, key.sky.environmentImage);
    auto material = [&](auto& mat) {
        visit(mat.image, mat.img);
        for (auto& layer : mat.layers) visit(layer.image, layer.img);
    };
    for (auto& o : scene.objs) {
        material(o.mat);
        visit(o.picture.path, o.picture.image);
        visit(o.lighting.gel, o.lighting.gelImage);
        for (auto& key : o.appearances) {
            material(key.material);
            visit(key.picture.path, key.picture.image);
            if (key.lighting) visit(key.lighting->gel, key.lighting->gelImage);
        }
    }
}

static bool assetIsFile(const std::string& path) {
    return !path.empty() && path.rfind("builtin:", 0) != 0 && path.rfind("embed:", 0) != 0;
}

bool Scene::embedAssets(std::string& error) {
    Scene output = *this;
    std::map<std::string, std::string> names;
    size_t total = 0;
    for (auto& entry : embeddedImages) total += entry.second->px.size();
    std::string prefix = "embed:" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_";
    sceneAssets(output, [&](std::string& value, auto& data) {
        if (!error.empty() || !assetIsFile(value)) return;
        if (names.count(value)) { value = names[value]; data = output.embeddedImages[value]; return; }
        auto image = data ? data : loadImageCached(value, true);
        if (!image) { error = "could not decode " + value; return; }
        if ((total += image->px.size()) > 8000000) { error = "embedded-image limit is eight million pixels"; return; }
        std::string key = prefix + std::to_string(names.size());
        names.emplace(value, key);
        output.embeddedImages.emplace(key, image);
        value = key;
        data = image;
    });
    if (!error.empty()) return false;
    *this = std::move(output);
    return true;
}

bool Scene::save(const std::string& path) const {
    namespace fs = std::filesystem;
    std::error_code error;
    auto dest = fs::absolute(path, error);
    if (error) return false;
    auto root = dest.parent_path();
    Scene output = *this;
    sceneAssets(output, [&](std::string& value, auto&) {
        if (!assetIsFile(value)) return;
        auto absolute = fs::absolute(value, error);
        if (error) { error.clear(); return; }
        auto relative = absolute.lexically_normal().lexically_relative(root.lexically_normal());
        value = relative.empty() ? absolute.string() : relative.generic_string();
    });
    std::string temp = dest.string() + ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    if (output.writeFile(temp)) {
        fs::rename(temp, dest, error);
        if (!error) return true;
    }
    fs::remove(temp, error);
    return false;
}

bool Scene::package(const std::string& path, std::string& error) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto file = fs::absolute(path, ec);
    if (ec) { error = "invalid destination"; return false; }
    auto folder = file.parent_path() / (file.stem().string() + "_assets");
    fs::create_directories(folder, ec);
    if (ec) { error = "cannot create the asset folder"; return false; }
    Scene output = *this;
    std::map<std::string, std::string> copies;
    sceneAssets(output, [&](std::string& value, auto&) {
        if (!error.empty() || !assetIsFile(value)) return;
        auto old = copies.find(value);
        if (old != copies.end()) { value = old->second; return; }
        auto source = fs::path(value);
        if (!fs::is_regular_file(source, ec)) { error = "missing asset " + value; return; }
        std::string stem = source.filename().string();
        auto dest = folder / stem;
        for (unsigned suffix = 2; fs::exists(dest, ec) && !ec; ++suffix) dest = folder / (std::to_string(suffix) + "_" + stem);
        if (ec || !fs::copy_file(source, dest, fs::copy_options::none, ec)) { error = "could not copy " + value; return; }
        value = copies.emplace(value, dest.string()).first->second;
    });
    if (!error.empty()) return false;
    if (output.save(path)) return true;
    error = "could not save the scene";
    return false;
}

std::vector<std::string> Scene::missingAssets() const {
    std::set<std::string> paths;
    sceneAssets(*this, [&](const std::string& value, const auto&) {
        std::error_code ec;
        if (assetIsFile(value) && !std::filesystem::is_regular_file(value, ec)) paths.insert(value);
    });
    return {paths.begin(), paths.end()};
}

bool Scene::relinkAsset(const std::string& source, const std::string& replacement) {
    auto image = loadImageCached(replacement, true);
    if (!image) return false;
    bool found = false;
    sceneAssets(*this, [&](std::string& value, auto& data) {
        if (value == source) { value = replacement; data = image; found = true; }
    });
    return found;
}

void Scene::reloadAssets() {
    std::map<std::string, std::shared_ptr<const Image>> loaded;
    sceneAssets(*this, [&](std::string& value, auto& data) {
        if (value.empty()) return;
        if (value.rfind("embed:", 0) == 0) return;
        auto [at, added] = loaded.emplace(value, nullptr);
        if (added) at->second = loadImageCached(value, true);
        data = at->second;
    });
}

static void writeTransformCurves(std::ostream& f, const TransformCurves& curves) {
    auto count = std::count_if(curves.channels.begin(), curves.channels.end(), [](const auto& track) { return !track.empty(); });
    f << count << '\n';
    for (int i = 0; i < TRANSFORM_CHANNELS; ++i) {
        const auto& channel = curves.channels[i];
        if (channel.empty()) continue;
        f << i << ' ' << channel.size() << '\n';
        for (const auto& key : channel)
            f << key.frame << ' ' << key.value << ' ' << (int)key.ease << ' ' << key.in.fraction << ' '
              << key.in.offset << ' ' << key.out.fraction << ' ' << key.out.offset << '\n';
    }
}

static std::shared_ptr<const TransformCurves> readTransformCurves(std::istream& f, bool joint) {
    int count;
    if (!(f >> count) || count < 1 || count > TRANSFORM_CHANNELS) return {};
    auto curves = std::make_shared<TransformCurves>();
    for (int i = 0; i < count; ++i) {
        int channel;
        size_t size;
        size_t limit = joint ? (size_t)MAX_JOINT_KEYS : MAX_OBJECT_CURVE_KEYS;
        if (!(f >> channel >> size) || channel < 0 || channel >= TRANSFORM_CHANNELS ||
            !curves->channels[channel].empty() || size == 0 || size > limit) return {};
        auto& track = curves->channels[channel];
        track.resize(size);
        for (auto& k : track) {
            int mode;
            if (!(f >> k.frame >> k.value >> mode >> k.in.fraction >> k.in.offset >> k.out.fraction >> k.out.offset) ||
                mode < 0 || mode >= (int)CurveEase::COUNT) return {};
            k.ease = (CurveEase)mode;
        }
    }
    if (!validTransformCurves(*curves, joint)) return {};
    return curves;
}

bool Scene::writeFile(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    bool hasRig = std::any_of(objs.begin(), objs.end(), [](const Object& o) { return bool(o.rig); });
    bool hasIK = std::any_of(objs.begin(), objs.end(), [](const Object& o) { return o.rig && !o.rig->ik.empty(); });
    bool hasActions = std::any_of(objs.begin(), objs.end(), [](const Object& o) { return o.rig && !o.rig->actions.empty(); });
    bool hasPolygons = std::any_of(objs.begin(), objs.end(), [](const Object& o) {
        return o.mesh && o.mesh->polygons && o.instanceSource < 0;
    });
    bool hasCurves = std::any_of(objs.begin(), objs.end(), [](const Object& o) {
        return o.curves || (o.rig && std::any_of(o.rig->joints.begin(), o.rig->joints.end(), [](const Joint& j) { return bool(j.curves); }));
    });
    f << std::setprecision(9) << "CANYON3D " << (hasCurves ? 11 : hasPolygons ? 10 : hasActions ? 9 : hasIK ? 8 : hasRig ? 7 : 6) << "\ncamera ";
    writeCam(f, cam);
    f << "\nsky ";
    writeSky(f, sky);
    if (!title.empty() || !description.empty()) {
        f << "\nmetadata ";
        ws(f, title); ws(f, description);
    }
    const RenderSettings& r = render;
    f << "\nrender " << r.width << ' ' << r.height << ' ' << r.quality << ' ' << r.samples << ' ' << r.dof << ' ' << r.panorama << ' '
      << r.softShadows << ' ' << r.blurReflections << ' ' << r.focusDist << ' ' << r.aperture << ' ' << r.gamma << ' ' << r.brightness
      << ' ' << r.contrast << ' ' << r.saturation << "\nanim " << frames << ' ' << fps << ' ' << frame << "\n";
    f << "renderfx " << r.bounces << ' ' << r.seed << ' ' << r.transparentShadows << ' ' << r.adaptive << ' '
      << r.denoise << ' ' << r.region << ' ' << r.cropX << ' ' << r.cropY << ' ' << r.cropW << ' ' << r.cropH << ' '
      << r.alpha << ' ' << r.depthOutput << ' ' << r.idOutput << '\n';
    if (cutterMaterial) f << "cuttermaterial\n";
    for (auto& link : booleanLinks) f << "booleanlink " << link.child << ' ' << link.parent << ' ' << (int)link.mode << '\n';
    for (int i = 0; i < 6; i++)
        if (memorySet[i]) { f << "memory " << i << ' '; writeCam(f, memory[i]); f << "\n"; }
    for (auto& k : camKeys) { f << "camkey " << k.frame << ' '; writeCam(f, k.cam); f << (int)k.ease << '\n'; }
    if (!cameras.empty()) f << "activecamera " << activeCamera << '\n';
    for (size_t i = 0; i < cameras.size(); ++i) {
        const auto& slot = cameras[i];
        bool active = (int)i == activeCamera;
        f << "cameraslot "; ws(f, slot.name); writeCam(f, active ? cam : slot.view);
        const auto& keys = active ? camKeys : slot.keys;
        f << keys.size() << ' ';
        for (const auto& key : keys) {
            f << key.frame << ' '; writeCam(f, key.cam); f << (int)key.ease << ' ';
        }
        f << '\n';
    }
    for (auto& k : skyKeys) { f << "skykey " << k.frame << ' '; writeSky(f, k.sky); f << "\n"; }
    const char* hex = "0123456789abcdef";
    size_t totalEmbedded = 0;
    for (const auto& [key, image] : embeddedImages) {
        if ((totalEmbedded += image->px.size()) > 8000000) return false;
        f << (image->hdr ? "embeddedhdr " : "embedded "); ws(f, key); f << image->w << ' ' << image->h << ' ';
        std::string pixels;
        pixels.resize(image->px.size() * 8);
        size_t mark = 0;
        auto byte = [&](unsigned value) { pixels[mark++] = hex[value >> 4]; pixels[mark++] = hex[value & 15]; };
        for (size_t i = 0; i < image->px.size(); ++i) {
            V3 c = image->px[i];
            if (image->hdr) {
                int exponent = 0;
                float peak = maxc(c);
                if (peak < 1e-32f) { byte(0); byte(0); byte(0); byte(0); continue; }
                std::frexp(peak, &exponent);
                float scale = std::ldexp(1.f, 8 - exponent);
                byte((unsigned)(c.x * scale)); byte((unsigned)(c.y * scale)); byte((unsigned)(c.z * scale));
                byte((unsigned)(exponent + 128));
                continue;
            }
            byte((unsigned)(clampf(c.x, 0, 1) * 255 + 0.5f));
            byte((unsigned)(clampf(c.y, 0, 1) * 255 + 0.5f));
            byte((unsigned)(clampf(c.z, 0, 1) * 255 + 0.5f));
            byte(image->alpha.empty() ? 255 : image->alpha[i]);
        }
        f << pixels << '\n';
    }
    std::map<const Heightfield*, int> storedHeights;
    std::map<const Mesh*, int> storedMeshes;
    for (int objectIndex = 0; objectIndex < (int)objs.size(); ++objectIndex) {
        const Object& o = objs[objectIndex];
        f << "object " << (int)o.type << ' ';
        ws(f, o.name);
        wv(f, o.pos); wv(f, o.rot); wv(f, o.scl); wv(f, o.lightColor);
        f << o.lightPower << ' ' << o.spotAngle << ' ' << o.spotSoft << ' ' << o.lightSize << ' ' << o.group << ' ' << (int)o.boolMode << ' '
          << o.hidden << ' ' << o.locked << ' ' << o.seed << ' ' << o.treePart << "\nmaterial ";
        writeMat(f, o.mat);
        f << "\n";
        if (o.hasBind || o.parent >= 0) {
            f << "parent " << o.parent << ' ';
            wv(f, o.bindOffset);
            for (const auto& row : o.bind.m) for (float v : row) f << v << ' ';
            f << '\n';
        }
        if (o.instanceSource >= 0) f << "instancesource " << o.instanceSource << '\n';
        if (o.terrainAnchor) { f << "terrainanchor "; wv(f, o.anchorPoint); wv(f, o.anchorNormal); f << '\n'; }
        if (o.type == ObjType::Metaball) f << "metaball " << o.metaRadius << ' ' << o.metaStrength << ' ' << o.metaNegative << "\n";
        if (isLight(o.type)) {
            f << "lightfx "; writeLightSettings(f, o.lighting); f << '\n';
        }
        if (o.type == ObjType::Picture) { f << "picture "; writePicture(f, o.picture); f << '\n'; }
        if (o.type == ObjType::Torus)
            f << "torus " << o.torus.major << ' ' << o.torus.minor << ' ' << o.torus.section << ' ' << o.torus.start << ' ' << o.torus.sweep << "\n";
        if (o.type == ObjType::Stone)
            f << "stone " << o.stone.irregularity << ' ' << o.stone.blockiness << ' ' << o.stone.smoothing << "\n";
        if (o.type == ObjType::Water)
            f << "waterwaves " << o.directionalWater << ' ' << o.waveAngle << ' ' << o.waveLength << ' ' << o.waveHeight << '\n';
        if (o.type == ObjType::Mesh && o.flatMesh) f << "flatmesh\n";
        if (o.treePart) {
            const TreeParams& t = o.tree;
            f << "tree " << t.height << ' ' << t.spread << ' ' << t.trunkRadius << ' ' << t.leafSize << ' ' << t.leafDensity << ' ' << t.levels
              << ' ' << t.branches << ' ' << t.seed << ' ' << t.upwardBias << ' ' << t.taper << ' ' << t.foliageShape << "\n";
        }
        for (auto& k : o.keys) {
            f << "key " << k.frame << ' '; wv(f, k.pos); wv(f, k.rot); wv(f, k.scl); f << (int)k.ease << '\n';
        }
        if (o.curves) {
            if (!o.keys.empty() || !validTransformCurves(*o.curves, false)) return false;
            f << "curves ";
            writeTransformCurves(f, *o.curves);
        }
        for (auto& k : o.appearances) {
            f << "appearance " << k.frame << ' ' << (int)k.ease << ' ' << k.hidden << ' ';
            wv(f, k.lightColor);
            for (float v : k.light) f << v << ' ';
            f << k.terrain.height << ' ' << k.terrain.clip << ' ' << k.terrain.thickness << ' ' << k.terrain.smoothing << ' ';
            writePicture(f, k.picture);
            writeMat(f, k.material);
            f << k.lighting.has_value() << ' ';
            if (k.lighting) writeLightSettings(f, *k.lighting);
            f << '\n';
        }
        if (isHeightfield(o.type) && o.hf && o.instanceSource < 0) {
            f << "terrainshape " << o.terrain.height << ' ' << o.terrain.clip << ' ' << o.terrain.thickness << ' ' << o.terrain.smoothing << '\n';
            auto [at, added] = storedHeights.emplace(o.hf.get(), objectIndex);
            if (added) {
                f << "terrain " << o.hf->res << "\n";
                for (size_t i = 0; i < o.hf->h.size(); i++) f << o.hf->h[i] << ((i % 16 == 15) ? '\n' : ' ');
                f << "\n";
            } else f << "terrainref " << at->second << '\n';
        }
        for (const auto& key : o.terrainKeys) {
            f << "terrainkey " << key.frame << ' ' << (int)key.ease << ' ' << key.field->res << '\n';
            for (float value : key.field->h) f << value << ' ';
            f << '\n';
        }
        if (o.type == ObjType::Mesh && o.mesh && !o.mesh->v.empty() && !o.treePart && o.instanceSource < 0) {
            auto [at, added] = storedMeshes.emplace(o.mesh.get(), objectIndex);
            if (added) {
                const Mesh& m = *o.mesh;
                if (m.polygons) {
                    const auto& p = *m.polygons;
                    f << "polygonmesh " << p.positions.size() << ' ' << p.uv.size() << ' ' << p.normals.size() << ' '
                      << p.faces.size() << '\n';
                    for (const auto* buffer : {&p.positions, &p.uv, &p.normals})
                        for (V3 value : *buffer) { wv(f, value); f << '\n'; }
                    for (const auto& face : p.faces) {
                        f << face.size();
                        for (const auto& corner : face)
                            f << ' ' << corner.vertex << ' ' << corner.uv << ' ' << corner.normal;
                        f << '\n';
                    }
                } else {
                    bool textured = m.uv.size() == m.v.size();
                    f << (textured ? "meshtextured " : "mesh ") << m.v.size() << ' ' << m.triCount() << "\n";
                    for (size_t i = 0; i < m.v.size(); i++) {
                        wv(f, m.v[i]); wv(f, m.n[i]);
                        if (textured) wv(f, m.uv[i]);
                        f << "\n";
                    }
                    for (size_t i = 0; i < m.tri.size(); i++) f << m.tri[i] << ((i % 3 == 2) ? '\n' : ' ');
                }
            } else f << "meshref " << at->second << '\n';
        }
        if (o.rig) {
            if (o.type != ObjType::Mesh || o.treePart || o.instanceSource >= 0 ||
                !o.mesh || !validRig(*o.rig, o.mesh->skinVertices().size())) return false;
            f << "rig " << o.rig->joints.size() << ' ' << o.rig->weights.size() << '\n';
            auto writePose = [&](const Key& key) {
                wv(f, key.pos); wv(f, key.rot); wv(f, key.scl);
            };
            for (const auto& joint : o.rig->joints) {
                ws(f, joint.name);
                f << joint.parent << ' ';
                wv(f, joint.head); wv(f, joint.tail);
                writePose(joint.pose);
                f << (int)joint.pose.ease << ' ' << joint.keys.size() << '\n';
                for (const auto& key : joint.keys) {
                    f << key.frame << ' ' << (int)key.ease << ' ';
                    writePose(key);
                    f << '\n';
                }
            }
            for (const auto& vertex : o.rig->weights) {
                for (size_t i = 0; i < vertex.weight.size(); ++i)
                    f << vertex.joint[i] << ' ' << vertex.weight[i] << ' ';
                f << '\n';
            }
            const auto& joints = o.rig->joints;
            auto curveJoints = std::count_if(joints.begin(), joints.end(), [](const Joint& joint) { return bool(joint.curves); });
            if (curveJoints) {
                f << "rigcurves " << curveJoints << '\n';
                for (size_t i = 0; i < joints.size(); ++i) {
                    if (!joints[i].curves) continue;
                    f << i << ' ';
                    writeTransformCurves(f, *joints[i].curves);
                }
            }
            auto writeGoal = [&](const IKKey& key) {
                wv(f, key.target); wv(f, key.pole);
                f << key.influence << ' ' << (int)key.ease << ' ';
            };
            if (!o.rig->ik.empty()) {
                f << "rigik " << o.rig->ik.size() << '\n';
                for (const auto& chain : o.rig->ik) {
                    f << chain.end << ' ';
                    writeGoal(chain.settings);
                    f << chain.keys.size() << '\n';
                    for (const auto& key : chain.keys) {
                        f << key.frame << ' ';
                        writeGoal(key);
                        f << '\n';
                    }
                }
            }
            if (!o.rig->actions.empty()) {
                f << "rigactions " << o.rig->actions.size() << ' ' << o.rig->clips.size() << '\n';
                for (const auto& action : o.rig->actions) {
                    ws(f, action->name);
                    f << action->frames << ' ' << action->joints.size() << ' ' << action->ik.size() << '\n';
                    for (const auto& track : action->joints) {
                        f << track.joint << ' ' << track.keys.size() << '\n';
                        for (const auto& key : track.keys) {
                            f << key.frame << ' ' << (int)key.ease << ' ';
                            writePose(key);
                            f << '\n';
                        }
                    }
                    for (const auto& track : action->ik) {
                        f << track.end << ' ' << track.keys.size() << '\n';
                        for (const auto& key : track.keys) {
                            f << key.frame << ' ';
                            writeGoal(key);
                            f << '\n';
                        }
                    }
                }
                for (const auto& clip : o.rig->clips)
                    f << clip.action << ' ' << clip.start << ' ' << clip.repeats << ' ' << clip.speed << ' '
                      << clip.influence << ' ' << clip.fadeIn << ' ' << clip.fadeOut << '\n';
            }
        }
    }
    f << "end\n";
    f.close();
    return (bool)f;
}

bool Scene::load(const std::string& path) {
    std::ifstream f(path);
    std::string tok;
    int ver = 0;
    if (!(f >> tok >> ver) || tok != "CANYON3D" || ver < 1 || ver > 11) return false;
    Scene s;
    size_t embeddedPixels = 0;
    s.sky = skyPresets()[0];
    while (f >> tok) {
        if (tok == "camera") readCam(f, s.cam, ver);
        else if (tok == "sky") readSky(f, s.sky, ver);
        else if (tok == "metadata") {
            if (ver < 4) return false;
            rs(f, s.title, ver); rs(f, s.description, ver);
        } else if (tok == "render") {
            RenderSettings& r = s.render;
            f >> r.width >> r.height >> r.quality >> r.samples;
            rb(f, r.dof); rb(f, r.panorama); rb(f, r.softShadows); rb(f, r.blurReflections);
            f >> r.focusDist >> r.aperture >> r.gamma >> r.brightness >> r.contrast >> r.saturation;
            r.width = std::min(std::max(r.width, 0), 8192); r.height = std::min(std::max(r.height, 0), 8192);
            r.quality = std::min(std::max(r.quality, 0), 2); r.samples = std::min(std::max(r.samples, 1), 256);
        } else if (tok == "renderfx") {
            if (ver < 4) return false;
            auto& r = s.render;
            f >> r.bounces >> r.seed;
            rb(f, r.transparentShadows); rb(f, r.adaptive); rb(f, r.denoise); rb(f, r.region);
            f >> r.cropX >> r.cropY >> r.cropW >> r.cropH;
            rb(f, r.alpha); rb(f, r.depthOutput); rb(f, r.idOutput);
            r.bounces = std::clamp(r.bounces, 0, 16);
            r.cropX = clampf(r.cropX, 0, 0.99f); r.cropY = clampf(r.cropY, 0, 0.99f);
            r.cropW = clampf(r.cropW, 0.01f, 1); r.cropH = clampf(r.cropH, 0.01f, 1);
        } else if (tok == "anim") {
            f >> s.frames >> s.fps >> s.frame;
            s.frames = std::min(std::max(s.frames, 2), 100000); s.fps = std::min(std::max(s.fps, 1), 120);
            s.frame = std::min(std::max(s.frame, 0), s.frames - 1);
        } else if (tok == "cuttermaterial") {
            if (ver < 4) return false;
            s.cutterMaterial = true;
        } else if (tok == "booleanlink") {
            int child = 0, parent = 0, mode = 0;
            if (ver < 4 || !(f >> child >> parent >> mode) || mode < 1 || mode >= (int)BoolMode::COUNT ||
                !s.setBooleanParent(child, parent, (BoolMode)mode)) return false;
        } else if (tok == "memory") {
            int i = -1;
            f >> i;
            if (i < 0 || i >= 6) return false;
            readCam(f, s.memory[i], ver);
            s.memorySet[i] = true;
        } else if (tok == "camkey") {
            CamKey k;
            f >> k.frame;
            readCam(f, k.cam, ver);
            if (ver >= 4 && !readEase(f, k.ease)) return false;
            putKey(s.camKeys, k);
        } else if (tok == "activecamera") {
            if (ver < 4) return false;
            f >> s.activeCamera;
        } else if (tok == "cameraslot") {
            if (ver < 4 || s.cameras.size() >= 128) return false;
            NamedCamera camera;
            rs(f, camera.name, ver);
            readCam(f, camera.view, ver);
            int count = 0;
            if (!(f >> count) || count < 0 || count > 100000) return false;
            for (int i = 0; i < count; ++i) {
                CamKey key;
                f >> key.frame;
                readCam(f, key.cam, ver);
                if (!readEase(f, key.ease) || (!camera.keys.empty() && key.frame <= camera.keys.back().frame)) return false;
                camera.keys.push_back(key);
            }
            s.cameras.push_back(std::move(camera));
        } else if (tok == "skykey") {
            SkyKey k;
            f >> k.frame;
            readSky(f, k.sky, ver);
            putKey(s.skyKeys, k);
        } else if (tok == "embedded" || (ver >= 5 && tok == "embeddedhdr")) {
            if (ver < 4) return false;
            std::string key, pixels;
            auto img = std::make_shared<Image>();
            img->hdr = tok == "embeddedhdr";
            rs(f, key, ver);
            if (!(f >> img->w >> img->h) || img->w <= 0 || img->h <= 0 || img->w > 16384 || img->h > 16384 ||
                key.rfind("embed:", 0) != 0 || s.embeddedImages.count(key)) return false;
            size_t count = (size_t)img->w * img->h;
            if ((embeddedPixels += count) > 8000000) return false;
            f.width((std::streamsize)(count * 8 + 1));
            if (!(f >> pixels) || pixels.size() != count * 8) return false;
            auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1); };
            img->px.resize(count);
            if (!img->hdr) img->alpha.resize(count);
            for (size_t i = 0; i < count; ++i) {
                int channels[4];
                for (int j = 0; j < 4; ++j) {
                    int a = nibble(pixels[i * 8 + j * 2]), b = nibble(pixels[i * 8 + j * 2 + 1]);
                    if (a < 0 || b < 0) return false;
                    channels[j] = a * 16 + b;
                }
                V3 rgb((float)channels[0], (float)channels[1], (float)channels[2]);
                if (img->hdr) img->px[i] = rgb * (channels[3] ? std::ldexp(1.f, channels[3] - 136) : 0.f);
                else {
                    img->px[i] = rgb / 255.f;
                    img->alpha[i] = (uint8_t)channels[3];
                }
            }
            if (std::all_of(img->alpha.begin(), img->alpha.end(), [](uint8_t a) { return a == 255; })) img->alpha.clear();
            if (img->hdr) img->buildLightDistribution();
            s.embeddedImages.emplace(std::move(key), std::move(img));
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
        } else if (tok == "parent") {
            if (ver < 4 || s.objs.empty()) return false;
            Object& o = s.objs.back();
            f >> o.parent;
            rv(f, o.bindOffset);
            for (auto& row : o.bind.m) for (float& v : row) f >> v;
            o.hasBind = true;
        } else if (tok == "instancesource") {
            if (ver < 5 || s.objs.empty() || !(f >> s.objs.back().instanceSource) || s.objs.back().instanceSource < 0) return false;
        } else if (tok == "terrainanchor") {
            if (ver < 5 || s.objs.empty()) return false;
            s.objs.back().terrainAnchor = true;
            rv(f, s.objs.back().anchorPoint);
            if (ver >= 6) rv(f, s.objs.back().anchorNormal);
        } else if (tok == "waterwaves") {
            if (ver < 5 || s.objs.empty() || s.objs.back().type != ObjType::Water) return false;
            auto& o = s.objs.back();
            rb(f, o.directionalWater);
            f >> o.waveAngle >> o.waveLength >> o.waveHeight;
            o.waveLength = clampf(o.waveLength, .1f, 50);
            o.waveHeight = clampf(o.waveHeight, 0, 1);
        } else if (tok == "material") {
            if (s.objs.empty() || !readMat(f, s.objs.back().mat, ver)) return false;
        } else if (tok == "lightfx") {
            if (ver < 4 || s.objs.empty() || !isLight(s.objs.back().type)) return false;
            if (!readLightSettings(f, s.objs.back().lighting, ver)) return false;
        } else if (tok == "metaball") {
            if (ver < 3 || s.objs.empty() || s.objs.back().type != ObjType::Metaball) return false;
            Object& o = s.objs.back();
            f >> o.metaRadius >> o.metaStrength;
            rb(f, o.metaNegative);
            if (o.metaRadius < 0.2f || o.metaRadius > 4 || o.metaStrength < 0.1f || o.metaStrength > 4) return false;
        } else if (tok == "picture") {
            if (ver < 3 || s.objs.empty() || s.objs.back().type != ObjType::Picture) return false;
            if (!readPicture(f, s.objs.back().picture, ver)) return false;
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
            if (ver >= 5) {
                f >> t.upwardBias >> t.taper >> t.foliageShape;
                t.upwardBias = clampf(t.upwardBias, -.5f, .7f);
                t.taper = clampf(t.taper, .25f, 1);
                t.foliageShape = std::clamp(t.foliageShape, 0, 2);
            }
            t.levels = std::min(std::max(t.levels, 1), 6); t.branches = std::min(std::max(t.branches, 1), 8);
            t.leafDensity = clampf(t.leafDensity, 0, 4);
        } else if (tok == "key") {
            if (s.objs.empty()) return false;
            Key k;
            f >> k.frame;
            rv(f, k.pos); rv(f, k.rot); rv(f, k.scl);
            if (ver >= 4 && !readEase(f, k.ease)) return false;
            if (s.objs.back().curves) return false;
            putKey(s.objs.back().keys, k);
        } else if (tok == "curves") {
            if (ver < 11 || s.objs.empty() || s.objs.back().animatedTransform()) return false;
            s.objs.back().curves = readTransformCurves(f, false);
            if (!s.objs.back().curves) return false;
        } else if (tok == "appearance") {
            if (ver < 4 || s.objs.empty()) return false;
            AppearanceKey k;
            f >> k.frame;
            if (!readEase(f, k.ease)) return false;
            rb(f, k.hidden);
            rv(f, k.lightColor);
            for (float& v : k.light) f >> v;
            if (!readTerrainShape(f, k.terrain, ver) || !readPicture(f, k.picture, ver) || !readMat(f, k.material, ver)) return false;
            if (ver >= 5) {
                bool lighting = false;
                rb(f, lighting);
                if (lighting && (!isLight(s.objs.back().type) || !readLightSettings(f, k.lighting.emplace(), ver))) return false;
            }
            putKey(s.objs.back().appearances, k);
        } else if (tok == "terrainkey") {
            if (ver < 6 || s.objs.empty() || !isHeightfield(s.objs.back().type)) return false;
            auto& keys = s.objs.back().terrainKeys;
            TerrainKey key;
            int res;
            if (!(f >> key.frame) || key.frame < 0 || key.frame > 100000 || !readEase(f, key.ease)
                || !(f >> res) || res < 2 || res > 1025 || keys.size() >= 32) return false;
            auto field = std::make_shared<Heightfield>();
            field->res = res;
            field->h.resize((size_t)res * res);
            for (float& value : field->h) if (!(f >> value) || value < 0 || value > 1) return false;
            key.field = std::move(field);
            putKey(keys, key);
        } else if (tok == "terrainshape") {
            if (ver < 4 || s.objs.empty() || !isHeightfield(s.objs.back().type) || !readTerrainShape(f, s.objs.back().terrain, ver)) return false;
        } else if (tok == "terrainref" || tok == "meshref") {
            int i = -1;
            if (ver < 4 || !(f >> i) || i < 0 || i + 1 >= (int)s.objs.size()) return false;
            if (tok == "terrainref") {
                if (!isHeightfield(s.objs.back().type) || !s.objs[i].hf) return false;
                s.objs.back().hf = s.objs[i].hf;
            } else {
                if (s.objs.back().type != ObjType::Mesh || !s.objs[i].mesh) return false;
                s.objs.back().mesh = s.objs[i].mesh;
            }
        } else if (tok == "terrain") {
            int res = 0;
            f >> res;
            if (res < 2 || res > MAX_TERRAIN_RESOLUTION || s.objs.empty()) return false;
            auto hf = std::make_shared<Heightfield>();
            hf->res = res;
            hf->h.resize((size_t)res * res);
            for (float& v : hf->h) f >> v;
            hf->computeNormals();
            s.objs.back().hf = hf;
        } else if (tok == "polygonmesh") {
            if (ver < 10 || s.objs.empty()) return false;
            Object& o = s.objs.back();
            if (o.type != ObjType::Mesh || o.treePart || o.mesh || o.instanceSource >= 0) return false;
            size_t positions, uv, normals, faces;
            constexpr size_t limit = 20000000;
            if (!(f >> positions >> uv >> normals >> faces) || !positions || !faces ||
                std::max({positions, uv, normals, faces}) > limit) return false;
            PolygonMesh polygons;
            polygons.positions.resize(positions);
            polygons.uv.resize(uv);
            polygons.normals.resize(normals);
            for (auto* buffer : {&polygons.positions, &polygons.uv, &polygons.normals})
                for (V3& value : *buffer) { rv(f, value); if (!f) return false; }
            size_t triangles = 0;
            for (size_t i = 0; i < faces; ++i) {
                size_t count;
                if (!(f >> count) || count < 3 || count > MAX_POLYGON_CORNERS || triangles + count - 2 > limit)
                    return false;
                triangles += count - 2;
                std::vector<MeshCorner> face(count);
                for (auto& corner : face)
                    if (!(f >> corner.vertex >> corner.uv >> corner.normal) || corner.vertex >= positions ||
                        corner.uv < -1 || (corner.uv >= 0 && (size_t)corner.uv >= uv) ||
                        corner.normal < -1 || (corner.normal >= 0 && (size_t)corner.normal >= normals)) return false;
                polygons.faces.push_back(std::move(face));
            }
            auto mesh = std::make_shared<Mesh>();
            if (!buildPolygonMesh(std::move(polygons), *mesh)) return false;
            o.mesh = std::move(mesh);
        } else if (tok == "mesh" || (ver >= 5 && tok == "meshtextured")) {
            size_t nv = 0, nt = 0;
            f >> nv >> nt;
            if (s.objs.empty() || nv == 0 || nv > 20000000 || nt > 20000000) return false;
            auto m = std::make_shared<Mesh>();
            m->v.resize(nv);
            m->n.resize(nv);
            if (tok == "meshtextured") m->uv.resize(nv);
            for (size_t i = 0; i < nv && f; i++) {
                rv(f, m->v[i]); rv(f, m->n[i]);
                if (!m->uv.empty()) rv(f, m->uv[i]);
            }
            m->tri.resize(nt * 3);
            for (auto& x : m->tri) {
                f >> x;
                if (x >= nv) return false;
            }
            m->build();
            s.objs.back().mesh = m;
        } else if (tok == "rig") {
            if (ver < 7 || s.objs.empty()) return false;
            Object& o = s.objs.back();
            size_t count, vertices;
            if (o.rig || o.type != ObjType::Mesh || o.treePart || o.instanceSource >= 0 || !o.mesh ||
                !(f >> count >> vertices) || count == 0 || count > MAX_RIG_JOINTS ||
                o.mesh->skinVertices().size() > MAX_SKIN_VERTICES ||
                (vertices && vertices != o.mesh->skinVertices().size())) return false;
            auto rig = std::make_shared<Rig>();
            rig->joints.resize(count);
            auto readPose = [&](Key& key) { rv(f, key.pos); rv(f, key.rot); rv(f, key.scl); };
            for (auto& joint : rig->joints) {
                rs(f, joint.name, ver);
                f >> joint.parent;
                rv(f, joint.head); rv(f, joint.tail);
                readPose(joint.pose);
                size_t keys;
                if (!readEase(f, joint.pose.ease) || !(f >> keys) || keys > MAX_JOINT_KEYS) return false;
                joint.keys.resize(keys);
                for (auto& key : joint.keys) {
                    if (!(f >> key.frame) || !readEase(f, key.ease)) return false;
                    readPose(key);
                }
            }
            rig->weights.resize(vertices);
            for (auto& vertex : rig->weights)
                for (size_t i = 0; i < vertex.weight.size(); ++i)
                    f >> vertex.joint[i] >> vertex.weight[i];
            if (!f || !validRig(*rig, o.mesh->skinVertices().size())) return false;
            o.rig = std::move(rig);
        } else if (tok == "rigcurves") {
            if (ver < 11 || s.objs.empty() || !s.objs.back().rig) return false;
            auto& object = s.objs.back();
            if (std::any_of(object.rig->joints.begin(), object.rig->joints.end(), [](const Joint& j) { return bool(j.curves); }))
                return false;
            auto rig = std::make_shared<Rig>(*object.rig);
            size_t joints;
            if (!(f >> joints) || !joints || joints > rig->joints.size()) return false;
            for (size_t n = 0; n < joints; ++n) {
                size_t index;
                if (!(f >> index) || index >= rig->joints.size() || rig->joints[index].animatedTransform()) return false;
                rig->joints[index].curves = readTransformCurves(f, true);
                if (!rig->joints[index].curves) return false;
            }
            object.rig = std::move(rig);
        } else if (tok == "rigik") {
            if (ver < 8 || s.objs.empty()) return false;
            Object& o = s.objs.back();
            size_t count;
            if (!o.rig || !o.rig->ik.empty() || !(f >> count) || count == 0 || count > o.rig->joints.size() / 2) return false;
            auto rig = std::make_shared<Rig>(*o.rig);
            rig->ik.resize(count);
            auto readGoal = [&](IKKey& key) {
                rv(f, key.target); rv(f, key.pole);
                return (f >> key.influence) && readEase(f, key.ease);
            };
            for (auto& chain : rig->ik) {
                size_t keys;
                if (!(f >> chain.end) || !readGoal(chain.settings) || !(f >> keys) || keys > MAX_JOINT_KEYS) return false;
                chain.keys.resize(keys);
                for (auto& key : chain.keys)
                    if (!(f >> key.frame) || !readGoal(key)) return false;
            }
            if (!validRig(*rig, o.mesh->skinVertices().size())) return false;
            o.rig = std::move(rig);
        } else if (tok == "rigactions") {
            if (ver < 9 || s.objs.empty()) return false;
            Object& o = s.objs.back();
            size_t actions, clips;
            if (!o.rig || !o.rig->actions.empty() ||
                !(f >> actions >> clips) || actions == 0 || actions > MAX_RIG_ACTIONS || clips > MAX_RIG_CLIPS) return false;
            auto rig = std::make_shared<Rig>(*o.rig);
            size_t remaining = MAX_ACTION_SAMPLES;
            for (size_t i = 0; i < actions; ++i) {
                auto action = std::make_shared<RigAction>();
                rs(f, action->name, ver);
                size_t joints, controls;
                if (!(f >> action->frames >> joints >> controls) || action->frames < 1 ||
                    action->frames > MAX_ACTION_FRAMES || joints > rig->joints.size() || controls > rig->ik.size()) return false;
                action->joints.resize(joints);
                action->ik.resize(controls);
                auto readCount = [&](size_t& count) {
                    if (!(f >> count) || count == 0 || count > (size_t)action->frames || count > remaining) return false;
                    remaining -= count;
                    return true;
                };
                for (auto& track : action->joints) {
                    size_t count;
                    if (!(f >> track.joint) || !readCount(count)) return false;
                    track.keys.resize(count);
                    for (auto& key : track.keys) {
                        if (!(f >> key.frame) || !readEase(f, key.ease)) return false;
                        rv(f, key.pos); rv(f, key.rot); rv(f, key.scl);
                    }
                }
                for (auto& track : action->ik) {
                    size_t count;
                    if (!(f >> track.end) || !readCount(count)) return false;
                    track.keys.resize(count);
                    for (auto& key : track.keys) {
                        if (!(f >> key.frame)) return false;
                        rv(f, key.target); rv(f, key.pole);
                        if (!(f >> key.influence) || !readEase(f, key.ease)) return false;
                    }
                }
                rig->actions.push_back(std::move(action));
            }
            rig->clips.resize(clips);
            for (auto& clip : rig->clips)
                if (!(f >> clip.action >> clip.start >> clip.repeats >> clip.speed >> clip.influence >>
                      clip.fadeIn >> clip.fadeOut)) return false;
            if (!validRigMotion(*rig)) return false;
            o.rig = std::move(rig);
        } else if (tok == "end") break;
        else return false;
        if (!f) return false;
    }
    std::vector<int> order;
    if (tok != "end" || !hierarchyOrder(s.objs, order)) return false;
    if (s.activeCamera < -1 || s.activeCamera >= (int)s.cameras.size()) return false;
    std::error_code assetError;
    auto assetRoot = std::filesystem::absolute(path, assetError).parent_path();
    if (!assetError) sceneAssets(s, [&](std::string& value, auto&) {
        if (!assetIsFile(value)) return;
        std::filesystem::path file(value);
        if (!file.is_relative()) return;
        std::error_code error;
        if (ver < 4 && std::filesystem::is_regular_file(file, error)) file = std::filesystem::absolute(file, error);
        else file = assetRoot / file;
        value = file.lexically_normal().string();
    });
    for (Object& o : s.objs) {
        if (o.terrainAnchor && (o.parent < 0 || s.objs[o.parent].type != ObjType::Terrain)) return false;
        if (o.rig && (!o.mesh || o.type != ObjType::Mesh || o.treePart || o.instanceSource >= 0 ||
                      !validRig(*o.rig, o.mesh->skinVertices().size()))) return false;
        if (o.instanceSource >= 0) {
            if (o.instanceSource >= (int)s.objs.size()) return false;
            const Object& src = s.objs[o.instanceSource];
            if (src.type != o.type || src.instanceSource >= 0 || o.treePart) return false;
        }
        if (isHeightfield(o.type) && !o.hf && o.instanceSource < 0) { o.hf = std::make_shared<Heightfield>(); hfInit(*o.hf, 129); }
        if (o.treePart) regenTree(o);
    }
    s.updateAll();
    *this = std::move(s);
    return true;
}

bool Scene::merge(const std::string& path) {
    Scene o;
    if (!o.load(path)) return false;
    append(std::move(o));
    return true;
}

void Scene::append(Scene other) {
    int g = maxGroup();
    int first = (int)objs.size();
    for (const auto& entry : other.embeddedImages) {
        const auto& key = entry.first;
        const auto& image = entry.second;
        std::string id = key;
        for (unsigned i = 2; embeddedImages.count(id) && embeddedImages[id] != image; ++i)
            id = key + "_" + std::to_string(i);
        if (id != key) sceneAssets(other, [&](std::string& path, auto&) { if (path == key) path = id; });
        embeddedImages.emplace(id, image);
    }
    for (auto& link : other.booleanLinks) booleanLinks.push_back({link.child + g, link.parent + g, link.mode});
    for (Object& x : other.objs) {
        if (x.group > 0) x.group += g;
        if (x.parent >= 0) x.parent += first;
        if (x.instanceSource >= 0) x.instanceSource += first;
        if (x.rig)
            for (const auto& clip : x.rig->clips)
                frames = std::max(frames, clip.start + rigClipFrames(*x.rig->actions[clip.action], clip));
        auto remapLight = [g](LightSettings& light) {
            if (light.targetGroup > 0)
                light.targetGroup = (int)std::min<int64_t>((int64_t)light.targetGroup + g, (std::numeric_limits<int>::max)());
        };
        remapLight(x.lighting);
        for (auto& key : x.appearances) if (key.lighting) remapLight(*key.lighting);
        objs.push_back(std::move(x));
    }
    updateAll();
}

bool Library::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << std::setprecision(7) << "CANYONLIB 6\n";
    for (auto& m : mats) { f << "material "; writeMat(f, m); f << "\n"; }
    for (auto& k : skies) { f << "sky "; writeSky(f, k); f << "\n"; }
    return (bool)f;
}

bool Library::load(const std::string& path) {
    std::ifstream f(path);
    std::string tok;
    int ver = 0;
    if (!(f >> tok >> ver) || tok != "CANYONLIB" || ver < 2 || ver > 6) return false;
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

namespace {

Mesh contourScene(const Scene& scene, V3 lo, V3 hi, int resolution) {
    Mesh mesh;
    float step = maxc(hi - lo) / std::clamp(resolution, 8, 96);
    if (step <= 1e-6f) return mesh;
    lo -= V3(step);
    hi += V3(step);
    int nx = (int)std::ceil((hi.x - lo.x) / step) + 1;
    int ny = (int)std::ceil((hi.y - lo.y) / step) + 1;
    int nz = (int)std::ceil((hi.z - lo.z) / step) + 1;
    auto index = [=](int x, int y, int z) { return (z * ny + y) * nx + x; };
    std::vector<uint8_t> inside((size_t)nx * ny * nz);
    V3 ray = normalize({1, 0.317f, 0.253f});
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                Hit hit;
                V3 p = lo + V3((float)x, (float)y, (float)z) * step;
                inside[index(x, y, z)] = intersectScene(scene, {p, ray}, 1e30f, hit) && dot(hit.n, ray) > 0;
            }
    auto edge = [&](V3 a, V3 b) {
        Hit hit;
        return intersectScene(scene, {a, normalize(b - a)}, length(b - a) + 1e-5f, hit) ? hit.p : (a + b) * 0.5f;
    };
    auto triangle = [&](V3 a, V3 b, V3 c, V3 out) {
        V3 n = cross(b - a, c - a);
        if (length(n) <= 1e-10f) return;
        if (dot(n, out) < 0) std::swap(b, c);
        auto start = (uint32_t)mesh.v.size();
        mesh.v.insert(mesh.v.end(), {a, b, c});
        mesh.tri.insert(mesh.tri.end(), {start, start + 1, start + 2});
    };
    const int tetra[][4] = {{0, 1, 3, 7}, {0, 3, 2, 7}, {0, 2, 6, 7}, {0, 6, 4, 7}, {0, 4, 5, 7}, {0, 5, 1, 7}};
    for (int z = 0; z + 1 < nz; ++z)
        for (int y = 0; y + 1 < ny; ++y)
            for (int x = 0; x + 1 < nx; ++x) {
                V3 p[8];
                bool in[8];
                int count = 0;
                for (int i = 0; i < 8; ++i) {
                    int a = x + (i & 1), b = y + ((i >> 1) & 1), c = z + (i >> 2);
                    p[i] = lo + V3((float)a, (float)b, (float)c) * step;
                    count += in[i] = inside[index(a, b, c)] != 0;
                }
                if (!count || count == 8) continue;
                for (const auto& t : tetra) {
                    V3 a[4], b[4];
                    int na = 0, nb = 0;
                    for (int i : t) {
                        if (in[i]) a[na++] = p[i];
                        else b[nb++] = p[i];
                    }
                    if (na == 1) triangle(edge(a[0], b[0]), edge(a[0], b[1]), edge(a[0], b[2]), b[0] - a[0]);
                    else if (nb == 1) triangle(edge(b[0], a[0]), edge(b[0], a[1]), edge(b[0], a[2]), b[0] - a[0]);
                    else if (na == 2) {
                        V3 p0 = edge(a[0], b[0]), p1 = edge(a[0], b[1]), p2 = edge(a[1], b[0]), p3 = edge(a[1], b[1]);
                        triangle(p0, p1, p2, b[0] - a[0]);
                        triangle(p1, p2, p3, b[0] - a[0]);
                    }
                }
            }
    return mesh;
}

Mesh meshSurface(const Object& o) {
    Mesh m;
    const Mesh* mesh = o.visibleMesh();
    bool mirrored = dot(cross(o.fwd.col(0), o.fwd.col(1)), o.fwd.col(2)) < 0.f;
    if (o.type == ObjType::Mesh && mesh) {
        if (o.mesh && o.mesh->polygons) {
            PolygonMesh polygons = *o.mesh->polygons;
            if (mesh != o.mesh.get()) {
                // Recreate the same corner-to-render-vertex order used by buildPolygonMesh.
                std::map<std::tuple<uint32_t, int, int>, int> vertices;
                for (auto& face : polygons.faces) for (auto& corner : face) {
                    auto key = std::make_tuple(corner.vertex, corner.uv, corner.normal);
                    auto entry = vertices.emplace(key, (int)vertices.size()).first;
                    corner.normal = entry->second;
                    polygons.positions[corner.vertex] = mesh->v[entry->second];
                }
                polygons.normals = mesh->n;
            }
            for (V3& p : polygons.positions) p = o.worldPos + o.fwd * p;
            for (V3& n : polygons.normals) n = normalize(o.nrm * n);
            if (mirrored)
                for (auto& face : polygons.faces) std::reverse(face.begin(), face.end());
            if (buildPolygonMesh(std::move(polygons), m)) return m;
        }
        m.v = mesh->v;
        m.tri = mesh->tri;
        m.uv = mesh->uv;
        m.n = mesh->n;
        for (V3& n : m.n) n = normalize(o.nrm * n);
    } else if (isHeightfield(o.type) && o.hf && o.hf->res > 1) {
        int n = std::min(o.hf->res, 513);
        for (int side = 0; side < (o.type == ObjType::Lattice ? 2 : 1); ++side) {
            uint32_t base = (uint32_t)m.v.size();
            for (int z = 0; z < n; ++z)
                for (int x = 0; x < n; ++x)
                    m.v.push_back({2.f * x / (n - 1) - 1, (side ? -1.f : 1.f) * o.terrainAt(x * (o.hf->res - 1) / (n - 1),
                        z * (o.hf->res - 1) / (n - 1)), 2.f * z / (n - 1) - 1});
            for (int z = 0; z + 1 < n; ++z)
                for (int x = 0; x + 1 < n; ++x) {
                    uint32_t a = base + z * n + x, b = a + 1, c = a + n, d = c + 1;
                    if (side) m.tri.insert(m.tri.end(), {a, b, d, a, d, c});
                    else m.tri.insert(m.tri.end(), {a, d, b, a, c, d});
                }
        }
    } else if (o.type == ObjType::Disc) {
        m.v.emplace_back();
        for (int i = 0; i < 48; ++i) {
            float a = 2 * PI * i / 48;
            m.v.push_back({std::cos(a), 0, -std::sin(a)});
        }
        for (uint32_t i = 0; i < 48; ++i) m.tri.insert(m.tri.end(), {0, i + 1, (i + 1) % 48 + 1});
    } else if (o.type == ObjType::Square || o.type == ObjType::Picture) {
        if (o.type == ObjType::Picture) {
            V3 a = o.picture.halfExtent();
            m.v = {{-a.x, -a.y, 0}, {a.x, -a.y, 0}, {-a.x, a.y, 0}, {a.x, a.y, 0}};
        } else {
            m.v = {{-1, 0, 1}, {1, 0, 1}, {-1, 0, -1}, {1, 0, -1}};
        }
        m.tri = {0, 1, 2, 2, 1, 3};
    }
    for (V3& p : m.v) p = o.worldPos + o.fwd * p;
    if (mirrored)
        for (size_t i = 0; i + 2 < m.tri.size(); i += 3) std::swap(m.tri[i + 1], m.tri[i + 2]);
    return m;
}

}  // namespace

std::vector<MeshLayer> meshSelection(const Scene& scene, const std::vector<int>& indices, int resolution) {
    std::vector<MeshLayer> parts;
    std::set<int> done;
    std::set<int> seen;
    for (int i : indices) {
        if (i < 0 || i >= (int)scene.objs.size() || !seen.insert(i).second) continue;
        const Object& o = scene.objs[i];
        if (o.hidden || isInfinite(o.type) || isLight(o.type)) continue;
        Mesh m = o.csg ? Mesh{} : meshSurface(o);
        std::string name = o.name;
        if (m.tri.empty()) {
            std::vector<int> members{i};
            if (o.csg && o.group > 0) {
                int id = o.group;
                for (int level = 0; level < 32; ++level) {
                    auto link = std::find_if(scene.booleanLinks.begin(), scene.booleanLinks.end(), [=](const BooleanLink& l) { return l.child == id; });
                    if (link == scene.booleanLinks.end()) break;
                    id = link->parent;
                }
                if (!done.insert(id).second) continue;
                name = "Boolean " + std::to_string(id);
                std::set<int> assembly{id};
                for (size_t level = 0; level < scene.booleanLinks.size(); ++level)
                    for (auto& link : scene.booleanLinks) if (assembly.count(link.parent)) assembly.insert(link.child);
                members.clear();
                for (int k = 0; k < (int)scene.objs.size(); ++k)
                    if (assembly.count(scene.objs[k].group) && scene.objs[k].csg) members.push_back(k);
            }
            Scene sub = scene.subset(members);
            V3 lo(1e30f), hi(-1e30f), a, b;
            for (const auto& p : sub.objs) if (worldBounds(p, a, b)) {
                lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)};
                hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
            }
            m = contourScene(sub, lo, hi, resolution);
        }
        if (!m.tri.empty()) parts.push_back({std::move(name), std::move(m)});
    }
    return parts;
}
