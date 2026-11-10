// Tree meshes (.tm v3). Spec: docs/formats/tm.md; reference: tools/tm.py
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace obf {

struct TmPart {
    uint32_t first, count;  // index range: count * (24, 3, 24) indices for groups 1, 2, 3
    std::string texture;
};

struct TmMesh {
    std::vector<TmPart> groups[3];  // 1: geometry blocks, 2: triangles, 3: camera-facing sprite blocks
    std::vector<float> vertices;    // 10 floats each: pos, normal, uv, sprite corner offset (colour dropped)
    std::vector<uint16_t> indices;
};

constexpr uint32_t kTmIndicesPer[3] = {24, 3, 24};

TmMesh tm_parse(const std::vector<uint8_t>& data);  // throws std::runtime_error

}  // namespace obf
