#pragma once
#include <memory>
#include <string>
#include <vector>

#include "vmath.h"

struct Image {
    int w = 0, h = 0;
    std::vector<V3> px;
    V3 sample(float u, float v) const;
    V3 sampleClamped(float u, float v, bool nearest) const;
};

std::shared_ptr<Image> loadBMP(const std::string& path);
std::shared_ptr<const Image> loadImageCached(const std::string& path, bool refresh = false);
