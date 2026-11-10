// Virtual file system over a mod stack. Same rules as tools/con.py Vfs.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "rfa.h"

namespace obf {

class Vfs {
public:
    // Mounts every archive in Mods/<mod>/init.con's addModPath list (first = highest priority).
    void mount_mod(const std::string& game_dir, const std::string& mod);
    // Later mounts override earlier ones; patches (x_001.rfa) override their base archive.
    void mount(const std::string& mod_dir);
    bool has(const std::string& path) const;
    std::vector<uint8_t> read(const std::string& path) const;
    size_t size() const { return files_.size(); }

    static std::string key(std::string path);  // lower case, '/' separators

private:
    std::unordered_map<std::string, RfaEntry> files_;
};

}  // namespace obf
