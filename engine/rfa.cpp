#include "rfa.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace obf {

std::vector<uint8_t> lzo1x_decompress(const uint8_t* src, size_t n, size_t out_len) {
    std::vector<uint8_t> out;
    out.reserve(out_len);
    size_t ip = 0;
    size_t state = 0;  // 0: literal run allowed; 1-3: trailing literals copied; 4: long literal run copied

    auto byte = [&]() -> size_t {
        if (ip >= n) throw std::runtime_error("LZO: input overrun");
        return src[ip++];
    };
    auto literals = [&](size_t t) {
        if (ip + t > n || out.size() + t > out_len) throw std::runtime_error("LZO: literal overrun");
        out.insert(out.end(), src + ip, src + ip + t);
        ip += t;
    };
    auto ext_len = [&](size_t base) {  // each zero byte adds 255, the final byte is added
        size_t b;
        while ((b = byte()) == 0) base += 255;
        return base + b;
    };
    auto match = [&](size_t dist, size_t len) {
        if (dist > out.size() || out.size() + len > out_len) throw std::runtime_error("LZO: bad match");
        size_t s = out.size() - dist;
        for (size_t i = 0; i < len; ++i) {  // byte by byte: matches may overlap their own output
            uint8_t v = out[s + i];
            out.push_back(v);
        }
    };

    if (n == 0) throw std::runtime_error("LZO: empty input");
    if (src[0] > 17) {
        ip = 1;
        size_t t = src[0] - 17u;
        literals(t);
        state = t >= 4 ? 4 : t;
    }
    for (;;) {
        size_t t = byte(), dist, len, nxt;
        if (t < 16) {
            if (state == 0) {
                if (t == 0) t = ext_len(15);
                literals(t + 3);
                state = 4;
                continue;
            }
            size_t b = byte();
            if (state == 4) {
                dist = 1 + 0x800 + (t >> 2) + (b << 2);
                len = 3;
            } else {
                dist = 1 + (t >> 2) + (b << 2);
                len = 2;
            }
            nxt = t & 3;
        } else if (t >= 64) {
            dist = 1 + ((t >> 2) & 7) + (byte() << 3);
            len = (t >> 5) + 1;
            nxt = t & 3;
        } else if (t >= 32) {
            len = t & 31;
            if (len == 0) len = ext_len(31);
            len += 2;
            size_t d = byte();
            d |= byte() << 8;
            dist = (d >> 2) + 1;
            nxt = d & 3;
        } else {  // 16..31: M4 match or end-of-stream marker
            len = t & 7;
            if (len == 0) len = ext_len(7);
            len += 2;
            size_t d = byte();
            d |= byte() << 8;
            dist = 0x4000 + ((t & 8) << 11) + (d >> 2);
            if (dist == 0x4000) break;
            nxt = d & 3;
        }
        match(dist, len);
        literals(nxt);
        state = nxt;
    }
    if (out.size() != out_len) throw std::runtime_error("LZO: wrong output size");
    return out;
}

static uint32_t u32(const std::vector<uint8_t>& b, size_t o) {
    if (o + 4 > b.size()) throw std::runtime_error("RFA: truncated");
    uint32_t v;
    std::memcpy(&v, b.data() + o, 4);  // little-endian hosts only
    return v;
}

std::vector<std::pair<std::string, RfaEntry>> rfa_index(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::vector<uint8_t> head(8);
    f.read(reinterpret_cast<char*>(head.data()), 8);
    uint32_t dir_off = u32(head, 0);
    bool compressed = u32(head, 4) != 0;
    f.seekg(0, std::ios::end);
    size_t file_size = static_cast<size_t>(f.tellg());
    if (dir_off > file_size) throw std::runtime_error("RFA: bad directory offset in " + path);
    std::vector<uint8_t> dir(file_size - dir_off);
    f.seekg(dir_off);
    f.read(reinterpret_cast<char*>(dir.data()), static_cast<std::streamsize>(dir.size()));

    std::vector<std::pair<std::string, RfaEntry>> out;
    uint32_t count = u32(dir, 0);
    size_t o = 4;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t len = u32(dir, o);
        if (o + 4 + len > dir.size()) throw std::runtime_error("RFA: truncated name");
        std::string name(reinterpret_cast<const char*>(dir.data() + o + 4), len);
        o += 4 + len;
        out.push_back({name, {path, compressed, u32(dir, o), u32(dir, o + 4), u32(dir, o + 8)}});
        o += 24;
    }
    return out;
}

std::vector<uint8_t> rfa_read(const RfaEntry& e) {
    std::ifstream f(e.archive, std::ios::binary);
    std::vector<uint8_t> raw(e.compressed ? e.stored : e.size);
    f.seekg(e.offset);
    f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (static_cast<size_t>(f.gcount()) != raw.size()) throw std::runtime_error("RFA: truncated file data");
    if (!e.compressed) return raw;

    uint32_t nseg = u32(raw, 0);
    size_t base = 4 + 12ull * nseg;
    std::vector<uint8_t> out;
    out.reserve(e.size);
    for (uint32_t i = 0; i < nseg; ++i) {
        uint32_t cs = u32(raw, 4 + 12 * i), us = u32(raw, 8 + 12 * i), rel = u32(raw, 12 + 12 * i);
        if (base + rel + cs > raw.size()) throw std::runtime_error("RFA: segment out of range");
        auto seg = lzo1x_decompress(raw.data() + base + rel, cs, us);
        out.insert(out.end(), seg.begin(), seg.end());
    }
    if (out.size() != e.size) throw std::runtime_error("RFA: size mismatch");
    return out;
}

}  // namespace obf
