// CON script interpreter. Spec: docs/formats/con.md; reference: tools/con.py (same model and rules).
#pragma once
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vfs.h"

namespace obf {

struct ConObject {
    std::string type, name;  // templates: create <type> <name>; instances: name = template
    std::vector<std::pair<std::string, std::vector<std::string>>> props;  // method lower-cased
    bool deleted = false;
};

class Con {
public:
    using Scope = std::unordered_map<std::string, std::string>;
    explicit Con(const Vfs& vfs) : vfs_(vfs) {}

    // Engine-side loading (docs/formats/con.md "What the engine loads by itself").
    void load_game();  // bf1942/game/init.con + every objects/** folder
    void load_level(const std::string& level, const std::string& mode = "conquest");

    void run(const std::string& target, const std::vector<std::string>& args = {}, const std::string& cur_dir = "");
    void execute(const std::string& text, Scope& scope, const std::string& cur_dir = "");
    std::string resolve(const std::string& target, const std::string& cur_dir) const;  // "" if missing

    // subsystem -> lower name -> template; node-based map, so references stay valid.
    std::unordered_map<std::string, std::unordered_map<std::string, ConObject>> templates;
    std::deque<ConObject> instances;  // deque: stable references while appending
    // Commands with no active object (renderer.*, textureManager.*): subsystem -> [(method, args)].
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::vector<std::string>>>> settings;
    std::map<std::string, int> unknown, missing;
    std::string level_dir;

private:
    void command(const std::string& name, const std::vector<std::string>& args);
    bool test(const std::vector<std::string>& tok, const Scope& scope);
    const Vfs& vfs_;
    std::unordered_map<std::string, ConObject*> active_;
    int depth_ = 0;
    std::vector<std::string> args_;  // arguments of the script running now  // run/include nesting; cycles in mod scripts would otherwise overflow the stack
};

std::string lower(std::string s);

}  // namespace obf
