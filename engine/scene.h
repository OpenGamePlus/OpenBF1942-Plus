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
    std::string owner;     // lower-case name of the object template that holds the geometry
    bool spawned = false;  // placed by an ObjectSpawner (a vehicle), not part of the static level
    int spawn = -1;        // index into build_scene's spawn points, if spawned
    std::string bone;      // skeleton bone it follows: bindToSkeletonPart / useSkeletonPartAsMain (hand weapons)
};

// What an ObjectSpawner places (the template its team would get) and where.
struct SpawnPoint {
    std::string name;
    Mat4 world;
};

// skipped counts what could not be drawn, by reason ("treemesh", "missing template", ...).
// hide: child templates to leave out (lower-case name), e.g. first-person soldier parts.
// spawns: if given, receives every ObjectSpawner's placement (DrawItem::spawn indexes it).
std::vector<DrawItem> build_scene(const Con& con, const Vfs& vfs, std::map<std::string, int>& skipped,
                                  const std::function<bool(const std::string&)>& hide = nullptr,
                                  std::vector<SpawnPoint>* spawns = nullptr);

// Extra local transform of a template node (a moving part), by lower-case template name and the how-many-th visit of
// that name in pre-order (0 for the first). Applied after the node's setPosition / setRotation, so children follow.
using JointFn = std::function<Mat4(const std::string& name, int occurrence)>;
// One template tree (e.g. a vehicle) placed at world, without a level instance. ObjectSpawners inside it (a
// destroyer's landing craft) are not followed: what they place is a vehicle of its own.
std::vector<DrawItem> build_object(const Con& con, const Vfs& vfs, const std::string& name, const Mat4& world,
                                   std::map<std::string, int>& skipped, const JointFn& joint = nullptr,
                                   const std::function<bool(const std::string&)>& hide = nullptr, bool wreck = false);
// "x/y/z" -> 3 floats; atof semantics, missing parts are 0.
void parse_vec3(const std::string& s, float out[3]);

}  // namespace obf
