// DDS textures (public format). Only DXT1, top mip level; GL builds the rest.
#pragma once
#include <cstdint>
#include <vector>

namespace obf {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
};

void dxt1_block(const uint8_t* block, uint8_t out[16][3]);
Image dds_decode(const std::vector<uint8_t>& file);  // throws on anything but DXT1

}  // namespace obf
