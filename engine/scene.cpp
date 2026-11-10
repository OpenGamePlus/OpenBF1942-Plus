#include "scene.h"

#include <cstdlib>
#include <sstream>

namespace obf {

void parse_vec3(const std::string& s, float out[3]) {
    std::istringstream in(s);
    std::string part;
    for (int i = 0; i < 3; ++i) out[i] = std::getline(in, part, '/') ? static_cast<float>(std::atof(part.c_str())) : 0.0f;
}

namespace {

struct Builder {
    const Con& con;
    const Vfs& vfs;
    std::map<std::string, int>& skipped;
    std::vector<DrawItem> out;

    const ConObject* find(const std::string& sub, const std::string& name) const {
        auto s = con.templates.find(sub);
        if (s == con.templates.end()) return nullptr;
        auto t = s->second.find(lower(name));
        return t == s->second.end() ? nullptr : &t->second;
    }

    void geometry(const std::string& name, const Mat4& world) {
        const ConObject* g = find("geometrytemplate", name);
        if (!g) return void(++skipped["missing geometry template"]);
        std::string type = lower(g->type);
        std::string ext = type == "standardmesh" ? ".sm" : type == "treemesh" ? ".tm" : "";
        if (ext.empty()) return void(++skipped[type]);
        std::string file;
        for (auto& [k, a] : g->props)
            if (k == "file" && !a.empty()) file = Vfs::key(a[0]);
        // Level-local meshes first (e.g. Coral Sea carriers), then the shared folder.
        for (const std::string& p : {con.level_dir + "/" + type + "/" + file + ext, type + "/" + file + ext})
            if (vfs.has(p)) return void(out.push_back({p, world}));
        ++skipped["missing mesh file"];
    }

    void object(const std::string& name, const Mat4& world, int depth, int team = 0) {
        if (depth > 16) return void(++skipped["template nesting too deep"]);
        const ConObject* t = find("objecttemplate", name);
        if (!t) return void(++skipped["missing object template"]);
        if (lower(t->type) == "objectspawner") {
            // setObjectTemplate <team> <template>: show the instance's team, else the first one listed.
            // shortcut: in game the owner of the nearby control point decides; the viewer has no game state.
            std::string pick;
            for (auto& [k, a] : t->props)
                if (k == "setobjecttemplate" && a.size() >= 2 && (pick.empty() || std::atoi(a[0].c_str()) == team)) pick = a[1];
            if (pick.empty()) return void(++skipped["empty spawner"]);
            return object(pick, world, depth + 1);
        }
        struct Child {
            std::string name;
            float pos[3] = {}, rot[3] = {};
        };
        std::vector<Child> children;
        for (auto& [k, a] : t->props) {
            if (a.empty()) continue;
            if (k == "geometry") geometry(a[0], world);
            else if (k == "addtemplate") children.push_back({a[0]});
            // setPosition/setRotation after addTemplate place that child relative to the parent.
            else if (k == "setposition" && !children.empty()) parse_vec3(a[0], children.back().pos);
            else if (k == "setrotation" && !children.empty()) parse_vec3(a[0], children.back().rot);
        }
        for (auto& c : children)
            object(c.name, world * translate(c.pos[0], c.pos[1], c.pos[2]) * rotation_ypr(c.rot[0], c.rot[1], c.rot[2]), depth + 1);
    }
};

}  // namespace

std::vector<DrawItem> build_scene(const Con& con, const Vfs& vfs, std::map<std::string, int>& skipped) {
    Builder b{con, vfs, skipped, {}};
    for (auto& inst : con.instances) {
        if (inst.deleted) continue;
        float pos[3] = {}, rot[3] = {}, scl[3] = {1, 1, 1};
        int team = 0;
        for (auto& [k, a] : inst.props) {
            if (a.empty()) continue;
            if (k == "setteam") team = std::atoi(a[0].c_str());
            else if (k == "absoluteposition") parse_vec3(a[0], pos);
            else if (k == "rotation") parse_vec3(a[0], rot);
            else if (k == "geometry.scale") parse_vec3(a[0], scl);
        }
        b.object(inst.name, translate(pos[0], pos[1], pos[2]) * rotation_ypr(rot[0], rot[1], rot[2]) * scale(scl[0], scl[1], scl[2]), 0, team);
    }
    return std::move(b.out);
}

}  // namespace obf
