#include "terrain.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "vfs.h"

namespace obf {

TerrainInfo parse_terrain_con(const std::string& text) {
    TerrainInfo t;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string cmd, arg;
        if (!(ls >> cmd >> arg)) continue;
        cmd = Vfs::key(cmd);
        // atof: retail values carry junk such as "1315.69`"; the original engine ignores it too.
        if (cmd == "geometrytemplate.worldsize") t.world_size = static_cast<float>(std::atof(arg.c_str()));
        else if (cmd == "geometrytemplate.yscale") t.y_scale = static_cast<float>(std::atof(arg.c_str()));
        else if (cmd == "geometrytemplate.waterlevel") t.water_level = static_cast<float>(std::atof(arg.c_str()));
        else if (cmd == "geometrytemplate.file") t.heightmap = arg;
        else if (cmd == "geometrytemplate.texbasename") t.tex_base = arg;
    }
    if (!(t.world_size > 0) || !(t.y_scale > 0) || !std::isfinite(t.world_size) || !std::isfinite(t.y_scale) || t.heightmap.empty())
        throw std::runtime_error("Terrain.con: missing or invalid settings");
    if (!std::isfinite(t.water_level)) t.water_level = 0;
    return t;
}

Heightmap load_heightmap(const std::vector<uint8_t>& raw, float y_scale) {
    Heightmap m;
    m.n = static_cast<int>(std::lround(std::sqrt(static_cast<double>(raw.size()) / 2.0)));
    if (m.n < 2 || static_cast<size_t>(m.n) * m.n * 2 != raw.size()) throw std::runtime_error("heightmap is not square u16");
    m.h.resize(raw.size() / 2);
    std::memcpy(m.h.data(), raw.data(), raw.size());  // little-endian hosts only
    m.y_scale = y_scale;
    return m;
}

}  // namespace obf
