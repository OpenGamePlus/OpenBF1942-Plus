// Engine unit tests on synthetic data (no game files). Run via ctest.
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "con.h"
#include "dds.h"
#include "math.h"
#include "scene.h"
#include "sm.h"
#include "rfa.h"
#include "terrain.h"
#include "tm.h"
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
    uint8_t px[16][4];
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

    // CON interpreter: same cases as tools/test_con.py.
    auto bytes = [](const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    {
        Vfs v;
        v.add("lvl/sub/a.con", bytes("if v_arg1 == host\nObject.create A\nelse\nObject.create B\nendIf\n"));
        v.add("lvl/conquest/cp.con", bytes("Object.create Level"));
        v.add("x/y/cp.con", bytes("Object.create Script"));
        v.add("common/z.inc", bytes("Object.create Root"));
        Con c(v);
        Con::Scope sc;
        c.execute("ObjectTemplate.create Bundle Jeep\nObjectTemplate.addTemplate Wheel\nObjectTemplate.active jeep\n"
                  "ObjectTemplate.setPosition 1/2/3\nrem x\nremObject.create Z\nbeginrem\nObject.create Z\nendrem\n"
                  "run sub/a host\nrun sub/a.con client\nif a == b\nif c == c\nObject.create X\nendIf\nendIf\n",
                  sc, "lvl");
        auto& jeep = c.templates["objecttemplate"]["jeep"];
        assert(jeep.type == "Bundle" && jeep.props.size() == 2 && jeep.props[1].first == "setposition");
        assert(c.instances.size() == 2 && c.instances[0].name == "A" && c.instances[1].name == "B");
        c.level_dir = "lvl";
        c.run("x/y/gametypes.con");
        Con::Scope s2;
        c.execute("run conquest/cp\nrun ../y/cp\ninclude common/z.inc\n", s2, "x/y");
        assert(c.instances.size() == 5 && c.instances[2].name == "Level" && c.instances[3].name == "Script" &&
               c.instances[4].name == "Root");
        assert(c.missing["x/y/gametypes.con"] == 1);
        Con::Scope s3{{"v_arg1", "Tiger"}};
        c.execute("var v_name\necho v_arg1 -> v_name\nObject.create v_name\nTextureManager.alternativePath t/p\n", s3);
        assert(c.instances.back().name == "Tiger" && c.settings["texturemanager"][0].second[0] == "t/p");
    }

    // SM: minimal v10 file with one collision block, one LOD, one 3-vertex triangle list.
    {
        std::vector<uint8_t> d;
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) d.push_back(static_cast<uint8_t>(v >> (8 * i))); };
        auto f32 = [&](float f) { uint32_t v; std::memcpy(&v, &f, 4); u32(v); };
        u32(10), u32(0);
        for (int i = 0; i < 6; ++i) f32(0);
        d.push_back(0);                         // v10 byte
        u32(1), u32(3), d.insert(d.end(), {1, 2, 3});  // one collision block of 3 bytes
        u32(1), u32(1), u32(4), d.insert(d.end(), {'M', 'a', 't', '0'});
        u32(0), u32(0), u32(0), u32(4), u32(0x411), u32(32), u32(3), u32(3), u32(0);
        for (int v = 0; v < 3; ++v)
            for (int k = 0; k < 8; ++k) f32(k == 0 ? static_cast<float>(v) : 0.0f);
        d.insert(d.end(), {0, 0, 1, 0, 2, 0});
        SmMesh m = sm_parse(d);
        assert(m.lods.size() == 1 && m.lods[0][0].name == "Mat0" && m.lods[0][0].vertex_count() == 3);
        assert(m.lods[0][0].vertices[16] == 2.0f && sm_triangles(m.lods[0][0]).size() == 3);
        d[d.size() - 2] = 7;  // index out of range
        assert(throws([&] { sm_parse(d); }));
        SmMaterial strip;
        strip.prim = 5;
        strip.indices = {0, 1, 2, 3, 3, 4};
        auto tris = sm_triangles(strip);
        assert((tris == std::vector<uint32_t>{0, 1, 2, 2, 1, 3}));
        auto rs = rs_parse("subshader \"Mat0\" \"StandardMesh/Default\"\n{\n\tlighting true;\n\ttexture \"texture/x_s\";\n}\n");
        assert(rs["mat0"]["texture"] == "texture/x_s" && rs["mat0"]["shader"] == "StandardMesh/Default");
    }

    // Rotation: yaw 90 maps +x to -z (left-handed, D3DXMatrixRotationY); vec parsing tolerates junk.
    {
        Mat4 r = rotation_ypr(90, 0, 0);
        assert(std::fabs(r.m[0]) < 1e-6f && std::fabs(r.m[2] + 1) < 1e-6f);
        float v[3];
        parse_vec3("1315.69`/2/", v);
        assert(std::fabs(v[0] - 1315.69f) < 1e-3f && v[1] == 2 && v[2] == 0);
    }

    // TM: one triangle in group 2, an unsized "collision" block before the geometry (same as tools/test_tm.py).
    {
        std::vector<uint8_t> d;
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) d.push_back(static_cast<uint8_t>(v >> (8 * i))); };
        auto f32 = [&](float f) { uint32_t v; std::memcpy(&v, &f, 4); u32(v); };
        u32(3), u32(0), u32(8);
        for (int i = 0; i < 6; ++i) f32(0);
        d.resize(d.size() + 24, 0);
        u32(0), u32(1), u32(0), u32(1), u32(9);
        for (char c : std::string("texture/x")) d.push_back(static_cast<uint8_t>(c));
        u32(0), u32(0);
        d.insert(d.end(), {0xfa, 0xc2, 0x97, 0xeb, 7, 7, 7, 7, 7});
        u32(4);
        for (int v = 0; v < 4; ++v) {
            f32(static_cast<float>(v)), f32(0), f32(0), f32(0), f32(1), f32(0), u32(0x80808080);
            f32(0), f32(0), f32(0), f32(v == 3 ? 1.5f : 0.0f);
        }
        u32(3), d.insert(d.end(), {0, 0, 1, 0, 2, 0});
        TmMesh tm = tm_parse(d);
        assert(tm.groups[1].size() == 1 && tm.groups[1][0].texture == "texture/x" && tm.indices.size() == 3);
        assert(tm.vertices.size() == 40 && tm.vertices[30] == 3.0f && tm.vertices[39] == 1.5f);
        d[d.size() - 10] = 4;  // stored index count no longer matches the groups
        assert(throws([&] { tm_parse(d); }));
    }

    std::puts("engine tests ok");
    return 0;
}
