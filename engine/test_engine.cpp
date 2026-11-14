// Engine unit tests on synthetic data (no game files). Run via ctest.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "anim.h"
#include "audio.h"
#include "hud.h"
#include "con.h"
#include "conquest.h"
#include "damage.h"
#include "game.h"
#include "dds.h"
#include "mat.h"
#include "nav.h"
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
        assert(std::fabs(out[0] - 2.5f) < 1e-3f && std::fabs(out[1]) < 1e-3f);    // frame 0: rotated (conjugated), moved
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
        // Moving parts: a steering hub (yaw input, +-30 deg at 200 deg/s, auto reset), an unlimited mouse-look turret,
        // and wheel 0 rolling.
        VehicleDef rig = car;
        JointDef hub, turret, wheel;
        hub.input[0] = JointDef::Yaw, hub.mn[0] = -30, hub.mx[0] = 30, hub.speed[0] = 200, hub.reset = true;
        turret.input[0] = JointDef::LookX, turret.speed[0] = 35;
        wheel.wheel = 0;
        JointDef mg = turret;  // a gunner's machine gun: moves with seat 1's look only
        mg.seat = 1;
        rig.joints[{"hub", 0}] = hub, rig.joints[{"turret", 0}] = turret, rig.joints[{"wheel", 0}] = wheel, rig.joints[{"mg", 0}] = mg;
        int r = phys.add_vehicle(rig, translate(100, 2, 30));
        for (int i = 0; i < 120; ++i) phys.step(1.0f / 60);
        phys.drive(r, 1, 1);
        phys.look(r, 1, 0);
        for (int i = 0; i < 60; ++i) phys.step(1.0f / 60);
        assert(std::fabs(phys.joint(r, "hub", 0).m[8] - 0.5f) < 0.01f);            // sin 30: clamped at the limit
        assert(std::fabs(phys.joint(r, "turret", 0).m[8] - 0.5736f) < 0.02f);     // sin 35: 1 s at 35 deg/s
        assert(phys.joint(r, "wheel", 0).m[5] < 0.999f);                           // rolled
        assert(phys.joint(r, "other", 0).m[0] == 1 && phys.joint(r, "hub", 1).m[0] == 1);  // not a joint: identity
        assert(phys.joint(r, "mg", 0).m[0] == 1);                                   // the driver's look leaves seat 1 alone
        phys.look(r, 0, 0), phys.look(r, -1, 0, 1);
        for (int i = 0; i < 60; ++i) phys.step(1.0f / 60);
        assert(std::fabs(phys.joint(r, "mg", 0).m[8] + 0.5736f) < 0.02f && std::fabs(phys.joint(r, "turret", 0).m[8] - 0.5736f) < 0.02f);
        phys.drive(r, 0, 0);
        for (int i = 0; i < 30; ++i) phys.step(1.0f / 60);
        assert(std::fabs(phys.joint(r, "hub", 0).m[8]) < 0.01f);                  // automatic reset
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
    // Aircraft: regulated main wings hold 1 g at speed; controls move the nose the documented way.
    {
        Heightmap hm;
        hm.n = 32, hm.y_scale = 1, hm.h.assign(32 * 32, 0);
        VehicleDef plane;
        plane.mass = 2500, plane.drag = 0.07f, plane.plane = true, plane.thrust = 3.75f;
        for (int i = 0; i < 8; ++i) plane.hull.insert(plane.hull.end(), {i & 1 ? 5.0f : -5.0f, i & 2 ? 0.6f : -0.6f, i & 4 ? 4.0f : -5.0f});
        auto wing = [&](float x, float y, float z, float nx, float lift, float flap, float reg, int input, float sign) {
            WingDef w;
            w.pos[0] = x, w.pos[1] = y, w.pos[2] = z, w.normal[0] = nx, w.normal[1] = nx != 0 ? 0.0f : 1.0f;
            w.lift = lift, w.flap = flap, w.regulate = reg, w.input = input, w.sign = sign;
            plane.wings.push_back(w);
        };
        wing(0, 0, 0, 0, 0, 3, 4.91f, 0, 1), wing(0, 0, 0, 0, 0, 3, 4.91f, 0, 1);    // main wings, lift at the centre
        wing(-4, 0, 0.3f, 0, 2.5f, 2, 0, 2, -1), wing(4, 0, 0.3f, 0, 2.5f, 2, 0, 2, 1);  // ailerons
        wing(-1, 0.9f, -4, 0, 0.5f, 0.5f, 0, 1, -1), wing(1, 0.9f, -4, 0, 0.5f, 0.5f, 0, 1, -1);  // elevators
        wing(0, 1.3f, -4.7f, 1, 1.5f, 1.5f, 0, 3, 1);  // rudder (vertical: normal +x)
        auto fly = [&](float yaw, float pitch, float roll) {
            Physics phys(hm, 4096);
            int v = phys.add_vehicle(plane, translate(2048, 500, 500));
            phys.set_velocity(v, 0, 0, 60);
            phys.drive(v, 0.7f, yaw, pitch, roll);
            for (int i = 0; i < 30; ++i) phys.step(1.0f / 60);
            return phys.transform(v);
        };
        Mat4 level = fly(0, 0, 0);
        assert(std::fabs(level.m[13] - 500) < 1 && std::fabs(level.m[9]) < 0.02f);  // altitude and pitch held
        assert(fly(0, -1, 0).m[9] > 0.1f);   // pitch -1: nose (z axis) up
        assert(fly(0, 0, 1).m[1] < -0.1f);   // roll +1: right wing (x axis) down
        assert(fly(1, 0, 0).m[8] > 0.05f);   // yaw +1: nose towards +x
    }
    // Boat (LCVP floats): 4 x lift 6 hold g with 9.81 / 24 of each 2.8 m column under water, so the origin rests
    // 0.3 + 0.409 x 2.8 = 1.444 m below the surface (retail spawns LCVPs 1.44 m below waterLevel).
    {
        Heightmap hm;
        hm.n = 32, hm.y_scale = 1, hm.h.assign(32 * 32, 0);
        Physics phys(hm, 4096, 30);
        VehicleDef boat;
        boat.mass = 30000, boat.drag = 0.488f, boat.ship = true, boat.thrust = 1.25f;
        for (int i = 0; i < 8; ++i) boat.hull.insert(boat.hull.end(), {i & 1 ? 1.8f : -1.8f, i & 2 ? 3.0f : -0.2f, i & 4 ? 5.0f : -5.0f});
        for (int i = 0; i < 4; ++i) {
            FloatDef f;
            f.pos[0] = i & 1 ? 1.7f : -1.7f, f.pos[1] = 3.1f, f.pos[2] = i & 2 ? 4.5f : -4.5f, f.height = 2.8f, f.lift = 6;
            boat.floats.push_back(f);
        }
        for (float z : {4.5f, -4.5f}) {  // water rudders, as the LCVP's
            WingDef w;
            w.pos[2] = z, w.normal[0] = 1, w.normal[1] = 0, w.flap = 2, w.input = 3, w.sign = z > 0 ? -1.0f : 1.0f;
            boat.wings.push_back(w);
        }
        int b = phys.add_vehicle(boat, translate(2048, 31, 2048));
        for (int i = 0; i < 600; ++i) phys.step(1.0f / 60);
        Mat4 t = phys.transform(b);
        assert(std::fabs(t.m[13] - 30 + 1.444f) < 0.05f && t.m[5] > 0.99f && phys.speed(b) < 0.1f);
        phys.drive(b, 1, 0);
        for (int i = 0; i < 300; ++i) phys.step(1.0f / 60);
        assert(phys.transform(b).m[14] > 2048 + 15);
        phys.drive(b, 1, 1);
        for (int i = 0; i < 120; ++i) phys.step(1.0f / 60);
        assert(phys.transform(b).m[8] > 0.05f);  // yaw +1: bow towards +x
    }

    // Postures: a shot at chest height passes over a prone soldier and hits a standing one.
    {
        Heightmap hm;
        hm.n = 32, hm.y_scale = 1, hm.h.assign(32 * 32, 0);
        Physics phys(hm, 128, -1);
        int ch = phys.add_character(64, 0.1f, 64);
        for (int i = 0; i < 30; ++i) phys.step(1.0f / 60);
        assert(phys.set_posture(ch, 1) && phys.set_posture(ch, 2));
        for (int i = 0; i < 10; ++i) phys.step(1.0f / 60);
        float from[3] = {64, 1.0f, 54}, at[3] = {0, 0.2f, 300};  // nearly flat, through the chest of a standing man
        phys.fire(from, at, 220, -1, 1);
        for (int i = 0; i < 30; ++i) phys.step(1.0f / 60);
        auto hits = phys.take_hits();
        assert(std::none_of(hits.begin(), hits.end(), [&](const Physics::Hit& h) { return h.character == ch; }));
        assert(phys.set_posture(ch, 0));
        for (int i = 0; i < 10; ++i) phys.step(1.0f / 60);
        phys.fire(from, at, 220, -1, 1);
        for (int i = 0; i < 30; ++i) phys.step(1.0f / 60);
        hits = phys.take_hits();
        // (the first shot, over the prone soldier, lands in the water 200 m on meanwhile)
        assert(std::count_if(hits.begin(), hits.end(), [&](const Physics::Hit& h) { return h.character == ch; }) == 1);
    }

    // Parachute: falls freely, then settles near sqrt(g / k) = 6.4 m/s for k = 24 / 100; landing speed reported once.
    {
        Heightmap hm;
        hm.n = 32, hm.y_scale = 1, hm.h.assign(32 * 32, 0);
        Physics phys(hm, 128, -1);
        int ch = phys.add_character(64, 300, 64);
        float p0[3], p1[3];
        for (int i = 0; i < 120; ++i) phys.step(1.0f / 60);
        phys.open_parachute(ch, 0.24f);
        assert(phys.parachute_open(ch));
        for (int i = 0; i < 600; ++i) phys.step(1.0f / 60);
        phys.character_position(ch, p0);
        phys.step(1.0f / 60);
        phys.character_position(ch, p1);
        assert(std::fabs((p0[1] - p1[1]) * 60 - 6.39f) < 0.2f);
        for (int i = 0; i < 60 * 40 && !phys.character_on_ground(ch); ++i) phys.step(1.0f / 60);
        phys.step(1.0f / 60);
        float land = phys.take_landing(ch);
        assert(phys.character_on_ground(ch) && !phys.parachute_open(ch) && land > 5.5f && land < 7 && phys.take_landing(ch) == 0);
    }

    // Soldier: lands on the ground, walks 5 m in 1 s, jumps, and swims with the head above water.
    {
        Heightmap hm;
        hm.n = 32, hm.y_scale = 1, hm.h.assign(32 * 32, 0);
        Physics phys(hm, 128, -1);
        int ch = phys.add_character(64, 2, 64);
        float p[3];
        for (int i = 0; i < 90; ++i) phys.step(1.0f / 60);
        phys.character_position(ch, p);
        assert(std::fabs(p[1]) < 0.05f && phys.character_on_ground(ch));
        phys.walk(ch, 0, 5, false);
        for (int i = 0; i < 60; ++i) phys.step(1.0f / 60);
        phys.character_position(ch, p);
        assert(std::fabs(p[2] - 69) < 0.3f && std::fabs(p[0] - 64) < 0.05f);
        phys.walk(ch, 0, 0, true);
        for (int i = 0; i < 18; ++i) phys.step(1.0f / 60);
        phys.character_position(ch, p);
        assert(p[1] > 0.5f && !phys.character_on_ground(ch));
        // Sound counts: the 2 m drop at the start and the jump's touch-down are landings, the jump is one jump.
        unsigned jumps, landings;
        phys.character_steps(ch, jumps, landings);
        assert(jumps == 1 && landings == 1);
        for (int i = 0; i < 60; ++i) phys.step(1.0f / 60);
        phys.character_steps(ch, jumps, landings);
        assert(phys.character_on_ground(ch) && jumps == 1 && landings == 2);
        {  // Swimming: dropped into 5 m of water, the soldier floats with its feet about 1.4 m under the surface
            Physics wet(hm, 128, 5);
            int sw = wet.add_character(64, 6, 64);
            for (int i = 0; i < 300; ++i) wet.step(1.0f / 60);
            wet.character_position(sw, p);
            assert(wet.character_swimming(sw) && p[1] > 3.3f && p[1] < 3.9f);
            assert(!phys.character_swimming(ch));
        }
        // Hull parts: a 20 x 20 m deck 1 m high with a 10 m tower is a compound; a soldier lands on the deck, not on the
        // single convex hull's slope up to the tower top.
        VehicleDef ship;
        ship.mass = 100000;
        auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
            for (int i = 0; i < 8; ++i) ship.hull.insert(ship.hull.end(), {i & 1 ? x1 : x0, i & 2 ? y1 : y0, i & 4 ? z1 : z0});
            ship.hull_split.push_back(ship.hull.size());
        };
        box(-10, 0, -10, 10, 1, 10), box(5, 1, -1, 7, 10, 1);
        phys.add_vehicle(ship, translate(30, 0.1f, 30));
        int on_deck = phys.add_character(25, 3, 30);
        for (int i = 0; i < 120; ++i) phys.step(1.0f / 60);
        phys.character_position(on_deck, p);
        assert(std::fabs(p[1] - 1.1f) < 0.15f);
        // Shots hit a soldier's body at the right height, not the one firing, and pass a dead one.
        int target = phys.add_character(80, 0.1f, 80);
        for (int i = 0; i < 30; ++i) phys.step(1.0f / 60);
        phys.take_hits();
        float from[3] = {80, 1.2f, 60}, at[3] = {0, 0, 400}, own[3] = {80, 1.2f, 80.5f};
        phys.fire(from, at, 220, -1, 2);
        phys.fire(own, at, 220, -1, 2, -1, target);  // its own shot, from inside its capsule
        for (int i = 0; i < 20; ++i) phys.step(1.0f / 60);
        auto hits = phys.take_hits();
        int on_target = 0;
        for (auto& h : hits)
            if (h.character == target) ++on_target, assert(std::fabs(h.height - 1.2f) < 0.3f && h.vehicle < 0);
        assert(on_target == 1);
        float far_eye[3] = {80, 1.5f, 40}, chest[3] = {80, 1.2f, 80};
        assert(phys.line_of_sight(far_eye, chest, -1, target, -1));
        int wall = phys.add_character(80, 0.1f, 60);  // another soldier between them blocks the view
        for (int i = 0; i < 10; ++i) phys.step(1.0f / 60);
        assert(!phys.line_of_sight(far_eye, chest, -1, target, -1) && phys.line_of_sight(far_eye, chest, wall, target, -1));
        phys.kill_character(wall);
        phys.kill_character(target);
        phys.fire(from, at, 220, -1, 2);
        for (int i = 0; i < 20; ++i) phys.step(1.0f / 60);
        for (auto& h : phys.take_hits()) assert(h.character < 0);
        Physics sea(hm, 128, 10);  // ground 10 m under the surface
        int sw = sea.add_character(64, 12, 64);
        for (int i = 0; i < 300; ++i) sea.step(1.0f / 60);
        sea.character_position(sw, p);
        assert(std::fabs(p[1] - 8.6f) < 0.3f);
    }

    // Projectiles: hit the armour face they meet (front 52, rear 50), the ground, never the vehicle that fired.
    {
        Heightmap hm;
        hm.n = 32, hm.y_scale = 1, hm.h.assign(32 * 32, 0);
        Physics phys(hm, 128, -1);
        VehicleDef box;
        box.mass = 1000, box.material = 45;
        for (int i = 0; i < 8; ++i) box.hull.insert(box.hull.end(), {i & 1 ? 1.0f : -1.0f, i & 2 ? 2.0f : 0.0f, i & 4 ? 1.5f : -1.5f});
        for (float z : {1.4f, -1.4f}) {  // a quad across each end, just inside the hull
            box.armor.insert(box.armor.end(), {-1, 0, z, 1, 0, z, 1, 2, z, -1, 0, z, 1, 2, z, -1, 2, z});
            box.armor_material.insert(box.armor_material.end(), 2, z > 0 ? 52 : 50);
        }
        int v = phys.add_vehicle(box, translate(64, 0.1f, 64));
        for (int i = 0; i < 30; ++i) phys.step(1.0f / 60);
        float front[3] = {64, 1, 84}, back[3] = {64, 1, 44}, south[3] = {0, 0, -100}, north[3] = {0, 0, 100}, down[3] = {0, -100, 0};
        phys.fire(front, south, 236, 206, 5);
        phys.fire(back, north, 236, 206, 5);
        float above[3] = {80, 5, 80};
        phys.fire(above, down, 220, -1, 5);
        float inside[3] = {64, 1, 64}, up[3] = {0, 50, -20};
        phys.fire(inside, up, 236, -1, 5, v);  // fired from the vehicle itself: passes out of it, still rising after 1 s
        for (int i = 0; i < 60; ++i) phys.step(1.0f / 60);
        auto hits = phys.take_hits();
        assert(hits.size() == 3 && phys.take_hits().empty());
        int seen = 0;
        for (auto& h : hits) {
            if (h.vehicle == v && h.armor == 52 && h.pos[2] > 65) seen |= 1;
            if (h.vehicle == v && h.armor == 50 && h.pos[2] < 63) seen |= 2;
            if (h.vehicle == -1 && h.material == 220 && std::fabs(h.pos[1]) < 0.2f) seen |= 4;
        }
        assert(seen == 7);
        std::vector<float> live;
        phys.projectiles(live);
        assert(live.size() == 6);  // the shot from inside is still flying
        float high[3] = {20, 40, 20}, still[3] = {0, 0, 0};
        phys.fire(high, still, 70, 205, 0.5f, -1, -1, 2, 15);  // a grenade: its fuse runs out in the air
        phys.fire(high, still, 220, -1, 0.5f);                  // a bullet without radius: just gone
        for (int i = 0; i < 40; ++i) phys.step(1.0f / 60);
        hits = phys.take_hits();
        assert(hits.size() == 1 && hits[0].radius == 15 && hits[0].material2 == 205 && hits[0].tag == 2 && hits[0].pos[1] > 35);
        float toss[3] = {30, 3, 30}, fwd[3] = {4, 2, 0};
        int g = phys.fire(toss, fwd, 70, 205, 300, -1, -1, 2, 15, true);  // bounces, comes to rest, waits
        for (int i = 0; i < 300; ++i) phys.step(1.0f / 60);
        float rest[3];
        assert(phys.take_hits().empty() && phys.shot_position(g, rest) && rest[1] < 0.3f && rest[1] > -0.1f && rest[0] > 31);
        phys.explode(g);
        phys.step(1.0f / 60);
        hits = phys.take_hits();
        assert(hits.size() == 1 && hits[0].radius == 15 && !phys.shot_position(g, rest));
    }

    // Navigation: level-0 map tiles (free, blocked, bits), coarse grid, A* around a wall with string pulling.
    {
        std::vector<uint8_t> d(32, 0);
        uint32_t hdr[8] = {1, 1, 5, 0, 0, 2, 0, 0xFFFFFFFFu};
        std::memcpy(d.data(), hdr, 32);
        auto put = [&](uint32_t w) { d.insert(d.end(), reinterpret_cast<uint8_t*>(&w), reinterpret_cast<uint8_t*>(&w) + 4); };
        put(0), put(1), put(0xFFFFFFFFu);  // tiles: free, blocked, bits
        for (int r = 0; r < 32; ++r) put(r == 5 ? 1u : 0u);  // row 5, cell 0
        put(0);
        NavGrid g = nav_parse(d, 128);
        assert(g.n == 64 && g.cell == 2 && !g.at(0, 0) && g.at(32, 0) && g.at(0, 37) && !g.at(1, 37) && g.at(-1, 0));
        assert(throws([&] { auto e = d; e.push_back(0); nav_parse(e, 128); }));
        assert(throws([&] { auto e = d; e[36] = 7; nav_parse(e, 128); }));
        NavGrid c = nav_coarse(g, 2);
        assert(c.n == 32 && c.cell == 4 && c.at(16, 0) && !c.at(0, 0));
        // A wall across x = 20 from z = 0 to z = 50 on an open 64 x 64 grid: the way round passes its end.
        NavGrid open;
        open.n = 64, open.cell = 1, open.blocked.assign(64 * 64, 0);
        for (int z = 0; z <= 50; ++z) open.blocked[static_cast<size_t>(z) * 64 + 20] = 1;
        std::vector<float> p = nav_path(open, 10.5f, 10.5f, 30.5f, 10.5f);
        assert(p.size() >= 4 && p.size() <= 8);  // two or three straight legs
        bool round = false;
        for (size_t i = 0; i + 1 < p.size(); i += 2) round |= p[i + 1] > 50;
        assert(round && std::fabs(p[p.size() - 2] - 30.5f) < 0.01f && std::fabs(p.back() - 10.5f) < 0.01f);
        for (int z = 0; z < 64; ++z) open.blocked[static_cast<size_t>(z) * 64 + 20] = 1;  // wall closed: no path
        assert(nav_path(open, 10.5f, 10.5f, 30.5f, 10.5f).empty());
    }

    // CON: a bare run keeps the caller's arguments (control points are guarded by "if v_arg1 == host" twice).
    {
        Vfs vfs;
        std::string cp = "if v_arg1 == host\nObject.create CP\nendIf\n", mode = "run cp\nrun cp client\n";
        vfs.add("cp.con", std::vector<uint8_t>(cp.begin(), cp.end()));
        vfs.add("mode.con", std::vector<uint8_t>(mode.begin(), mode.end()));
        Con con(vfs);
        con.run("mode.con", {"host"});
        assert(con.instances.size() == 1 && con.instances[0].name == "CP");
        std::string jeep = "ObjectTemplate.create Bundle Jeep\n";
        vfs.add("objects/jeep/objects.con", std::vector<uint8_t>(jeep.begin(), jeep.end()));
        con.run("objects/jeep/objects.con");
        assert(con.templates["objecttemplate"]["jeep"].dir == "objects/jeep");  // relative paths (sound scripts) start here
    }

    // Conquest: neutralise then capture when alone; contested freezes; bases stay; fewer flags bleed tickets.
    {
        Conquest q;
        ConquestFlag base, mid;
        base.team = 1, base.locked = true, mid.team = 1, mid.to_lose = 5, mid.to_get = 10, mid.name = "Mid";
        q.flags = {base, mid};
        q.tickets = {0, 10, 10};
        std::vector<std::array<int, 3>> attack = {{0, 0, 3}, {0, 0, 1}}, both = {{0, 0, 0}, {0, 1, 1}};
        std::vector<std::string> ev;
        for (int i = 0; i < 51; ++i) ev = q.update(0.1f, attack);  // 5 s
        assert(q.flags[1].team == 0 && q.flags[0].team == 1 && q.tickets[1] == 10);
        for (int i = 0; i < 95; ++i) q.update(0.1f, attack);
        assert(q.flags[1].team == 0);
        for (int i = 0; i < 20; ++i) q.update(0.1f, both);  // contested: frozen
        for (int i = 0; i < 6; ++i) q.update(0.1f, attack);
        assert(q.flags[1].team == 2 && q.owned(2) == 1 && q.owned(1) == 1);
        q.flags[0].team = 0, q.bleed = {0, 60, 60};  // team 1 now holds fewer: 60 tickets a minute
        for (int i = 0; i < 50; ++i) q.update(0.1f, both);
        assert(std::fabs(q.tickets[1] - 5) < 0.01f && q.tickets[2] == 10 && !q.winner);
        for (int i = 0; i < 5; ++i) q.death(1);
        ev = q.update(0.1f, both);
        assert(q.winner == 2 && !ev.empty() && ev.back() == "team 2 wins");
    }

    // Damage: materialDamage x damageMod(attGroup, defGroup), material groups default to the material number.
    {
        Vfs vfs;
        std::string mm = "MaterialManager.material 236\nMaterialManager.materialDamage 10\n"
                               "MaterialManager.material 52\nMaterialManager.materialDefGroup 52\nMaterialManager.materialDamage 1\n"
                               "MaterialManager.material 7\nMaterialManager.materialAttGroup 236\nMaterialManager.materialDamage 0.1.0\n"
                               "MaterialManager.attGroup 236\nMaterialManager.defGroup 52\nMaterialManager.damageMod 4\n"
                               "MaterialManager.setEffectTemplate e_ExplSmall2\n";
        vfs.add("game/mm.con", std::vector<uint8_t>(mm.begin(), mm.end()));
        Con con(vfs);
        con.run("game/mm.con");
        DamageTable t = damage_table(con.settings["materialmanager"]);
        assert(t.damage(236, 52) == 40 && t.damage(52, 236) == 0 && t.damage(999, 52) == 0);
        assert(std::fabs(t.damage(7, 52) - 0.4f) < 1e-6f);  // shared attGroup; "0.1.0" reads as 0.1
        assert(t.effect(236, 52) == "e_explsmall2" && t.effect(7, 52) == "e_explsmall2" && t.effect(52, 236).empty());
    }

    {  // audio: wav, sound script, mixer
        std::vector<uint8_t> w = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                                  0x44, 0xAC, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'L', 'I', 'S', 'T', 2, 0, 0, 0, 0, 0,
                                  'd', 'a', 't', 'a', 4, 0, 0, 0, 0, 0x40, 0, 0xC0};
        Wav wav = wav_parse(w);  // skips the LIST chunk
        assert(wav.rate == 44100 && wav.channels == 1 && wav.frames() == 2 && wav.samples[0] == 16384 && wav.samples[1] == -16384);
        bool threw = false;
        try {
            w[40] = 200;  // LIST chunk runs past the end
            wav_parse(w);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(threw);

        std::map<std::string, std::string> files = {
            {"objects/gun/sounds/gun.ssc", "#templateLevel HIGH\r\n\t#include High/Gun.ssc\r\n#templateLevel LOW\r\n\t#include Low/Gun.ssc\r\n"},
            {"objects/gun/sounds/high/gun.ssc",
             "newPatch\nload @ROOT/Sound/@RTD/Shot.wav\nminDistance 4\nvolume .5\n*** Distance Volume ***\nbeginEffect\n"
             "controlDestination Volume\ncontrolSource Distance\nenvelope Ramp\nparam 100\nparam 200\nparam 1\nparam -1\nendEffect\n"
             "/*\nload @ROOT/Sound/@RTD/Unused.wav\n*/\nload @ROOT/Sound/@RTD/Loop.wav\nloop\nstereo\n"},
            {"objects/gun/sounds/low/gun.ssc", "load @ROOT/Sound/@RTD/Low.wav\n"}};
        auto read = [&](const std::string& f) { return files.count(f) ? files[f] : std::string(); };
        SoundPatch sp = ssc_parse("Objects/Gun/Sounds/../Sounds/Gun.ssc", read);
        assert(sp.layers.size() == 2 && sp.layers[0].wav == "sound/44khz/shot.wav" && sp.layers[0].min_distance == 4);
        assert(sp.layers[0].volume == 0.5f && sp.layers[0].effects.size() == 1 && sp.layers[1].loop && sp.layers[1].stereo);
        const SoundEffect& fx = sp.layers[0].effects[0];
        assert(fx.source == SoundEffect::Distance && fx.eval(50) == 1 && fx.eval(150) == 0.5f && fx.eval(300) == 0);
        assert(ssc_parse("objects/gun/sounds/gun.ssc", read, "LOW").layers.size() == 1);
        files["objects/mg/mg.ssc"] = "newPatch\nload @ROOT/Sound/@RTD/Single.wav\nnewPatch\nload @ROOT/Sound/@RTD/Reload.wav\n"
                                     "newPatch\nnewPatch\nnewPatch\nnewPatch\nload @ROOT/Sound/@RTD/Burst.wav\nloop\nstop FinishSample\n";
        auto ps = ssc_patches("objects/mg/mg.ssc", read);
        assert(ps.size() == 6 && ps[1].layers[0].wav == "sound/44khz/reload.wav" && ps[3].layers.empty());
        assert(ps[5].layers[0].loop && ps[5].layers[0].finish && ssc_parse("objects/mg/mg.ssc", read).layers.size() == 3);

        auto tone = std::make_shared<Wav>();  // 0.1 s at 44.1 kHz, constant
        tone->rate = 44100, tone->samples.assign(4410, 16384);
        SoundPatch one;
        one.layers.push_back(sp.layers[0]);
        Mixer mixer(44100);
        float right_of_listener[3] = {8, 0, 0};  // 8 m: half the level of minDistance 4
        mixer.play(one, {tone}, right_of_listener);
        std::vector<float> out(2 * 2048, 0.0f);
        mixer.mix(out.data(), 2048, Listener{});
        assert(out[1] > 0.17f && out[1] < 0.18f && std::fabs(out[0]) < 1e-6f);  // 0.5 sample * 0.5 volume * 0.5 distance * sqrt(2) pan, all right
        mixer.mix(out.data(), 4096 - 2048, Listener{});
        mixer.mix(out.data(), 2048, Listener{});
        assert(mixer.voices() == 0);  // the one-shot ended
        {  // trigger Volume: silent and unread until its time step (0.05 s), then from its first sample
            files["objects/knife/knife.ssc"] = "newPatch\nload @ROOT/Sound/@RTD/knf1.wav\ntrigger Volume\nbeginEffect\n"
                                               "controlDestination Volume\ncontrolSource Time\nenvelope Ramp\nparam 0.05\nparam 0.05\n"
                                               "param 0\nparam 1\nendEffect\n";
            SoundPatch knife = ssc_parse("objects/knife/knife.ssc", read);
            assert(knife.layers.size() == 1 && knife.layers[0].trigger);
            auto ramp = std::make_shared<Wav>();  // rising samples: shows where reading started
            ramp->rate = 44100;
            for (int i = 0; i < 4410; ++i) ramp->samples.push_back(static_cast<int16_t>(i + 1));
            Mixer m2(44100);
            float here[3] = {0, 0, 0};
            m2.play(knife, {ramp}, here);
            std::vector<float> o(2 * 2048, 0.0f);
            m2.mix(o.data(), 1024, Listener{});  // 0-23 ms: before its step
            assert(std::fabs(o[0]) < 1e-9f && std::fabs(o[2 * 1023]) < 1e-9f);
            m2.mix(o.data(), 1024, Listener{}), m2.mix(o.data(), 1024, Listener{});  // up to 70 ms: started at the block after 50 ms
            std::fill(o.begin(), o.end(), 0.0f);
            m2.mix(o.data(), 1, Listener{});
            assert(o[0] > 0 && o[0] < 1024.0f / 32768.0f);  // early in the sample, not 3 blocks in
        }
        {  // trigger Release: silent until the patch is released (landing gear's stop clunk), then plays once
            files["objects/gear/gear.ssc"] = "newPatch\nload @ROOT/Sound/@RTD/stop.wav\ntrigger Release\n";
            SoundPatch gear = ssc_parse("objects/gear/gear.ssc", read);
            assert(gear.layers.size() == 1 && gear.layers[0].on_release && !gear.layers[0].trigger);
            Mixer m4(44100);
            float here[3] = {0, 0, 0};
            int h4 = m4.play(gear, {tone}, here);
            std::vector<float> o(2 * 64, 0.0f);
            m4.mix(o.data(), 64, Listener{});
            assert(std::fabs(o[0]) < 1e-9f && m4.voices() == 1);
            m4.release(h4);
            m4.mix(o.data(), 64, Listener{});
            assert(std::fabs(o[2 * 63]) > 1e-6f);
        }
        {  // Speed curve: a track loop is silent at rest and full at 10 m/s
            files["objects/track/track.ssc"] = "newPatch\nload @ROOT/Sound/@RTD/track.wav\nloop\nbeginEffect\ncontrolDestination Volume\n"
                                               "controlSource Speed\nenvelope Ramp\nparam 0\nparam 10\nparam 0\nparam 1\nendEffect\n";
            SoundPatch track = ssc_parse("objects/track/track.ssc", read);
            assert(track.layers[0].effects[0].source == SoundEffect::Speed);
            Mixer m3(44100);
            float here[3] = {0, 0, 0};
            int h3 = m3.play(track, {tone}, here);
            std::vector<float> o(2 * 64, 0.0f);
            m3.mix(o.data(), 64, Listener{});
            assert(std::fabs(o[0]) < 1e-9f);
            m3.update(h3, here, 0, 10, 0);
            m3.mix(o.data(), 64, Listener{});
            assert(o[0] > 0.4f);
        }
        SoundPatch loop;
        loop.layers.push_back(sp.layers[1]);
        int h = mixer.play(loop, {tone}, right_of_listener);
        for (int i = 0; i < 10; ++i) mixer.mix(out.data(), 2048, Listener{});
        assert(mixer.voices() == 1);
        mixer.release(h);  // no TimeRelease effect: stops at once
        mixer.mix(out.data(), 2048, Listener{});
        assert(mixer.voices() == 0);
        loop.layers[0].finish = true;  // stop FinishSample: the pass under way plays to its end (0.1 s), then stops
        h = mixer.play(loop, {tone}, right_of_listener);
        for (int i = 0; i < 3; ++i) mixer.mix(out.data(), 2048, Listener{});
        mixer.release(h);
        mixer.mix(out.data(), 1000, Listener{});
        assert(mixer.voices() == 1);
        mixer.mix(out.data(), 4410, Listener{});
        assert(mixer.voices() == 0);
    }
    {  // Lexicon: records of a key and one text per language, UTF-16LE; Latin-1 kept, others become '?'
        std::vector<uint8_t> lex = {2, 0, 0, 0, 3, 0, 0, 0};
        auto put = [&](const std::u16string& s) {
            for (char16_t c : s) lex.push_back(static_cast<uint8_t>(c & 0xff)), lex.push_back(static_cast<uint8_t>(c >> 8));
            lex.push_back(0), lex.push_back(0);
        };
        for (auto s : {u"LANGUAGE", u"English", u"Spanish", u"RADIO_HEADING_ROGER", u"ROGER", u"RECIBIDO \u00c9\u4e86"}) put(s);
        auto en = lexicon_parse(lex), es = lexicon_parse(lex, 1);
        assert(en.size() == 1 && en["RADIO_HEADING_ROGER"] == "ROGER" && es["RADIO_HEADING_ROGER"] == "RECIBIDO \xc9?");
        lex.resize(lex.size() - 3);  // cut short: the last record is dropped, no overrun
        assert(lexicon_parse(lex).empty() && lexicon_parse({1, 2}).empty());
    }
    {  // HUD: .font, layout, gray TGA
        Font f = font_parse("Texture = font/BF1942.tga\r\nTextureWidth = 128\r\nTextureHeight = 128\r\nBetweenWidth = 1\r\nSpaceWidth = 1\r\n"
                            "Height = 11\r\n-------\r\n  0 0 0\r\n! 0 0 1\r\nA 31 12 36\r\nZ 120 125 127\r\n");
        assert(f.texture == "font/bf1942.tga" && f.height == 11 && f.glyphs['A'].x0 == 31 && f.glyphs['A'].y == 12);
        assert(f.glyphs['Z'].x1 < f.glyphs['Z'].x0);  // runs off the texture: dropped
        assert(text_width(f, "A!", 1) == 6 + 1 + 2 + 1);  // glyph widths (x1 inclusive) plus BetweenWidth each
        std::vector<HudQuad> q;
        const float white[4] = {1, 1, 1, 1};
        hud_text(f, "A A", 10, 20, 2, white, q);
        assert(q.size() == 2 && q[0].w == 12 && q[0].h == 22 && q[0].u0 == 31.0f / 128 && q[1].x == 10 + 2 * (7 + 4.0f));
        bool threw = false;
        try {
            font_parse("Height = 11\n");
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(threw);
        HudInfo info;
        assert(hud_layout(f, info, 640, 480).empty());  // no player, no HUD
        info.on = true, info.hp = 50, info.feed = {"A"};
        assert(hud_layout(f, info, 640, 480).size() == 8);  // crosshair 4, tickets panel, health panel and bar, feed "A" (no digit glyphs)
        std::vector<uint8_t> gray(18 + 4, 0);
        gray[2] = 3, gray[12] = 2, gray[14] = 2, gray[16] = 8, gray[17] = 0x20, gray[18] = 200;
        assert(tga_decode(gray).rgba[3] == 200 && tga_decode(gray).rgba[0] == 200);
    }
    {  // magazines and reload
        WeaponDef w;
        w.mag_size = 2, w.mags = 2, w.reload = 3;
        Ammo a = Ammo::full(w);
        assert(!a.unlimited && a.rounds == 2 && a.spare == 1);
        assert(a.take(0) && a.take(0) && !a.take(0));  // empty
        a.tick(0, w);                                  // starts the reload
        assert(a.reloading(1) && !a.take(1));
        a.tick(3, w);
        assert(a.rounds == 2 && a.spare == 0 && a.take(3) && a.take(3));
        a.tick(3, w);
        assert(!a.reloading(3) && !a.take(4));  // no spare left
        assert(Ammo::full(WeaponDef{}).take(0));  // magSize 0: unlimited
        assert(!a.supply(1.5f, w));  // numOfMag 2: one spare at most; true only when a magazine is added
        assert(a.spare == 0 && a.refill == 1.5f);
        assert(a.supply(0.5f, w));
        assert(a.spare == 1 && a.refill == 0);
        assert(!a.supply(5, w));
        assert(a.spare == 1);
    }
    std::puts("engine tests ok");
    return 0;
}
