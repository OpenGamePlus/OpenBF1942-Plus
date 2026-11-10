// Engine unit tests on synthetic data (no game files). Run via ctest.
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "dds.h"
#include "rfa.h"
#include "terrain.h"
#include "vfs.h"

using namespace obf;

template <class F>
static bool throws(F f) {
    try {
        f();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

static std::vector<uint8_t> lzo(std::vector<uint8_t> s, size_t n) { return lzo1x_decompress(s.data(), s.size(), n); }

int main() {
    // Same streams as tools/test_rfa.py.
    auto a = lzo({20, 'a', 'b', 'c', 0x11, 0, 0}, 3);
    assert(std::string(a.begin(), a.end()) == "abc");
    auto b = lzo({21, 'a', 'b', 'c', 'd', (7 << 5) | (3 << 2), 0, 0x11, 0, 0}, 12);
    assert(std::string(b.begin(), b.end()) == "abcdabcdabcd");
    assert(throws([] { lzo({20, 'a', 'b', 'c', 0x11, 0, 0}, 4); }));
    assert(throws([] { lzo({20, 'a', 'b'}, 3); }));                   // truncated input
    assert(throws([] { lzo({18, 'a', 0x40 | (7 << 5), 9, 0x11, 0, 0}, 9); }));  // match before start

    // DXT1: c0 = white, c1 = black, indices 0,1,2,3 in the first row.
    uint8_t block[8] = {0xff, 0xff, 0x00, 0x00, 0xe4, 0, 0, 0};
    uint8_t px[16][3];
    dxt1_block(block, px);
    assert(px[0][0] == 255 && px[1][0] == 0 && px[2][0] == 170 && px[3][0] == 85 && px[4][0] == 255);

    auto t = parse_terrain_con(
        "rem x\nGeometryTemplate.worldSize 2048\ngeometrytemplate.yScale 0.6`\n"
        "GeometryTemplate.file bf1942\\levels\\X\\Heightmap\nGeometryTemplate.waterLevel 95\n");
    assert(t.world_size == 2048 && t.y_scale > 0.59f && t.y_scale < 0.61f && t.water_level == 95);
    assert(Vfs::key(t.heightmap) == "bf1942/levels/x/heightmap");
    assert(throws([] { parse_terrain_con("GeometryTemplate.yScale 1\n"); }));

    std::vector<uint8_t> raw(2 * 2 * 2, 0);
    raw[6] = 0x00, raw[7] = 0x01;  // row 1, col 1 = 256
    Heightmap h = load_heightmap(raw, 0.6f);
    assert(h.n == 2 && h.height(1, 1) > 0.59f && h.height(1, 1) < 0.61f && h.height(0, 1) == 0);
    assert(throws([] { load_heightmap(std::vector<uint8_t>(6), 1); }));

    std::puts("engine tests ok");
    return 0;
}
