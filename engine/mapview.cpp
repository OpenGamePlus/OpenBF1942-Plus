// Map viewer: terrain, colour tiles, water and static objects. Usage in docs/ai/SYSTEM_SUMMARY.md.
#include <SDL.h>
#include <epoxy/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "anim.h"
#include "con.h"
#include "damage.h"
#include "dds.h"
#include "math.h"
#include "physics.h"
#include "scene.h"
#include "sm.h"
#include "terrain.h"
#include "tm.h"
#include "vfs.h"

using namespace obf;

namespace {

const char* kVert = R"(#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nrm;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec2 corner;  // tree sprites: offset from the shared centre; static meshes: lightmap uv
uniform mat4 vp;
uniform mat4 model;
uniform vec3 cam_right;
uniform int sprite;  // 1 for tree meshes
out vec3 wpos;
out vec3 wnrm;
out vec2 tuv;
out vec2 luv;
void main() {
    wpos = (model * vec4(pos, 1.0)).xyz;
    wnrm = mat3(model) * nrm;
    luv = corner;
    if (sprite == 1 && corner != vec2(0.0)) {  // cylindrical billboard: faces the camera, stays upright
        float s = length(model[0].xyz);
        wpos += (cam_right * corner.x - vec3(0.0, 1.0, 0.0) * corner.y) * s;
        wnrm = vec3(0.0, 1.0, 0.0);
    }
    tuv = uv;
    gl_Position = vp * vec4(wpos, 1.0);
})";

const char* kFrag = R"(#version 330 core
in vec3 wpos;
in vec3 wnrm;
in vec2 tuv;
in vec2 luv;
uniform sampler2D lmap;
uniform int has_lm;
uniform float lm_scale;
uniform sampler2DArray tiles;
uniform sampler2D tex;
uniform int ntiles;
uniform float world;
uniform int mode;  // 0 terrain, 1 water, 2 mesh, 3 skinned mesh, 4 sky, 5 collision lines
uniform vec3 sun_dir;    // towards the sun
uniform vec3 ambient;
uniform vec3 diffuse;
uniform vec3 fog_color;
uniform vec2 fog_range;  // start, end (metres)
uniform vec3 cam_pos;
out vec4 color;
vec4 fogged(vec4 c) {
    float f = clamp((distance(wpos, cam_pos) - fog_range.x) / max(fog_range.y - fog_range.x, 1.0), 0.0, 1.0);
    return vec4(mix(c.rgb, fog_color, f), c.a);
}
void main() {
    if (mode == 4) { color = vec4(texture(tex, tuv).rgb, 1.0); return; }
    if (mode == 5) { color = vec4(1.0, 0.15, 0.1, 1.0); return; }
    if (mode == 1) { color = fogged(vec4(0.12, 0.30, 0.45, 0.75)); return; }
    vec4 base;
    if (mode >= 2) {
        base = texture(tex, tuv);
        if (base.a < 0.5) discard;  // fences, foliage
    } else {
        vec2 uv = clamp(wpos.xz / world, 0.0, 0.9999) * float(ntiles);
        ivec2 t = ivec2(floor(uv));
        // Tx<a>x<b>: a along x, b along z, texture u along x, v along z (checked visually: seamless only this way).
        int layer = t.x * ntiles + t.y;
        base = ntiles > 0 ? texture(tiles, vec3(fract(uv), float(layer))) : vec4(0.6);
    }
    // Skinned meshes have no posed normals (no bind pose to transform them): use the face normal.
    // Terrain colour tiles have lighting and shadows baked in: drawn unlit.
    if (mode == 0) { color = fogged(vec4(base.rgb, 1.0)); return; }
    vec3 n = mode == 3 ? normalize(cross(dFdx(wpos), dFdy(wpos))) : normalize(wnrm);
    if (mode == 3 && dot(n, sun_dir) < 0.0 && n.y < 0.0) n = -n;
    if (mode != 3 && !gl_FrontFacing) n = -n;  // two-sided: game winding is mirrored by the view
    // shortcut: ambient + diffuse from Init.con, doubled (assumed D3D modulate2x); no lightmaps.
    vec3 light = min(2.0 * (ambient + diffuse * max(dot(n, sun_dir), 0.0)), vec3(1.5));
    if (has_lm == 1) light = texture(lmap, luv).rgb * lm_scale;  // baked object lighting replaces the sun term
    color = fogged(vec4(base.rgb * light, 1.0));
})";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        throw std::runtime_error(std::string("shader: ") + log);
    }
    return s;
}

std::string text(const Vfs& vfs, const std::string& path) {
    auto b = vfs.read(path);
    return std::string(b.begin(), b.end());
}

void save_ppm(const std::string& path, int w, int h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) std::fwrite(&px[static_cast<size_t>(y) * w * 3], 1, static_cast<size_t>(w) * 3, f);
    std::fclose(f);
}

GLuint upload_texture(const Image& img) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    return t;
}

// Textures by VFS path without extension; .dds first, then .tga. Missing or broken -> white.
class Textures {
public:
    Textures(const Vfs& vfs, std::vector<std::string> alt_paths) : vfs_(vfs), alt_(std::move(alt_paths)) {
        white_ = upload_texture(Image{1, 1, {255, 255, 255, 255}});
    }
    GLuint get(const std::string& name) {
        std::string key = Vfs::key(name);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        GLuint t = white_;
        // shortcut: alternativePath is tried as "<alt>/<file name>" before the given path; unverified.
        std::vector<std::string> candidates;
        auto slash = key.rfind('/');
        for (auto& a : alt_) candidates.push_back(Vfs::key(a) + "/" + key.substr(slash == std::string::npos ? 0 : slash + 1));
        candidates.push_back(key);
        for (auto& c : candidates) {
            try {
                if (vfs_.has(c + ".dds")) t = upload_texture(dds_decode(vfs_.read(c + ".dds")));
                else if (vfs_.has(c + ".tga")) t = upload_texture(tga_decode(vfs_.read(c + ".tga")));
                else continue;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "texture %s: %s\n", c.c_str(), e.what());
                ++failed;
            }
            break;
        }
        if (t == white_) ++missing;
        return cache_[key] = t;
    }
    int missing = 0, failed = 0;

private:
    const Vfs& vfs_;
    std::vector<std::string> alt_;
    std::map<std::string, GLuint> cache_;
    GLuint white_;
};

struct GpuMesh {
    GLuint vao = 0, vbo = 0;
    GLuint col_vao = 0;      // collision wireframe (first block), drawn with GL_LINES
    GLsizei col_count = 0;
    struct Part {
        GLuint tex;
        GLsizei count;
        size_t offset;  // bytes into the index buffer
    };
    std::vector<Part> parts;
};

// One VBO (10 floats: pos, normal, uv, sprite corner) and one index buffer per mesh file.
void upload_buffers(GpuMesh& g, const std::vector<float>& verts, const std::vector<uint32_t>& idx) {
    GLuint ebo;
    glGenVertexArrays(1, &g.vao);
    glBindVertexArray(g.vao);
    glGenBuffers(1, &g.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * 4), verts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 40, nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 40, reinterpret_cast<void*>(12));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 40, reinterpret_cast<void*>(24));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 40, reinterpret_cast<void*>(32));
    for (GLuint a = 0; a < 4; ++a) glEnableVertexAttribArray(a);
}

// LOD 0 of a .sm; lightmap uv dropped. Textures come from the .rs next to it.
std::unique_ptr<GpuMesh> upload_sm(const SmMesh& m, const std::string& path, const Vfs& vfs, Textures& tex,
                                   std::vector<float>* keep_vertices = nullptr) {
    auto g = std::make_unique<GpuMesh>();
    if (m.lods.empty()) return g;
    std::string rs_path = path.substr(0, path.size() - 3) + ".rs";
    auto shaders = vfs.has(rs_path) ? rs_parse(text(vfs, rs_path)) : decltype(rs_parse("")){};
    std::vector<float> verts;
    std::vector<uint32_t> idx;
    for (auto& mat : m.lods[0]) {
        uint32_t base = static_cast<uint32_t>(verts.size() / 10);
        size_t fl = mat.stride / 4;
        for (size_t v = 0; v < mat.vertex_count(); ++v) {
            verts.insert(verts.end(), mat.vertices.data() + v * fl, mat.vertices.data() + v * fl + 8);
            if (fl >= 10) verts.insert(verts.end(), mat.vertices.data() + v * fl + 8, mat.vertices.data() + v * fl + 10);
            else verts.insert(verts.end(), {0.f, 0.f});
        }
        size_t first = idx.size();
        for (uint32_t i : sm_triangles(mat)) idx.push_back(base + i);
        auto s = shaders.find(lower(mat.name));
        GLuint t = s != shaders.end() && s->second.count("texture") ? tex.get(s->second.at("texture")) : tex.get("");
        g->parts.push_back({t, static_cast<GLsizei>(idx.size() - first), first * 4});
    }
    upload_buffers(*g, verts, idx);
    if (keep_vertices) *keep_vertices = std::move(verts);
    if (!m.collision.empty()) {  // edges of the first collision block, same 10-float layout
        const CollisionMesh& c = m.collision[0];
        std::vector<float> cv;
        for (size_t i = 0; i < c.vertices.size(); i += 3) cv.insert(cv.end(), {c.vertices[i], c.vertices[i + 1], c.vertices[i + 2], 0, 1, 0, 0, 0, 0, 0});
        std::vector<uint32_t> ci;
        for (size_t f = 0; f < c.faces.size(); f += 3)
            for (int e = 0; e < 3; ++e) ci.insert(ci.end(), {c.faces[f + static_cast<size_t>(e)], c.faces[f + static_cast<size_t>((e + 1) % 3)]});
        GpuMesh tmp;
        upload_buffers(tmp, cv, ci);
        g->col_vao = tmp.vao;
        g->col_count = static_cast<GLsizei>(ci.size());
    }
    return g;
}

// A skinned mesh instance: its own vertex buffer, re-skinned every frame on the CPU.
struct Animated {
    size_t item;
    std::unique_ptr<GpuMesh> gpu;
    std::vector<float> base;  // 10 floats per vertex, as uploaded
    std::shared_ptr<Skeleton> skeleton;
    std::shared_ptr<Skin> skin;
    std::vector<int> map, to_ske;
    std::vector<std::shared_ptr<Animation>> anims;
    float speed = 1, phase = 0;
    bool biped = false;
};

// Tree mesh: all three groups share one buffer; group 3 sprites carry corner offsets.
std::unique_ptr<GpuMesh> upload_tm(const TmMesh& m, Textures& tex) {
    auto g = std::make_unique<GpuMesh>();
    std::vector<uint32_t> idx(m.indices.begin(), m.indices.end());
    for (int gi = 0; gi < 3; ++gi)
        for (auto& p : m.groups[gi]) {
            size_t count = static_cast<size_t>(p.count) * kTmIndicesPer[gi];
            if (p.first + count > idx.size()) throw std::runtime_error("TM: part outside index buffer");
            g->parts.push_back({tex.get(p.texture), static_cast<GLsizei>(count), static_cast<size_t>(p.first) * 4});
        }
    upload_buffers(*g, m.vertices, idx);
    return g;
}

int run(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: obf_mapview <BF1942_DIR> <mod> <level> [--shot out.ppm] [--cam x y z yaw pitch] [--mode conquest|ctf|tdm|coop] [--soldier <template>] [--drive <vehicle>] [--play <soldier>] [--fire-ahead] [--collision] [--no-objects]\n"
                     "  right mouse: look, WASD/QE: move (or drive), shift: fast, C: collision meshes, esc: quit\n"
                     "  aircraft: W/S throttle (held), arrows pitch (up = nose down) and roll, A/D rudder; right mouse aims turrets\n"
                     "  --shot with --drive: settles 2 s, drives 4 s (last 2 s steering right), prints the track;\n"
                     "  aircraft: 20 s take-off run, pulling up from 12 s to 16 s; the vehicle fires from 2 s (--fire-ahead: gun\n"
                     "  straight ahead, no steering)\n"
                     "  --play: walk a soldier (WASD, shift walks, space jumps, right mouse looks, left mouse fires), E gets into / out of the nearest vehicle\n"
                     "  (parked vehicles are drivable with --play or --drive); with --shot: runs 2 s, gets into the nearest vehicle,\n"
                     "  drives 2 s, gets out, then reports how far the parked vehicles moved while settling\n");
        return 2;
    }
    std::string shot;
    float cx = -1, cy = 0, cz = 0, yaw = 0.6f, pitch = -0.5f;
    bool objects = true, show_collision = false;
    std::string mode = "conquest", soldier, drive, play;
    bool look_demo = false;  // --shot vehicle demo: keep the gun straight ahead (--fire-ahead)
    for (int i = 4; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--no-objects")) objects = false;
        else if (!std::strcmp(argv[i], "--collision")) show_collision = true;
        else if (!std::strcmp(argv[i], "--mode") && i + 1 < argc) mode = argv[++i];
        else if (!std::strcmp(argv[i], "--soldier") && i + 1 < argc) soldier = argv[++i];
        else if (!std::strcmp(argv[i], "--drive") && i + 1 < argc) drive = argv[++i];
        else if (!std::strcmp(argv[i], "--play") && i + 1 < argc) play = argv[++i];
        else if (!std::strcmp(argv[i], "--fire-ahead")) look_demo = true;
        else if (!std::strcmp(argv[i], "--cam") && i + 5 < argc) {
            cx = std::stof(argv[i + 1]), cy = std::stof(argv[i + 2]), cz = std::stof(argv[i + 3]);
            yaw = std::stof(argv[i + 4]), pitch = std::stof(argv[i + 5]);
            i += 5;
        }
    }

    Vfs vfs;
    vfs.mount_mod(argv[1], argv[2]);
    std::string lvl = "bf1942/levels/" + lower(argv[3]) + "/";
    TerrainInfo info = parse_terrain_con(text(vfs, lvl + "init/terrain.con"));
    Heightmap hm = load_heightmap(vfs.read(info.heightmap + ".raw"), info.y_scale);
    std::printf("%s: %zu files, heightmap %dx%d, world %.0f m\n", argv[3], vfs.size(), hm.n, hm.n, info.world_size);

    Con con(vfs);
    std::vector<DrawItem> scene;
    std::vector<SpawnPoint> spawns;
    std::map<std::string, int> skipped;
    if (objects) {
        con.load_game();
        con.run("bf1942/game/materialmanagersettings.con");  // shortcut: assumed engine-loaded; damage tables
        // shortcut: assumed engine-loaded like objects/; defines AnimationStateMachine states.
        con.run("animations/animationstates.con");
        con.load_level(argv[3], mode);
        if (!soldier.empty()) {  // demo: place one soldier 6 m in front of the start camera, on the ground
            float sx = cx < 0 ? info.world_size * 0.15f : cx, sz = cx < 0 ? info.world_size * 0.15f : cz;
            sx += std::sin(yaw) * 6, sz += std::cos(yaw) * 6;
            float cell = info.world_size / static_cast<float>(hm.n);
            int r = std::clamp(static_cast<int>(sz / cell), 0, hm.n - 1), c = std::clamp(static_cast<int>(sx / cell), 0, hm.n - 1);
            char pos[96];
            std::snprintf(pos, sizeof pos, "%g/%g/%g", sx, hm.height(r, c), sz);
            con.instances.push_back({"", soldier, {{"absoluteposition", {pos}}, {"rotation", {std::to_string(yaw * 57.29578f + 180) + "/0/0"}}}});
        }
        // Third-person view only: the engine swaps these parts by camera and state at runtime.
        scene = build_scene(con, vfs, skipped, [](const std::string& t) {
            return t == "parachute" || t.rfind("1p", 0) == 0 || t.find("1pbody") != std::string::npos;
        }, &spawns);
        std::printf("objects: %zu instances -> %zu meshes drawn; skipped:", con.instances.size(), scene.size());
        for (auto& [why, n] : skipped) std::printf(" %s %d,", why.c_str(), n);
        std::printf("\n");
    }

    // Physics: terrain + static meshes.
    Physics phys(hm, info.world_size, info.water_level);
    if (objects) std::printf("physics: %zu static bodies\n", phys.add_scene(scene, vfs));

    // Vehicles: the --drive one in front of the camera and, when driving or playing, every parked vehicle that can move
    // (wheels, wings or floats) from the level's spawners. Their parts are appended to the scene and posed each frame.
    struct Veh {
        int id = -1;
        std::string name;
        size_t first = 0, parts = 0;
        bool plane = false;
        float len = 0, exit[3] = {-2, 0, 0}, throttle = 0;
        float hp = 100, next_fire = 0;
        int material = 0;
        bool wrecked = false;
        size_t wreck_first = 0, wreck_parts = 0;  // the wreck detail level, drawn once destroyed
        WeaponDef weapon;
        Mat4 muzzle;  // posed FireArms part, for firing
        bool has_muzzle = false;
        Mat4 start;  // where it was placed
    };
    std::vector<Veh> vehicles;
    int cur = -1;  // the vehicle under control
    std::map<std::string, std::unique_ptr<VehicleDef>> defs;  // by template; null if it cannot move
    auto def_for = [&](const std::string& name) -> const VehicleDef* {
        auto it = defs.find(lower(name));
        if (it == defs.end()) {
            std::unique_ptr<VehicleDef> d;
            try {
                VehicleDef v = vehicle_def(con, vfs, name);
                if (!v.wheels.empty() || !v.wings.empty() || !v.floats.empty()) d = std::make_unique<VehicleDef>(std::move(v));
            } catch (const std::exception&) {
            }
            it = defs.emplace(lower(name), std::move(d)).first;
        }
        return it->second.get();
    };
    // Terrain height under (x, z), bilinear between samples.
    auto ground = [&](float x, float z) {
        float cell = info.world_size / static_cast<float>(hm.n), fx = std::clamp(x / cell, 0.0f, static_cast<float>(hm.n - 1)),
              fz = std::clamp(z / cell, 0.0f, static_cast<float>(hm.n - 1));
        int c = std::min(static_cast<int>(fx), hm.n - 2), r = std::min(static_cast<int>(fz), hm.n - 2);
        float u = fx - static_cast<float>(c), w = fz - static_cast<float>(r);
        return (hm.height(r, c) * (1 - u) + hm.height(r, c + 1) * u) * (1 - w) + (hm.height(r + 1, c) * (1 - u) + hm.height(r + 1, c + 1) * u) * w;
    };
    auto add_vehicle = [&](const std::string& name, Mat4 world) {
        const VehicleDef* def = def_for(name);
        if (!def) return;
        // Some retail spawns sit a little under our terrain (M3A1, T95, Sexton fell through it): lift the vehicle until no
        // hull point or wheel bottom is below the ground under it. Vehicles on bridges and decks are above it already.
        float lift = 0;
        auto check = [&](float x, float y, float z) {
            float wx = world.m[0] * x + world.m[4] * y + world.m[8] * z + world.m[12], wy = world.m[1] * x + world.m[5] * y + world.m[9] * z + world.m[13],
                  wz = world.m[2] * x + world.m[6] * y + world.m[10] * z + world.m[14];
            lift = std::max(lift, ground(wx, wz) - wy);
        };
        for (size_t i = 0; i + 2 < def->hull.size(); i += 3) check(def->hull[i], def->hull[i + 1], def->hull[i + 2]);
        for (auto& w : def->wheels)
            if (w.strength > 0) check(w.pos[0], w.pos[1] - w.radius, w.pos[2]);
        if (lift > 0) world = translate(0, lift + 0.05f, 0) * world;
        Veh v;
        v.id = phys.add_vehicle(*def, world), v.name = name, v.plane = def->plane, v.first = scene.size(), v.start = world;
        std::copy(def->exit, def->exit + 3, v.exit);
        for (size_t i = 2; i < def->hull.size(); i += 3) v.len = std::max(v.len, std::fabs(def->hull[i]));
        for (DrawItem& item : build_object(con, vfs, name, Mat4::identity(), skipped)) ++v.parts, scene.push_back(std::move(item));
        v.wreck_first = scene.size();
        for (DrawItem& item : build_object(con, vfs, name, Mat4::identity(), skipped, nullptr, nullptr, true))
            ++v.wreck_parts, scene.push_back(std::move(item));
        v.hp = def->hitpoints, v.material = def->material, v.weapon = def->weapon;
        vehicles.push_back(v);
    };
    if (objects && (!drive.empty() || !play.empty())) {
        std::vector<char> movable(spawns.size());
        for (size_t i = 0; i < spawns.size(); ++i) movable[i] = def_for(spawns[i].name) != nullptr;
        scene.erase(std::remove_if(scene.begin(), scene.end(), [&](const DrawItem& d) { return d.spawn >= 0 && movable[static_cast<size_t>(d.spawn)]; }),
                    scene.end());
        for (size_t i = 0; i < spawns.size(); ++i)
            if (movable[i]) add_vehicle(spawns[i].name, spawns[i].world);
        std::printf("vehicles: %zu of %zu spawners\n", vehicles.size(), spawns.size());
    }
    if (objects && !drive.empty()) {
        float sx = cx < 0 ? info.world_size * 0.15f : cx, sz = cx < 0 ? info.world_size * 0.15f : cz;
        sx += std::sin(yaw) * 10, sz += std::cos(yaw) * 10;
        float cell = info.world_size / static_cast<float>(hm.n);
        int r = std::clamp(static_cast<int>(sz / cell), 0, hm.n - 1), c = std::clamp(static_cast<int>(sx / cell), 0, hm.n - 1);
        size_t before = vehicles.size();
        add_vehicle(drive, translate(sx, std::max(hm.height(r, c), info.water_level) + 1, sz) * rotation_ypr(yaw * 57.29578f, 0, 0));
        if (vehicles.size() == before) throw std::runtime_error("--drive " + drive + ": not a vehicle that can move");
        if (play.empty()) cur = static_cast<int>(before);
        const VehicleDef& def = *def_for(drive);
        std::printf("drive %s: mass %.0f, %zu wheels, %zu wings, %zu floats, %zu hull points, %zu parts\n", drive.c_str(), def.mass,
                    def.wheels.size(), def.wings.size(), def.floats.size(), def.hull.size() / 3, vehicles.back().parts);
    }

    // Playable soldier (--play): a character capsule at the camera spot; its parts are appended like the vehicle's.
    auto hide_1p = [](const std::string& t) { return t == "parachute" || t.rfind("1p", 0) == 0 || t.find("1pbody") != std::string::npos; };
    int player = -1;
    size_t sol_first = scene.size(), sol_parts = 0;
    if (objects && !play.empty()) {
        float sx = cx < 0 ? info.world_size * 0.15f : cx, sz = cx < 0 ? info.world_size * 0.15f : cz;
        float cell = info.world_size / static_cast<float>(hm.n);
        int r = std::clamp(static_cast<int>(sz / cell), 0, hm.n - 1), c = std::clamp(static_cast<int>(sx / cell), 0, hm.n - 1);
        player = phys.add_character(sx, std::max(hm.height(r, c), info.water_level - 1.4f) + 0.5f, sz);
        for (DrawItem& item : build_object(con, vfs, play, Mat4::identity(), skipped, nullptr, hide_1p)) {
            ++sol_parts;
            scene.push_back(std::move(item));
        }
        std::printf("play %s: %zu parts\n", play.c_str(), sol_parts);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    int W = 1280, H = 720;
    SDL_Window* win = SDL_CreateWindow("OpenBF1942-Plus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) throw std::runtime_error(SDL_GetError());
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetSwapInterval(1);

    // Terrain grid: one vertex per sample, normals from central differences. Layout as meshes (pos, normal, uv).
    const int n = hm.n;
    const float cell = info.world_size / static_cast<float>(n);
    std::vector<float> verts;
    verts.reserve(static_cast<size_t>(n) * n * 10);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            float dx = hm.height(r, std::min(c + 1, n - 1)) - hm.height(r, std::max(c - 1, 0));
            float dz = hm.height(std::min(r + 1, n - 1), c) - hm.height(std::max(r - 1, 0), c);
            float nx = -dx, ny = 2 * cell, nz = -dz, l = std::sqrt(nx * nx + ny * ny + nz * nz);
            verts.insert(verts.end(), {static_cast<float>(c) * cell, hm.height(r, c), static_cast<float>(r) * cell, nx / l, ny / l, nz / l, 0, 0, 0, 0});
        }
    float wl = info.water_level, ws = info.world_size;
    size_t water_first = verts.size() / 10;
    for (auto [x, z] : {std::pair{0.f, 0.f}, {ws, 0.f}, {ws, ws}, {0.f, 0.f}, {ws, ws}, {0.f, ws}})
        verts.insert(verts.end(), {x, wl, z, 0, 1, 0, 0, 0, 0, 0});
    std::vector<uint32_t> idx;
    idx.reserve(static_cast<size_t>(n - 1) * (n - 1) * 6);
    for (int r = 0; r + 1 < n; ++r)
        for (int c = 0; c + 1 < n; ++c) {
            uint32_t a = static_cast<uint32_t>(r * n + c), b = a + 1, d = a + static_cast<uint32_t>(n), e = d + 1;
            idx.insert(idx.end(), {a, d, b, b, d, e});
        }

    GpuMesh terrain;
    upload_buffers(terrain, verts, idx);
    GLuint vao = terrain.vao;

    // Colour tiles Tx<a>x<b>.dds, ntiles x ntiles of equal size, as one texture array.
    int ntiles = 0;
    auto tile = [&](int a, int b) {
        char s[32];
        std::snprintf(s, sizeof s, "%02dx%02d.dds", a, b);
        return info.tex_base + s;
    };
    // Some maps leave tiles out (open sea): grid size comes from the highest index present.
    Image first;
    for (int a = 0; a < 64; ++a)
        for (int b = 0; b < 64; ++b)
            if (vfs.has(tile(a, b))) {
                ntiles = std::max({ntiles, a + 1, b + 1});
                if (first.w == 0) first = dds_decode(vfs.read(tile(a, b)));
            }
    GLuint tiles;
    glGenTextures(1, &tiles);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tiles);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (ntiles > 0)
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, first.w, first.h, ntiles * ntiles, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    // shortcut: missing tiles are drawn neutral grey; what the original engine shows there is unverified.
    std::vector<uint8_t> grey(first.rgba.size(), 110);
    for (int a = 0; a < ntiles; ++a)
        for (int b = 0; b < ntiles; ++b) {
            Image img = vfs.has(tile(a, b)) ? dds_decode(vfs.read(tile(a, b))) : Image{first.w, first.h, grey};
            if (img.w != first.w || img.h != first.h) throw std::runtime_error("colour tiles differ in size: " + tile(a, b));
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, a * ntiles + b, img.w, img.h, 1, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
        }
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);
    std::printf("colour tiles: %dx%d\n", ntiles, ntiles);

    // Meshes: one GPU copy per file, shared by all instances.
    std::vector<std::string> alt;
    for (auto& [method, args] : con.settings["texturemanager"])
        if (method == "alternativepath" && !args.empty()) alt.push_back(args[0]);
    Textures textures(vfs, alt);
    // Level look: sky box, sun, fog and light colours (Init.con, Init/SkyAndSun.con).
    auto setting = [&](const std::string& sub, const std::string& key) -> std::string {
        std::string v;
        for (auto& [k, a] : con.settings[sub])
            if (k == key && !a.empty()) v = a[0];
        return v;
    };
    auto vec3_or = [&](const std::string& s, float x, float y, float z) {
        float v[3] = {x, y, z};
        if (!s.empty()) parse_vec3(s, v);
        return std::vector<float>{v[0], v[1], v[2]};
    };
    auto sun = vec3_or(setting("sky", "sunlightdirectionvec"), 0.4f, 0.8f, 0.3f);
    auto amb = vec3_or(setting("renderer", "globalambientcolor"), 0.15f, 0.15f, 0.15f);
    auto amb2 = vec3_or(setting("renderer", "ambientcolor"), 0.1f, 0.1f, 0.1f);
    auto dif = vec3_or(setting("renderer", "diffusecolor"), 0.4f, 0.4f, 0.4f);
    auto fogc = vec3_or(setting("renderer", "fogcolorvec"), 0.62f, 0.72f, 0.82f);
    std::string vd = setting("game", "setviewdistance");
    float view_dist = vd.empty() ? 1500.0f : static_cast<float>(std::atof(vd.c_str()));
    std::string sky_rot = setting("sky", "setrotangle");
    std::string sky_file;
    if (con.templates.count("geometrytemplate") && con.templates["geometrytemplate"].count("skybox"))
        for (auto& [k, a] : con.templates["geometrytemplate"]["skybox"].props)
            if (k == "file" && !a.empty()) sky_file = "standardmesh/" + Vfs::key(a[0]) + ".sm";

    // Animation states: createState <name>, then addAnimation <baf> <speed> <flags>; the first animation is used.
    struct State {
        std::string baf;
        float speed = 1;
    };
    std::map<std::string, State> states;
    {
        std::string cur;
        for (auto& [method, args] : con.settings["animationstatemachine"]) {
            if (method == "createstate" && !args.empty()) cur = lower(args[0]);
            if (method == "addanimation" && !args.empty() && !cur.empty() && !states.count(cur))
                {
                float sp = args.size() > 1 ? static_cast<float>(std::atof(args[1].c_str())) : 1.0f;
                states[cur] = {Vfs::key(args[0]), std::isfinite(sp) ? sp : 1.0f};
            }
        }
    }
    std::vector<Animated> animated;
    std::vector<const GpuMesh*> item_mesh(scene.size(), nullptr);
    std::map<std::string, std::shared_ptr<Skeleton>> skeletons;
    std::map<std::string, std::shared_ptr<Skin>> skins;
    std::map<std::string, std::shared_ptr<Animation>> anims;
    int anim_errors = 0;
    for (size_t i = 0; i < scene.size(); ++i) {
        const DrawItem& item = scene[i];
        if (item.skin.empty()) continue;
        try {
            Animated a;
            a.item = i;
            a.gpu = upload_sm(sm_parse(vfs.read(item.mesh)), item.mesh, vfs, textures, &a.base);
            auto& sk = skeletons[item.skeleton];
            if (!sk) sk = std::make_shared<Skeleton>(ske_parse(vfs.read(item.skeleton)));
            auto& sn = skins[item.skin];
            if (!sn) sn = std::make_shared<Skin>(skn_parse(vfs.read(item.skin)));
            a.skeleton = sk, a.skin = sn;
            a.map = skin_map(*sn, a.base, 10);
            a.to_ske = bone_index(*sn, *sk);
            for (auto& name : item.states) {
                if (name.empty()) continue;
                // shortcut: weapon-specific states ("Ub_StandAim" + weapon) resolve to the first state with that prefix.
                auto st = states.lower_bound(name);
                if (st == states.end() || st->first.compare(0, name.size(), name) != 0 || !vfs.has(st->second.baf)) continue;
                auto& an = anims[st->second.baf];
                if (!an) an = std::make_shared<Animation>(baf_parse(vfs.read(st->second.baf)));
                a.anims.push_back(an);
                a.speed = st->second.speed;
            }
            // Biped rigs (soldiers): root is 3ds Max space; drop it and turn to face +z (docs/formats/anim.md).
            a.biped = !sk->empty() && (*sk)[0].name == "Bip01";
            if (a.biped) scene[i].world = scene[i].world * rotation_ypr(180, 0, 0);
            a.phase = static_cast<float>(i % 17);  // shortcut: stands in for setUserRandomStartTime
            item_mesh[i] = a.gpu.get();
            animated.push_back(std::move(a));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "animated %s: %s\n", item.mesh.c_str(), e.what());
            ++anim_errors;
        }
    }
    // The player's skinned parts switch their lower-body state with the movement.
    std::vector<size_t> player_anims;
    for (size_t ai = 0; ai < animated.size(); ++ai)
        if (animated[ai].item >= sol_first && animated[ai].item < sol_first + sol_parts) player_anims.push_back(ai);
    auto state_anim = [&](const std::string& name) -> std::shared_ptr<Animation> {
        auto st = states.find(name);
        if (st == states.end() || !vfs.has(st->second.baf)) return nullptr;
        auto& an = anims[st->second.baf];
        if (!an) an = std::make_shared<Animation>(baf_parse(vfs.read(st->second.baf)));
        return an;
    };
    std::string lower_state = "lb_stand";
    // Combat: damage tables, the soldier's weapon (shortcut: an M1 Garand for everyone, no kits yet), tracers.
    DamageTable damage = damage_table(con.settings["materialmanager"]);
    WeaponDef hand = weapon_def(con, "M1Garand");
    if (player >= 0)
        std::printf("weapon %s: %.0f m/s every %.2f s, material %d; damage table: %zu materials, %zu modifiers\n", hand.name.c_str(),
                    hand.velocity, hand.interval, hand.material, damage.materials.size(), damage.mods.size());
    float hand_next = 0;
    GLuint tracer_vao = 0, tracer_vbo = 0;
    glGenVertexArrays(1, &tracer_vao);
    glBindVertexArray(tracer_vao);
    glGenBuffers(1, &tracer_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, tracer_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 40, nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 40, reinterpret_cast<void*>(12));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 40, reinterpret_cast<void*>(24));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 40, reinterpret_cast<void*>(32));
    for (GLuint a = 0; a < 4; ++a) glEnableVertexAttribArray(a);
    glBindVertexArray(0);
    std::map<std::string, std::unique_ptr<GpuMesh>> meshes;
    int mesh_errors = 0;
    for (auto& item : scene) {
        if (!item.skin.empty() || meshes.count(item.mesh)) continue;
        try {
            bool tree = item.mesh.size() > 3 && item.mesh.compare(item.mesh.size() - 3, 3, ".tm") == 0;
            meshes[item.mesh] = tree ? upload_tm(tm_parse(vfs.read(item.mesh)), textures)
                                     : upload_sm(sm_parse(vfs.read(item.mesh)), item.mesh, vfs, textures);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "mesh %s: %s\n", item.mesh.c_str(), e.what());
            meshes[item.mesh] = std::make_unique<GpuMesh>();
            ++mesh_errors;
        }
    }
    for (size_t i = 0; i < scene.size(); ++i)
        if (!item_mesh[i] && meshes.count(scene[i].mesh)) item_mesh[i] = meshes[scene[i].mesh].get();
    std::unique_ptr<GpuMesh> sky;
    if (!sky_file.empty() && vfs.has(sky_file)) sky = upload_sm(sm_parse(vfs.read(sky_file)), sky_file, vfs, textures);
    if (objects)
        std::printf("meshes: %zu unique, %d errors; animated %zu (%zu with animation), %d errors; textures missing %d, failed %d\n",
                    meshes.size(), mesh_errors, animated.size(),
                    static_cast<size_t>(std::count_if(animated.begin(), animated.end(), [](const Animated& a) { return !a.anims.empty(); })),
                    anim_errors, textures.missing, textures.failed);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, kVert));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, kFrag));
    glLinkProgram(prog);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "ntiles"), ntiles);
    glUniform1f(glGetUniformLocation(prog, "world"), info.world_size);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);
    glUniform1i(glGetUniformLocation(prog, "tiles"), 1);
    glUniform1i(glGetUniformLocation(prog, "lmap"), 2);
    // shortcut: lightmaps average 0.13-0.21 (Wake, El Alamein, Berlin, Bocage), so x2 (D3D MODULATE2X,
    // the usual lightmap blend of the era) is assumed; x1 leaves buildings at ~20% brightness. Unverified.
    glUniform1f(glGetUniformLocation(prog, "lm_scale"), 2.0f);
    GLint u_sprite = glGetUniformLocation(prog, "sprite"), u_has_lm = glGetUniformLocation(prog, "has_lm");
    GLint u_vp = glGetUniformLocation(prog, "vp"), u_model = glGetUniformLocation(prog, "model"),
          u_mode = glGetUniformLocation(prog, "mode"), u_right = glGetUniformLocation(prog, "cam_right"),
          u_cam = glGetUniformLocation(prog, "cam_pos");
    {
        float l = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
        glUniform3f(glGetUniformLocation(prog, "sun_dir"), sun[0] / l, sun[1] / l, sun[2] / l);
        glUniform3f(glGetUniformLocation(prog, "ambient"), amb[0] + amb2[0], amb[1] + amb2[1], amb[2] + amb2[2]);
        glUniform3f(glGetUniformLocation(prog, "diffuse"), dif[0], dif[1], dif[2]);
        glUniform3f(glGetUniformLocation(prog, "fog_color"), fogc[0], fogc[1], fogc[2]);
        // shortcut: fog starts at half the view distance; the original start value is unknown.
        glUniform2f(glGetUniformLocation(prog, "fog_range"), view_dist * 0.5f, view_dist);
    }

    if (cx < 0) cx = ws * 0.15f, cy = 300, cz = ws * 0.15f;  // default: corner, looking across the map
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    bool running = true, look = false;
    Uint64 last = SDL_GetPerformanceCounter();
    const Mat4 identity = Mat4::identity();
    while (running) {
        SDL_Event ev;
        float mouse_x = 0, mouse_y = 0;  // this frame's look motion (turrets when driving)
        bool toggle_vehicle = false;     // E: get in / out
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_c) show_collision = !show_collision;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_e && !ev.key.repeat) toggle_vehicle = true;
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_RIGHT) look = true;
            if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_RIGHT) look = false;
            if (ev.type == SDL_MOUSEMOTION && look) {
                mouse_x += static_cast<float>(ev.motion.xrel), mouse_y += static_cast<float>(ev.motion.yrel);
                yaw += static_cast<float>(ev.motion.xrel) * 0.004f;
                pitch = std::clamp(pitch - static_cast<float>(ev.motion.yrel) * 0.004f, -1.5f, 1.5f);
            }
        }
        SDL_SetRelativeMouseMode(look ? SDL_TRUE : SDL_FALSE);
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - last) / static_cast<float>(SDL_GetPerformanceFrequency());
        last = now;
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        auto key = [&](SDL_Scancode a, SDL_Scancode b) { return static_cast<float>(k[a] - k[b]); };
        // E: get into the nearest vehicle within reach (shortcut: 5 m + half its length, not the EntryPoint radius);
        // again: out at its setSoldierExitLocation.
        auto get_in_out = [&] {
            if (player < 0) return;
            if (cur >= 0) {
                const Veh& v = vehicles[static_cast<size_t>(cur)];
                Mat4 t = phys.transform(v.id);
                float e[3];
                for (int i = 0; i < 3; ++i) e[i] = t.m[i] * v.exit[0] + t.m[4 + i] * v.exit[1] + t.m[8 + i] * v.exit[2] + t.m[12 + i];
                phys.drive(v.id, 0, 0);
                phys.look(v.id, 0, 0);
                phys.place_character(player, e[0], e[1] + 0.5f, e[2]);
                phys.set_character_active(player, true);
                std::printf("out of %s at %.1f %.1f %.1f\n", v.name.c_str(), e[0], e[1], e[2]);
                cur = -1;
                return;
            }
            float p[3], best = 0;
            phys.character_position(player, p);
            for (size_t i = 0; i < vehicles.size(); ++i) {
                Mat4 t = phys.transform(vehicles[i].id);
                float dx = t.m[12] - p[0], dz = t.m[14] - p[2], margin = 5 + vehicles[i].len * 0.5f - std::sqrt(dx * dx + dz * dz);
                if (margin >= 0 && (cur < 0 || margin > best)) cur = static_cast<int>(i), best = margin;
            }
            if (cur < 0) return;
            phys.set_character_active(player, false);
            std::printf("in %s\n", vehicles[static_cast<size_t>(cur)].name.c_str());
        };
        // On foot: run (5 m/s) or walk with shift (2.5 m/s) relative to the camera; shortcut: speeds guessed.
        float move_f = 0, move_r = 0;
        auto walk = [&](float forward, float right, bool jump, bool slow) {
            move_f = forward, move_r = right;
            if (player < 0) return;
            float l = std::sqrt(forward * forward + right * right), sp = (slow ? 2.5f : 5.0f) / std::max(l, 1.0f);
            float fx = std::sin(yaw), fz = std::cos(yaw);
            phys.walk(player, (fx * forward + fz * right) * sp, (fz * forward - fx * right) * sp, jump);
        };
        // Fire the controlled vehicle's weapon from its posed barrel, or the soldier's along the camera. t: game time.
        auto fire = [&](float t) {
            if (cur >= 0) {
                Veh& v = vehicles[static_cast<size_t>(cur)];
                if (v.wrecked || v.weapon.name.empty() || t < v.next_fire) return;
                v.next_fire = t + v.weapon.interval;
                Mat4 m = v.has_muzzle ? v.muzzle : phys.transform(v.id);
                float d[3] = {m.m[8], m.m[9], m.m[10]}, l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (!(l > 1e-6f)) return;
                float pos[3], vel[3];
                for (int i = 0; i < 3; ++i) d[i] /= l, pos[i] = m.m[12 + i] + d[i] * (v.has_muzzle ? 1.0f : v.len), vel[i] = d[i] * v.weapon.velocity;
                phys.fire(pos, vel, v.weapon.material, v.weapon.material2, v.weapon.ttl, v.id);
            } else if (player >= 0 && !hand.name.empty() && t >= hand_next) {
                hand_next = t + hand.interval;
                float p[3], d[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)}, pos[3], vel[3];
                phys.character_position(player, p);
                for (int i = 0; i < 3; ++i) pos[i] = p[i] + (i == 1 ? 1.5f : 0.0f) + d[i] * 0.6f, vel[i] = d[i] * hand.velocity;
                phys.fire(pos, vel, hand.material, hand.material2, hand.ttl);
            }
        };
        // Hits: damage = hit material vs the armour face met (else the vehicle's material), plus the explosion material.
        auto apply_hits = [&] {
            for (const Physics::Hit& h : phys.take_hits()) {
                if (h.vehicle < 0) continue;
                for (size_t i = 0; i < vehicles.size(); ++i) {
                    Veh& v = vehicles[i];
                    if (v.id != h.vehicle || v.wrecked) continue;
                    int target = h.armor >= 0 ? h.armor : v.material;
                    float d = damage.damage(h.material, target) + (h.material2 >= 0 ? damage.damage(h.material2, target) : 0.0f);
                    v.hp -= d;
                    std::printf("hit %s (armour %d): %.1f damage, %.1f hp left\n", v.name.c_str(), target, d, std::max(0.0f, v.hp));
                    if (v.hp > 0) continue;
                    v.wrecked = true;
                    std::printf("%s destroyed\n", v.name.c_str());
                    if (static_cast<int>(i) == cur && player >= 0) get_in_out();
                    phys.drive(v.id, 0, 0);
                }
            }
        };
        bool firing = SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT);
        float now_s = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        if (shot.empty()) {
            if (toggle_vehicle) get_in_out();
            if (firing) fire(now_s);
            if (cur >= 0 && !vehicles[static_cast<size_t>(cur)].wrecked) {
                Veh& v = vehicles[static_cast<size_t>(cur)];
                float th = key(SDL_SCANCODE_W, SDL_SCANCODE_S);
                if (v.plane) v.throttle = std::clamp(v.throttle + th * 0.5f * dt, 0.0f, 1.0f), th = v.throttle;
                phys.drive(v.id, th, key(SDL_SCANCODE_D, SDL_SCANCODE_A), key(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN),
                           key(SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT));
                phys.look(v.id, mouse_x / 20.0f, mouse_y / 20.0f);  // mouse right / down = +1 at ~20 px per frame
                walk(0, 0, false, false);
            } else {
                walk(key(SDL_SCANCODE_W, SDL_SCANCODE_S), key(SDL_SCANCODE_D, SDL_SCANCODE_A), k[SDL_SCANCODE_SPACE], k[SDL_SCANCODE_LSHIFT]);
            }
            phys.step(dt);
            apply_hits();
        } else if (player >= 0) {  // demo: settle, run 2 s, get into the nearest vehicle, drive 2 s, get out
            auto track = [&](const char* what) {
                float p[3];
                phys.character_position(player, p);
                std::printf("%s: soldier %.1f %.1f %.1f%s\n", what, p[0], p[1], p[2], cur >= 0 ? " (in a vehicle)" : "");
            };
            for (int i = 0; i < 60; ++i) walk(0, 0, false, false), phys.step(1.0f / 60);
            track("start");
            for (int i = 0; i < 120; ++i) walk(1, 0, false, false), phys.step(1.0f / 60);
            walk(0, 0, false, false);
            track("ran 2 s");
            get_in_out();
            for (int i = 0; i < 120 && cur >= 0; ++i) {
                phys.drive(vehicles[static_cast<size_t>(cur)].id, 1, 0);
                fire(3 + static_cast<float>(i) / 60.0f);
                phys.step(1.0f / 60);
                apply_hits();
            }
            get_in_out();
            for (int i = 0; i < 60; ++i) phys.step(1.0f / 60);
            track("end");
            // Parked vehicles after 5 s: those pushed away (spawned inside something) or tipped over.
            int moved = 0, tipped = 0;
            for (size_t i = 0; i < vehicles.size(); ++i) {
                if (static_cast<int>(i) == cur) continue;
                Mat4 t = phys.transform(vehicles[i].id);
                const Mat4& w = vehicles[i].start;
                float dx = t.m[12] - w.m[12], dy = t.m[13] - w.m[13], dz = t.m[14] - w.m[14];
                if (dx * dx + dy * dy + dz * dz > 4) ++moved, std::printf("  moved: %s %.1f %.1f %.1f\n", vehicles[i].name.c_str(), dx, dy, dz);
                if (t.m[5] < 0.7f) ++tipped, std::printf("  tipped: %s up %.2f\n", vehicles[i].name.c_str(), t.m[5]);
            }
            std::printf("parked vehicles: %zu, moved > 2 m: %d, tipped: %d\n", vehicles.size(), moved, tipped);
        } else if (cur >= 0) {  // vehicle demo
            const Veh& v = vehicles[static_cast<size_t>(cur)];
            int steps = v.plane ? 1200 : 360;
            for (int i = 0; i < steps; ++i) {
                if (v.plane) phys.drive(v.id, i < 120 ? 0.0f : 1.0f, 0, i >= 720 && i < 960 ? -0.4f : 0.0f);
                else phys.drive(v.id, i < 120 ? 0.0f : 1.0f, i < 240 || look_demo ? 0.0f : 1.0f);
                if (!look_demo) phys.look(v.id, i >= 120 ? 1.0f : 0.0f, i >= 120 ? -1.0f : 0.0f);  // turrets swing round, guns up
                if (i >= 120 && i % 20 == 0) fire(static_cast<float>(i) / 60.0f);
                phys.step(1.0f / 60);
                apply_hits();
                Mat4 t = phys.transform(v.id);
                if (i % 30 == 29) std::printf("t %.1f s: pos %.1f %.1f %.1f, up %.2f %.2f %.2f, speed %.1f m/s\n", (i + 1) / 60.0, t.m[12], t.m[13], t.m[14], t.m[4], t.m[5], t.m[6], phys.speed(v.id));
            }
        }
        // Pose every vehicle's parts (wheels, turrets, control surfaces, propellers).
        const Mat4 hidden = translate(0, -10000, 0);
        for (Veh& v : vehicles) {
            Mat4 t = phys.transform(v.id);
            std::vector<DrawItem> parts = build_object(con, vfs, v.name, v.wrecked ? hidden : t, skipped,
                                                       [&](const std::string& n, int o) { return phys.joint(v.id, n, o); });
            for (size_t i = 0; i < parts.size() && i < v.parts; ++i) {
                scene[v.first + i].world = parts[i].world;
                if (!v.weapon.name.empty() && parts[i].owner == v.weapon.name) v.muzzle = parts[i].world, v.has_muzzle = true;
            }
            std::vector<DrawItem> wreck = build_object(con, vfs, v.name, v.wrecked ? t : hidden, skipped, nullptr, nullptr, true);
            for (size_t i = 0; i < wreck.size() && i < v.wreck_parts; ++i) scene[v.wreck_first + i].world = wreck[i].world;
        }
        if (cur >= 0) {  // chase camera behind the vehicle, distance from its length (Fletcher ~110 m)
            const Veh& v = vehicles[static_cast<size_t>(cur)];
            Mat4 t = phys.transform(v.id);
            float hx = t.m[8], hz = t.m[10], hl = std::sqrt(hx * hx + hz * hz);
            if (hl > 1e-3f) hx /= hl, hz /= hl;
            float back = v.plane ? 16.0f : std::max(9.0f, v.len * 1.2f);
            cx = t.m[12] - hx * back, cy = t.m[13] + (v.plane ? 4.5f : std::max(3.5f, back * 0.35f)), cz = t.m[14] - hz * back;
            yaw = std::atan2(hx, hz), pitch = -0.25f;
        }
        if (player >= 0) {
            float p[3];
            phys.character_position(player, p);
            // The soldier faces the camera direction; hidden far below while in a vehicle.
            Mat4 w = cur >= 0 ? translate(0, -10000, 0) : translate(p[0], p[1], p[2]) * rotation_ypr(yaw * 57.29578f, 0, 0);
            std::vector<DrawItem> parts = build_object(con, vfs, play, w, skipped, nullptr, hide_1p);
            for (size_t i = 0; i < parts.size() && i < sol_parts; ++i) scene[sol_first + i].world = parts[i].world;
            std::string want = !phys.character_on_ground(player) && cur < 0 ? lower_state
                             : move_f > 0.1f ? "lb_runforward" : move_f < -0.1f ? "lb_runbackward"
                             : move_r > 0.1f ? "lb_straferight" : move_r < -0.1f ? "lb_strafeleft" : "lb_stand";
            auto an = want != lower_state ? state_anim(want) : nullptr;
            if (an) lower_state = want;
            for (size_t ai : player_anims) {
                Animated& a = animated[ai];
                if (a.biped) scene[a.item].world = scene[a.item].world * rotation_ypr(180, 0, 0);  // as at load
                if (an && !a.anims.empty()) a.anims[0] = an;
            }
            if (cur < 0) {  // third-person camera over the shoulder
                float dx = std::sin(yaw) * std::cos(pitch), dy = std::sin(pitch), dz = std::cos(yaw) * std::cos(pitch);
                cx = p[0] - dx * 4.5f, cy = p[1] + 1.7f - dy * 4.5f, cz = p[2] - dz * 4.5f;
            }
        }
        float speed = (k[SDL_SCANCODE_LSHIFT] ? 400.f : 60.f) * dt * (cur < 0 && player < 0);
        float fx = std::sin(yaw), fz = std::cos(yaw);
        if (k[SDL_SCANCODE_W]) cx += fx * speed, cz += fz * speed;
        if (k[SDL_SCANCODE_S]) cx -= fx * speed, cz -= fz * speed;
        if (k[SDL_SCANCODE_D]) cx += fz * speed, cz -= fx * speed;
        if (k[SDL_SCANCODE_A]) cx -= fz * speed, cz += fx * speed;
        if (k[SDL_SCANCODE_E]) cy += speed;
        if (k[SDL_SCANCODE_Q]) cy -= speed;

        SDL_GL_GetDrawableSize(win, &W, &H);
        glViewport(0, 0, W, H);
        glClearColor(fogc[0], fogc[1], fogc[2], 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        Mat4 vp = perspective(1.0f, static_cast<float>(W) / static_cast<float>(H), 0.5f, std::max(ws * 2, 4000.0f)) *
                  view(cx, cy, cz, yaw, pitch);
        glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp.m);
        glUniform3f(u_cam, cx, cy, cz);
        if (sky) {  // box around the camera, behind everything
            Mat4 m = translate(cx, cy, cz) * rotation_ypr(static_cast<float>(std::atof(sky_rot.c_str())), 0, 0);
            glUniformMatrix4fv(u_model, 1, GL_FALSE, m.m);
            glUniform1i(u_mode, 4);
            glDepthMask(GL_FALSE);
            glBindVertexArray(sky->vao);
            for (auto& p : sky->parts) {
                glBindTexture(GL_TEXTURE_2D, p.tex);
                glDrawElements(GL_TRIANGLES, p.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(p.offset));
            }
            glDepthMask(GL_TRUE);
        }
        glUniformMatrix4fv(u_model, 1, GL_FALSE, identity.m);
        glUniform3f(u_right, std::cos(yaw), 0.0f, -std::sin(yaw));
        glUniform1i(u_mode, 0);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(idx.size()), GL_UNSIGNED_INT, nullptr);

        // Skinning on the CPU; 25 frames per second times the state speed.
        // shortcut: the .baf frame rate is a guess; --shot renders at t = 1 s so captures are repeatable.
        float seconds = shot.empty() ? static_cast<float>(SDL_GetTicks()) / 1000.0f : 1.0f;
        std::vector<float> skinned, verts10;
        for (auto& a : animated) {
            if (a.anims.empty()) continue;
            float dx = scene[a.item].world.m[12] - cx, dz = scene[a.item].world.m[14] - cz;
            if (dx * dx + dz * dz > 300.f * 300.f) continue;
            std::vector<const Animation*> list;
            for (auto& an : a.anims) list.push_back(an.get());
            auto world = pose(*a.skeleton, list, (seconds * 25.0f + a.phase) * a.speed, a.biped);
            skin_apply(*a.skin, a.map, a.to_ske, world, a.base, 10, skinned);
            verts10 = a.base;
            for (size_t v = 0; v < skinned.size() / 3; ++v) std::memcpy(&verts10[v * 10], &skinned[v * 3], 12);
            glBindBuffer(GL_ARRAY_BUFFER, a.gpu->vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(verts10.size() * 4), verts10.data());
        }

        glUniform1i(u_mode, 2);
        for (size_t i = 0; i < scene.size(); ++i) {
            const DrawItem& item = scene[i];
            float dx = item.world.m[12] - cx, dz = item.world.m[14] - cz;
            if (dx * dx + dz * dz > 1500.f * 1500.f) continue;  // shortcut: plain distance cull, no LOD
            if (!item_mesh[i] || !item_mesh[i]->vao) continue;
            const GpuMesh& g = *item_mesh[i];
            glUniform1i(u_mode, item.skin.empty() ? 2 : 3);
            glUniform1i(u_sprite, item.mesh.size() > 3 && item.mesh.compare(item.mesh.size() - 3, 3, ".tm") == 0);
            GLuint lm = item.lightmap.empty() ? 0 : textures.get(item.lightmap.substr(0, item.lightmap.size() - 4));
            glUniform1i(u_has_lm, lm != 0 && !item.lightmap.empty());
            if (lm) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, lm);
                glActiveTexture(GL_TEXTURE0);
            }
            glUniformMatrix4fv(u_model, 1, GL_FALSE, item.world.m);
            glBindVertexArray(g.vao);
            for (auto& p : g.parts) {
                glBindTexture(GL_TEXTURE_2D, p.tex);
                glDrawElements(GL_TRIANGLES, p.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(p.offset));
            }
            if (show_collision && g.col_vao) {
                glUniform1i(u_mode, 5);
                glDisable(GL_DEPTH_TEST);
                glBindVertexArray(g.col_vao);
                glDrawElements(GL_LINES, g.col_count, GL_UNSIGNED_INT, nullptr);
                glEnable(GL_DEPTH_TEST);
            }
        }

        glUniformMatrix4fv(u_model, 1, GL_FALSE, identity.m);
        glBindVertexArray(vao);
        glEnable(GL_BLEND);
        glUniform1i(u_mode, 1);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(water_first), 6);
        glDisable(GL_BLEND);
        {  // tracers: a 3 cm-per-m/s streak behind each projectile, collision-line colour
            std::vector<float> live, lines;
            phys.projectiles(live);
            for (size_t i = 0; i + 5 < live.size(); i += 6)
                for (float back : {0.0f, 0.03f})
                    lines.insert(lines.end(), {live[i] - live[i + 3] * back, live[i + 1] - live[i + 4] * back, live[i + 2] - live[i + 5] * back, 0, 1, 0, 0, 0, 0, 0});
            if (!lines.empty()) {
                glBindVertexArray(tracer_vao);
                glBindBuffer(GL_ARRAY_BUFFER, tracer_vbo);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * 4), lines.data(), GL_STREAM_DRAW);
                glUniform1i(u_mode, 5);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size() / 10));
            }
        }
        if (!shot.empty()) {
            save_ppm(shot, W, H);
            running = false;
        }
        SDL_GL_SwapWindow(win);
    }
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
