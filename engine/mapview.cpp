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

#include "con.h"
#include "dds.h"
#include "math.h"
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
layout(location = 3) in vec2 corner;  // tree sprites: offset from the shared centre
uniform mat4 vp;
uniform mat4 model;
uniform vec3 cam_right;
out vec3 wpos;
out vec3 wnrm;
out vec2 tuv;
void main() {
    wpos = (model * vec4(pos, 1.0)).xyz;
    wnrm = mat3(model) * nrm;
    if (corner != vec2(0.0)) {  // cylindrical billboard: faces the camera, stays upright
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
uniform sampler2DArray tiles;
uniform sampler2D tex;
uniform int ntiles;
uniform float world;
uniform int mode;  // 0 terrain, 1 water, 2 mesh
out vec4 color;
void main() {
    if (mode == 1) { color = vec4(0.12, 0.30, 0.45, 0.75); return; }
    vec4 base;
    if (mode == 2) {
        base = texture(tex, tuv);
        if (base.a < 0.5) discard;  // fences, foliage
    } else {
        vec2 uv = clamp(wpos.xz / world, 0.0, 0.9999) * float(ntiles);
        ivec2 t = ivec2(floor(uv));
        // Tx<a>x<b>: a along x, b along z, texture u along x, v along z (checked visually: seamless only this way).
        int layer = t.x * ntiles + t.y;
        base = ntiles > 0 ? texture(tiles, vec3(fract(uv), float(layer))) : vec4(0.6);
    }
    vec3 n = normalize(wnrm);
    if (!gl_FrontFacing) n = -n;  // two-sided: game winding is mirrored by the view
    float light = 0.45 + 0.55 * max(dot(n, normalize(vec3(0.4, 0.8, 0.3))), 0.0);
    color = vec4(base.rgb * light, 1.0);
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
    GLuint vao = 0;
    struct Part {
        GLuint tex;
        GLsizei count;
        size_t offset;  // bytes into the index buffer
    };
    std::vector<Part> parts;
};

// One VBO (10 floats: pos, normal, uv, sprite corner) and one index buffer per mesh file.
void upload_buffers(GpuMesh& g, const std::vector<float>& verts, const std::vector<uint32_t>& idx) {
    GLuint vbo, ebo;
    glGenVertexArrays(1, &g.vao);
    glBindVertexArray(g.vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
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
std::unique_ptr<GpuMesh> upload_sm(const SmMesh& m, const std::string& path, const Vfs& vfs, Textures& tex) {
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
            verts.insert(verts.end(), &mat.vertices[v * fl], &mat.vertices[v * fl + 8]);
            verts.insert(verts.end(), {0.f, 0.f});
        }
        size_t first = idx.size();
        for (uint32_t i : sm_triangles(mat)) idx.push_back(base + i);
        auto s = shaders.find(lower(mat.name));
        GLuint t = s != shaders.end() && s->second.count("texture") ? tex.get(s->second.at("texture")) : tex.get("");
        g->parts.push_back({t, static_cast<GLsizei>(idx.size() - first), first * 4});
    }
    upload_buffers(*g, verts, idx);
    return g;
}

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
                     "usage: obf_mapview <BF1942_DIR> <mod> <level> [--shot out.ppm] [--cam x y z yaw pitch] [--no-objects]\n"
                     "  right mouse: look, WASD/QE: move, shift: fast, esc: quit\n");
        return 2;
    }
    std::string shot;
    float cx = -1, cy = 0, cz = 0, yaw = 0.6f, pitch = -0.5f;
    bool objects = true;
    for (int i = 4; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--no-objects")) objects = false;
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
    std::map<std::string, int> skipped;
    if (objects) {
        con.load_game();
        con.load_level(argv[3]);
        scene = build_scene(con, vfs, skipped);
        std::printf("objects: %zu instances -> %zu meshes drawn; skipped:", con.instances.size(), scene.size());
        for (auto& [why, n] : skipped) std::printf(" %s %d,", why.c_str(), n);
        std::printf("\n");
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
    std::map<std::string, std::unique_ptr<GpuMesh>> meshes;
    int mesh_errors = 0;
    for (auto& item : scene) {
        if (meshes.count(item.mesh)) continue;
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
    if (objects)
        std::printf("meshes: %zu unique, %d errors; textures missing %d, failed %d\n", meshes.size(), mesh_errors,
                    textures.missing, textures.failed);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, kVert));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, kFrag));
    glLinkProgram(prog);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "ntiles"), ntiles);
    glUniform1f(glGetUniformLocation(prog, "world"), info.world_size);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);
    glUniform1i(glGetUniformLocation(prog, "tiles"), 1);
    GLint u_vp = glGetUniformLocation(prog, "vp"), u_model = glGetUniformLocation(prog, "model"),
          u_mode = glGetUniformLocation(prog, "mode"), u_right = glGetUniformLocation(prog, "cam_right");

    if (cx < 0) cx = ws * 0.15f, cy = 300, cz = ws * 0.15f;  // default: corner, looking across the map
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    bool running = true, look = false;
    Uint64 last = SDL_GetPerformanceCounter();
    const Mat4 identity = Mat4::identity();
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) running = false;
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_RIGHT) look = true;
            if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_RIGHT) look = false;
            if (ev.type == SDL_MOUSEMOTION && look) {
                yaw += static_cast<float>(ev.motion.xrel) * 0.004f;
                pitch = std::clamp(pitch - static_cast<float>(ev.motion.yrel) * 0.004f, -1.5f, 1.5f);
            }
        }
        SDL_SetRelativeMouseMode(look ? SDL_TRUE : SDL_FALSE);
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - last) / static_cast<float>(SDL_GetPerformanceFrequency());
        last = now;
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        float speed = (k[SDL_SCANCODE_LSHIFT] ? 400.f : 60.f) * dt;
        float fx = std::sin(yaw), fz = std::cos(yaw);
        if (k[SDL_SCANCODE_W]) cx += fx * speed, cz += fz * speed;
        if (k[SDL_SCANCODE_S]) cx -= fx * speed, cz -= fz * speed;
        if (k[SDL_SCANCODE_D]) cx += fz * speed, cz -= fx * speed;
        if (k[SDL_SCANCODE_A]) cx -= fz * speed, cz += fx * speed;
        if (k[SDL_SCANCODE_E]) cy += speed;
        if (k[SDL_SCANCODE_Q]) cy -= speed;

        SDL_GL_GetDrawableSize(win, &W, &H);
        glViewport(0, 0, W, H);
        glClearColor(0.62f, 0.72f, 0.82f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        Mat4 vp = perspective(1.0f, static_cast<float>(W) / static_cast<float>(H), 0.5f, ws * 2) * view(cx, cy, cz, yaw, pitch);
        glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp.m);
        glUniformMatrix4fv(u_model, 1, GL_FALSE, identity.m);
        glUniform3f(u_right, std::cos(yaw), 0.0f, -std::sin(yaw));
        glUniform1i(u_mode, 0);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(idx.size()), GL_UNSIGNED_INT, nullptr);

        glUniform1i(u_mode, 2);
        for (auto& item : scene) {
            float dx = item.world.m[12] - cx, dz = item.world.m[14] - cz;
            if (dx * dx + dz * dz > 1500.f * 1500.f) continue;  // shortcut: plain distance cull, no LOD
            const GpuMesh& g = *meshes[item.mesh];
            if (!g.vao) continue;
            glUniformMatrix4fv(u_model, 1, GL_FALSE, item.world.m);
            glBindVertexArray(g.vao);
            for (auto& p : g.parts) {
                glBindTexture(GL_TEXTURE_2D, p.tex);
                glDrawElements(GL_TRIANGLES, p.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(p.offset));
            }
        }

        glUniformMatrix4fv(u_model, 1, GL_FALSE, identity.m);
        glBindVertexArray(vao);
        glEnable(GL_BLEND);
        glUniform1i(u_mode, 1);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(water_first), 6);
        glDisable(GL_BLEND);
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
