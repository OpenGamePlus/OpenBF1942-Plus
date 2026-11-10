#include "anim.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace obf {

namespace {
struct Reader {
    const std::vector<uint8_t>& d;
    size_t o = 0;
    void need(size_t n) const {
        if (o + n > d.size()) throw std::runtime_error("anim: truncated");
    }
    template <class T>
    T get() {
        need(sizeof(T));
        T v;
        std::memcpy(&v, &d[o], sizeof(T));  // little-endian hosts only
        o += sizeof(T);
        return v;
    }
    std::string name() {
        uint16_t n = get<uint16_t>();
        need(n);
        std::string s(reinterpret_cast<const char*>(&d[o]), n);
        o += n;
        while (!s.empty() && s.back() == '\0') s.pop_back();  // same as Python rstrip
        return s;
    }
};

Mat4 from_rows(const float a[12]) {  // row-major [R | t] -> column-major Mat4
    Mat4 m = Mat4::identity();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) m.m[c * 4 + r] = a[r * 4 + c];
        m.m[12 + r] = a[r * 4 + 3];
    }
    return m;
}

Mat4 quat_local(float x, float y, float z, float w, float px, float py, float pz) {
    Mat4 m = Mat4::identity();
    m.m[0] = 1 - 2 * (y * y + z * z), m.m[4] = 2 * (x * y - z * w), m.m[8] = 2 * (x * z + y * w);
    m.m[1] = 2 * (x * y + z * w), m.m[5] = 1 - 2 * (x * x + z * z), m.m[9] = 2 * (y * z - x * w);
    m.m[2] = 2 * (x * z - y * w), m.m[6] = 2 * (y * z + x * w), m.m[10] = 1 - 2 * (x * x + y * y);
    m.m[12] = px, m.m[13] = py, m.m[14] = pz;
    return m;
}
}  // namespace

Skeleton ske_parse(const std::vector<uint8_t>& d) {
    Reader r{d};
    if (r.get<uint32_t>() != 1) throw std::runtime_error("SKE: unsupported version");
    uint32_t n = r.get<uint32_t>();
    if (n > 4096) throw std::runtime_error("SKE: implausible bone count");
    Skeleton s;
    for (uint32_t i = 0; i < n; ++i) {
        Bone b;
        b.name = r.name();
        b.parent = r.get<int16_t>();
        if (b.parent >= static_cast<int>(i)) throw std::runtime_error("SKE: parent after child");
        float a[12];
        for (float& f : a) f = r.get<float>();
        b.local = from_rows(a);
        s.push_back(std::move(b));
    }
    if (r.o != d.size()) throw std::runtime_error("SKE: trailing bytes");
    return s;
}

Skin skn_parse(const std::vector<uint8_t>& d) {
    Reader r{d};
    if (r.get<uint32_t>() != 1) throw std::runtime_error("SKN: unsupported version");
    uint32_t n = r.get<uint32_t>();
    if (n > 1000000) throw std::runtime_error("SKN: implausible record count");
    Skin s;
    s.records.resize(n);
    for (auto& rec : s.records) {
        for (float& f : rec.pos) f = r.get<float>();
        uint8_t k = r.get<uint8_t>();
        for (uint8_t j = 0; j < k; ++j) {
            SkinInfluence inf{};
            inf.bone = r.get<uint16_t>();
            inf.weight = r.get<float>();
            for (float& f : inf.offset) f = r.get<float>();
            rec.influences.push_back(inf);
        }
    }
    uint16_t nb = r.get<uint16_t>();
    for (uint16_t i = 0; i < nb; ++i) s.bones.push_back(r.name());
    if (r.o != d.size()) throw std::runtime_error("SKN: trailing bytes");
    for (auto& rec : s.records)
        for (auto& inf : rec.influences)
            if (inf.bone >= nb) throw std::runtime_error("SKN: bone index out of range");
    return s;
}

Animation baf_parse(const std::vector<uint8_t>& d) {
    Reader r{d};
    if (r.get<uint32_t>() != 3) throw std::runtime_error("BAF: unsupported version");
    Animation a;
    uint16_t nb = r.get<uint16_t>();
    for (uint16_t i = 0; i < nb; ++i) a.bones.push_back(r.name());
    a.frames = r.get<uint32_t>();
    uint8_t prec = r.get<uint8_t>();
    if (a.frames == 0 || a.frames > 100000 || prec > 30) throw std::runtime_error("BAF: bad header");
    const float pscale = 1.0f / static_cast<float>(1u << prec);
    for (uint16_t b = 0; b < nb; ++b) {
        size_t total = r.get<uint16_t>(), used = 0;
        for (int c = 0; c < 7; ++c) {
            size_t words = r.get<uint16_t>();
            size_t end = r.o + 2 * words;
            r.need(2 * words);
            std::vector<float> vals;
            vals.reserve(a.frames);
            float scale = c < 4 ? 1.0f / 32767.0f : pscale;
            while (r.o < end) {  // blocks: count in low 7 bits; high bit = one repeated value
                uint8_t head = r.get<uint8_t>();
                r.get<uint8_t>();
                int n = head & 0x7f;
                if (head & 0x80) {
                    float v = static_cast<float>(r.get<int16_t>()) * scale;
                    vals.insert(vals.end(), static_cast<size_t>(n), v);
                } else {
                    for (int i = 0; i < n; ++i) vals.push_back(static_cast<float>(r.get<int16_t>()) * scale);
                }
            }
            if (r.o != end || vals.size() != a.frames) throw std::runtime_error("BAF: bad channel");
            used += words;
            a.channels.push_back(std::move(vals));
        }
        if (used != total) throw std::runtime_error("BAF: bone word count mismatch");
    }
    if (r.o != d.size()) throw std::runtime_error("BAF: trailing bytes");
    return a;
}

std::vector<Mat4> pose(const Skeleton& s, const std::vector<const Animation*>& anims, float frame, bool biped_root) {
    std::vector<Mat4> local(s.size());
    for (size_t i = 0; i < s.size(); ++i) local[i] = s[i].local;
    for (const Animation* a : anims) {
        float f = std::isfinite(frame) ? std::fmod(std::max(frame, 0.0f), static_cast<float>(a->frames)) : 0.0f;
        uint32_t f0 = static_cast<uint32_t>(f), f1 = (f0 + 1) % a->frames;
        float t = f - static_cast<float>(f0);
        for (size_t b = 0; b < a->bones.size(); ++b)
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i].name != a->bones[b]) continue;
                float v[7];
                for (int c = 0; c < 7; ++c) v[c] = a->channel(b, c)[f0] * (1 - t) + a->channel(b, c)[f1] * t;
                float ql = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);  // nlerp
                if (ql > 0) v[0] /= ql, v[1] /= ql, v[2] /= ql, v[3] /= ql;
                local[i] = quat_local(v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
            }
    }
    if (biped_root && !s.empty()) {  // keep only the root's height: |t| is the pelvis height in metres
        const float* t = &local[0].m[12];
        local[0] = translate(0, std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]), 0);
    }
    std::vector<Mat4> world(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        world[i] = s[i].parent < 0 ? local[i] : world[static_cast<size_t>(s[i].parent)] * local[i];
    return world;
}

std::vector<int> bone_index(const Skin& skin, const Skeleton& s) {
    std::vector<int> out;
    for (auto& name : skin.bones) {
        int found = -1;
        for (size_t i = 0; i < s.size(); ++i)
            if (s[i].name == name) found = static_cast<int>(i);
        out.push_back(found);
    }
    return out;
}

std::vector<int> skin_map(const Skin& skin, const std::vector<float>& v, size_t fpv) {
    std::vector<int> out(v.size() / fpv, -1);
    for (size_t i = 0; i < out.size(); ++i) {
        const float* p = &v[i * fpv];
        float best = 1e-6f;  // 1 mm squared
        for (size_t r = 0; r < skin.records.size(); ++r) {
            const float* q = skin.records[r].pos;
            float d = (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]);
            if (d <= best) {
                best = d;
                out[i] = static_cast<int>(r);
                if (d == 0) break;
            }
        }
    }
    return out;
}

void skin_apply(const Skin& skin, const std::vector<int>& map, const std::vector<int>& to_ske, const std::vector<Mat4>& world,
                const std::vector<float>& v, size_t fpv, std::vector<float>& out) {
    out.resize(map.size() * 3);
    for (size_t i = 0; i < map.size(); ++i) {
        float* o = &out[i * 3];
        const float* p = &v[i * fpv];
        o[0] = p[0], o[1] = p[1], o[2] = p[2];
        if (map[i] < 0) continue;
        const SkinRecord& rec = skin.records[static_cast<size_t>(map[i])];
        bool ok = true;
        for (auto& inf : rec.influences) ok = ok && to_ske[inf.bone] >= 0;
        if (!ok) continue;
        o[0] = o[1] = o[2] = 0;
        for (auto& inf : rec.influences) {
            const Mat4& m = world[static_cast<size_t>(to_ske[inf.bone])];
            for (int k = 0; k < 3; ++k)
                o[k] += inf.weight * (m.m[k] * inf.offset[0] + m.m[4 + k] * inf.offset[1] + m.m[8 + k] * inf.offset[2] + m.m[12 + k]);
        }
    }
}

}  // namespace obf
