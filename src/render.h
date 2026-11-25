#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "scene.h"

V3 skyColor(const Sky& k, V3 d);
Image terrainMaterialHeightMap(const Object& terrain, int samples, int layerIndex = -1);
// quality: -1 = use the scene's render settings, 0 draft, 1 normal (adaptive AA), 2 premium (accumulated samples).
void renderImage(const Scene& s, int w, int h, uint32_t* out, int quality = -1);
enum class RenderPass { Alpha, Depth, ObjectId };
void renderOutputPass(const Scene& scene, int w, int h, std::vector<uint32_t>& pixels, RenderPass pass);

// Progressive renderer: coarse-to-fine block passes, then adaptive anti-aliasing (normal)
// or accumulated jittered samples with soft shadows, depth of field and blurry reflections (premium).
class Renderer {
public:
    ~Renderer() { cancel(); }
    void start(const Scene& s, int w, int h, int quality = -1, const ViewBasis* previewView = nullptr);
    void cancel();
    bool resume();
    void wait();
    bool busy() const { return running.load(); }
    bool hasImage() const { return w > 0; }
    bool finished() const { return !running.load() && done.load(); }
    bool resumable() const { return !busy() && hasImage() && !finished(); }
    const Scene& scene() const { return sc; }
    int width() const { return w; }
    int height() const { return h; }
    float progress() const { return prog.load(); }
    void copyTo(std::vector<uint32_t>& out) const;

private:
    void run();
    Scene sc;
    int w = 0, h = 0, quality = 1;
    std::unique_ptr<std::atomic<uint32_t>[]> px;
    std::vector<V3> acc;
    std::vector<float> variance;
    std::vector<uint8_t> completedRows;
    int nextPass = 0;
    ViewBasis view;
    bool overrideView = false, filtered = false;
    std::thread th;
    std::atomic<bool> stop{false}, running{false}, done{false};
    std::atomic<float> prog{0};
};
