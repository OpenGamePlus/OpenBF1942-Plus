#include "scene.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace obf {

void parse_vec3(const std::string& s, float out[3]) {
    std::istringstream in(s);
    std::string part;
    for (int i = 0; i < 3; ++i) out[i] = std::getline(in, part, '/') ? static_cast<float>(std::atof(part.c_str())) : 0.0f;
}

namespace {

// Context inherited down a template tree (outside Builder: its default member initialisers are needed for a
// default argument inside Builder).
struct Anim {
    std::string skeleton;
    std::vector<std::string> states;
    std::string root_pos;  // "x-y-z" of the placed instance, truncated: object lightmap names
    bool spawned = false;  // below an ObjectSpawner
    int spawn = -1;
    std::string bone;      // useSkeletonPartAsMain, or the bindToSkeletonPart under which the node was added
};

struct Builder {
    const Con& con;
    const Vfs& vfs;
    std::map<std::string, int>& skipped;
    const std::function<bool(const std::string&)>& hide;
    std::vector<DrawItem> out;
    long calls = 0;  // total template visits; fan-out (many addTemplate of the same child) is exponential
    const JointFn* joint = nullptr;
    std::vector<SpawnPoint>* spawns = nullptr;
    bool follow_spawners = true;
    bool wreck = false;
    std::map<std::string, int> seen;  // template visits by name: the occurrence index passed to joint

    const ConObject* find(const std::string& sub, const std::string& name) const {
        auto s = con.templates.find(sub);
        if (s == con.templates.end()) return nullptr;
        auto t = s->second.find(lower(name));
        return t == s->second.end() ? nullptr : &t->second;
    }

    void geometry(const std::string& name, const Mat4& world, const Anim& anim, const std::string& owner) {
        const ConObject* g = find("geometrytemplate", name);
        if (!g) return void(++skipped["missing geometry template"]);
        std::string type = lower(g->type);
        // AnimatedMesh: a .sm in the standardMesh folder plus a skin (setSkin).
        std::string folder = type == "animatedmesh" ? "standardmesh" : type;
        std::string ext = type == "treemesh" ? ".tm" : type == "standardmesh" || type == "animatedmesh" ? ".sm" : "";
        if (ext.empty()) return void(++skipped[type]);
        std::string file, skin;
        for (auto& [k, a] : g->props) {
            if (k == "file" && !a.empty()) file = Vfs::key(a[0]);
            if (k == "setskin" && !a.empty()) skin = Vfs::key(a[0]);
        }
        // Level-local meshes first (e.g. Coral Sea carriers), then the shared folder.
        for (const std::string& p : {con.level_dir + "/" + folder + "/" + file + ext, folder + "/" + file + ext})
            if (vfs.has(p)) {
                if (type == "animatedmesh" && (!vfs.has(skin) || !vfs.has(anim.skeleton))) skin.clear();
                std::string lm = con.level_dir + "/objectlightmaps/" + file + "_" + anim.root_pos + ".tga";
                if (type != "standardmesh" || anim.root_pos.empty() || !vfs.has(lm)) lm.clear();
                return void(out.push_back({p, world, type == "animatedmesh" ? skin : "", anim.skeleton, anim.states, lm, owner, anim.spawned, anim.spawn, anim.bone}));
            }
        ++skipped["missing mesh file"];
    }

    void object(const std::string& name, const Mat4& placed, int depth, int team = 0, Anim anim = {}) {
        if (depth > 16) return void(++skipped["template nesting too deep"]);
        if (++calls > 2000000) return void(++skipped["template budget exceeded"]);
        const ConObject* t = find("objecttemplate", name);
        if (!t) return void(++skipped["missing object template"]);
        int occurrence = seen[lower(name)]++;
        const Mat4 world = joint && *joint ? placed * (*joint)(lower(name), occurrence) : placed;
        if (lower(t->type) == "objectspawner") {
            // setObjectTemplate <team> <template>: show the instance's team, else the first one listed.
            // shortcut: in game the owner of the nearby control point decides; the viewer has no game state.
            std::string pick;
            for (auto& [k, a] : t->props)
                if (k == "setobjecttemplate" && a.size() >= 2 && (pick.empty() || std::atoi(a[0].c_str()) == team)) pick = a[1];
            if (!follow_spawners) return;
            if (pick.empty()) return void(++skipped["empty spawner"]);
            Anim spawned;
            spawned.spawned = true;
            if (spawns) spawned.spawn = static_cast<int>(spawns->size()), spawns->push_back({pick, world});
            return object(pick, world, depth + 1, 0, spawned);
        }
        for (auto& [k, a] : t->props) {
            if (k == "createskeleton" && !a.empty()) anim.skeleton = Vfs::key(a[0]);
            if (k == "useskeletonpartasmain" && !a.empty()) anim.bone = lower(a[0]);
            if (k == "setanimationstate" && !a.empty()) {  // "[index] <state>"
                size_t index = a.size() > 1 ? static_cast<size_t>(std::max(0, std::atoi(a[0].c_str()))) : 0;
                if (index < 8) {
                    if (anim.states.size() <= index) anim.states.resize(index + 1);
                    anim.states[index] = lower(a.back());
                }
            }
        }
        struct Child {
            std::string name;
            float pos[3] = {}, rot[3] = {};
            std::string bone;
        };
        bool invisible = false;
        for (auto& [k, a] : t->props)
            if (k == "createinvisible" && !a.empty() && std::atoi(a[0].c_str()) != 0) invisible = true;
        std::vector<Child> children;
        for (auto& [k, a] : t->props) {
            if (a.empty()) continue;
            // createInvisible 1: physical only (e.g. the Elco's beaching wheels).
            if (k == "geometry" && !invisible) geometry(a[0], world, anim, lower(name));
            else if (k == "addtemplate") children.push_back({a[0], {}, {}, {}});
            // setPosition/setRotation after addTemplate place that child relative to the parent.
            else if (k == "setposition" && !children.empty()) parse_vec3(a[0], children.back().pos);
            else if (k == "setrotation" && !children.empty()) parse_vec3(a[0], children.back().rot);
            else if (k == "bindtoskeletonpart" && !children.empty()) children.back().bone = lower(a[0]);
        }
        // LodObject children are alternative detail levels (complex, simple, wreck); draw the first only.
        // shortcut: no distance-based switching yet (the lodSelector templates define it).
        if (lower(t->type) == "lodobject" && children.size() > 1) {
            // A destroyed vehicle shows its last detail level when that is a wreck (complex, simple, wreck).
            bool to_wreck = wreck && children.size() >= 3 && lower(children.back().name).find("wreck") != std::string::npos;
            children.erase(to_wreck ? children.begin() : children.begin() + 1, to_wreck ? children.end() - 1 : children.end());
        }
        for (auto& c : children) {
            if (hide && hide(lower(c.name))) continue;
            Anim ca = anim;
            if (!c.bone.empty()) ca.bone = c.bone;
            object(c.name, world * translate(c.pos[0], c.pos[1], c.pos[2]) * rotation_ypr(c.rot[0], c.rot[1], c.rot[2]), depth + 1, 0, ca);
        }
    }
};

}  // namespace

std::vector<DrawItem> build_scene(const Con& con, const Vfs& vfs, std::map<std::string, int>& skipped,
                                  const std::function<bool(const std::string&)>& hide, std::vector<SpawnPoint>* spawns) {
    Builder b{con, vfs, skipped, hide, {}, 0, nullptr, nullptr, true, false, {}};
    b.spawns = spawns;
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
        // Verified on Wake: all 67 lightmap names use the instance position truncated to integers.
        Anim anim;
        anim.root_pos = std::to_string(static_cast<long>(pos[0])) + "-" + std::to_string(static_cast<long>(pos[1])) + "-" +
                        std::to_string(static_cast<long>(pos[2]));
        b.object(inst.name, translate(pos[0], pos[1], pos[2]) * rotation_ypr(rot[0], rot[1], rot[2]) * scale(scl[0], scl[1], scl[2]), 0, team, anim);
    }
    return std::move(b.out);
}

std::vector<DrawItem> build_object(const Con& con, const Vfs& vfs, const std::string& name, const Mat4& world,
                                   std::map<std::string, int>& skipped, const JointFn& joint,
                                   const std::function<bool(const std::string&)>& hide, bool wreck) {
    Builder b{con, vfs, skipped, hide, {}, 0, nullptr, nullptr, true, false, {}};
    b.joint = &joint;
    b.wreck = wreck;
    b.follow_spawners = false;
    b.object(name, world, 0);
    return std::move(b.out);
}

}  // namespace obf
