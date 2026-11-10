// OpenGL 3.3 renderer for the map viewer: terrain, colour tiles, water, sky, static, tree and skinned meshes
// (skinned on the CPU), collision wireframes, tracers. Needs a current GL context.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "con.h"
#include "game.h"
#include "scene.h"
#include "terrain.h"
#include "vfs.h"

namespace obf {

class Renderer {
public:
    // Uploads everything the scene uses; scene items must not be added or removed afterwards.
    Renderer(const Vfs& vfs, Con& con, const Heightmap& hm, const TerrainInfo& info, const std::vector<DrawItem>& scene, bool objects);
    ~Renderer();
    // seconds: animation clock; projectiles: 6 floats each (position, velocity), drawn as tracers.
    void draw(const std::vector<DrawItem>& scene, const Camera& cam, int w, int h, float seconds, bool show_collision,
              const std::vector<float>& projectiles, const std::vector<SoldierView>& soldiers);
    void save_ppm(const std::string& path, int w, int h) const;
    const float* clear_colour() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace obf
