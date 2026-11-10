#include "vfs.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace obf {

std::string Vfs::key(std::string p) {
    for (auto& c : p) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return p;
}

void Vfs::mount(const std::string& mod_dir) {
    fs::path arch = fs::path(mod_dir) / "Archives";
    if (!fs::is_directory(arch)) throw std::runtime_error("no Archives folder in " + mod_dir);
    std::vector<std::string> archives;
    for (auto& e : fs::recursive_directory_iterator(arch))
        if (e.is_regular_file() && key(e.path().extension().string()) == ".rfa") archives.push_back(e.path().string());
    // "x.rfa" sorts before "x_001.rfa", so patches mount after their base.
    std::sort(archives.begin(), archives.end(), [](const std::string& a, const std::string& b) { return key(a) < key(b); });
    // shortcut: loose files in the mod folder are not mounted; only archives. Add when a mod needs it.
    for (auto& a : archives)
        for (auto& [name, entry] : rfa_index(a)) files_[key(name)] = entry;
}

void Vfs::mount_mod(const std::string& game_dir, const std::string& mod) {
    fs::path init = fs::path(game_dir) / "Mods" / mod / "init.con";
    std::ifstream f(init);
    if (!f) throw std::runtime_error("cannot open " + init.string());
    std::vector<std::string> paths;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string cmd, arg;
        if (in >> cmd >> arg && key(cmd) == "game.addmodpath") paths.push_back(arg);
    }
    if (paths.empty()) paths.push_back("Mods/" + mod + "/");
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
        // Mod paths are written "Mods/BF1942/" but folders may differ in case on Linux.
        fs::path p = fs::path(game_dir);
        std::istringstream parts(*it);
        std::string part;
        while (std::getline(parts, part, '/')) {
            if (part.empty()) continue;
            fs::path next = p / part;
            if (!fs::exists(next) && fs::is_directory(p))
                for (auto& e : fs::directory_iterator(p))
                    if (key(e.path().filename().string()) == key(part)) next = e.path();
            p = next;
        }
        mount(p.string());
    }
}

std::vector<std::string> Vfs::paths(const std::string& prefix) const {
    std::vector<std::string> out;
    for (auto& [k, e] : files_)
        if (k.compare(0, prefix.size(), prefix) == 0 && !mem_.count(k)) out.push_back(k);
    for (auto& [k, d] : mem_)
        if (k.compare(0, prefix.size(), prefix) == 0) out.push_back(k);
    return out;
}

void Vfs::add(const std::string& path, std::vector<uint8_t> data) { mem_[key(path)] = std::move(data); }

bool Vfs::has(const std::string& path) const {
    std::string k = key(path);
    return mem_.count(k) || files_.count(k);
}

std::vector<uint8_t> Vfs::read(const std::string& path) const {
    auto m = mem_.find(key(path));
    if (m != mem_.end()) return m->second;
    auto it = files_.find(key(path));
    if (it == files_.end()) throw std::runtime_error("not found in VFS: " + path);
    return rfa_read(it->second);
}

}  // namespace obf
