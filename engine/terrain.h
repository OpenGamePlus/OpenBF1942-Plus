// Terrain: Init/Terrain.con settings + Heightmap.raw. Spec: docs/formats/terrain.md
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace obf {

struct TerrainInfo {
    float world_size = 0, y_scale = 0, water_level = 0;
    std::string heightmap, tex_base;  // VFS paths without extension
    std::string detail;               // detailTexName: VFS path without extension; empty if none
    std::string material_map;         // materialMap: VFS path without extension; empty if none
    int material_size = 0;            // materialSize: cells per side
};

// shortcut: scans GeometryTemplate.<key> lines instead of running the CON interpreter;
// replace when the engine gets a C++ CON interpreter (placing objects needs it).
TerrainInfo parse_terrain_con(const std::string& text);

struct Heightmap {
    int n = 0;  // samples per side
    std::vector<uint16_t> h;
    float y_scale = 0;
    // row = z / cell, column = x / cell (verified against object positions on all maps)
    float height(int row, int col) const { return h[static_cast<size_t>(row) * n + col] * y_scale / 256.0f; }
};

Heightmap load_heightmap(const std::vector<uint8_t>& raw, float y_scale);

}  // namespace obf
