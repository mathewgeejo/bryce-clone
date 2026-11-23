#pragma once
#include "scene.h"

void hfInit(Heightfield& f, int res);
void hfFractal(Heightfield& f, uint32_t seed, float rough, bool ridged);
void hfSmooth(Heightfield& f);
void hfErode(Heightfield& f, int drops, uint32_t seed);
void hfAdd(Heightfield& f, float v);
void hfInvert(Heightfield& f);
void hfPlateau(Heightfield& f, float level);
void hfNormalize(Heightfield& f);
void hfBrush(Heightfield& f, float u, float v, float radius, float amount);
void hfResample(Heightfield& f, int res);
