// Placed objects -> meshes with world transforms. Rules: docs/formats/con.md "Object placement".
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "con.h"
#include "math.h"

namespace obf {

struct DrawItem {
    std::string mesh;  // VFS path of a .sm or .tm file
    Mat4 world;
    // AnimatedMesh only: .skn path, skeleton (.ske) and animation state, inherited from the owning templates.
    std::string skin, skeleton;
    std::vector<std::string> states;  // by setAnimationState index (soldiers: 0 lower body, 1 upper body)
    std::string lightmap;  // static .sm only: <level>/objectlightmaps/<file>_<x>-<y>-<z>.tga, if present
};

// skipped counts what could not be drawn, by reason ("treemesh", "missing template", ...).
// hide: child templates to leave out (lower-case name), e.g. first-person soldier parts.
std::vector<DrawItem> build_scene(const Con& con, const Vfs& vfs, std::map<std::string, int>& skipped,
                                  const std::function<bool(const std::string&)>& hide = nullptr);

// "x/y/z" -> 3 floats; atof semantics, missing parts are 0.
void parse_vec3(const std::string& s, float out[3]);

}  // namespace obf
