// Placed objects -> meshes with world transforms. Rules: docs/formats/con.md "Object placement".
#pragma once
#include <map>
#include <string>
#include <vector>

#include "con.h"
#include "math.h"

namespace obf {

struct DrawItem {
    std::string mesh;  // VFS path of a .sm or .tm file
    Mat4 world;
};

// skipped counts what could not be drawn, by reason ("treemesh", "missing template", ...).
std::vector<DrawItem> build_scene(const Con& con, const Vfs& vfs, std::map<std::string, int>& skipped);

// "x/y/z" -> 3 floats; atof semantics, missing parts are 0.
void parse_vec3(const std::string& s, float out[3]);

}  // namespace obf
