// Map viewer renderer: the GL part of what used to be mapview.cpp. See render.h.
#include "render.h"

#include <epoxy/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <map>
#include <memory>
#include <stdexcept>

#include "anim.h"
#include "dds.h"
#include "math.h"
#include "sm.h"
#include "tm.h"

namespace obf {

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
uniform sampler2D detail;     // terrain detail texture (detailTexName), tiled
uniform vec3 detail_gain;     // 1 / its mean colour: neutral on average
uniform float detail_scale;   // repeats per metre; 0: none
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
    // Skinned meshes: normals recomputed each frame from the posed triangles (smooth over welded seams).
    // Terrain colour tiles have lighting and shadows baked in: drawn unlit.
    if (mode == 0) {
        // Detail texture multiplied in near the camera, normalised to its mean; fades out by 200 m (guessed).
        if (detail_scale > 0.0) {
            float fade = 1.0 - smoothstep(60.0, 200.0, distance(wpos, cam_pos));
            base.rgb *= mix(vec3(1.0), texture(detail, wpos.xz * detail_scale).rgb * detail_gain, fade);
        }
        color = fogged(vec4(base.rgb, 1.0));
        return;
    }
    vec3 n = normalize(wnrm);  // skinned meshes: smooth normals recomputed on the CPU from the posed mesh
    if (mode != 3 && !gl_FrontFacing) n = -n;  // two-sided: game winding is mirrored by the view
    // shortcut: ambient + diffuse from Init.con, doubled (assumed D3D modulate2x); no lightmaps.
    vec3 light = min(2.0 * (ambient + diffuse * max(dot(n, sun_dir), 0.0)), vec3(1.5));
    if (has_lm == 1) light = texture(lmap, luv).rgb * lm_scale;  // baked object lighting replaces the sun term
    color = fogged(vec4(base.rgb * light, 1.0));
})";

// Anisotropic filtering (GL 4.6 core, EXT before): ground textures seen at grazing angles stay sharp, as in the original.
void anisotropic(GLenum target) {
    if (!epoxy_has_gl_extension("GL_EXT_texture_filter_anisotropic") && !epoxy_has_gl_extension("GL_ARB_texture_filter_anisotropic")) return;
    GLfloat most = 1;
    glGetFloatv(0x84FF, &most);  // GL_MAX_TEXTURE_MAX_ANISOTROPY
    glTexParameterf(target, 0x84FE, std::min(most, 8.0f));  // GL_TEXTURE_MAX_ANISOTROPY
}

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

GLuint upload_texture(const Image& img) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    anisotropic(GL_TEXTURE_2D);
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
                                   std::vector<float>* keep_vertices = nullptr, std::vector<uint32_t>* keep_indices = nullptr) {
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
    if (keep_indices) *keep_indices = idx;
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
    float hold = -1;  // >= 0: play once from this time and stay on the last frame (deaths)
    std::vector<std::shared_ptr<Animation>> anims0;  // as loaded from the item's states
    std::vector<Mat4> bones;  // last pose (model space)
    std::vector<uint32_t> idx;  // triangles, for the posed normals
    std::vector<uint32_t> weld; // per vertex: the first vertex at the same rest position (seams shade smoothly)
    float normal_sign = 1;      // posed face normals point the stored normals' way
    float upper_speed = 0, upper_hold = -1;  // the upper-body layer's own speed (0: the item's) and play-once start
    float span = 0;                          // > 0: the play-once upper layer is stretched to this many seconds
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

Mat4 rigid_inverse(const Mat4& m) {  // rotation and translation only
    Mat4 r = Mat4::identity();
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) r.m[a * 4 + b] = m.m[b * 4 + a];
    for (int a = 0; a < 3; ++a) r.m[12 + a] = -(r.m[a] * m.m[12] + r.m[4 + a] * m.m[13] + r.m[8 + a] * m.m[14]);
    return r;
}

struct State {
    std::string baf;
    float speed = 1;
    std::string baf1p;  // the state's second animation: first person
    float speed1p = 1;
};

}  // namespace

struct Renderer::Impl {
    std::unique_ptr<Textures> textures;
    GLuint vao = 0, prog = 0, tracer_vao = 0, tracer_vbo = 0;
    GLuint hud_prog = 0, hud_vao = 0, hud_vbo = 0, hud_tex = 0;
    std::string hud_font;
    GLsizei terrain_count = 0;
    size_t water_first = 0;
    float ws = 0, fogc[3] = {};
    std::string sky_rot;
    std::unique_ptr<GpuMesh> sky;
    std::map<std::string, std::unique_ptr<GpuMesh>> meshes;
    std::vector<const GpuMesh*> item_mesh;
    std::vector<Animated> animated;
    std::vector<int> anim_of_item;  // scene item -> animated index, or -1
    std::map<std::string, State> states;
    std::map<std::string, std::shared_ptr<Animation>> anims;
    std::map<std::string, std::shared_ptr<Skeleton>> skeletons;  // by path
    std::map<size_t, Mat4> held;  // this frame's held-weapon parts
    GLint u_sprite = -1, u_has_lm = -1, u_vp = -1, u_model = -1, u_mode = -1, u_right = -1, u_cam = -1;
    const Vfs& vfs;

    Impl(const Vfs& vfs, Con& con, const Heightmap& hm, const TerrainInfo& info, const std::vector<DrawItem>& scene, bool objects);
    std::shared_ptr<Animation> state_anim(const std::string& name, bool first_person = false, float* speed = nullptr) {
        auto st = states.find(name);
        if (st == states.end()) return nullptr;
        bool one = first_person && !st->second.baf1p.empty();
        const std::string& baf = one ? st->second.baf1p : st->second.baf;
        if (speed) *speed = one ? st->second.speed1p : st->second.speed;
        if (!vfs.has(baf)) return nullptr;
        auto& an = anims[baf];
        if (!an) an = std::make_shared<Animation>(baf_parse(vfs.read(baf)));
        return an;
    }
    void draw(const std::vector<DrawItem>& scene, const Camera& cam, int W, int H, float seconds, bool show_collision,
              const std::vector<float>& projectiles, const std::vector<SoldierView>& soldiers);
};

Renderer::Impl::Impl(const Vfs& vfs_, Con& con, const Heightmap& hm, const TerrainInfo& info, const std::vector<DrawItem>& scene, bool objects)
    : vfs(vfs_) {
    const Vfs& vfs = vfs_;
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
    float wl = info.water_level;
    ws = info.world_size;
    water_first = verts.size() / 10;
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
    vao = terrain.vao, terrain_count = static_cast<GLsizei>(idx.size());

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
    anisotropic(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);
    std::printf("colour tiles: %dx%d\n", ntiles, ntiles);
    // Detail texture (Berlin: grey gravel, 512 x 512 DXT5), on unit 3.
    float detail_gain[3] = {1, 1, 1}, detail_scale = 0;
    if (!info.detail.empty() && vfs.has(Vfs::key(info.detail) + ".dds")) {
        try {
            Image d = dds_decode(vfs.read(Vfs::key(info.detail) + ".dds"));
            double sum[3] = {};
            for (size_t i = 0; i + 3 < d.rgba.size(); i += 4)
                for (int k = 0; k < 3; ++k) sum[k] += d.rgba[i + static_cast<size_t>(k)];
            for (int k = 0; k < 3; ++k) detail_gain[k] = static_cast<float>(255.0 * static_cast<double>(d.w) * d.h / std::max(sum[k], 1.0));
            GLuint dt;
            glGenTextures(1, &dt);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, dt);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, d.w, d.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.rgba.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            anisotropic(GL_TEXTURE_2D);
            glActiveTexture(GL_TEXTURE0);
            detail_scale = 1.0f / 4.0f;  // shortcut: one repeat per 4 m, pebble size matched by eye to the original
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "detail texture: %s\n", ex.what());
        }
    }

    // Meshes: one GPU copy per file, shared by all instances.
    std::vector<std::string> alt;
    for (auto& [method, args] : con.settings["texturemanager"])
        if (method == "alternativepath" && !args.empty()) alt.push_back(args[0]);
    textures = std::make_unique<Textures>(vfs, alt);
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
    auto fog = vec3_or(setting("renderer", "fogcolorvec"), 0.62f, 0.72f, 0.82f);
    std::copy(fog.begin(), fog.end(), fogc);
    std::string vd = setting("game", "setviewdistance");
    float view_dist = vd.empty() ? 1500.0f : static_cast<float>(std::atof(vd.c_str()));
    sky_rot = setting("sky", "setrotangle");
    std::string sky_file;
    if (con.templates.count("geometrytemplate") && con.templates["geometrytemplate"].count("skybox"))
        for (auto& [k, a] : con.templates["geometrytemplate"]["skybox"].props)
            if (k == "file" && !a.empty()) sky_file = "standardmesh/" + Vfs::key(a[0]) + ".sm";

    // Animation states: createState <name>, then addAnimation <baf> <speed> <flags>; the first animation is used.
    {
        std::string cur;
        auto swap = [](std::string s, const std::string& from, const std::string& to) {
            for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
            return s;
        };
        for (auto& [method, args] : con.settings["animationstatemachine"]) {
            if (method == "createstate" && !args.empty()) cur = lower(args[0]);
            // copyState2 <new> <from> [<3p weapon> <speed> <1p weapon> <speed>]: the state just made, for another
            // weapon: <from> replaced in its name and animation paths (Ub_StandAimThompson -> Ub_StandAimBar1918).
            if (method == "copystate2" && args.size() >= 2 && states.count(cur)) {
                std::string to = lower(args[0]), from = lower(args[1]), name = swap(cur, from, to);
                State st = states[cur];
                std::string baf = swap(st.baf, from, lower(args.size() >= 3 ? args[2] : args[0]));
                if (vfs.has(baf)) st.baf = baf;
                std::string baf1p = swap(st.baf1p, from, lower(args.size() >= 5 ? args[4] : args[0]));
                if (vfs.has(baf1p)) st.baf1p = baf1p;
                if (name != cur && !states.count(name)) states[name] = st;
            }
            if (method == "addanimation" && !args.empty() && !cur.empty()) {
                float sp = args.size() > 1 ? static_cast<float>(std::atof(args[1].c_str())) : 1.0f;
                if (!states.count(cur)) states[cur] = {Vfs::key(args[0]), std::isfinite(sp) ? sp : 1.0f, "", 1};
                else if (states[cur].baf1p.empty()) states[cur].baf1p = Vfs::key(args[0]), states[cur].speed1p = std::isfinite(sp) ? sp : 1.0f;
            }
        }
    }
    item_mesh.assign(scene.size(), nullptr);
    anim_of_item.assign(scene.size(), -1);
    std::map<std::string, std::shared_ptr<Skin>> skins;
    int anim_errors = 0;
    for (size_t i = 0; i < scene.size(); ++i) {
        const DrawItem& item = scene[i];
        if (item.skin.empty()) continue;
        try {
            Animated a;
            a.item = i;
            a.gpu = upload_sm(sm_parse(vfs.read(item.mesh)), item.mesh, vfs, *textures, &a.base, &a.idx);
            {  // weld by rest position; winding sign from the stored normals
                size_t n = a.base.size() / 10;
                std::map<std::array<float, 3>, uint32_t> at;
                a.weld.resize(n);
                for (size_t v = 0; v < n; ++v)
                    a.weld[v] = at.emplace(std::array<float, 3>{a.base[v * 10], a.base[v * 10 + 1], a.base[v * 10 + 2]}, static_cast<uint32_t>(v)).first->second;
                double agree = 0;
                for (size_t t = 0; t + 2 < a.idx.size(); t += 3) {
                    const float *p0 = &a.base[a.idx[t] * 10], *p1 = &a.base[a.idx[t + 1] * 10], *p2 = &a.base[a.idx[t + 2] * 10];
                    float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
                    float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                    agree += fn[0] * p0[3] + fn[1] * p0[4] + fn[2] * p0[5];
                }
                a.normal_sign = agree < 0 ? -1.0f : 1.0f;
            }
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
            // Biped rigs (soldiers): 3ds Max space turned Y-up facing +z by pose() (docs/formats/anim.md).
            a.biped = !sk->empty() && (*sk)[0].name == "Bip01";
            a.phase = static_cast<float>(i % 17);  // shortcut: stands in for setUserRandomStartTime
            item_mesh[i] = a.gpu.get();
            a.anims0 = a.anims;
            anim_of_item[i] = static_cast<int>(animated.size());
            animated.push_back(std::move(a));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "animated %s: %s\n", item.mesh.c_str(), e.what());
            ++anim_errors;
        }
    }
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
    int mesh_errors = 0;
    for (auto& item : scene) {
        if (!item.skin.empty() || meshes.count(item.mesh)) continue;
        try {
            bool tree = item.mesh.size() > 3 && item.mesh.compare(item.mesh.size() - 3, 3, ".tm") == 0;
            meshes[item.mesh] = tree ? upload_tm(tm_parse(vfs.read(item.mesh)), *textures)
                                     : upload_sm(sm_parse(vfs.read(item.mesh)), item.mesh, vfs, *textures);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "mesh %s: %s\n", item.mesh.c_str(), e.what());
            meshes[item.mesh] = std::make_unique<GpuMesh>();
            ++mesh_errors;
        }
    }
    for (size_t i = 0; i < scene.size(); ++i)
        if (!item_mesh[i] && meshes.count(scene[i].mesh)) item_mesh[i] = meshes[scene[i].mesh].get();
    if (!sky_file.empty() && vfs.has(sky_file)) sky = upload_sm(sm_parse(vfs.read(sky_file)), sky_file, vfs, *textures);
    if (objects)
        std::printf("meshes: %zu unique, %d errors; animated %zu (%zu with animation), %d errors; textures missing %d, failed %d\n",
                    meshes.size(), mesh_errors, animated.size(),
                    static_cast<size_t>(std::count_if(animated.begin(), animated.end(), [](const Animated& a) { return !a.anims.empty(); })),
                    anim_errors, textures->missing, textures->failed);

    prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, kVert));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, kFrag));
    glLinkProgram(prog);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "ntiles"), ntiles);
    glUniform1f(glGetUniformLocation(prog, "world"), info.world_size);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);
    glUniform1i(glGetUniformLocation(prog, "tiles"), 1);
    glUniform1i(glGetUniformLocation(prog, "lmap"), 2);
    glUniform1i(glGetUniformLocation(prog, "detail"), 3);
    glUniform3fv(glGetUniformLocation(prog, "detail_gain"), 1, detail_gain);
    glUniform1f(glGetUniformLocation(prog, "detail_scale"), detail_scale);
    // shortcut: lightmaps average 0.13-0.21 (Wake, El Alamein, Berlin, Bocage), so x2 (D3D MODULATE2X,
    // the usual lightmap blend of the era) is assumed; x1 leaves buildings at ~20% brightness. Unverified.
    glUniform1f(glGetUniformLocation(prog, "lm_scale"), 2.0f);
    u_sprite = glGetUniformLocation(prog, "sprite"), u_has_lm = glGetUniformLocation(prog, "has_lm");
    u_vp = glGetUniformLocation(prog, "vp"), u_model = glGetUniformLocation(prog, "model"), u_mode = glGetUniformLocation(prog, "mode"),
    u_right = glGetUniformLocation(prog, "cam_right"), u_cam = glGetUniformLocation(prog, "cam_pos");
    {
        float l = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
        glUniform3f(glGetUniformLocation(prog, "sun_dir"), sun[0] / l, sun[1] / l, sun[2] / l);
        glUniform3f(glGetUniformLocation(prog, "ambient"), amb[0] + amb2[0], amb[1] + amb2[1], amb[2] + amb2[2]);
        glUniform3f(glGetUniformLocation(prog, "diffuse"), dif[0], dif[1], dif[2]);
        glUniform3f(glGetUniformLocation(prog, "fog_color"), fogc[0], fogc[1], fogc[2]);
        // shortcut: fog starts at half the view distance; the original start value is unknown.
        glUniform2f(glGetUniformLocation(prog, "fog_range"), view_dist * 0.5f, view_dist);
    }
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::Impl::draw(const std::vector<DrawItem>& scene, const Camera& cam, int W, int H, float seconds, bool show_collision,
                          const std::vector<float>& projectiles, const std::vector<SoldierView>& soldiers) {
    const Mat4 identity = Mat4::identity();
        glViewport(0, 0, W, H);
        glClearColor(fogc[0], fogc[1], fogc[2], 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        Mat4 vp = perspective(1.0f, static_cast<float>(W) / static_cast<float>(H), 0.5f, std::max(ws * 2, 4000.0f)) *
                  view(cam.x, cam.y, cam.z, cam.yaw, cam.pitch);
        glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp.m);
        glUniform3f(u_cam, cam.x, cam.y, cam.z);
        if (sky) {  // box around the camera, behind everything
            Mat4 m = translate(cam.x, cam.y, cam.z) * rotation_ypr(static_cast<float>(std::atof(sky_rot.c_str())), 0, 0);
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
        glUniform3f(u_right, std::cos(cam.yaw), 0.0f, -std::sin(cam.yaw));
        glUniform1i(u_mode, 0);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, terrain_count, GL_UNSIGNED_INT, nullptr);

        // Soldier animation states chosen by the game (lower / upper body, death hold).
        for (const SoldierView& sv : soldiers)
            for (size_t i = sv.first; i < sv.first + sv.parts && i < anim_of_item.size(); ++i) {
                if (anim_of_item[i] < 0) continue;
                Animated& a = animated[static_cast<size_t>(anim_of_item[i])];
                if (a.anims0.empty()) continue;
                a.anims = a.anims0, a.hold = sv.hold, a.upper_speed = 0, a.upper_hold = sv.upper_hold, a.span = sv.span;
                if (auto an = sv.lower.empty() ? nullptr : state_anim(sv.lower, sv.first_person)) a.anims[0] = an;
                float us = 0;
                if (auto an = sv.upper.empty() || a.anims.size() < 2 ? nullptr : state_anim(sv.upper, sv.first_person, &us))
                    a.anims[1] = an, a.upper_speed = us;
            }
        // Skinning on the CPU; 25 frames per second times the state speed.
        // shortcut: the .baf frame rate is a guess; --shot renders at t = 1 s so captures are repeatable.
        std::vector<float> skinned, verts10;
        for (auto& a : animated) {
            if (a.anims.empty()) continue;
            float dx = scene[a.item].world.m[12] - cam.x, dz = scene[a.item].world.m[14] - cam.z;
            if (dx * dx + dz * dz > 300.f * 300.f) continue;
            std::vector<const Animation*> list;
            for (auto& an : a.anims) list.push_back(an.get());
            float frame = (seconds * 25.0f + a.phase) * a.speed;
            if (a.hold != -1) {
                uint32_t frames = UINT32_MAX;
                for (auto* an : list) frames = std::min(frames, an->frames);
                frame = std::clamp((seconds - a.hold) * 25.0f * a.speed, 0.0f, static_cast<float>(frames) - 1.01f);
            }
            std::vector<float> frames(list.size(), frame);
            if (list.size() >= 2 && a.hold == -1 && a.upper_speed > 0) {  // the upper body at its own speed / start
                float s = a.upper_speed;
                // Reloads fill the weapon's reloadTime: 3p reloads (about 36 frames at speed 0.4) match it at 25 fps,
                // 1p ones have 4-6 times the frames, so a fixed rate cut them off before the new magazine went in.
                float last = static_cast<float>(list[1]->frames) - 1.01f;
                frames[1] = a.upper_hold >= 0 ? (a.span > 0 ? std::clamp((seconds - a.upper_hold) / a.span, 0.0f, 1.0f) * last
                                                             : std::clamp((seconds - a.upper_hold) * 25.0f * s, 0.0f, last))
                                              : (seconds * 25.0f + a.phase) * s;
            }
            auto world = pose(*a.skeleton, list, frames, a.biped);
            a.bones = world;
            skin_apply(*a.skin, a.map, a.to_ske, world, a.base, 10, skinned);
            verts10 = a.base;
            for (size_t v = 0; v < skinned.size() / 3; ++v) std::memcpy(&verts10[v * 10], &skinned[v * 3], 12);
            // Smooth normals of the posed mesh: area-weighted face normals summed per welded vertex.
            std::vector<float> acc(skinned.size(), 0.0f);
            for (size_t t = 0; t + 2 < a.idx.size(); t += 3) {
                uint32_t i0 = a.idx[t], i1 = a.idx[t + 1], i2 = a.idx[t + 2];
                if ((static_cast<size_t>(std::max({i0, i1, i2})) + 1) * 3 > skinned.size()) continue;
                const float *p0 = &skinned[i0 * 3], *p1 = &skinned[i1 * 3], *p2 = &skinned[i2 * 3];
                float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
                float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                for (uint32_t i : {i0, i1, i2})
                    for (int k = 0; k < 3; ++k) acc[a.weld[i] * 3 + static_cast<size_t>(k)] += fn[k];
            }
            for (size_t v = 0; v < skinned.size() / 3; ++v) {
                const float* n = &acc[a.weld[v] * 3];
                float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (l > 1e-12f)
                    for (int k = 0; k < 3; ++k) verts10[v * 10 + 3 + static_cast<size_t>(k)] = n[k] / l * a.normal_sign;
            }
            glBindBuffer(GL_ARRAY_BUFFER, a.gpu->vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(verts10.size() * 4), verts10.data());
        }

        // Held weapons: parts on their weapon-skeleton bones, the weapon's main bone (Base) on the soldier's right hand.
        // The main bone's frame is turned 180 degrees about y: as stored, the barrel points back through the body and
        // the left hand lies on its axis behind the grip; turned, the hand is on the forestock (unverified reading).
        held.clear();
        for (const SoldierView& sv : soldiers) {
            const Animated* body = nullptr;
            int rh = -1;
            for (size_t i = sv.first; i < sv.first + sv.parts && i < anim_of_item.size() && !body; ++i) {
                if (anim_of_item[i] < 0) continue;
                const Animated& a = animated[static_cast<size_t>(anim_of_item[i])];
                for (size_t b = 0; b < a.skeleton->size() && !a.bones.empty() && !body; ++b)
                    if ((*a.skeleton)[b].name == "Bip01 R Hand") body = &a, rh = static_cast<int>(b);
            }
            if (!body) continue;
            Mat4 at_hand = scene[body->item].world * body->bones[static_cast<size_t>(rh)];
            float wspeed = 1;
            std::shared_ptr<Animation> wanim = sv.weapon.empty() ? nullptr : state_anim(sv.weapon, false, &wspeed);
            for (size_t i = sv.held_first; i < sv.held_first + sv.held_parts && i < scene.size(); ++i) {
                const DrawItem& it = scene[i];
                auto& ws = skeletons[it.skeleton];
                if (!ws && vfs.has(it.skeleton)) ws = std::make_shared<Skeleton>(ske_parse(vfs.read(it.skeleton)));
                if (!ws || ws->size() < 2) continue;
                // The weapon's own state (bolt, magazine) poses its skeleton; parts sit at their bone relative to
                // the main bone, which stays on the hand.
                std::vector<const Animation*> wl;
                std::vector<float> wf;
                if (wanim)
                    wl.push_back(wanim.get()), wf.push_back(sv.weapon_hold < 0 ? seconds * 25.0f * wspeed
                                                            : sv.span > 0 ? std::clamp((seconds - sv.weapon_hold) / sv.span, 0.0f, 1.0f) * (static_cast<float>(wanim->frames) - 1.01f)
                                                                          : std::clamp((seconds - sv.weapon_hold) * 25.0f * wspeed, 0.0f, static_cast<float>(wanim->frames) - 1.01f));
                std::vector<Mat4> wp = pose(*ws, wl, wf);
                size_t bi = 1;
                for (size_t b = 0; b < ws->size(); ++b)
                    if (lower((*ws)[b].name) == it.bone) bi = b;
                held[i] = at_hand * (*ws)[1].local * rotation_ypr(180, 0, 0) * rigid_inverse(wp[1]) * wp[bi];
            }
        }

        // First-person arms and their weapon: drawn last, over everything, with their own field of view.
        std::vector<char> overlay(scene.size(), 0);
        float fov1p = 0;
        for (const SoldierView& sv : soldiers) {
            if (!sv.first_person) continue;
            fov1p = sv.fov > 0 ? sv.fov : 1.0f;
            for (size_t i = sv.first; i < sv.first + sv.parts && i < scene.size(); ++i) overlay[i] = 1;
            for (size_t i = sv.held_first; i < sv.held_first + sv.held_parts && i < scene.size(); ++i) overlay[i] = 1;
        }
        auto draw_item = [&](size_t i) {
            const DrawItem& item = scene[i];
            auto hw = held.find(i);
            const Mat4& at = hw != held.end() ? hw->second : item.world;
            float dx = at.m[12] - cam.x, dz = at.m[14] - cam.z;
            if (dx * dx + dz * dz > 1500.f * 1500.f) return;  // shortcut: plain distance cull, no LOD
            if (!item_mesh[i] || !item_mesh[i]->vao) return;
            const GpuMesh& g = *item_mesh[i];
            glUniform1i(u_mode, item.skin.empty() ? 2 : 3);
            glUniform1i(u_sprite, item.mesh.size() > 3 && item.mesh.compare(item.mesh.size() - 3, 3, ".tm") == 0);
            GLuint lm = item.lightmap.empty() ? 0 : textures->get(item.lightmap.substr(0, item.lightmap.size() - 4));
            glUniform1i(u_has_lm, lm != 0 && !item.lightmap.empty());
            if (lm) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, lm);
                glActiveTexture(GL_TEXTURE0);
            }
            Mat4 model = item.world;
            if (hw != held.end()) model = hw->second;
        glUniformMatrix4fv(u_model, 1, GL_FALSE, model.m);
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
        };
        glUniform1i(u_mode, 2);
        for (size_t i = 0; i < scene.size(); ++i)
            if (!overlay[i]) draw_item(i);

        glUniformMatrix4fv(u_model, 1, GL_FALSE, identity.m);
        glBindVertexArray(vao);
        glEnable(GL_BLEND);
        glUniform1i(u_mode, 1);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(water_first), 6);
        glDisable(GL_BLEND);
        {  // tracers: a 3 cm-per-m/s streak behind each projectile, collision-line colour
            const std::vector<float>& live = projectiles;
            std::vector<float> lines;
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
        if (fov1p > 0) {  // the 1p rig sits 0.2-0.5 m from the eye: a near plane of 5 cm, depth cleared
            glClear(GL_DEPTH_BUFFER_BIT);
            Mat4 vp1 = perspective(fov1p, static_cast<float>(W) / static_cast<float>(H), 0.05f, 50.0f) * view(cam.x, cam.y, cam.z, cam.yaw, cam.pitch);
            glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp1.m);
            for (size_t i = 0; i < scene.size(); ++i)
                if (overlay[i]) draw_item(i);
            glUniformMatrix4fv(u_vp, 1, GL_FALSE, vp.m);
        }
}

Renderer::Renderer(const Vfs& vfs, Con& con, const Heightmap& hm, const TerrainInfo& info, const std::vector<DrawItem>& scene, bool objects)
    : p_(std::make_unique<Impl>(vfs, con, hm, info, scene, objects)) {}

Renderer::~Renderer() = default;

void Renderer::draw(const std::vector<DrawItem>& scene, const Camera& cam, int w, int h, float seconds, bool show_collision,
                    const std::vector<float>& projectiles, const std::vector<SoldierView>& soldiers) {
    p_->draw(scene, cam, w, h, seconds, show_collision, projectiles, soldiers);
}

const float* Renderer::clear_colour() const { return p_->fogc; }

void Renderer::hud(const std::vector<HudQuad>& quads, int w, int h, const std::string& font_texture) {
    Impl& p = *p_;
    if (!p.hud_prog) {
        static const char* vs = R"(#version 330 core
layout(location = 0) in vec2 pos; layout(location = 1) in vec2 uv; layout(location = 2) in vec4 col;
uniform vec2 screen; out vec2 v_uv; out vec4 v_col;
void main() { gl_Position = vec4(pos.x / screen.x * 2.0 - 1.0, 1.0 - pos.y / screen.y * 2.0, 0.0, 1.0); v_uv = uv; v_col = col; })";
        static const char* fs = R"(#version 330 core
in vec2 v_uv; in vec4 v_col; uniform sampler2D font; out vec4 o;
void main() { o = v_uv.x < 0.0 ? v_col : vec4(v_col.rgb, v_col.a * texture(font, v_uv).a); })";
        p.hud_prog = glCreateProgram();
        glAttachShader(p.hud_prog, compile(GL_VERTEX_SHADER, vs));
        glAttachShader(p.hud_prog, compile(GL_FRAGMENT_SHADER, fs));
        glLinkProgram(p.hud_prog);
        glGenVertexArrays(1, &p.hud_vao);
        glBindVertexArray(p.hud_vao);
        glGenBuffers(1, &p.hud_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, p.hud_vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 32, nullptr);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(8));
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(16));
        for (GLuint a = 0; a < 3; ++a) glEnableVertexAttribArray(a);
    }
    if (font_texture != p.hud_font) {
        p.hud_font = font_texture;
        Image img{1, 1, {255, 255, 255, 255}};
        try {
            if (p.vfs.has(font_texture)) img = tga_decode(p.vfs.read(font_texture));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "font %s: %s\n", font_texture.c_str(), e.what());
        }
        if (!p.hud_tex) glGenTextures(1, &p.hud_tex);
        glBindTexture(GL_TEXTURE_2D, p.hud_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);  // pixel font, integer scales
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    std::vector<float> v;
    v.reserve(quads.size() * 48);
    for (const HudQuad& q : quads) {
        float x[2] = {q.x, q.x + q.w}, y[2] = {q.y, q.y + q.h}, u[2] = {q.u0, q.u1}, t[2] = {q.v0, q.v1};
        for (int c : {0, 1, 2, 2, 1, 3}) {
            int i = c & 1, j = c >> 1;
            v.insert(v.end(), {x[i], y[j], q.u0 < 0 ? -1.0f : u[i], t[j], q.rgba[0], q.rgba[1], q.rgba[2], q.rgba[3]});
        }
    }
    if (v.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(p.hud_prog);
    glUniform2f(glGetUniformLocation(p.hud_prog, "screen"), static_cast<float>(w), static_cast<float>(h));
    glUniform1i(glGetUniformLocation(p.hud_prog, "font"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p.hud_tex);
    glBindVertexArray(p.hud_vao);
    glBindBuffer(GL_ARRAY_BUFFER, p.hud_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * 4), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size() / 8));
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(p.prog);
}

void Renderer::save_ppm(const std::string& path, int w, int h) const {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) std::fwrite(&px[static_cast<size_t>(y) * w * 3], 1, static_cast<size_t>(w) * 3, f);
    std::fclose(f);
}


}  // namespace obf
