// Textures (public formats): DDS DXT1/DXT3/DXT5 top mip, uncompressed TGA. Output RGBA8, top row first.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace obf {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

void dxt1_block(const uint8_t* block, uint8_t out[16][4]);
Image dds_decode(const std::vector<uint8_t>& file);  // throws on unsupported formats
Image tga_decode(const std::vector<uint8_t>& file);  // type 2 (24/32 bit), type 1 (8-bit, 24/32-bit palette), type 3 (8-bit gray)

}  // namespace obf
