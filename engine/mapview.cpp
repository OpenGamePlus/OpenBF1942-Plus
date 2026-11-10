// Map viewer: terrain + colour tiles + water. Usage in docs/ai/SYSTEM_SUMMARY.md.
#include <SDL.h>
#include <epoxy/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "dds.h"
#include "terrain.h"
#include "vfs.h"

using namespace obf;

namespace {

struct Mat4 {
    float m[16] = {};  // column-major
    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
        return r;
    }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int i = 0; i < 4; ++i)
                for (int k = 0; k < 4; ++k) r.m[c * 4 + i] += m[k * 4 + i] * b.m[c * 4 + k];
        return r;
    }
};

Mat4 perspective(float fovy, float aspect, float zn, float zf) {
    Mat4 r;
    float f = 1.0f / std::tan(fovy / 2);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1;
    r.m[14] = 2 * zf * zn / (zn - zf);
    return r;
}

// Camera in game coordinates; yaw 0 looks along +z, pitch > 0 looks up.
Mat4 view(float x, float y, float z, float yaw, float pitch) {
    float fx = std::sin(yaw) * std::cos(pitch), fy = std::sin(pitch), fz = std::cos(yaw) * std::cos(pitch);
    // Game data is left-handed (Direct3D); mirror z into OpenGL's right-handed space.
    fz = -fz;
    z = -z;
    float rx = -fz, rz = fx;  // right = forward x up
    float rl = std::sqrt(rx * rx + rz * rz);
    rx /= rl;
    rz /= rl;
    float ux = -rz * fy, uy = rz * fx - rx * fz, uz = rx * fy;  // up = right x forward
    Mat4 v = Mat4::identity();
    v.m[0] = rx, v.m[4] = 0, v.m[8] = rz;
    v.m[1] = ux, v.m[5] = uy, v.m[9] = uz;
    v.m[2] = -fx, v.m[6] = -fy, v.m[10] = -fz;
    v.m[12] = -(rx * x + rz * z);
    v.m[13] = -(ux * x + uy * y + uz * z);
    v.m[14] = fx * x + fy * y + fz * z;
    Mat4 flip = Mat4::identity();
    flip.m[10] = -1;
    return v * flip;
}

const char* kVert = R"(#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nrm;
uniform mat4 mvp;
out vec3 wpos;
out vec3 wnrm;
void main() { wpos = pos; wnrm = nrm; gl_Position = mvp * vec4(pos, 1.0); })";

const char* kFrag = R"(#version 330 core
in vec3 wpos;
in vec3 wnrm;
uniform sampler2DArray tiles;
uniform int ntiles;
uniform float world;
uniform int mode;  // 0 terrain, 1 water
out vec4 color;
void main() {
    if (mode == 1) { color = vec4(0.12, 0.30, 0.45, 0.75); return; }
    vec2 uv = clamp(wpos.xz / world, 0.0, 0.9999) * float(ntiles);
    ivec2 t = ivec2(floor(uv));
    // Tx<a>x<b>: a along x, b along z, texture u along x, v along z (checked visually: seamless only this way).
    int layer = t.x * ntiles + t.y;
    vec2 local = fract(uv);
    vec3 tex = ntiles > 0 ? texture(tiles, vec3(local, float(layer))).rgb : vec3(0.6);
    float light = 0.45 + 0.55 * max(dot(normalize(wnrm), normalize(vec3(0.4, 0.8, 0.3))), 0.0);
    color = vec4(tex * light, 1.0);
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

int run(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: obf_mapview <BF1942_DIR> <mod> <level> [--shot out.ppm] [--cam x y z yaw pitch]\n"
                     "  right mouse: look, WASD/QE: move, shift: fast, esc: quit\n");
        return 2;
    }
    std::string shot;
    float cx = -1, cy = 0, cz = 0, yaw = 0.6f, pitch = -0.5f;
    for (int i = 4; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--cam") && i + 5 < argc) {
            cx = std::stof(argv[i + 1]), cy = std::stof(argv[i + 2]), cz = std::stof(argv[i + 3]);
            yaw = std::stof(argv[i + 4]), pitch = std::stof(argv[i + 5]);
            i += 5;
        }
    }

    Vfs vfs;
    vfs.mount_mod(argv[1], argv[2]);
    std::string lvl = std::string("bf1942/levels/") + argv[3] + "/";
    TerrainInfo info = parse_terrain_con(text(vfs, lvl + "init/terrain.con"));
    Heightmap hm = load_heightmap(vfs.read(info.heightmap + ".raw"), info.y_scale);
    std::printf("%s: %zu files, heightmap %dx%d, world %.0f m\n", argv[3], vfs.size(), hm.n, hm.n, info.world_size);

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

    // Terrain grid: one vertex per sample, normals from central differences.
    const int n = hm.n;
    const float cell = info.world_size / static_cast<float>(n);
    std::vector<float> verts;
    verts.reserve(static_cast<size_t>(n) * n * 6);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            float dx = hm.height(r, std::min(c + 1, n - 1)) - hm.height(r, std::max(c - 1, 0));
            float dz = hm.height(std::min(r + 1, n - 1), c) - hm.height(std::max(r - 1, 0), c);
            float nx = -dx, ny = 2 * cell, nz = -dz, l = std::sqrt(nx * nx + ny * ny + nz * nz);
            verts.insert(verts.end(), {static_cast<float>(c) * cell, hm.height(r, c), static_cast<float>(r) * cell, nx / l, ny / l, nz / l});
        }
    float wl = info.water_level, ws = info.world_size;
    size_t water_first = verts.size() / 6;
    for (auto [x, z] : {std::pair{0.f, 0.f}, {ws, 0.f}, {ws, ws}, {0.f, 0.f}, {ws, ws}, {0.f, ws}})
        verts.insert(verts.end(), {x, wl, z, 0, 1, 0});
    std::vector<uint32_t> idx;
    idx.reserve(static_cast<size_t>(n - 1) * (n - 1) * 6);
    for (int r = 0; r + 1 < n; ++r)
        for (int c = 0; c + 1 < n; ++c) {
            uint32_t a = static_cast<uint32_t>(r * n + c), b = a + 1, d = a + static_cast<uint32_t>(n), e = d + 1;
            idx.insert(idx.end(), {a, d, b, b, d, e});
        }

    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

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
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (ntiles > 0)
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, first.w, first.h, ntiles * ntiles, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    // shortcut: missing tiles are drawn neutral grey; what the original engine shows there is unverified.
    std::vector<uint8_t> grey(first.rgb.size(), 110);
    for (int a = 0; a < ntiles; ++a)
        for (int b = 0; b < ntiles; ++b) {
            Image img = vfs.has(tile(a, b)) ? dds_decode(vfs.read(tile(a, b))) : Image{first.w, first.h, grey};
            if (img.w != first.w || img.h != first.h) throw std::runtime_error("colour tiles differ in size: " + tile(a, b));
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, a * ntiles + b, img.w, img.h, 1, GL_RGB, GL_UNSIGNED_BYTE, img.rgb.data());
        }
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::printf("colour tiles: %dx%d\n", ntiles, ntiles);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, kVert));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, kFrag));
    glLinkProgram(prog);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "ntiles"), ntiles);
    glUniform1f(glGetUniformLocation(prog, "world"), info.world_size);
    GLint u_mvp = glGetUniformLocation(prog, "mvp"), u_mode = glGetUniformLocation(prog, "mode");

    if (cx < 0) cx = ws * 0.15f, cy = 300, cz = ws * 0.15f;  // default: corner, looking across the map
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    bool running = true, look = false;
    Uint64 last = SDL_GetPerformanceCounter();
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
        Mat4 mvp = perspective(1.0f, static_cast<float>(W) / static_cast<float>(H), 1.0f, ws * 2) * view(cx, cy, cz, yaw, pitch);
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp.m);
        glUniform1i(u_mode, 0);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(idx.size()), GL_UNSIGNED_INT, nullptr);
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
