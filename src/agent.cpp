#include "agent.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

#include "terrain.h"
#include "simulation.h"

namespace {

template<class T, class Name> int named(const T& items, const std::string& key, Name name) {
    for (size_t i = 0; i < items.size(); ++i)
        if (name(items[i]) == key) return (int)i;
    return -1;
}

template<class T> int namedEnum(const std::string& key, const char* (*name)(T)) {
    for (int i = 0; i < (int)T::COUNT; ++i)
        if (name((T)i) == key) return i;
    return -1;
}

bool vec(std::istream& input, V3& v) {
    return input >> v.x >> v.y >> v.z && std::fabs(v.x) <= 100000 && std::fabs(v.y) <= 100000 && std::fabs(v.z) <= 100000;
}

bool name(std::istream& input, std::string& s) {
    return input >> std::quoted(s) && !s.empty() && s.size() <= 4096;
}

bool color(std::istream& input, V3& v) {
    return vec(input, v) && v.x >= 0 && v.y >= 0 && v.z >= 0 && maxc(v) <= 1;
}

bool scalar(std::istream& input, float& v, float min, float max) {
    return input >> v && v >= min && v <= max;
}

bool transform(std::istream& input, V3& position, V3& rotation, V3& scale) {
    return vec(input, position) && vec(input, rotation) && vec(input, scale)
        && scale.x > .001f && scale.y > .001f && scale.z > .001f;
}

bool camera(std::istream& input, Camera& c) {
    c.lookAt = -1;
    return vec(input, c.target) && scalar(input, c.yaw, -360, 360) && scalar(input, c.pitch, -89.9f, 89.9f)
        && scalar(input, c.dist, .1f, 100000) && scalar(input, c.fov, 5, 150);
}

bool flag(std::istream& in, bool& enabled) {
    int value;
    if (!(in >> value) || (value != 0 && value != 1)) return false;
    enabled = value != 0;
    return true;
}

bool simulationTime(std::istream& in, SimulationSettings& settings) {
    return in >> settings.frames && scalar(in, settings.gravity, 0, 50)
        && vec(in, settings.velocity) && flag(in, settings.floor);
}

bool presetNumber(std::istream& in, float& value, float min, float max) {
    float overrideValue;
    if (!(in >> overrideValue)) return false;
    if (overrideValue == -1) return true;
    if (overrideValue < min || overrideValue > max) return false;
    value = overrideValue;
    return true;
}

bool simulationControls(std::istream& in, SimulationSettings& time) {
    std::string preset;
    if (!name(in, preset)) return false;
    int index = namedEnum(preset, gravityPresetName);
    if (index < 0) return false;
    applyGravityPreset(time, (GravityPreset)index);
    int floor;
    if (!(in >> time.frames >> time.startFrame >> time.detail) ||
        !presetNumber(in, time.gravity, 0, 50) || !vec(in, time.velocity) ||
        !(in >> floor) || floor < -1 || floor > 1 || !vec(in, time.direction) ||
        !vec(in, time.center) || !scalar(in, time.radius, .1f, 1000)) return false;
    if (floor >= 0) time.floor = floor != 0;
    return true;
}

int objectIndex(const Scene& s, std::istream& input) {
    std::string key;
    if (!name(input, key)) return -1;
    int match = -1;
    for (size_t i = 0; i < s.objs.size(); ++i) if (s.objs[i].name == key) {
        if (match >= 0) return -1;
        match = (int)i;
    }
    return match;
}

bool apply(Scene& s, const std::string& op, std::istream& in, bool first) {
    std::string key, value;
    if (op == "base") {
        if (!first || !name(in, key)) return false;
        return s.load(key);
    }
    if (op == "example") {
        int index;
        if (!first || !(in >> index) || index < 0 || index >= (int)exampleNames().size()) return false;
        s = makeExample(index);
        return true;
    }
    if (op == "title") return name(in, s.title);
    if (op == "sky") {
        if (!name(in, key)) return false;
        const auto& list = skyPresets();
        int i = named(list, key, [](const Sky& k) { return k.name; });
        if (i < 0) return false;
        s.sky = list[i];
        return true;
    }
    if (op == "environment") {
        if (!name(in, s.sky.environment) || !scalar(in, s.sky.environmentYaw, -360, 360)
            || !scalar(in, s.sky.environmentStrength, 0, 64)) return false;
        s.sky.environmentImage = loadImageCached(s.sky.environment);
        return s.sky.environmentImage && s.sky.environmentImage->hdr;
    }
    if (op == "camera") return camera(in, s.cam);
    if (op == "timeline") {
        return in >> s.frames >> s.fps && s.frames >= 1 && s.frames <= 100000 && s.fps >= 1 && s.fps <= 240;
    }
    if (op == "render") {
        return in >> s.render.width >> s.render.height >> s.render.quality >> s.render.samples
            && s.render.width >= 1 && s.render.width <= 8192 && s.render.height >= 1 && s.render.height <= 8192
            && (int64_t)s.render.width * s.render.height <= 64000000 && s.render.quality >= 0 && s.render.quality <= 2
            && s.render.samples >= 1 && s.render.samples <= 256;
    }
    if (op == "camera_key") {
        CamKey k;
        if (!(in >> k.frame) || k.frame < 0 || k.frame >= s.frames || !camera(in, k.cam)) return false;
        putKey(s.camKeys, k);
        return true;
    }
    if (op == "rigid_bake" || op == "rigid_bake_v2") {
        RigidSettings settings;
        if (op == "rigid_bake_v2") {
            if (!simulationControls(in, settings.time) || !name(in, key)) return false;
            int preset = namedEnum(key, rigidPresetName);
            if (preset < 0) return false;
            applyRigidPreset(settings, (RigidPreset)preset);
            if (!presetNumber(in, settings.bounce, 0, 1) || !presetNumber(in, settings.friction, 0, 1) ||
                !presetNumber(in, settings.damping, 0, 5)) return false;
        } else if (!simulationTime(in, settings.time) || !scalar(in, settings.bounce, 0, 1) ||
                   !scalar(in, settings.friction, 0, 1)) return false;
        std::vector<int> selection;
        while ((in >> std::ws) && !in.eof()) {
            int index = objectIndex(s, in);
            if (index < 0 || selection.size() >= SIM_MAX_BODIES) return false;
            selection.push_back(index);
        }
        std::string error;
        bool baked = bakeRigidBodies(s, selection, settings, error);
        if (!baked) std::fprintf(stderr, "%s\n", error.c_str());
        return baked;
    }
    if (op == "object" || op == "terrain" || op == "tree" || op == "mesh" || op == "picture") {
        if (!name(in, key) || s.objs.size() >= 20000) return false;
        for (const auto& obj : s.objs)
            if (obj.name == key || (op == "tree" && (obj.name == key + " trunk" || obj.name == key + " foliage"))) return false;
        int type = (int)ObjType::Terrain;
        if (op == "object") {
            if (!name(in, value)) return false;
            type = namedEnum(value, objTypeName);
            if (type < 0 || type == (int)ObjType::Mesh || type == (int)ObjType::Picture) return false;
        }
        if (op == "mesh") type = (int)ObjType::Mesh;
        if (op == "picture") type = (int)ObjType::Picture;
        Object obj = makeObject((ObjType)type, 1);
        obj.name = key;
        if (!transform(in, obj.pos, obj.rot, obj.scl)) return false;
        if (op == "terrain") {
            int resolution, seed;
            float rough;
            if (!name(in, value) || !(in >> resolution >> seed) || !scalar(in, rough, 0, 1)
                || resolution < 17 || resolution > MAX_TERRAIN_RESOLUTION || seed < 0) return false;
            int kind = namedEnum(value, terrainKindName);
            if (kind < 0) return false;
            hfInit(*obj.hf, resolution);
            hfGenerate(*obj.hf, (TerrainKind)kind, (uint32_t)seed, rough);
        } else if (op == "tree") {
            int seed;
            if (!(in >> seed) || seed < 0 || s.objs.size() >= 19999) return false;
            TreeParams p;
            p.seed = (uint32_t)seed;
            size_t start = s.objs.size();
            addTree(s, p, obj.pos, 1, findMaterial("Bark"), findMaterial("Leaves"), s.maxGroup() + 1);
            for (size_t i = start; i < s.objs.size(); ++i) {
                s.objs[i].name = key + (s.objs[i].treePart == 1 ? " trunk" : " foliage");
                s.objs[i].rot = obj.rot;
                s.objs[i].scl = obj.scl;
            }
            return true;
        } else if (op == "mesh") {
            auto mesh = std::make_shared<Mesh>();
            std::string error;
            if (!name(in, value) || !loadOBJ(value, *mesh, error)) return false;
            obj.mesh = mesh;
        } else if (op == "picture") {
            if (!name(in, obj.picture.path)) return false;
            obj.picture.image = loadImageCached(obj.picture.path);
            if (!obj.picture.image) return false;
            obj.picture.aspect = (float)obj.picture.image->w / obj.picture.image->h;
        }
        s.objs.push_back(std::move(obj));
        return true;
    }
    int index = objectIndex(s, in);
    if (index < 0) return false;
    Object& obj = s.objs[index];
    if (op == "liquid_bake" || op == "liquid_bake_v2") {
        LiquidSettings settings;
        if (op == "liquid_bake_v2") {
            if (!simulationControls(in, settings.time) || !name(in, key)) return false;
            int preset = namedEnum(key, liquidPresetName);
            if (preset < 0) return false;
            applyLiquidPreset(settings, (LiquidPreset)preset);
            if (!(in >> settings.resolution) || !presetNumber(in, settings.viscosity, 0, 1) ||
                !presetNumber(in, settings.cohesion, 0, 1) || !presetNumber(in, settings.surface, .8f, 1.3f) ||
                !flag(in, settings.hideSource)) return false;
        } else if (!simulationTime(in, settings.time) || !(in >> settings.resolution) ||
                   !scalar(in, settings.viscosity, 0, 1) || !flag(in, settings.hideSource)) return false;
        std::string error;
        int particles = bakeLiquid(s, index, settings, error);
        if (!particles) std::fprintf(stderr, "%s\n", error.c_str());
        return particles > 0;
    }
    if (op == "transform") return transform(in, obj.pos, obj.rot, obj.scl);
    if (op == "color") return color(in, obj.mat.color1);
    if (op == "heightmap" || op == "terrain_key") {
        if (!obj.hf || !name(in, value)) return false;
        auto image = loadImageCached(value);
        if (!image) return false;
        if (op == "heightmap") {
            hfFromImage(*obj.hf, *image);
        } else {
            TerrainKey key;
            if (!(in >> key.frame) || key.frame < 0 || key.frame >= s.frames || obj.terrainKeys.size() >= 32) return false;
            auto field = std::make_shared<Heightfield>();
            hfInit(*field, std::min(obj.hf->res, 1025));
            hfFromImage(*field, *image);
            field->n.clear();
            key.field = std::move(field);
            putKey(obj.terrainKeys, key);
        }
        return true;
    }
    if (op == "tileable") {
        float border;
        if (!obj.hf || !scalar(in, border, .01f, .5f)) return false;
        hfMakeTileable(*obj.hf, border);
        return true;
    }
    if (op == "terrain_tiles") {
        int columns, rows;
        if (!(in >> columns >> rows) || columns < 1 || columns > 16 || rows < 1 || rows > 16
            || obj.type != ObjType::Terrain || !obj.hf || obj.parent >= 0 || obj.instanceSource >= 0
            || obj.group || !obj.keys.empty() || !obj.appearances.empty() || !obj.terrainKeys.empty()
            || s.objs.size() + columns * rows - 1 > 20000) return false;
        obj.update();
        Object prototype = obj;
        std::vector<Object> tiles;
        for (int z = 0; z < rows; ++z) for (int x = 0; x < columns; ++x) {
            if (!x && !z) continue;
            Object tile = prototype;
            tile.name += " [" + std::to_string(x) + "," + std::to_string(z) + "]";
            for (const auto& existing : s.objs) if (existing.name == tile.name) return false;
            tile.moveWorld(prototype.fwd * V3(2.f * x, 0, 2.f * z));
            tiles.push_back(std::move(tile));
        }
        hfMakeTileable(*prototype.hf, .15f);
        for (auto& tile : tiles) s.objs.push_back(std::move(tile));
        return true;
    }
    if (op == "material") {
        if (!name(in, key)) return false;
        const auto& list = materialPresets();
        int i = named(list, key, [](const Material& m) { return m.name; });
        if (i < 0) return false;
        obj.mat = list[i];
        return true;
    }
    if (op == "layer") {
        TextureLayer layer;
        if (!name(in, key) || obj.mat.layers.size() >= MAX_TEXTURE_LAYERS
            || !color(in, layer.color1) || !color(in, layer.color2)
            || !scalar(in, layer.texScale, .01f, 20) || !scalar(in, layer.opacity, 0, 1)) return false;
        int kind = namedEnum(key, texName);
        if (kind < 0 || kind == (int)TexType::Image) return false;
        layer.tex = (TexType)kind;
        obj.mat.layers.push_back(layer);
        return true;
    }
    if (op == "light") return isLight(obj.type) && color(in, obj.lightColor) && scalar(in, obj.lightPower, 0, 1000);
    if (op == "key") {
        Key k;
        if (!(in >> k.frame) || k.frame < 0 || k.frame >= s.frames || !transform(in, k.pos, k.rot, k.scl)) return false;
        putKey(obj.keys, k);
        return true;
    }
    if (op == "parent") {
        int parent = objectIndex(s, in);
        s.updateAll();
        return parent >= 0 && s.setParent(index, parent);
    }
    if (op == "scatter") {
        int count, seed;
        V3 min, max;
        if (!name(in, key) || !(in >> count >> seed) || count < 1 || count > 10000 || seed < 0
            || !vec(in, min) || !vec(in, max) || min.x >= max.x || min.z >= max.z) return false;
        std::vector<int> sources;
        for (size_t i = 0; i < s.objs.size(); ++i)
            if (s.objs[i].name == key || (s.objs[i].treePart &&
                (s.objs[i].name == key + " trunk" || s.objs[i].name == key + " foliage"))) sources.push_back((int)i);
        if (sources.empty()) return false;
        s.updateAll();
        Rng rng((uint32_t)seed);
        std::vector<Ray> rays;
        for (int i = 0; i < count; ++i) {
            float x = lerpf(min.x, max.x, rng.f()), z = lerpf(min.z, max.z, rng.f());
            rays.push_back({{x, max.y, z}, {0, -1, 0}});
        }
        return s.scatter(sources, index, rays, .7f, 1.3f, 360.f, true, rng) > 0;
    }
    return false;
}

void jsonString(std::ostream& out, const std::string& value) {
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
        else out << c;
    }
    out << '"';
}

template<class Items, class Name> void list(std::ostream& out, const Items& items, Name name) {
    bool first = true;
    out << '[';
    for (const auto& item : items) {
        if (!first) out << ',';
        jsonString(out, name(item));
        first = false;
    }
    out << ']';
}

template<class T> void options(std::ostream& out, const char* (*name)(T)) {
    out << '[';
    for (int i = 0; i < (int)T::COUNT; ++i) {
        if (i) out << ',';
        jsonString(out, name((T)i));
    }
    out << ']';
}

}  // namespace

int agentCommand(int argc, char** argv) {
    std::string_view op = argv[1];
    if (op == "--catalog" && argc == 3) {
        std::ofstream out(argv[2], std::ios::binary);
        out << "{\"objects\":"; options(out, objTypeName);
        out << ",\"terrains\":"; options(out, terrainKindName);
        out << ",\"textures\":"; options(out, texName);
        out << ",\"materials\":"; list(out, materialPresets(), [](const auto& m) { return m.name; });
        out << ",\"skies\":"; list(out, skyPresets(), [](const auto& k) { return k.name; });
        out << ",\"examples\":"; list(out, exampleNames(), [](const auto& n) { return n; });
        out << "}\n";
        out.close();
        return out ? 0 : 4;
    }
    if (op == "--inspect" && argc == 4) {
        Scene scene;
        if (!scene.load(argv[2])) return 3;
        std::ofstream out(argv[3], std::ios::binary);
        out << "{\"title\":"; jsonString(out, scene.title);
        out << ",\"frames\":" << scene.frames << ",\"objects\":[";
        for (size_t i = 0; i < scene.objs.size(); ++i) {
            const auto& obj = scene.objs[i];
            if (i) out << ',';
            out << "{\"name\":"; jsonString(out, obj.name);
            out << ",\"type\":"; jsonString(out, objTypeName(obj.type));
            out << ",\"position\":[" << obj.pos.x << ',' << obj.pos.y << ',' << obj.pos.z << "]}";
        }
        out << "]}\n";
        out.close();
        return out ? 0 : 4;
    }
    if (op != "--compose" || argc != 4) return 2;
    std::ifstream recipe(argv[2]);
    std::string line;
    if (!std::getline(recipe, line) || line != "BRYCESCAPE_RECIPE 1") {
        std::fprintf(stderr, "Expected BRYCESCAPE_RECIPE 1 on line 1.\n");
        return 2;
    }
    Scene scene;
    bool first = true;
    for (size_t number = 2; std::getline(recipe, line); ++number) {
        std::istringstream fields(line);
        std::string command;
        if (!(fields >> command) || command[0] == '#') continue;
        if (!apply(scene, command, fields, first) || !(fields >> std::ws).eof()) {
            std::fprintf(stderr, "Invalid command or arguments on recipe line %zu (%s).\n", number, command.c_str());
            return 2;
        }
        first = false;
    }
    if (recipe.bad()) return 3;
    scene.updateAll();
    return scene.save(argv[3]) ? 0 : 4;
}
