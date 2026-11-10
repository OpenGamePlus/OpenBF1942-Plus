#include "dds.h"

#include <cstring>
#include <stdexcept>

namespace obf {

void dxt1_block(const uint8_t* b, uint8_t out[16][3]) {
    uint16_t c0 = static_cast<uint16_t>(b[0] | b[1] << 8), c1 = static_cast<uint16_t>(b[2] | b[3] << 8);
    int pal[4][3];
    for (int i = 0; i < 2; ++i) {
        uint16_t c = i ? c1 : c0;
        pal[i][0] = (c >> 11 & 31) * 255 / 31;
        pal[i][1] = (c >> 5 & 63) * 255 / 63;
        pal[i][2] = (c & 31) * 255 / 31;
    }
    for (int k = 0; k < 3; ++k) {
        if (c0 > c1) {
            pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
            pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
        } else {  // 3-colour mode; index 3 is transparent, drawn black
            pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
            pal[3][k] = 0;
        }
    }
    uint32_t idx = static_cast<uint32_t>(b[4] | b[5] << 8 | b[6] << 16) | static_cast<uint32_t>(b[7]) << 24;
    for (int p = 0; p < 16; ++p)
        for (int k = 0; k < 3; ++k) out[p][k] = static_cast<uint8_t>(pal[idx >> (2 * p) & 3][k]);
}

Image dds_decode(const std::vector<uint8_t>& d) {
    if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) throw std::runtime_error("not a DDS file");
    if (std::memcmp(d.data() + 84, "DXT1", 4) != 0) throw std::runtime_error("DDS: only DXT1 supported");
    Image img;
    std::memcpy(&img.h, d.data() + 12, 4);
    std::memcpy(&img.w, d.data() + 16, 4);
    if (img.w <= 0 || img.h <= 0 || img.w % 4 || img.h % 4 || img.w > 16384 || img.h > 16384)
        throw std::runtime_error("DDS: bad size");
    size_t blocks = static_cast<size_t>(img.w / 4) * static_cast<size_t>(img.h / 4);
    if (d.size() < 128 + blocks * 8) throw std::runtime_error("DDS: truncated");
    img.rgb.resize(static_cast<size_t>(img.w) * img.h * 3);
    uint8_t px[16][3];
    const uint8_t* b = d.data() + 128;
    for (int by = 0; by < img.h / 4; ++by)
        for (int bx = 0; bx < img.w / 4; ++bx, b += 8) {
            dxt1_block(b, px);
            for (int p = 0; p < 16; ++p) {
                size_t o = (static_cast<size_t>(by * 4 + p / 4) * img.w + bx * 4 + p % 4) * 3;
                std::memcpy(&img.rgb[o], px[p], 3);
            }
        }
    return img;
}

}  // namespace obf
