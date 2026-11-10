// RFA archives and LZO1X. Spec: docs/formats/rfa.md; reference: tools/rfa.py
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace obf {

// Throws std::runtime_error on malformed input or if the output is not exactly out_len bytes.
std::vector<uint8_t> lzo1x_decompress(const uint8_t* src, size_t src_len, size_t out_len);

struct RfaEntry {
    std::string archive;
    bool compressed;
    uint32_t stored, size, offset;
};

// Reads only the directory. Names are returned as stored in the archive.
std::vector<std::pair<std::string, RfaEntry>> rfa_index(const std::string& path);
std::vector<uint8_t> rfa_read(const RfaEntry& e);

}  // namespace obf
