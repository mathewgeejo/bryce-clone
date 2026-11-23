#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "scene.h"

V3 skyColor(const Sky& k, V3 d);
void renderImage(const Scene& s, int w, int h, uint32_t* out, bool aa);

// Progressive renderer: coarse-to-fine block passes followed by an adaptive anti-aliasing pass.
class Renderer {
public:
    ~Renderer() { cancel(); }
    void start(const Scene& s, int w, int h, bool aa);
    void cancel();
    bool busy() const { return running.load(); }
    bool hasImage() const { return w > 0; }
    int width() const { return w; }
    int height() const { return h; }
    float progress() const { return prog.load(); }
    void copyTo(std::vector<uint32_t>& out) const;

private:
    void run();
    Scene sc;
    int w = 0, h = 0;
    bool aa = true;
    std::unique_ptr<std::atomic<uint32_t>[]> px;
    std::vector<V3> acc;
    std::thread th;
    std::atomic<bool> stop{false}, running{false};
    std::atomic<float> prog{0};
};
