#pragma once
#include "scene.h"

enum class BrushMode { Raise, Lower, Smooth, Flatten, Peak, Plateau, Roughen, Erode, COUNT };
const char* brushName(BrushMode m);

enum class TerrainKind { Mountains, Alpine, Hills, Canyon, Dunes, Volcano, Mesas, Archipelago, Crags, Badlands, SaltFlats, Glacial, COUNT };
const char* terrainKindName(TerrainKind k);

void hfInit(Heightfield& f, int res);
void hfGenerate(Heightfield& f, TerrainKind k, uint32_t seed, float rough);
void hfFractal(Heightfield& f, uint32_t seed, float rough, bool ridged);
void hfCustomFractal(Heightfield& f, uint32_t seed, float rough, bool ridged, float frequency, int octaves);
void hfSmooth(Heightfield& f);
void hfErode(Heightfield& f, int drops, uint32_t seed);
void hfAdd(Heightfield& f, float v);
void hfInvert(Heightfield& f);
void hfPlateau(Heightfield& f, float level);
void hfNormalize(Heightfield& f);
struct HeightRegion { int x0, z0, x1, z1; };
struct TerrainPatch { int width = 0, height = 0; std::vector<float> values; };
TerrainPatch hfCopyRegion(const Heightfield& f, HeightRegion area);
void hfPasteRegion(Heightfield& f, HeightRegion area, const TerrainPatch& patch);
void hfBlendImage(Heightfield& f, HeightRegion area, const Image& image, float amount, const Image* mask);
void hfBrush(Heightfield& f, float u, float v, float radius, float amount, BrushMode mode = BrushMode::Raise, const HeightRegion* mask = nullptr);
void hfResample(Heightfield& f, int res);
void hfTerrace(Heightfield& f, int levels);
void hfSpikes(Heightfield& f, uint32_t seed, int count);
void hfCrater(Heightfield& f, float u, float v, float radius, float depth);
void hfRoundEdges(Heightfield& f);
void hfMound(Heightfield& f);
void hfNoise(Heightfield& f, uint32_t seed, float amount);
void hfSharpen(Heightfield& f);
void hfFromImage(Heightfield& f, const Image& img);
