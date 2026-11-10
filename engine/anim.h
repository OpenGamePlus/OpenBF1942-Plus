// Skeletons (.ske), skins (.skn), animations (.baf), pose and skinning.
// Spec: docs/formats/anim.md; reference: tools/anim.py
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "math.h"

namespace obf {

struct Bone {
    std::string name;
    int parent;  // -1 = root; parents come first
    Mat4 local;
};
using Skeleton = std::vector<Bone>;

struct SkinInfluence {
    uint16_t bone;  // index into Skin::bones
    float weight, offset[3];
};
struct SkinRecord {
    float pos[3];
    std::vector<SkinInfluence> influences;
};
struct Skin {
    std::vector<SkinRecord> records;
    std::vector<std::string> bones;
};

struct Animation {
    std::vector<std::string> bones;
    uint32_t frames = 0;
    std::vector<std::vector<float>> channels;  // per bone 7 channels (qx qy qz qw px py pz), each `frames` long
    const float* channel(size_t bone, int c) const { return channels[bone * 7 + static_cast<size_t>(c)].data(); }
};

Skeleton ske_parse(const std::vector<uint8_t>& data);
Skin skn_parse(const std::vector<uint8_t>& data);
Animation baf_parse(const std::vector<uint8_t>& data);

// World matrix per skeleton bone. biped_root: turn 3ds Max space (up -z, facing +y) to Y-up facing +z (soldiers).
std::vector<Mat4> pose(const Skeleton& s, const std::vector<const Animation*>& anims, float frame, bool biped_root = false);

// Mesh vertex -> skin record (by position; exact, then within 1 mm), -1 = rigid.
std::vector<int> skin_map(const Skin& skin, const std::vector<float>& vertices, size_t floats_per_vertex);
// Skinned positions (3 floats per vertex); rigid vertices keep their mesh position.
void skin_apply(const Skin& skin, const std::vector<int>& map, const std::vector<int>& skin_to_skeleton,
                const std::vector<Mat4>& world, const std::vector<float>& vertices, size_t floats_per_vertex,
                std::vector<float>& out);
std::vector<int> bone_index(const Skin& skin, const Skeleton& s);  // skin bone -> skeleton bone, -1 if absent

}  // namespace obf
