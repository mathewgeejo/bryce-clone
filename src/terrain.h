#pragma once
#include "scene.h"

enum class BrushMode { Raise, Lower, Smooth, Flatten, Peak, Plateau, Roughen, Erode, COUNT };
const char* brushName(BrushMode m);

enum class TerrainKind { Mountains, Alpine, Hills, Canyon, Dunes, Volcano, Mesas, Archipelago, Crags, COUNT };
const char* terrainKindName(TerrainKind k);

void hfInit(Heightfield& f, int res);
void hfGenerate(Heightfield& f, TerrainKind k, uint32_t seed, float rough);
void hfFractal(Heightfield& f, uint32_t seed, float rough, bool ridged);
void hfSmooth(Heightfield& f);
void hfErode(Heightfield& f, int drops, uint32_t seed);
void hfAdd(Heightfield& f, float v);
void hfInvert(Heightfield& f);
void hfPlateau(Heightfield& f, float level);
void hfNormalize(Heightfield& f);
void hfBrush(Heightfield& f, float u, float v, float radius, float amount, BrushMode mode = BrushMode::Raise);
void hfResample(Heightfield& f, int res);
void hfTerrace(Heightfield& f, int levels);
void hfSpikes(Heightfield& f, uint32_t seed, int count);
void hfCrater(Heightfield& f, float u, float v, float radius, float depth);
void hfRoundEdges(Heightfield& f);
void hfMound(Heightfield& f);
void hfNoise(Heightfield& f, uint32_t seed, float amount);
void hfSharpen(Heightfield& f);
void hfFromImage(Heightfield& f, const Image& img);
