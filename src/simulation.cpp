#include "simulation.h"

#include <limits>
#include <numeric>

namespace {

bool inRange(float value, float low, float high) { return value >= low && value <= high; }

bool usablePoint(V3 p) {
    return inRange(p.x, -1e6f, 1e6f) && inRange(p.y, -1e6f, 1e6f) && inRange(p.z, -1e6f, 1e6f);
}

bool valid(const SimulationSettings& settings, int fps, std::string& error) {
    if (settings.frames < 2 || settings.frames > simulationFrameLimit(fps) ||
        settings.startFrame < 0 || settings.startFrame > 100000 - settings.frames ||
        settings.detail < 0 || settings.detail > 2 ||
        !inRange(settings.gravity, 0, 50) || !(length(settings.velocity) <= 100)) {
        error = "Use 2 to 240 frames within the timeline, up to 12 seconds, gravity up to 50, and speed up to 100.";
        return false;
    }
    if (settings.gravityMode < GravityMode::Direction || settings.gravityMode > GravityMode::Repel ||
        !usablePoint(settings.direction) || !usablePoint(settings.center) || !inRange(settings.radius, .1f, 1000) ||
        (settings.gravity > 0 && settings.gravityMode == GravityMode::Direction && length(settings.direction) < 1e-6f)) {
        error = "Give gravity a nonzero direction or a field center and radius.";
        return false;
    }
    return true;
}

struct Step {
    int substeps;
    float dt;
    Step(int fps, int detail) : substeps(((60 << detail) + fps - 1) / fps), dt(1.f / (fps * substeps)) {}
};

struct GravityField {
    const SimulationSettings& settings;
    V3 uniform;
    explicit GravityField(const SimulationSettings& time) : settings(time), uniform(normalize(time.direction) * time.gravity) {}

    V3 at(V3 point) const {
        if (settings.gravityMode == GravityMode::Direction) return uniform;
        V3 delta = settings.center - point;
        float distance2 = dot(delta, delta);
        float radius2 = settings.radius * settings.radius;
        float sign = settings.gravityMode == GravityMode::Repel ? -1.f : 1.f;
        return delta * (sign * settings.gravity * radius2 / ((radius2 + distance2) * std::sqrt(distance2 + .04f * radius2)));
    }
};

V3 axis(int i, float sign = 1) { return {i == 0 ? sign : 0.f, i == 1 ? sign : 0.f, i == 2 ? sign : 0.f}; }
V3 absComponents(V3 v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }
int smallestAxis(V3 v) { return v.x <= v.y && v.x <= v.z ? 0 : (v.y <= v.z ? 1 : 2); }

struct Shape {
    V3 half;
    float radius = 0;
};

Shape shapeOf(const Object& o, V3 half) {
    if (o.type != ObjType::Sphere) return {half};
    float radius = maxc(half);
    return {V3(radius), radius};
}

struct Contact { V3 normal; float depth = 0; };

Contact sphereBox(V3 offset, float radius, V3 half) {
    V3 nearest(clampf(offset.x, -half.x, half.x), clampf(offset.y, -half.y, half.y), clampf(offset.z, -half.z, half.z));
    V3 delta = offset - nearest;
    float distance = length(delta);
    if (distance >= radius) return {};
    if (distance > 1e-6f) return {delta / distance, radius - distance};
    V3 overlap = half - absComponents(offset);
    int i = smallestAxis(overlap);
    return {axis(i, offset[i] < 0 ? -1.f : 1.f), radius + overlap[i]};
}

Contact overlap(V3 a, const Shape& sa, V3 b, const Shape& sb) {
    V3 offset = a - b;
    V3 depth = sa.half + sb.half - absComponents(offset);
    if (depth.x <= 0 || depth.y <= 0 || depth.z <= 0) return {};
    if (sa.radius && sb.radius) {
        float distance = length(offset), radius = sa.radius + sb.radius;
        if (distance >= radius) return {};
        return {distance > 1e-6f ? offset / distance : V3(1, 0, 0), radius - distance};
    }
    if (sa.radius) return sphereBox(offset, sa.radius, sb.half);
    if (sb.radius) {
        Contact hit = sphereBox(-offset, sb.radius, sa.half);
        return {-hit.normal, hit.depth};
    }
    int i = smallestAxis(depth);
    return {axis(i, offset[i] < 0 ? -1.f : 1.f), depth[i]};
}

struct Solid { V3 center; Shape shape; };
struct Surface { const Object* object; V3 low, high; };

struct CollisionWorld {
    bool floor;
    std::vector<Solid> solids;
    std::vector<Surface> surfaces;

    CollisionWorld(const Scene& scene, const std::vector<bool>& excluded, bool useFloor) : floor(useFloor) {
        for (size_t i = 0; i < scene.objs.size(); ++i) {
            const auto& o = scene.objs[i];
            if (excluded[i] || o.hidden || o.csg || isLight(o.type) ||
                o.type == ObjType::Water || o.type == ObjType::Cloud || o.type == ObjType::Picture) continue;
            V3 low, high;
            if (o.type == ObjType::Ground) surfaces.push_back({&o, {}, {}});
            else if (worldBounds(o, low, high) && usablePoint(low) && usablePoint(high)) {
                if (o.type == ObjType::Terrain && o.hf) surfaces.push_back({&o, low, high});
                else solids.push_back({(low + high) * .5f, shapeOf(o, (high - low) * .5f + V3(.001f))});
            }
        }
    }

    template<class Resolve> void resolve(V3& p, const Shape& shape, Resolve apply) const {
        float bottom = p.y - shape.half.y;
        float height = floor ? 0.f : -1e20f;
        V3 normal(0, 1, 0);
        for (const auto& s : surfaces) {
            bool terrain = s.object->type == ObjType::Terrain;
            if (terrain && (p.x < s.low.x || p.x > s.high.x || p.z < s.low.z ||
                p.z > s.high.z || bottom > s.high.y)) continue;
            float top = terrain ? s.high.y + 1.f : std::max(p.y, s.object->worldPos.y) + 1000.f;
            Hit hit;
            if (intersectObject(*s.object, {{p.x, top, p.z}, {0, -1, 0}}, top - std::max(bottom - .01f, height), hit)
                && hit.p.y > height && hit.n.y > .1f) {
                height = hit.p.y;
                normal = hit.n;
            }
        }
        if (height > bottom) apply(Contact{normal, (height - bottom) * normal.y});
        for (const auto& solid : solids) {
            Contact hit = overlap(p, shape, solid.center, solid.shape);
            if (hit.depth > 0) apply(hit);
        }
    }
};

struct Body {
    int index;
    Shape shape;
    V3 center, initial, velocity, rotation, spin;
    M3 orientation;
    float inverseMass;
    std::vector<Key> keys;
};

M3 angularStep(V3 velocity, float dt) {
    float speed = length(velocity);
    if (speed < 1e-6f) return {};
    V3 n = velocity / speed;
    float c = std::cos(speed * dt), s = std::sin(speed * dt);
    M3 step;
    for (int i = 0; i < 3; ++i) {
        V3 column = axis(i) * c + cross(n, axis(i)) * s + n * (n[i] * (1 - c));
        for (int row = 0; row < 3; ++row) step.m[row][i] = column[row];
    }
    return step;
}

V3 continuousAngles(const M3& orientation, V3 previous) {
    const auto& m = orientation.m;
    constexpr float degrees = 180.f / PI;
    float sinX = clampf(-m[1][2], -1, 1);
    float x = std::asin(sinX) * degrees;
    auto nearest = [](float value, float reference) { return reference + std::remainder(value - reference, 360.f); };
    if (std::fabs(sinX) > .999999f) {
        M3 withoutRoll = orientation * rotZ(-rad(previous.z));
        float y = std::atan2(-withoutRoll.m[2][0], withoutRoll.m[0][0]) * degrees;
        return {nearest(x, previous.x), nearest(y, previous.y), previous.z};
    }
    float y = std::atan2(m[0][2], m[2][2]) * degrees;
    float z = std::atan2(m[1][0], m[1][1]) * degrees;
    V3 a(nearest(x, previous.x), nearest(y, previous.y), nearest(z, previous.z));
    V3 b(nearest(180 - x, previous.x), nearest(y + 180, previous.y), nearest(z + 180, previous.z));
    return length(a - previous) <= length(b - previous) ? a : b;
}

void rigidContact(Body& a, Body* b, const Contact& hit, const RigidSettings& settings) {
    float otherMass = b ? b->inverseMass : 0.f;
    float totalMass = a.inverseMass + otherMass;
    a.center += hit.normal * (hit.depth * a.inverseMass / totalMass);
    if (b) b->center -= hit.normal * (hit.depth * otherMass / totalMass);
    V3 va = a.velocity, vb = b ? b->velocity : V3{};
    V3 ra = hit.normal * -a.shape.radius, rb = b ? hit.normal * b->shape.radius : V3{};
    va += cross(a.spin, ra);
    if (b) vb += cross(b->spin, rb);
    V3 relative = va - vb;
    float speed = dot(relative, hit.normal);
    if (speed >= 0) return;
    float bounce = speed < -.5f ? settings.bounce : 0.f;
    float normalImpulse = -(1 + bounce) * speed / totalMass;
    V3 tangent = relative - hit.normal * speed;
    float angularMass = (a.shape.radius ? 2.5f * a.inverseMass : 0.f)
                      + (b && b->shape.radius ? 2.5f * otherMass : 0.f);
    float slip = length(tangent);
    V3 friction;
    if (slip > 1e-6f)
        friction = tangent * (-std::min(slip / (totalMass + angularMass), settings.friction * normalImpulse) / slip);
    V3 impulse = hit.normal * normalImpulse + friction;
    a.velocity += impulse * a.inverseMass;
    if (a.shape.radius) a.spin += cross(ra, friction) * (2.5f * a.inverseMass / (a.shape.radius * a.shape.radius));
    if (b) {
        b->velocity -= impulse * b->inverseMass;
        if (b->shape.radius) b->spin -= cross(rb, friction) * (2.5f * otherMass / (b->shape.radius * b->shape.radius));
    }
}

struct Particle {
    V3 position, previous, velocity;
    float density = 1, lambda = 0;
    V3 gradient, correction;
};

// The density kernel uses particle spacing as its length unit.
constexpr float kernelRadius = 2.1f;
constexpr float kernelRadius2 = kernelRadius * kernelRadius;
float kernelWeight(float distance2) {
    float q = std::max(0.f, 1 - distance2 / kernelRadius2);
    return q * q * q;
}

struct Pair {
    size_t i, j;
    V3 gradient;
    float weight;
};

void liquidPairs(const std::vector<Particle>& particles, float spacing, std::vector<Pair>& pairs, std::vector<size_t>& order) {
    pairs.clear();
    float inverseSpacing = 1 / spacing;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        float x = particles[a].position.x, y = particles[b].position.x;
        return x == y ? a < b : x < y;
    });
    for (size_t first = 0; first < order.size(); ++first) {
        size_t i = order[first];
        for (size_t next = first + 1; next < order.size(); ++next) {
            size_t j = order[next];
            V3 delta = (particles[i].position - particles[j].position) * inverseSpacing;
            if (delta.x <= -kernelRadius) break;
            float q = 1 - dot(delta, delta) / kernelRadius2;
            if (q > 0) pairs.push_back({i, j, delta * (-6.f * q * q / kernelRadius2), q * q * q});
        }
    }
}

void solveLiquid(std::vector<Particle>& particles, const std::vector<Pair>& pairs, float spacing, float rest) {
    for (auto& p : particles) { p.density = 1; p.gradient = {}; p.correction = {}; p.lambda = 0; }
    for (const auto& pair : pairs) {
        auto& a = particles[pair.i];
        auto& b = particles[pair.j];
        V3 grad = pair.gradient / rest;
        a.density += pair.weight;
        b.density += pair.weight;
        a.gradient += grad;
        b.gradient -= grad;
        a.lambda += dot(grad, grad);
        b.lambda += dot(grad, grad);
    }
    for (auto& p : particles)
        p.lambda = -std::max(0.f, p.density / rest - 1) / (.02f + p.lambda + dot(p.gradient, p.gradient));
    const float pressureScale = 1 / kernelWeight(.09f * kernelRadius2);
    for (const auto& pair : pairs) {
        auto& a = particles[pair.i];
        auto& b = particles[pair.j];
        float pressure = pair.weight * pressureScale;
        pressure *= pressure;
        V3 correction = pair.gradient * ((a.lambda + b.lambda - .003f * pressure * pressure) / rest);
        a.correction += correction;
        b.correction -= correction;
    }
    for (auto& p : particles) {
        float magnitude = length(p.correction);
        p.position += p.correction * (spacing * (magnitude > .2f ? .2f / magnitude : 1.f));
    }
}

}  // namespace

int simulationFrameLimit(int fps) { return std::min(240, std::clamp(fps, 1, 240) * 12 + 1); }

const char* gravityPresetName(GravityPreset p) {
    const char* names[] = {"Earth", "Moon", "Mars", "Jupiter", "Zero G", "Reverse", "Attract", "Repel"};
    return names[(int)p];
}

const char* rigidPresetName(RigidPreset p) {
    const char* names[] = {"Balanced", "Rubber", "Steel", "Ice", "Wood"};
    return names[(int)p];
}

const char* liquidPresetName(LiquidPreset p) {
    const char* names[] = {"Water", "Oil", "Syrup", "Lava"};
    return names[(int)p];
}

void applyGravityPreset(SimulationSettings& time, GravityPreset p) {
    const float strength[] = {9.81f, 1.62f, 3.73f, 23.1f, 0, 9.81f, 9.81f, 9.81f};
    time.gravity = strength[(int)p];
    time.direction = {0, p == GravityPreset::Reverse ? 1.f : -1.f, 0};
    time.gravityMode = p == GravityPreset::Attract ? GravityMode::Attract :
                       p == GravityPreset::Repel ? GravityMode::Repel : GravityMode::Direction;
    time.floor = p <= GravityPreset::Jupiter;
}

void applyRigidPreset(RigidSettings& settings, RigidPreset p) {
    const float bounce[] = {.35f, .88f, .42f, .13f, .24f};
    const float friction[] = {.45f, .8f, .26f, .025f, .58f};
    const float damping[] = {0.f, .08f, .03f, 0.f, .15f};
    settings.bounce = bounce[(int)p];
    settings.friction = friction[(int)p];
    settings.damping = damping[(int)p];
}

void applyLiquidPreset(LiquidSettings& settings, LiquidPreset p) {
    const float viscosity[] = {.08f, .32f, .77f, .94f};
    const float cohesion[] = {0.f, .2f, .56f, .78f};
    const float surface[] = {1.f, 1.06f, 1.12f, 1.13f};
    settings.preset = p;
    settings.viscosity = viscosity[(int)p];
    settings.cohesion = cohesion[(int)p];
    settings.surface = surface[(int)p];
}

bool bakeRigidBodies(Scene& scene, const std::vector<int>& selection, const RigidSettings& settings, std::string& error) {
    if (!valid(settings.time, scene.fps, error)) return false;
    if (!inRange(settings.bounce, 0, 1) || !inRange(settings.friction, 0, 1) || !inRange(settings.damping, 0, 5)) {
        error = "Use bounce and friction from 0 to 1 and damping from 0 to 5.";
        return false;
    }
    scene.updateAll();
    std::vector<bool> moving(scene.objs.size(), false);
    std::vector<Body> bodies;
    for (int i : selection) {
        if (i < 0 || i >= (int)scene.objs.size() || moving[i]) continue;
        const auto& o = scene.objs[i];
        if (o.hidden || o.locked || o.parent >= 0 || o.csg || isLight(o.type) ||
            isInfinite(o.type) || isHeightfield(o.type) || o.type == ObjType::Picture) continue;
        V3 low, high;
        if (!worldBounds(o, low, high) || !usablePoint(low) || !usablePoint(high)) continue;
        V3 half = (high - low) * .5f, center = (low + high) * .5f;
        half = {std::max(.025f, half.x), std::max(.025f, half.y), std::max(.025f, half.z)};
        Shape shape = shapeOf(o, half);
        float mass = shape.radius ? 4.f / 3.f * PI * shape.radius * shape.radius * shape.radius : 8 * half.x * half.y * half.z;
        M3 orientation = rotY(rad(o.rot.y)) * rotX(rad(o.rot.x)) * rotZ(rad(o.rot.z));
        bodies.push_back({i, shape, center, center, settings.time.velocity, o.rot, {}, orientation, 1 / mass, {}});
        moving[i] = true;
    }
    if (bodies.empty() || bodies.size() > SIM_MAX_BODIES) {
        error = "Select up to 96 visible, unlocked root-level solids.";
        return false;
    }
    for (size_t i = 0; i < scene.objs.size(); ++i)
        for (int p = scene.objs[i].parent; !moving[i] && p >= 0; p = scene.objs[p].parent)
            moving[i] = moving[p];
    const CollisionWorld world(scene, moving, settings.time.floor);
    const Step step(std::clamp(scene.fps, 1, 240), settings.time.detail);
    const GravityField gravity(settings.time);
    const float damping = std::exp(-settings.damping * step.dt);
    int start = settings.time.startFrame;
    for (int frame = 0; frame < settings.time.frames; ++frame) {
        for (auto& body : bodies) {
            const auto& o = scene.objs[body.index];
            if (frame && body.shape.radius) body.rotation = continuousAngles(body.orientation, body.rotation);
            V3 local = o.pos + o.parentInv * (body.center - body.initial);
            body.keys.push_back({start + frame, local, body.rotation, o.scl, Ease::Linear});
        }
        if (frame + 1 == settings.time.frames) break;
        for (int substep = 0; substep < step.substeps; ++substep) {
            for (auto& body : bodies) {
                V3 previous = body.velocity;
                body.velocity = (body.velocity + gravity.at(body.center) * step.dt) * damping;
                body.center += (body.velocity + previous) * (.5f * step.dt);
            }
            for (int pass = 0; pass < 2 + 2 * settings.time.detail; ++pass) {
                for (size_t i = 0; i < bodies.size(); ++i)
                    for (size_t j = i + 1; j < bodies.size(); ++j) {
                        auto& a = bodies[i];
                        auto& b = bodies[j];
                        Contact hit = overlap(a.center, a.shape, b.center, b.shape);
                        if (hit.depth > 0) rigidContact(a, &b, hit, settings);
                    }
                for (auto& body : bodies)
                    world.resolve(body.center, body.shape, [&](Contact hit) { rigidContact(body, nullptr, hit, settings); });
            }
            for (auto& body : bodies) {
                body.spin *= damping;
                if (body.shape.radius && length(body.spin) > 1e-6f) {
                    const M3& inv = scene.objs[body.index].parentInv;
                    V3 direction = normalize(inv * body.spin);
                    float sign = dot(cross(inv.col(0), inv.col(1)), inv.col(2)) < 0 ? -1.f : 1.f;
                    body.orientation = angularStep(direction * (length(body.spin) * sign), step.dt) * body.orientation;
                }
                if (!usablePoint(body.center) || !usablePoint(body.spin)) {
                    error = "A rigid body exceeded the simulation range.";
                    return false;
                }
            }
        }
    }
    int end = start + settings.time.frames;
    for (const auto& body : bodies) {
        auto& object = scene.objs[body.index];
        const auto& keys = object.keys;
        auto at = [&](int frame) {
            return std::lower_bound(keys.begin(), keys.end(), frame, [](const Key& key, int f) { return key.frame < f; });
        };
        auto before = at(start), after = at(end);
        std::vector<Key> combined(keys.begin(), before);
        if (start && (combined.empty() || combined.back().frame < start - 1))
            combined.push_back(object.transformAt(start - 1));
        combined.insert(combined.end(), body.keys.begin(), body.keys.end());
        if (after != keys.end() && after->frame > end) combined.push_back(object.transformAt(end));
        combined.insert(combined.end(), after, keys.end());
        object.keys = std::move(combined);
    }
    scene.frames = std::max(scene.frames, end);
    scene.applyFrame(start);
    return true;
}

int bakeLiquid(Scene& scene, int source, const LiquidSettings& settings, std::string& error) {
    if (!valid(settings.time, scene.fps, error)) return 0;
    if (settings.resolution < 2 || settings.resolution > SIM_MAX_PARTICLE_GRID || !inRange(settings.viscosity, 0, 1) ||
        !inRange(settings.cohesion, 0, 1) || !inRange(settings.surface, .8f, 1.3f) ||
        settings.preset < LiquidPreset::Water || settings.preset >= LiquidPreset::COUNT) {
        error = "Use a grid from 2 to 8, viscosity and cohesion from 0 to 1, and surface size from 0.8 to 1.3.";
        return 0;
    }
    scene.updateAll();
    V3 low, high;
    if (source < 0 || source >= (int)scene.objs.size() || scene.objs[source].hidden || scene.objs[source].locked
        || isLight(scene.objs[source].type) || !worldBounds(scene.objs[source], low, high) ||
        !usablePoint(low) || !usablePoint(high) || std::min({high.x - low.x, high.y - low.y, high.z - low.z}) < .05f) {
        error = "Select a visible, unlocked object with three-dimensional bounds for the liquid volume.";
        return 0;
    }
    V3 extent = high - low;
    float spacing = maxc(extent) / settings.resolution;
    int nx = std::max(1, (int)std::lround(extent.x / spacing));
    int ny = std::max(1, (int)std::lround(extent.y / spacing));
    int nz = std::max(1, (int)std::lround(extent.z / spacing));
    int group = scene.maxGroup();
    if (scene.objs.size() + nx * ny * nz > 20000 || group == std::numeric_limits<int>::max()) {
        error = "This scene has no room for the liquid particles.";
        return 0;
    }
    ++group;
    V3 center = (low + high) * .5f;
    float radius = .72f * spacing * settings.surface;
    Shape shape{V3(radius), radius};
    const char* materials[] = {"Water", "Mirror Water", "Bottle Green", "Molten Lava"};
    Material material = findMaterial(materials[(int)settings.preset]);
    material.name = liquidPresetName(settings.preset);
    if (settings.preset != LiquidPreset::Lava) {
        material.tex = TexType::None;
        material.bump = 0;
    }
    if (settings.preset == LiquidPreset::Syrup) material.color1 = {.65f, .31f, .065f};
    int start = settings.time.startFrame;
    std::vector<Particle> particles;
    std::vector<Object> tracks;
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        V3 p = center + V3(x - (nx - 1) * .5f, y - (ny - 1) * .5f, z - (nz - 1) * .5f) * spacing;
        particles.push_back({p, p, settings.time.velocity});
        Object ball = makeObject(ObjType::Metaball, 1);
        ball.name = "Liquid " + std::to_string(group) + " / " + std::to_string(tracks.size() + 1);
        ball.pos = p;
        ball.scl = V3(spacing * 2 * settings.surface);
        ball.metaRadius = .5f;
        ball.group = group;
        ball.mat = material;
        ball.keys.reserve(settings.time.frames);
        if (start > 0) {
            auto before = ball.appearance(0);
            before.hidden = true;
            before.ease = Ease::Hold;
            ball.appearances = {before, ball.appearance(start)};
        }
        tracks.push_back(std::move(ball));
    }
    std::vector<bool> excluded(scene.objs.size(), false);
    excluded[source] = true;
    const CollisionWorld world(scene, excluded, settings.time.floor);
    const Step step(std::clamp(scene.fps, 1, 240), settings.time.detail);
    const GravityField gravity(settings.time);
    float viscosity = 1 - std::pow(1 - .95f * settings.viscosity, step.dt * 120);
    float rest = 0;
    for (int z = -2; z <= 2; ++z) for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x)
        rest += kernelWeight((float)(x * x + y * y + z * z));
    std::vector<Pair> pairs;
    std::vector<size_t> order(particles.size());
    std::iota(order.begin(), order.end(), 0);
    for (int frame = 0; frame < settings.time.frames; ++frame) {
        for (size_t i = 0; i < particles.size(); ++i)
            tracks[i].keys.push_back({start + frame, particles[i].position, {}, tracks[i].scl, Ease::Linear});
        if (frame + 1 == settings.time.frames) break;
        for (int substep = 0; substep < step.substeps; ++substep) {
            for (auto& p : particles) {
                p.previous = p.position;
                p.velocity += gravity.at(p.position) * step.dt;
                p.position += p.velocity * step.dt;
            }
            for (int pass = 0; pass < 2 + settings.time.detail; ++pass) {
                liquidPairs(particles, spacing, pairs, order);
                solveLiquid(particles, pairs, spacing, rest);
                for (auto& p : particles) world.resolve(p.position, shape, [&](Contact hit) {
                    p.position += hit.normal * hit.depth;
                });
            }
            for (auto& p : particles) {
                if (!usablePoint(p.position)) { error = "Liquid moved beyond the simulation range."; return 0; }
                p.velocity = (p.position - p.previous) / step.dt;
                float speed = length(p.velocity);
                if (speed > 100) p.velocity *= 100 / speed;
                p.correction = {};
            }
            if (viscosity > 0 || settings.cohesion > 0) {
                liquidPairs(particles, spacing, pairs, order);
                for (const auto& pair : pairs) {
                    auto& a = particles[pair.i];
                    auto& b = particles[pair.j];
                    V3 drag = (b.velocity - a.velocity) * (viscosity * pair.weight / rest);
                    V3 delta = b.position - a.position;
                    float distance = length(delta);
                    if (distance > spacing)
                        drag += delta * (30 * settings.cohesion * pair.weight * step.dt * (distance - spacing) / distance / spacing);
                    a.correction += drag;
                    b.correction -= drag;
                }
                for (auto& p : particles) p.velocity += p.correction;
            }
        }
    }
    if (settings.hideSource) {
        auto& object = scene.objs[source];
        if (start > 0 && (object.appearances.empty() || object.appearances.front().frame >= start)) {
            auto before = object.appearances.empty() ? object.appearance(0) : object.appearances.front();
            before.frame = 0;
            putKey(object.appearances, before);
        }
        auto hide = object.appearance(start);
        hide.hidden = true;
        putKey(object.appearances, hide);
        for (auto& key : object.appearances)
            if (key.frame >= start) key.hidden = true;
    }
    for (auto& track : tracks) scene.objs.push_back(std::move(track));
    scene.frames = std::max(scene.frames, start + settings.time.frames);
    scene.applyFrame(start);
    return (int)tracks.size();
}
