#include "dds.h"

#include <cstring>
#include <stdexcept>

namespace obf {

static void colour_block(const uint8_t* b, uint8_t out[16][4], bool allow_transparent) {
    uint16_t c0 = static_cast<uint16_t>(b[0] | b[1] << 8), c1 = static_cast<uint16_t>(b[2] | b[3] << 8);
    int pal[4][4];
    for (int i = 0; i < 2; ++i) {
        uint16_t c = i ? c1 : c0;
        pal[i][0] = (c >> 11 & 31) * 255 / 31;
        pal[i][1] = (c >> 5 & 63) * 255 / 63;
        pal[i][2] = (c & 31) * 255 / 31;
        pal[i][3] = 255;
    }
    // DXT3/5 colour blocks always use 4-colour mode.
    bool four = c0 > c1 || !allow_transparent;
    for (int k = 0; k < 3; ++k) {
        pal[2][k] = four ? (2 * pal[0][k] + pal[1][k]) / 3 : (pal[0][k] + pal[1][k]) / 2;
        pal[3][k] = four ? (pal[0][k] + 2 * pal[1][k]) / 3 : 0;
    }
    pal[2][3] = 255;
    pal[3][3] = four ? 255 : 0;
    uint32_t idx = static_cast<uint32_t>(b[4] | b[5] << 8 | b[6] << 16) | static_cast<uint32_t>(b[7]) << 24;
    for (int p = 0; p < 16; ++p)
        for (int k = 0; k < 4; ++k) out[p][k] = static_cast<uint8_t>(pal[idx >> (2 * p) & 3][k]);
}

void dxt1_block(const uint8_t* b, uint8_t out[16][4]) { colour_block(b, out, true); }

static void dxt3_alpha(const uint8_t* a, uint8_t out[16][4]) {
    for (int p = 0; p < 16; ++p) out[p][3] = static_cast<uint8_t>((a[p / 2] >> (4 * (p % 2)) & 15) * 17);
}

static void dxt5_alpha(const uint8_t* a, uint8_t out[16][4]) {
    int pal[8] = {a[0], a[1]};
    for (int i = 2; i < 8; ++i)
        pal[i] = a[0] > a[1] ? ((8 - i) * a[0] + (i - 1) * a[1]) / 7 : i < 6 ? ((6 - i) * a[0] + (i - 1) * a[1]) / 5 : i == 6 ? 0 : 255;
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(a[2 + i]) << (8 * i);
    for (int p = 0; p < 16; ++p) out[p][3] = static_cast<uint8_t>(pal[bits >> (3 * p) & 7]);
}

Image dds_decode(const std::vector<uint8_t>& d) {
    if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) throw std::runtime_error("not a DDS file");
    int fmt = !std::memcmp(d.data() + 84, "DXT1", 4) ? 1 : !std::memcmp(d.data() + 84, "DXT3", 4) ? 3
              : !std::memcmp(d.data() + 84, "DXT5", 4) ? 5 : 0;
    if (!fmt) throw std::runtime_error("DDS: only DXT1/3/5 supported");
    Image img;
    std::memcpy(&img.h, d.data() + 12, 4);
    std::memcpy(&img.w, d.data() + 16, 4);
    if (img.w <= 0 || img.h <= 0 || img.w > 16384 || img.h > 16384) throw std::runtime_error("DDS: bad size");
    int bw = (img.w + 3) / 4, bh = (img.h + 3) / 4;  // tiny mips can be smaller than a block
    size_t bsize = fmt == 1 ? 8 : 16;
    if (d.size() < 128 + static_cast<size_t>(bw) * bh * bsize) throw std::runtime_error("DDS: truncated");
    img.rgba.resize(static_cast<size_t>(img.w) * img.h * 4);
    uint8_t px[16][4];
    const uint8_t* b = d.data() + 128;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx, b += bsize) {
            colour_block(fmt == 1 ? b : b + 8, px, fmt == 1);
            if (fmt == 3) dxt3_alpha(b, px);
            if (fmt == 5) dxt5_alpha(b, px);
            for (int p = 0; p < 16; ++p) {
                int x = bx * 4 + p % 4, y = by * 4 + p / 4;
                if (x < img.w && y < img.h) std::memcpy(&img.rgba[(static_cast<size_t>(y) * img.w + x) * 4], px[p], 4);
            }
        }
    return img;
}

Image tga_decode(const std::vector<uint8_t>& d) {
    if (d.size() < 18) throw std::runtime_error("TGA: truncated header");
    Image img;
    img.w = d[12] | d[13] << 8;
    img.h = d[14] | d[15] << 8;
    bool top_first = d[17] & 0x20;
    if (d[2] == 1) {  // colour-mapped: 8-bit indices into an embedded palette (object lightmaps)
        size_t first = static_cast<size_t>(d[3] | d[4] << 8), count = static_cast<size_t>(d[5] | d[6] << 8), ebits = d[7];
        if (d[1] != 1 || d[16] != 8 || (ebits != 24 && ebits != 32)) throw std::runtime_error("TGA: unsupported colour map");
        size_t pal = 18u + d[0], eb = ebits / 8, start = pal + count * eb;
        if (d.size() < start + static_cast<size_t>(img.w) * img.h) throw std::runtime_error("TGA: truncated");
        img.rgba.resize(static_cast<size_t>(img.w) * img.h * 4);
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x) {
                size_t i = d[start + static_cast<size_t>(top_first ? y : img.h - 1 - y) * img.w + x];
                uint8_t* o = &img.rgba[(static_cast<size_t>(y) * img.w + x) * 4];
                if (i < first || i - first >= count) {
                    o[0] = o[1] = o[2] = 0, o[3] = 255;
                    continue;
                }
                const uint8_t* c = &d[pal + (i - first) * eb];
                o[0] = c[2], o[1] = c[1], o[2] = c[0], o[3] = eb == 4 ? c[3] : 255;  // stored BGR(A)
            }
        return img;
    }
    if (d[2] != 2 || (d[16] != 24 && d[16] != 32)) throw std::runtime_error("TGA: only uncompressed 24/32-bit or 8-bit mapped");
    size_t bpp = d[16] / 8, start = 18u + d[0];
    if (d.size() < start + static_cast<size_t>(img.w) * img.h * bpp) throw std::runtime_error("TGA: truncated");
    img.rgba.resize(static_cast<size_t>(img.w) * img.h * 4);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const uint8_t* s = &d[start + (static_cast<size_t>(top_first ? y : img.h - 1 - y) * img.w + x) * bpp];
            uint8_t* o = &img.rgba[(static_cast<size_t>(y) * img.w + x) * 4];
            o[0] = s[2], o[1] = s[1], o[2] = s[0], o[3] = bpp == 4 ? s[3] : 255;  // stored BGR(A)
        }
    return img;
}

}  // namespace obf
