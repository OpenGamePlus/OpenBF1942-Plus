#include "con.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace obf {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// posixpath.normpath on a relative, lower-case path: drops "." and resolves "..".
static std::string normpath(const std::string& p) {
    std::vector<std::string> parts;
    std::istringstream in(p);
    std::string s;
    while (std::getline(in, s, '/')) {
        if (s.empty() || s == ".") continue;
        if (s == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(s);
        }
    }
    std::string out;
    for (auto& x : parts) out += (out.empty() ? "" : "/") + x;
    return out;
}

static std::string dirname(const std::string& p) {
    auto i = p.rfind('/');
    return i == std::string::npos ? "" : p.substr(0, i);
}

std::string Con::resolve(const std::string& target, const std::string& cur_dir) const {
    // Verified on retail data: script folder, then level folder, then mod root.
    std::string t = Vfs::key(target);
    for (const std::string& base : {cur_dir, level_dir, std::string()}) {
        std::string p = normpath(base.empty() ? t : base + "/" + t);
        if (vfs_.has(p)) return p;
        if (vfs_.has(p + ".con")) return p + ".con";
    }
    return "";
}

void Con::run(const std::string& target, const std::vector<std::string>& args, const std::string& cur_dir) {
    std::string path = resolve(target, cur_dir);
    if (path.empty()) {
        ++missing[lower(target)];  // retail scripts reference optional/missing files
        return;
    }
    if (depth_ >= 64) {
        ++unknown["run nesting too deep: " + path];
        return;
    }
    // A run without arguments keeps the caller's (unverified): Conquest/ControlPoints.con is run bare from the game
    // mode script and checks "if v_arg1 == host" again; control points exist in the retail game.
    std::vector<std::string> outer = args_, use = args.empty() ? args_ : args;
    Scope scope;
    for (size_t i = 0; i < use.size(); ++i) scope["v_arg" + std::to_string(i + 1)] = use[i];
    auto bytes = vfs_.read(path);
    ++depth_;
    args_ = use;
    try {
        execute(std::string(bytes.begin(), bytes.end()), scope, dirname(path));
    } catch (...) {
        --depth_, args_ = outer;
        throw;
    }
    --depth_, args_ = outer;
}

void Con::execute(const std::string& text, Scope& scope, const std::string& cur_dir) {
    bool in_rem = false;
    std::vector<int> conds;  // per open if: 1 taking, 0 skipping, -1 parent inactive
    auto all_true = [&] { return std::all_of(conds.begin(), conds.end(), [](int c) { return c == 1; }); };
    std::string norm = text;  // old-Mac "\r" endings count as line breaks, as in Python's splitlines()
    for (auto& c : norm)
        if (c == '\r') c = '\n';
    std::istringstream lines(norm);
    std::string raw;
    while (std::getline(lines, raw)) {
        std::istringstream ws(raw);
        std::vector<std::string> tok;
        for (std::string t; ws >> t;) tok.push_back(t);
        if (tok.empty()) continue;
        std::string kw = lower(tok[0]);
        if (in_rem) {
            in_rem = kw != "endrem";
            continue;
        }
        if (kw == "beginrem") {
            in_rem = true;
            continue;
        }
        if (kw.rfind("rem", 0) == 0) continue;  // also "rem****" and commented-out "remObject.x"
        if (kw == "if") {
            conds.push_back(all_true() ? (test({tok.begin() + 1, tok.end()}, scope) ? 1 : 0) : -1);
            continue;
        }
        if (kw == "else") {
            if (!conds.empty() && conds.back() != -1) conds.back() = 1 - conds.back();
            continue;
        }
        if (kw == "endif") {
            if (!conds.empty()) conds.pop_back();
            continue;
        }
        if (!all_true()) continue;
        std::string dest;
        auto arrow = std::find(tok.begin(), tok.end(), "->");
        if (arrow != tok.end()) {  // split before substitution: the target is a name, not a value
            if (arrow + 1 != tok.end()) dest = lower(*(arrow + 1));
            tok.erase(arrow, tok.end());
        }
        for (auto& t : tok) {
            auto it = scope.find(lower(t));
            if (it != scope.end()) t = it->second;
        }
        std::string result;
        if (kw == "var") {
            for (size_t i = 1; i < tok.size(); ++i) scope.emplace(lower(tok[i]), "");
        } else if (kw == "echo") {
            for (size_t i = 1; i < tok.size(); ++i) result += (i > 1 ? " " : "") + tok[i];
        } else if ((kw == "run" || kw == "include") && tok.size() > 1) {
            run(tok[1], {tok.begin() + 2, tok.end()}, cur_dir);
        } else if (kw.find('.') != std::string::npos) {
            command(kw, {tok.begin() + 1, tok.end()});
        } else {
            ++unknown[kw];
        }
        if (!dest.empty()) scope[dest] = result;
    }
}

bool Con::test(const std::vector<std::string>& tok, const Scope& scope) {
    std::vector<std::string> t;
    for (auto& s : tok) {
        auto it = scope.find(lower(s));
        t.push_back(lower(it != scope.end() ? it->second : s));
    }
    if (t.size() == 3 && (t[1] == "==" || t[1] == "!=")) return (t[0] == t[2]) == (t[1] == "==");
    std::string joined = "if";
    for (auto& s : t) joined += " " + s;
    ++unknown[joined];
    return false;
}

void Con::command(const std::string& name, const std::vector<std::string>& args) {
    auto dot = name.find('.');
    std::string sub = name.substr(0, dot), method = name.substr(dot + 1);
    if (method == "create") {
        ConObject* obj;
        if (sub == "object") {
            instances.push_back({"", args.empty() ? "" : args[0], {}});
            obj = &instances.back();
        } else {
            std::string type = args.size() >= 2 ? args[0] : "", nm = args.size() >= 2 ? args[1] : args.empty() ? "" : args[0];
            obj = &(templates[sub][lower(nm)] = ConObject{type, nm, {}});
        }
        active_[sub] = obj;
    } else if (method == "active") {
        auto& m = templates[sub];
        auto it = args.empty() ? m.end() : m.find(lower(args[0]));
        active_[sub] = it == m.end() ? nullptr : &it->second;
    } else if (sub == "object" && method == "delete" && active_[sub]) {
        active_[sub]->deleted = true;
        active_[sub] = nullptr;
    } else if (active_[sub]) {
        active_[sub]->props.push_back({method, args});
    } else {
        settings[sub].push_back({method, args});
    }
}

// shortcut: order is a guess (geometry before templates), same as tools/con.py.
static const char* kObjectFiles[] = {"geometries.con", "network.con", "physics.con", "weapons.con",
                                     "effects.con",    "objects.con", "object.con"};

void Con::load_game() {
    run("bf1942/game/init.con");
    std::map<std::string, std::set<std::string>> dirs;
    for (auto& p : vfs_.paths("objects/")) dirs[dirname(p)].insert(p.substr(p.rfind('/') + 1));
    for (auto& [d, files] : dirs)
        for (const char* f : kObjectFiles)
            if (files.count(f)) run(d + "/" + f);
}

void Con::load_level(const std::string& level, const std::string& mode) {
    // shortcut: engine-side level sequence is assumed (Init, StaticObjects, game mode as host), unverified.
    level_dir = "bf1942/levels/" + lower(level);
    run(level_dir + "/init.con");
    run(level_dir + "/staticobjects.con");
    std::string gm = level_dir + "/gametypes/" + lower(mode);
    run(resolve(gm, "").empty() ? level_dir + "/" + lower(mode) + ".con" : gm + ".con", {"host"});
}

}  // namespace obf
