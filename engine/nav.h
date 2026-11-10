// AI navigation: level-0 path-finding maps (<Map>/Pathfinding/<name>level0map.raw) and A* on a coarse grid.
// Spec: docs/formats/pathfinding.md; reference: tools/pathfinding.py
#pragma once
#include <cstdint>
#include <vector>

namespace obf {

struct NavGrid {
    int n = 0;                     // cells per side
    float cell = 1;                // metres per cell
    std::vector<uint8_t> blocked;  // n * n, row = z, column = x
    bool at(int x, int z) const { return x < 0 || z < 0 || x >= n || z >= n || blocked[static_cast<size_t>(z) * n + x]; }
};

// Level-0 map -> one cell per map cell (world_size / cells metres). Throws std::runtime_error on bad data.
NavGrid nav_parse(const std::vector<uint8_t>& data, float world_size);
// Coarser grid: a cell is blocked when more than half of its factor x factor source cells are.
NavGrid nav_coarse(const NavGrid& g, int factor);
// Path from a to b (x, z in metres) through free cells: A* with 8 neighbours (no corner cutting), then shortened to
// the waypoints a straight walk cannot skip. Empty if no path; the start and goal snap to the nearest free cell.
std::vector<float> nav_path(const NavGrid& g, float ax, float az, float bx, float bz, int max_expand = 200000);

}  // namespace obf
