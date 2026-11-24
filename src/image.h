#pragma once
#include <memory>
#include <string>
#include <vector>

#include "vmath.h"

struct Image {
    int w = 0, h = 0;
    std::vector<V3> px;
    std::vector<uint8_t> alpha;
    V3 sample(float u, float v) const;
    V3 sampleClamped(float u, float v, bool nearest) const;
    float opacity(float u, float v) const;
};

std::shared_ptr<Image> loadBMP(const std::string& path);
std::shared_ptr<Image> loadImage(const std::string& path);
std::shared_ptr<const Image> loadImageCached(const std::string& path, bool refresh = false);
bool saveRaster(const std::string& path, int w, int h, const std::vector<uint32_t>& pixels, bool alpha = false);
