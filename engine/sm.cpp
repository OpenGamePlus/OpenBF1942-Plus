#include "sm.h"

#include <cstring>
#include <sstream>
#include <stdexcept>

#include "con.h"

namespace obf {

namespace {
struct Reader {
    const std::vector<uint8_t>& d;
    size_t o = 0;
    void need(size_t n) const {
        if (o + n > d.size()) throw std::runtime_error("SM: truncated");
    }
    uint32_t u32() {
        need(4);
        uint32_t v;
        std::memcpy(&v, d.data() + o, 4);  // little-endian hosts only
        o += 4;
        return v;
    }
};
}  // namespace

SmMesh sm_parse(const std::vector<uint8_t>& d) {
    Reader r{d};
    SmMesh m;
    m.version = r.u32();
    if (m.version != 9 && m.version != 10) throw std::runtime_error("SM: unsupported version");
    r.u32();
    r.need(24);
    std::memcpy(m.bbox, d.data() + r.o, 24);
    r.o = 0x20 + (m.version == 10 ? 1 : 0);  // v10 has one extra byte, meaning unknown
    uint32_t ncol = r.u32();
    for (uint32_t i = 0; i < ncol; ++i) {
        uint32_t size = r.u32();
        r.need(size);
        m.collision.push_back(collision_parse(d.data() + r.o, size));
        r.o += size;
    }
    uint32_t nlod = r.u32();
    if (nlod > 64) throw std::runtime_error("SM: implausible LOD count");
    for (uint32_t l = 0; l < nlod; ++l) {
        uint32_t nmat = r.u32();
        if (nmat > 4096) throw std::runtime_error("SM: implausible material count");
        std::vector<SmMaterial> mats(nmat);
        std::vector<uint32_t> nv(nmat), ni(nmat);
        for (uint32_t i = 0; i < nmat; ++i) {
            uint32_t n = r.u32();
            r.need(n);
            mats[i].name.assign(reinterpret_cast<const char*>(d.data() + r.o), n);
            r.o += n + 12;  // 3 x u32, always 0
            mats[i].prim = r.u32();
            r.u32();  // fvf
            mats[i].stride = r.u32();
            nv[i] = r.u32();
            ni[i] = r.u32();
            mats[i].alpha = r.u32();
            if ((mats[i].prim != 4 && mats[i].prim != 5) || (mats[i].stride != 32 && mats[i].stride != 40))
                throw std::runtime_error("SM: unsupported primitive or stride in " + mats[i].name);
        }
        for (uint32_t i = 0; i < nmat; ++i) {
            size_t vbytes = static_cast<size_t>(nv[i]) * mats[i].stride, ibytes = static_cast<size_t>(ni[i]) * 2;
            r.need(vbytes + ibytes);
            mats[i].vertices.resize(vbytes / 4);
            std::memcpy(mats[i].vertices.data(), d.data() + r.o, vbytes);
            r.o += vbytes;
            mats[i].indices.resize(ni[i]);
            std::memcpy(mats[i].indices.data(), d.data() + r.o, ibytes);
            r.o += ibytes;
            for (uint16_t ix : mats[i].indices)
                if (ix >= nv[i]) throw std::runtime_error("SM: index out of range in " + mats[i].name);
            if (mats[i].prim == 4 && ni[i] % 3) throw std::runtime_error("SM: bad triangle list in " + mats[i].name);
        }
        m.lods.push_back(std::move(mats));
    }
    return m;
}

CollisionMesh collision_parse(const uint8_t* data, size_t size) {
    std::vector<uint8_t> copy(data, data + size);
    Reader r{copy};
    if (r.u32() != 0xEB97C2FAu || r.u32() != 5) throw std::runtime_error("collision: bad magic or version");
    CollisionMesh c;
    uint32_t nv = r.u32();
    r.need(16ull * nv);
    for (uint32_t i = 0; i < nv; ++i) {  // xyz, u16 material, 2 bytes padding
        float v[3];
        std::memcpy(v, &copy[r.o], 12);
        c.vertices.insert(c.vertices.end(), v, v + 3);
        r.o += 16;
    }
    uint32_t nf = r.u32();
    r.need(8ull * nf);
    for (uint32_t i = 0; i < nf; ++i) {
        uint16_t f[4];
        std::memcpy(f, &copy[r.o], 8);
        r.o += 8;
        if (f[0] >= nv || f[1] >= nv || f[2] >= nv) throw std::runtime_error("collision: face index out of range");
        c.faces.insert(c.faces.end(), f, f + 3);
        c.material.push_back(f[3]);
    }
    return c;
}

std::vector<uint32_t> sm_triangles(const SmMaterial& m) {
    const auto& ix = m.indices;
    if (m.prim == 4) return {ix.begin(), ix.end()};
    std::vector<uint32_t> out;
    for (size_t i = 0; i + 2 < ix.size(); ++i) {  // strip: every other triangle flips winding
        uint32_t a = ix[i], b = ix[i + 1], c = ix[i + 2];
        if (i % 2) std::swap(a, b);
        if (a != b && b != c && a != c) out.insert(out.end(), {a, b, c});  // skip degenerate joints
    }
    return out;
}

std::map<std::string, std::map<std::string, std::string>> rs_parse(const std::string& text) {
    std::map<std::string, std::map<std::string, std::string>> out;
    // Hand scan (std::regex recurses per character and overflows the stack on large unclosed blocks).
    auto quoted = [&](size_t& pos, std::string& out_s) {
        size_t a = text.find('"', pos);
        if (a == std::string::npos) return false;
        size_t b = text.find('"', a + 1);
        if (b == std::string::npos) return false;
        out_s = text.substr(a + 1, b - a - 1);
        pos = b + 1;
        return true;
    };
    for (size_t pos = 0; (pos = text.find("subshader", pos)) != std::string::npos;) {
        pos += 9;
        std::string name, shader;
        if (!quoted(pos, name) || !quoted(pos, shader)) break;
        size_t open = text.find('{', pos), close = open == std::string::npos ? open : text.find('}', open);
        if (close == std::string::npos) break;
        auto& props = out[lower(name)];
        props["shader"] = shader;
        std::istringstream body(text.substr(open + 1, close - open - 1));
        for (std::string stmt; std::getline(body, stmt, ';');) {
            std::istringstream s(stmt);
            std::string k, v;
            if (!(s >> k)) continue;
            std::getline(s, v);
            v.erase(0, v.find_first_not_of(" \t\r\n"));
            v.erase(v.find_last_not_of(" \t\r\n") + 1);
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            props[k] = v;
        }
        pos = close + 1;
    }
    return out;
}

}  // namespace obf
