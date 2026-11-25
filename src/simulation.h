#pragma once

#include "scene.h"

enum class GravityMode { Direction, Attract, Repel };
enum class GravityPreset { Earth, Moon, Mars, Jupiter, Zero, Reverse, Attract, Repel, COUNT };
enum class RigidPreset { Balanced, Rubber, Steel, Ice, Wood, COUNT };
enum class LiquidPreset { Water, Oil, Syrup, Lava, COUNT };

struct SimulationSettings {
    int frames = 60;
    float gravity = 9.81f;
    V3 velocity;
    bool floor = true;
    int startFrame = 0;
    int detail = 1;
    GravityMode gravityMode = GravityMode::Direction;
    V3 direction{0, -1, 0}, center;
    float radius = 5;
};

struct RigidSettings {
    SimulationSettings time;
    float bounce = .35f, friction = .45f;
    float damping = 0;
};

struct LiquidSettings {
    SimulationSettings time;
    int resolution = 4;
    float viscosity = .08f;
    bool hideSource = true;
    LiquidPreset preset = LiquidPreset::Water;
    float cohesion = 0, surface = 1;
};

constexpr int SIM_MAX_BODIES = 96;
constexpr int SIM_MAX_PARTICLE_GRID = 8;
const char* gravityPresetName(GravityPreset preset);
const char* rigidPresetName(RigidPreset preset);
const char* liquidPresetName(LiquidPreset preset);
void applyGravityPreset(SimulationSettings& settings, GravityPreset preset);
void applyRigidPreset(RigidSettings& settings, RigidPreset preset);
void applyLiquidPreset(LiquidSettings& settings, LiquidPreset preset);
int simulationFrameLimit(int fps);
bool bakeRigidBodies(Scene& scene, const std::vector<int>& selection, const RigidSettings& settings, std::string& error);
int bakeLiquid(Scene& scene, int source, const LiquidSettings& settings, std::string& error);
