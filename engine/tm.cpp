#include "tm.h"

#include <cstring>
#include <stdexcept>

namespace obf {

static uint32_t u32_at(const std::vector<uint8_t>& d, size_t o) {
    if (o + 4 > d.size()) throw std::runtime_error("TM: truncated");
    uint32_t v;
    std::memcpy(&v, &d[o], 4);  // little-endian hosts only
    return v;
}

TmMesh tm_parse(const std::vector<uint8_t>& d) {
    if (u32_at(d, 0) != 3) throw std::runtime_error("TM: unsupported version");
    TmMesh m;
    size_t o = 0x3c;
    size_t ni = 0;
    for (int g = 0; g < 3; ++g) {
        uint32_t n = u32_at(d, o);
        o += 4;
        if (n > 1024) throw std::runtime_error("TM: implausible group size");
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t first = u32_at(d, o), count = u32_at(d, o + 4), len = u32_at(d, o + 8);
            if (o + 12 + len > d.size()) throw std::runtime_error("TM: truncated name");
            m.groups[g].push_back({first, count, std::string(reinterpret_cast<const char*>(&d[o + 12]), len)});
            o += 12 + len;
            ni += static_cast<size_t>(count) * kTmIndicesPer[g];
        }
    }
    // Geometry is the tail of the file; the collision block before it has no size field, so anchor at the end.
    const size_t vsize = 44;
    if (d.size() < o + 2 * ni + 4) throw std::runtime_error("TM: too small for its index count");
    size_t vend = d.size() - 2 * ni - 4;
    if (u32_at(d, vend) != ni) throw std::runtime_error("TM: index count does not match material groups");
    size_t nv = 0;
    for (size_t n = 1; vend >= o + vsize * n + 4; ++n)
        if (u32_at(d, vend - vsize * n - 4) == n) {
            nv = n;
            break;
        }
    if (!nv) throw std::runtime_error("TM: vertex block not found");
    size_t vs = vend - vsize * nv;
    m.vertices.reserve(nv * 10);
    for (size_t i = 0; i < nv; ++i) {
        float f[11];
        std::memcpy(f, &d[vs + vsize * i], vsize);
        m.vertices.insert(m.vertices.end(), {f[0], f[1], f[2], f[3], f[4], f[5], f[7], f[8], f[9], f[10]});  // f[6]: colour
    }
    m.indices.resize(ni);
    std::memcpy(m.indices.data(), &d[vend + 4], 2 * ni);
    for (uint16_t ix : m.indices)
        if (ix >= nv) throw std::runtime_error("TM: index out of range");
    return m;
}

}  // namespace obf
