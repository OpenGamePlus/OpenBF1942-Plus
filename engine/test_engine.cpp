// Engine unit tests on synthetic data (no game files). Run via ctest.
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "anim.h"
#include "con.h"
#include "dds.h"
#include "math.h"
#include "physics.h"
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
        // One collision block: 3 vertices (16 bytes each), 1 face, then a 4-byte "tree" the parser ignores.
        u32(1), u32(12 + 48 + 4 + 8 + 4), u32(0xEB97C2FA), u32(5), u32(3);
        for (int v = 0; v < 3; ++v) f32(static_cast<float>(v)), f32(0), f32(0), u32(50);
        u32(1), d.insert(d.end(), {0, 0, 1, 0, 2, 0, 50, 0}), u32(0);
        u32(1), u32(1), u32(4), d.insert(d.end(), {'M', 'a', 't', '0'});
        u32(0), u32(0), u32(0), u32(4), u32(0x411), u32(32), u32(3), u32(3), u32(0);
        for (int v = 0; v < 3; ++v)
            for (int k = 0; k < 8; ++k) f32(k == 0 ? static_cast<float>(v) : 0.0f);
        d.insert(d.end(), {0, 0, 1, 0, 2, 0});
        SmMesh m = sm_parse(d);
        assert(m.lods.size() == 1 && m.lods[0][0].name == "Mat0" && m.lods[0][0].vertex_count() == 3);
        assert(m.collision.size() == 1 && m.collision[0].faces.size() == 3 && m.collision[0].material[0] == 50);
        assert(m.collision[0].vertices[6] == 2.0f);
        uint8_t bad[] = {0xfa, 0xc2, 0x97, 0xeb, 5, 0, 0, 0, 1, 0, 0, 0};
        assert(throws([&] { collision_parse(bad, sizeof bad); }));  // vertex data missing
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

    // Animation: same synthetic files as tools/test_anim.py.
    {
        std::vector<uint8_t> d;
        auto put = [&](const void* p, size_t n) { d.insert(d.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n); };
        auto u16 = [&](uint16_t v) { put(&v, 2); };
        auto u32 = [&](uint32_t v) { put(&v, 4); };
        auto f32 = [&](float v) { put(&v, 4); };
        auto nm = [&](const std::string& s) { u16(static_cast<uint16_t>(s.size() + 1)); put(s.c_str(), s.size() + 1); };
        // Skeleton: Root at origin, Tip 1 m along x.
        u32(1), u32(2), nm("Root"), u16(0xffff);
        for (float f : {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f}) f32(f);
        nm("Tip"), u16(0);
        for (float f : {1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f}) f32(f);
        Skeleton ske = ske_parse(d);
        assert(ske.size() == 2 && ske[1].parent == 0 && ske[1].local.m[12] == 1.0f);
        // Skin: one vertex at (1, 0.5, 0) on Tip with offset (0, 0.5, 0).
        d.clear();
        u32(1), u32(1), f32(1), f32(0.5f), f32(0), d.push_back(1), u16(0), f32(1), f32(0), f32(0.5f), f32(0), u16(1), nm("Tip");
        Skin skin = skn_parse(d);
        assert(skin.bones.size() == 1 && skin.bones[0] == "Tip");
        // Animation: Tip, 2 frames, 90 deg about z then identity, x = 2 m (prec 10).
        d.clear();
        u32(3), u16(1), nm("Tip"), u32(2), d.push_back(10), u16(17);
        auto lit = [&](int16_t a, int16_t b) { u16(3), d.push_back(2), d.push_back(0), put(&a, 2), put(&b, 2); };
        auto rep = [&](int16_t a) { u16(2), d.push_back(0x82), d.push_back(0), put(&a, 2); };
        lit(0, 0), rep(0), lit(23170, 0), lit(23170, 32767), rep(2 << 10), rep(0), rep(0);
        Animation a = baf_parse(d);
        assert(a.frames == 2 && std::fabs(a.channel(0, 4)[0] - 2.0f) < 1e-6f);
        std::vector<float> verts = {1, 0.5f, 0};
        auto map = skin_map(skin, verts, 3);
        std::vector<float> out;
        skin_apply(skin, map, bone_index(skin, ske), pose(ske, {}, 0), verts, 3, out);
        assert(std::fabs(out[0] - 1) < 1e-5f && std::fabs(out[1] - 0.5f) < 1e-5f);  // rest: unchanged
        skin_apply(skin, map, bone_index(skin, ske), pose(ske, {&a}, 0), verts, 3, out);
        assert(std::fabs(out[0] - 1.5f) < 1e-3f && std::fabs(out[1]) < 1e-3f);    // frame 0: rotated, moved
        d.resize(d.size() - 2);
        assert(throws([&] { baf_parse(d); }));
    }

    // Regressions from the code review: malformed input must not crash.
    {
        std::string unclosed = "subshader \"a\" \"b\" {" + std::string(300000, 'x');
        assert(rs_parse(unclosed).empty());  // std::regex used to overflow the stack here
        auto rs = rs_parse("subshader \"M\" \"S\" { texture \"t/x\" ; }");
        assert(rs["m"]["texture"] == "t/x");  // trailing space before ';' (matches tools/sm.py)
        Vfs v;
        v.add("loop.con", std::vector<uint8_t>{'r', 'u', 'n', ' ', 'l', 'o', 'o', 'p', '\n'});
        Con c(v);
        c.run("loop.con");  // self-recursion stops instead of overflowing the stack
        assert(!c.unknown.empty());
        Skeleton one{{"Root", -1, Mat4::identity()}};
        Animation a1;
        a1.bones = {"Root"};
        a1.frames = 1;
        a1.channels.assign(7, std::vector<float>{0});
        a1.channels[3][0] = 1;
        auto w = pose(one, {&a1}, std::nanf(""));  // NaN frame (speed "nan" in a script) is clamped
        assert(w.size() == 1 && w[0].m[15] == 1);
        assert(throws([] { parse_terrain_con("GeometryTemplate.worldSize nan\nGeometryTemplate.yScale 1\nGeometryTemplate.file h\n"); }));
    }

    // TGA type 1: 2x1 image, 8-bit indices into a 2-entry BGR palette, bottom-up rows.
    {
        std::vector<uint8_t> tga = {0, 1, 1, 0, 0, 2, 0, 24, 0, 0, 0, 0, 2, 0, 1, 0, 8, 0,
                                    255, 0, 0,  0, 0, 255,  1, 0};  // palette: blue, red; pixels: red, blue
        Image img = tga_decode(tga);
        assert(img.w == 2 && img.h == 1 && img.rgba[0] == 255 && img.rgba[2] == 0 && img.rgba[6] == 255);
        tga[24] = 9;  // index outside the palette -> black, no read past the palette
        assert(tga_decode(tga).rgba[0] == 0);
        tga.pop_back();
        assert(throws([&] { tga_decode(tga); }));
    }

    // Physics: a 4-wheel car settles on its springs (Willys values 25 / 5), drives forward, steers right (+x).
    {
        Heightmap hm;
        hm.n = 32, hm.y_scale = 1, hm.h.assign(32 * 32, 0);
        Physics phys(hm, 128);
        VehicleDef car;
        car.mass = 1000, car.drag = 1.5f, car.torque = 10, car.differential = 7;
        for (int i = 0; i < 4; ++i) {
            WheelDef w;
            w.pos[0] = i & 1 ? 0.6f : -0.6f, w.pos[1] = -0.5f, w.pos[2] = i & 2 ? 1.0f : -1.0f;
            w.radius = 0.3f, w.driven = !(i & 2), w.steer_deg = i & 2 ? 30.0f : 0.0f, w.steer_rate = 200;
            car.wheels.push_back(w);
        }
        for (int i = 0; i < 8; ++i) car.hull.insert(car.hull.end(), {i & 1 ? 0.7f : -0.7f, i & 2 ? 0.4f : -0.3f, i & 4 ? 1.4f : -1.4f});
        int v = phys.add_vehicle(car, translate(64, 2, 30));
        for (int i = 0; i < 180; ++i) phys.step(1.0f / 60);
        Mat4 t = phys.transform(v);
        assert(std::fabs(t.m[13] - 0.8f) < 0.05f && phys.speed(v) < 0.1f);  // wheel centres where the model puts them
        phys.drive(v, 1, 0);
        for (int i = 0; i < 120; ++i) phys.step(1.0f / 60);
        t = phys.transform(v);
        assert(t.m[14] > 33 && std::fabs(t.m[12] - 64) < 0.5f && phys.speed(v) > 3);
        phys.drive(v, 1, 1);
        for (int i = 0; i < 120; ++i) phys.step(1.0f / 60);
        assert(phys.transform(v).m[12] > 65);
        // Tracked: zero-strength rollers carry nothing; full steer with no throttle turns on the spot.
        VehicleDef tank = car;
        tank.tracked = true, tank.turn_rate = 32, tank.mass = 20000;
        for (auto& w : tank.wheels) w.steer_deg = 0;
        WheelDef dummy = tank.wheels[0];
        dummy.strength = dummy.damping = 0, dummy.pos[2] = 0;
        tank.wheels.push_back(dummy);
        int k = phys.add_vehicle(tank, translate(30, 2, 90));
        for (int i = 0; i < 180; ++i) phys.step(1.0f / 60);
        assert(std::fabs(phys.transform(k).m[13] - 0.8f) < 0.05f);
        phys.drive(k, 0, 1);
        for (int i = 0; i < 120; ++i) phys.step(1.0f / 60);
        t = phys.transform(k);
        float heading = std::atan2(t.m[8], t.m[10]) * 57.29578f;  // +z forward, positive = turned right
        assert(heading > 45 && heading < 75 && std::fabs(t.m[12] - 30) < 0.5f && std::fabs(t.m[14] - 90) < 0.5f);
    }

    std::puts("engine tests ok");
    return 0;
}
