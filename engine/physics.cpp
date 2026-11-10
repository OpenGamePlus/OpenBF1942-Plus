#include "physics.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <array>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>

namespace obf {

using namespace JPH;

namespace {

constexpr float kGravity = 9.81f, kStep = 1.0f / 60.0f;
constexpr ObjectLayer kStatic = 0, kMoving = 1;

struct BpLayers final : BroadPhaseLayerInterface {
    uint GetNumBroadPhaseLayers() const override { return 2; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override { return BroadPhaseLayer(static_cast<uint8>(l)); }
};
struct ObjVsBp final : ObjectVsBroadPhaseLayerFilter {
    bool ShouldCollide(ObjectLayer a, BroadPhaseLayer b) const override { return a == kMoving || b.GetValue() == kMoving; }
};
struct ObjPair final : ObjectLayerPairFilter {
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override { return a == kMoving || b == kMoving; }
};

void jolt_init() {
    static std::once_flag once;
    std::call_once(once, [] {  // process-wide, never torn down
        RegisterDefaultAllocator();
        Factory::sInstance = new Factory();
        RegisterTypes();
    });
}

void xform(const Mat4& m, float x, float y, float z, float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = m.m[i] * x + m.m[4 + i] * y + m.m[8 + i] * z + m.m[12 + i];
}

const ConObject* find(const Con& con, const std::string& name) {
    auto s = con.templates.find("objecttemplate");
    if (s == con.templates.end()) return nullptr;
    auto t = s->second.find(lower(name));
    return t == s->second.end() ? nullptr : &t->second;
}

float num(const std::vector<std::string>& a, size_t i = 0) {
    float v = i < a.size() ? static_cast<float>(std::atof(a[i].c_str())) : 0.0f;
    return std::isfinite(v) ? v : 0.0f;
}

struct Walker {
    const Con& con;
    VehicleDef& def;
    std::vector<std::string> owner;  // spring template of each wheel
    std::set<std::string> gear;      // running gear: a moving bundle holding springs, and everything below it
    int visits = 0;
    std::map<std::string, int> seen;  // visits by name: joint keys, counted as scene.cpp's build_object does

    static JointDef::Input input(const std::string& s) {
        std::string in = lower(s);
        return in == "c_pipitch" ? JointDef::Pitch : in == "c_piroll" ? JointDef::Roll : in == "c_piyaw" ? JointDef::Yaw
             : in == "c_pithrottle" ? JointDef::Throttle : in == "c_pimouselookx" ? JointDef::LookX
             : in == "c_pimouselooky" ? JointDef::LookY : JointDef::None;
    }

    void wing(const ConObject& t, const Mat4& world) {
        WingDef w;
        float off[3] = {}, mn[3] = {}, mx[3] = {}, speed[3] = {}, acc[3] = {};
        for (auto& [k, a] : t.props) {
            if (a.empty()) continue;
            if (k == "setwinglift") w.lift = num(a);
            else if (k == "setflaplift") w.flap = num(a);
            else if (k == "setregulatetolift") w.regulate = num(a);
            else if (k == "setpositionoffset") parse_vec3(a[0], off);
            else if (k == "setminrotation") parse_vec3(a[0], mn);
            else if (k == "setmaxrotation") parse_vec3(a[0], mx);
            else if (k == "setmaxspeed") parse_vec3(a[0], speed);
            else if (k == "setacceleration") parse_vec3(a[0], acc);
            else if (k == "setinputtopitch") {
                std::string in = lower(a[0]);
                w.input = in == "c_pipitch" ? 1 : in == "c_piroll" ? 2 : in == "c_piyaw" ? 3 : 0;
            }
        }
        float range = std::max(std::fabs(mn[1]), std::fabs(mx[1]));
        if (range > 0 && speed[1] > 0) w.rate = speed[1] / range;
        if (acc[1] < 0) w.sign = -1;
        w.range = range;
        xform(world, 0, 0, 0, w.pos);
        for (int i = 0; i < 3; ++i) w.pos[i] += off[i];  // offset in vehicle axes: main wings use it to act at the centre
        // Normal without its forward part: sweep + dihedral tilt it ~0.5 deg back, which would act as built-in incidence
        // (excess lift ahead of the centre: a hands-off Mustang loops slowly).
        float n[3] = {world.m[4], world.m[5], 0.0f}, l = std::sqrt(n[0] * n[0] + n[1] * n[1]);
        if (std::isfinite(l) && l > 1e-6f)
            for (int i = 0; i < 3; ++i) w.normal[i] = n[i] / l;
        def.wings.push_back(w);
    }

    void walk(const std::string& name, const Mat4& world, int depth, WheelDef ctx, bool in_gear = false, bool prop = false) {
        const ConObject* t = find(con, name);
        if (!t || depth > 16 || ++visits > 10000) return;
        std::string type = lower(t->type);
        std::pair<std::string, int> key{lower(name), seen[lower(name)]++};
        if (prop) def.joints[key].propeller = true;
        if (type == "spring") def.joints[key].wheel = static_cast<int>(def.wheels.size());
        if (type == "wing") def.joints[key].wing = static_cast<int>(def.wings.size());
        if (type == "rotationalbundle") {
            JointDef j;
            float acc[3] = {};
            for (auto& [k, a] : t->props) {
                if (a.empty()) continue;
                if (k == "setinputtoyaw") j.input[0] = input(a[0]);
                else if (k == "setinputtopitch") j.input[1] = input(a[0]);
                else if (k == "setinputtoroll") j.input[2] = input(a[0]);
                else if (k == "setminrotation") parse_vec3(a[0], j.mn);
                else if (k == "setmaxrotation") parse_vec3(a[0], j.mx);
                else if (k == "setmaxspeed") parse_vec3(a[0], j.speed);
                else if (k == "setacceleration") parse_vec3(a[0], acc);
                else if (k == "setautomaticreset") j.reset = num(a) != 0;
            }
            for (int i = 0; i < 3; ++i) j.sign[i] = acc[i] < 0 ? -1.0f : 1.0f;
            if (j.input[0] || j.input[1] || j.input[2]) def.joints[key] = j;
        }
        bool yaw_input = false;
        // Engine kinds: c_ETCar / c_ETTank drive wheels; c_ETPlane / c_ETShip push the hull (propellers). Amphibians
        // (Elco, LVT, Schwimmwagen) carry both kinds.
        std::string etype;
        for (auto& [k, a] : t->props)
            if (type == "engine" && k == "setenginetype" && !a.empty()) etype = lower(a[0]);
        bool propeller = etype == "c_etplane" || etype == "c_etship";
        if (etype == "c_etplane") def.plane = true;
        if (etype == "c_etship") def.ship = true;
        if (type == "engine" && !propeller) ctx.driven = true;  // aircraft wheels roll freely
        if (propeller) {  // multi-engine aircraft and ships: thrust adds up
            float tq = 0;
            for (auto& [k, a] : t->props)
                if (!a.empty() && k == "settorque") tq = num(a);
            // shortcut: thrust = sum of setTorque / 4 m/s^2, differential ignored. Single-engine fighters say 15 x 5,
            // twins 2 x 7.5 x 2.1, the B-17 4 x 2.6 x 1.9: summed torque matches the fighters' (B-17 a bit lower), the product does not.
            def.thrust += tq / 4.0f;
        }
        if (type == "wing") wing(*t, world);
        if (type == "firearms" && def.weapon.name.empty()) def.weapon = weapon_def(con, name);
        if (type == "floatingbundle") {
            FloatDef f;
            xform(world, 0, 0, 0, f.pos);
            for (auto& [k, a] : t->props)
                if (!a.empty() && k == "sethullheight") f.height = num(a);
                else if (!a.empty() && k == "setfloatmaxlift") f.lift = num(a);
            if (f.height > 0) def.floats.push_back(f);
        }
        float max_yaw = 0, rate = 0;
        for (auto& [k, a] : t->props) {
            if (a.empty()) continue;
            if (depth == 0 && k == "mass") def.mass = num(a);
            if (depth == 0 && k == "drag") def.drag = num(a);
            if (depth == 0 && k == "setsoldierexitlocation") parse_vec3(a[0], def.exit);
            if (depth == 0 && k == "hitpoints") def.hitpoints = num(a);
            if (depth == 0 && k == "material") def.material = std::atoi(a[0].c_str());
            if (type == "engine" && !propeller && k == "settorque") def.torque = num(a);
            if (type == "engine" && !propeller && k == "setdifferential") def.differential = num(a);
            if ((type == "rotationalbundle" || type == "engine") && k == "setinputtoyaw" && lower(a[0]) == "c_piyaw") yaw_input = true;
            if (type == "engine" && k == "setenginetype" && lower(a[0]) == "c_ettank") def.tracked = true;
            if (k == "setmaxrotation" || k == "setmaxspeed") {
                float v[3];
                parse_vec3(a[0], v);
                (k == "setmaxrotation" ? max_yaw : rate) = std::fabs(v[0]);
            }
            if (type == "spring" && k == "setstrength") ctx.strength = num(a);
            if (type == "spring" && k == "setdamping") ctx.damping = num(a);
        }
        // shortcut: tank turn rate = engine yaw setMaxSpeed x 8 deg/s (4 -> 32 deg/s, artillery 1 -> 8); unverified.
        if (yaw_input && type == "engine") def.turn_rate = rate * 8;
        else if (yaw_input) ctx.steer_deg = max_yaw, ctx.steer_rate = rate;
        if (type == "spring") {
            WheelDef w = ctx;
            xform(world, 0, 0, 0, w.pos);
            owner.push_back(lower(name));
            def.wheels.push_back(w);
        }
        struct Child {
            std::string name;
            float pos[3] = {}, rot[3] = {};
        };
        std::vector<Child> children;
        for (auto& [k, a] : t->props) {
            if (a.empty()) continue;
            if (k == "addtemplate") children.push_back({a[0]});
            else if (k == "setposition" && !children.empty()) parse_vec3(a[0], children.back().pos);
            else if (k == "setrotation" && !children.empty()) parse_vec3(a[0], children.back().rot);
        }
        if (type == "lodobject" && children.size() > 1) children.resize(1);  // as scene.cpp: first detail level
        for (auto& c : children) {
            const ConObject* ct = find(con, c.name);
            // Only moving bundles (tracks, wheel hubs, engine); aircraft hang gear springs off the main body.
            if (ct && lower(ct->type) == "spring" && (type == "animatedbundle" || type == "rotationalbundle" || type == "engine")) in_gear = true;
        }
        if (in_gear) gear.insert(lower(name));
        for (auto& c : children) {
            const ConObject* ct = find(con, c.name);
            std::string ctype = ct ? lower(ct->type) : "";
            // Propellers: the meshes hanging straight off an aircraft engine (gear legs are LandingGear, not these).
            bool child_prop = etype == "c_etplane" && (ctype == "lodobject" || ctype == "simpleobject");
            walk(c.name, world * translate(c.pos[0], c.pos[1], c.pos[2]) * rotation_ypr(c.rot[0], c.rot[1], c.rot[2]), depth + 1, ctx,
                 in_gear, child_prop);
        }
    }
};

// A big concave part (a carrier: deck, island and mast in one mesh) as 8 m columns, each a convex hull of the
// triangles' surface sampled every metre, overlapping its neighbours by 1 m so there are no gaps. Deck columns stay
// flat at deck height; one hull would cover the deck with the island's top.
void columns(const std::vector<CollisionMesh>& meshes, const Mat4& world, VehicleDef& def) {
    const float cell = 8, margin = 1, step = 1;
    std::map<std::pair<int, int>, std::vector<float>> cols;
    auto add = [&](const float* q) {  // into its column and the neighbours within the margin
        int x0 = static_cast<int>(std::floor((q[0] - margin) / cell)), x1 = static_cast<int>(std::floor((q[0] + margin) / cell));
        int z0 = static_cast<int>(std::floor((q[2] - margin) / cell)), z1 = static_cast<int>(std::floor((q[2] + margin) / cell));
        for (int cx = x0; cx <= x1; ++cx)
            for (int cz = z0; cz <= z1; ++cz) cols[{cx, cz}].insert(cols[{cx, cz}].end(), q, q + 3);
    };
    for (const CollisionMesh& c : meshes) {
        size_t nv = c.vertices.size() / 3;
        for (size_t f = 0; f + 2 < c.faces.size(); f += 3) {
            if (c.faces[f] >= nv || c.faces[f + 1] >= nv || c.faces[f + 2] >= nv) continue;
            float t[3][3];
            for (int k = 0; k < 3; ++k) {
                const float* v = &c.vertices[static_cast<size_t>(c.faces[f + static_cast<size_t>(k)]) * 3];
                xform(world, v[0], v[1], v[2], t[k]);
            }
            float e = 0;
            for (int k = 0; k < 3; ++k)
                e = std::max(e, std::hypot(t[k][0] - t[(k + 1) % 3][0], t[k][1] - t[(k + 1) % 3][1], t[k][2] - t[(k + 1) % 3][2]));
            if (!std::isfinite(e)) continue;
            int n = std::clamp(static_cast<int>(std::ceil(e / step)), 1, 200);
            for (int i = 0; i <= n; ++i)
                for (int j = 0; i + j <= n; ++j) {
                    float a = static_cast<float>(i) / static_cast<float>(n), b = static_cast<float>(j) / static_cast<float>(n), q[3];
                    for (int k = 0; k < 3; ++k) q[k] = t[0][k] + a * (t[1][k] - t[0][k]) + b * (t[2][k] - t[0][k]);
                    add(q);
                }
        }
    }
    for (auto& [key, pts] : cols)
        if (pts.size() >= 12) def.hull.insert(def.hull.end(), pts.begin(), pts.end()), def.hull_split.push_back(def.hull.size());
}

}  // namespace

WeaponDef weapon_def(const Con& con, const std::string& name) {
    WeaponDef wd;
    const ConObject* t = find(con, name);
    if (!t) return wd;
    std::string proj;
    for (auto& [k, a] : t->props) {
        if (a.empty()) continue;
        if (k == "projectiletemplate") proj = a[0];
        else if (k == "velocity") wd.velocity = num(a);
        else if (k == "roundoffire" && num(a) > 0) wd.interval = 1.0f / std::min(num(a), 50.0f);  // rounds per second
        else if (k == "magsize") wd.mag_size = std::max(0, std::atoi(a[0].c_str()));
        else if (k == "numofmag") wd.mags = std::max(0, std::atoi(a[0].c_str()));
        else if (k == "reloadtime") wd.reload = std::clamp(num(a), 0.0f, 30.0f);
        else if (k == "fireonce") wd.fire_once = std::atoi(a[0].c_str()) != 0;
        else if (k == "ammotype") wd.ammo_type = std::atoi(a[0].c_str());
    }
    const ConObject* p = proj.empty() ? nullptr : find(con, proj);
    if (!p || !(wd.velocity > 0)) return WeaponDef{};
    wd.name = lower(name);
    for (auto& [k, a] : p->props) {
        if (a.empty()) continue;
        if (k == "material") wd.material = std::atoi(a[0].c_str());
        else if (k == "material2") wd.material2 = std::atoi(a[0].c_str());
        else if (k == "radius") wd.radius = std::clamp(num(a), 0.0f, 100.0f);
        else if (k == "dieaftercoll") wd.bounce = std::atoi(a[0].c_str()) == 0;
        else if (k == "explodenearenemydistance") wd.near = std::clamp(num(a), 0.0f, 100.0f);
        else if (k == "timetolive") {  // CRD_NONE/<seconds>/...
            auto s = a[0].find('/');
            if (s != std::string::npos) wd.ttl = std::clamp(static_cast<float>(std::atof(a[0].c_str() + s + 1)), 0.1f, 360.0f);
        }
    }
    return wd;
}

VehicleDef vehicle_def(const Con& con, const Vfs& vfs, const std::string& name) {
    VehicleDef def;
    Walker w{con, def, {}, {}, 0, {}};
    w.walk(name, Mat4::identity(), 0, WheelDef{});
    std::map<std::string, int> skipped;
    std::map<std::string, SmMesh> cache;
    std::vector<float> boxes;
    for (const DrawItem& item : build_object(con, vfs, name, Mat4::identity(), skipped)) {
        if (item.mesh.size() < 3 || item.mesh.compare(item.mesh.size() - 3, 3, ".sm") != 0) continue;
        auto it = cache.find(item.mesh);
        if (it == cache.end()) it = cache.emplace(item.mesh, sm_parse(vfs.read(item.mesh))).first;
        const SmMesh& m = it->second;
        // Armour: the last collision block (meshes with two carry the per-face armour materials in the second; unverified),
        // every part including wheels and tracks. shortcut: drawn at rest; turrets turning do not move their armour.
        if (!m.collision.empty()) {
            const CollisionMesh& cm = m.collision.back();
            size_t nv = cm.vertices.size() / 3;
            for (size_t f = 0; f + 2 < cm.faces.size(); f += 3) {
                if (cm.faces[f] >= nv || cm.faces[f + 1] >= nv || cm.faces[f + 2] >= nv) continue;
                float tri[9];
                for (int k = 0; k < 3; ++k) {
                    const float* v = &cm.vertices[static_cast<size_t>(cm.faces[f + static_cast<size_t>(k)]) * 3];
                    xform(item.world, v[0], v[1], v[2], tri + 3 * k);
                }
                def.armor.insert(def.armor.end(), tri, tri + 9);
                def.armor_material.push_back(f / 3 < cm.material.size() ? cm.material[f / 3] : def.material);
            }
        }
        if (std::find(w.owner.begin(), w.owner.end(), item.owner) != w.owner.end()) {  // wheel radius from mesh height
            float r = (m.bbox[4] - m.bbox[1]) / 2;
            if (std::isfinite(r) && r > 0.05f && r < 5)
                for (size_t i = 0; i < def.wheels.size(); ++i)
                    if (w.owner[i] == item.owner) def.wheels[i].radius = r;
            continue;
        }
        // Tracks and rollers are drawn but not part of the hull: the springs carry the vehicle.
        // (Tank track meshes reach the wheel bottoms; in the hull they scrape the ground.)
        if (w.gear.count(item.owner)) continue;
        float p[3];
        size_t before = def.hull.size();
        for (auto& c : m.collision)
            for (size_t v = 0; v + 2 < c.vertices.size(); v += 3) {
                xform(item.world, c.vertices[v], c.vertices[v + 1], c.vertices[v + 2], p);
                if (std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2])) def.hull.insert(def.hull.end(), p, p + 3);
            }
        if (def.hull.size() > before) {
            float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
            for (size_t i = before; i < def.hull.size(); i += 3)
                for (int k = 0; k < 2; ++k) lo[k] = std::min(lo[k], def.hull[i + 2 * k]), hi[k] = std::max(hi[k], def.hull[i + 2 * k]);
            if (std::max(hi[0] - lo[0], hi[1] - lo[1]) > 20) {  // big part: columns instead of one convex hull
                def.hull.resize(before);
                columns(m.collision, item.world, def);
            } else {
                def.hull_split.push_back(def.hull.size());
            }
        }
        for (int i = 0; i < 8; ++i) {  // fallback if no part has a collision block (cockpits, guns often have none)
            xform(item.world, m.bbox[i & 1 ? 3 : 0], m.bbox[i & 2 ? 4 : 1], m.bbox[i & 4 ? 5 : 2], p);
            boxes.insert(boxes.end(), p, p + 3);
        }
    }
    if (def.hull.empty()) {
        def.hull_split.clear();
        for (size_t i = 0; i + 2 < boxes.size(); i += 3)
            if (std::isfinite(boxes[i]) && std::isfinite(boxes[i + 1]) && std::isfinite(boxes[i + 2])) def.hull.insert(def.hull.end(), &boxes[i], &boxes[i] + 3);
    }
    if (def.hull.size() < 12) throw std::runtime_error("vehicle " + name + ": no hull geometry");
    if (!(def.mass > 0)) def.mass = 1000;
    return def;
}

struct Physics::Impl {
    BpLayers bp;
    ObjVsBp ovb;
    ObjPair pair;
    TempAllocatorImpl temp{32 * 1024 * 1024};
    // shortcut: single-threaded jobs keep runs repeatable; move to JobSystemThreadPool when maps get busy.
    JobSystemSingleThreaded jobs{cMaxPhysicsJobs};
    PhysicsSystem sys;
    std::map<std::string, RefConst<Shape>> meshes;
    struct Vehicle {
        VehicleDef def;
        BodyID id;
        float throttle = 0, steer = 0, pitch = 0, roll = 0, look_x = 0, look_y = 0, prop_angle = 0;
        std::vector<float> wheel_steer, deflection;
        std::vector<float> wheel_spin, wheel_offset;  // degrees; metres along the spring from the model position
        std::map<std::pair<std::string, int>, std::array<float, 3>> angle;  // generic joints, degrees
    };
    std::vector<Vehicle> vehicles;
    struct Soldier {
        Ref<CharacterVirtual> ch;
        Vec3 want = Vec3::sZero();
        bool jump = false, active = true;
        int posture = 0;
        float chute = 0;    // parachute drag per mass; 0 closed
        float fall = 0;     // downward speed last step in the air
        float landing = 0;  // downward speed at the last touch-down
    };
    std::vector<Soldier> soldiers;
    void soldier(Soldier& s);
    struct Shot {
        Vec3 p, v;
        int material, material2, ignore, ignore_character;
        float ttl;
        int tag;
        float radius;
        bool bounce;
        int id;
        bool resting = false;
    };
    int next_shot = 1;
    std::vector<Shot> shots;
    std::vector<Physics::Hit> hits;
    void projectiles();
    int armor(const Vehicle& v, RVec3 from, Vec3 dir) const;
    float acc = 0, water = -1e9f;

    void forces(Vehicle& v);
    void animate(Vehicle& v);
    void wings(Vehicle& v, const RMat44& T, Vec3 lin, Vec3 ang, RVec3 com);
    bool floats(Vehicle& v, const RMat44& T, Vec3 lin, Vec3 ang, RVec3 com);
};

// Jolt's allocator hooks must exist before Impl's members (TempAllocator) are built.
Physics::Physics(const Heightmap& hm, float world_size, float water_level) : p_((jolt_init(), std::make_unique<Impl>())) {
    p_->water = water_level;
    p_->sys.Init(65536, 0, 65536, 16384, p_->bp, p_->ovb, p_->pair);
    p_->sys.SetGravity(Vec3(0, -kGravity, 0));
    if (hm.n < 4) return;
    // Jolt wants an even sample count: repeat the last row and column if needed.
    uint32 n = static_cast<uint32>(hm.n + (hm.n & 1));
    std::vector<float> s(static_cast<size_t>(n) * n);
    for (uint32 r = 0; r < n; ++r)
        for (uint32 c = 0; c < n; ++c)
            s[r * n + c] = hm.height(std::min<int>(static_cast<int>(r), hm.n - 1), std::min<int>(static_cast<int>(c), hm.n - 1));
    float cell = world_size / static_cast<float>(hm.n);
    HeightFieldShapeSettings hs(s.data(), Vec3::sZero(), Vec3(cell, 1, cell), n);  // sample [z][x], as Heightmap
    auto res = hs.Create();
    if (res.HasError()) throw std::runtime_error(std::string("terrain collision: ") + res.GetError().c_str());
    p_->sys.GetBodyInterface().CreateAndAddBody(BodyCreationSettings(res.Get(), RVec3::sZero(), Quat::sIdentity(), EMotionType::Static, kStatic),
                                                EActivation::DontActivate);
}

Physics::~Physics() = default;

void Physics::add_static(const std::string& key, const CollisionMesh& mesh, const Mat4& world) {
    auto it = p_->meshes.find(key);
    if (it == p_->meshes.end()) {
        RefConst<Shape> shape;
        VertexList verts;
        IndexedTriangleList tris;
        size_t nv = mesh.vertices.size() / 3;
        for (size_t v = 0; v < nv; ++v) verts.push_back(Float3(mesh.vertices[v * 3], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2]));
        for (size_t f = 0; f + 2 < mesh.faces.size(); f += 3)  // file data: drop faces with out-of-range indices
            if (mesh.faces[f] < nv && mesh.faces[f + 1] < nv && mesh.faces[f + 2] < nv)
                tris.push_back(IndexedTriangle(mesh.faces[f], mesh.faces[f + 1], mesh.faces[f + 2]));
        if (!tris.empty()) {
            MeshShapeSettings ms(std::move(verts), std::move(tris));
            auto res = ms.Create();
            if (!res.HasError()) shape = res.Get();
        }
        it = p_->meshes.emplace(key, shape).first;
    }
    if (!it->second) return;
    // world = T * R * S: column lengths are the scale, a mirrored basis flips x.
    Vec3 col[3];
    float s[3];
    for (int i = 0; i < 3; ++i) {
        col[i] = Vec3(world.m[i * 4], world.m[i * 4 + 1], world.m[i * 4 + 2]);
        s[i] = col[i].Length();
        if (!(s[i] > 1e-6f) || !std::isfinite(s[i])) return;
        col[i] /= s[i];
    }
    if (col[0].Cross(col[1]).Dot(col[2]) < 0) col[0] = -col[0], s[0] = -s[0];
    Quat rot = Mat44(Vec4(col[0], 0), Vec4(col[1], 0), Vec4(col[2], 0), Vec4(0, 0, 0, 1)).GetQuaternion().Normalized();
    RefConst<Shape> shape = it->second;
    if (std::fabs(s[0] - 1) + std::fabs(s[1] - 1) + std::fabs(s[2] - 1) > 1e-4f) shape = new ScaledShape(shape, Vec3(s[0], s[1], s[2]));
    p_->sys.GetBodyInterface().CreateAndAddBody(
        BodyCreationSettings(shape, RVec3(world.m[12], world.m[13], world.m[14]), rot, EMotionType::Static, kStatic), EActivation::DontActivate);
}

size_t Physics::add_scene(const std::vector<DrawItem>& items, const Vfs& vfs) {
    size_t before = p_->sys.GetNumBodies();
    std::map<std::string, CollisionMesh> cache;
    for (const DrawItem& item : items) {
        // shortcut: trees (.tm), animated meshes and spawner vehicles get no collision (vehicles are not static;
        // as static bodies a vehicle driven from its spawn point starts inside one and is pushed under the terrain).
        if (item.spawned || !item.skin.empty() || item.mesh.size() < 3 || item.mesh.compare(item.mesh.size() - 3, 3, ".sm") != 0) continue;
        auto it = cache.find(item.mesh);
        if (it == cache.end()) {
            CollisionMesh c;
            try {
                SmMesh m = sm_parse(vfs.read(item.mesh));
                if (!m.collision.empty()) c = std::move(m.collision[0]);
            } catch (const std::exception&) {
            }
            it = cache.emplace(item.mesh, std::move(c)).first;
        }
        if (!it->second.faces.empty()) add_static(item.mesh, it->second, item.world);
    }
    p_->sys.OptimizeBroadPhase();
    return p_->sys.GetNumBodies() - before;
}

int Physics::add_vehicle(const VehicleDef& def, const Mat4& world) {
    auto hull = [&](size_t from, size_t to) -> RefConst<Shape> {
        Array<Vec3> pts;
        for (size_t i = from; i + 2 < to && i + 2 < def.hull.size(); i += 3) pts.push_back(Vec3(def.hull[i], def.hull[i + 1], def.hull[i + 2]));
        if (pts.size() < 4) return nullptr;
        ConvexHullShapeSettings hs(pts, 0.05f);
        auto res = hs.Create();
        return res.HasError() ? nullptr : res.Get();
    };
    // One convex hull per part (flat or tiny parts that fail are dropped); a single hull if that leaves nothing.
    RefConst<Shape> shape;
    if (def.hull_split.size() > 1) {
        StaticCompoundShapeSettings cs;
        size_t from = 0;
        for (size_t to : def.hull_split) {
            if (RefConst<Shape> part = hull(from, to)) cs.AddShape(Vec3::sZero(), Quat::sIdentity(), part);
            from = to;
        }
        if (!cs.mSubShapes.empty()) {
            auto res = cs.Create();
            if (!res.HasError()) shape = res.Get();
        }
    }
    if (!shape) shape = hull(0, def.hull.size());
    if (!shape) throw std::runtime_error("vehicle hull: no convex hull");
    // shortcut: centre of mass lowered to wheel-centre height (arcade stability; the hull's volume centre rolls a
    // Willys in ordinary turns). Revisit when the original's rollover behaviour is measured.
    float wy = 0;
    int carrying = 0;
    for (auto& w : def.wheels)
        if (w.strength > 0) wy += w.pos[1], ++carrying;
    // Aircraft: centre of mass at the model origin, where setPositionOffset puts the main wings' lift.
    Vec3 com_off = def.plane ? -shape->GetCenterOfMass()
                             : Vec3(0, carrying > 0 ? wy / static_cast<float>(carrying) - shape->GetCenterOfMass().GetY() : 0.0f, 0);
    if (!com_off.IsNearZero()) {
        OffsetCenterOfMassShapeSettings off(com_off, shape);
        auto r2 = off.Create();
        if (!r2.HasError()) shape = r2.Get();
    }
    Vec3 c0(world.m[0], world.m[1], world.m[2]), c1(world.m[4], world.m[5], world.m[6]), c2(world.m[8], world.m[9], world.m[10]);
    Quat rot = Mat44(Vec4(c0.Normalized(), 0), Vec4(c1.Normalized(), 0), Vec4(c2.Normalized(), 0), Vec4(0, 0, 0, 1)).GetQuaternion().Normalized();
    BodyCreationSettings bs(shape, RVec3(world.m[12], world.m[13], world.m[14]), rot, EMotionType::Dynamic, kMoving);
    bs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
    bs.mMassPropertiesOverride.mMass = def.mass;
    bs.mLinearDamping = 0;  // drag is ours
    bs.mAngularDamping = 0.5f;
    bs.mAllowSleeping = false;
    bs.mMotionQuality = EMotionQuality::LinearCast;  // fast, thin hulls (aircraft) must not tunnel through the terrain
    Impl::Vehicle v;
    v.def = def;
    v.id = p_->sys.GetBodyInterface().CreateAndAddBody(bs, EActivation::Activate);
    v.wheel_steer.assign(def.wheels.size(), 0.0f);
    v.deflection.assign(def.wings.size(), 0.0f);
    v.wheel_spin.assign(def.wheels.size(), 0.0f);
    v.wheel_offset.assign(def.wheels.size(), 0.0f);
    p_->vehicles.push_back(std::move(v));
    return static_cast<int>(p_->vehicles.size() - 1);
}

void Physics::drive(int vehicle, float throttle, float yaw, float pitch, float roll) {
    auto& v = p_->vehicles.at(static_cast<size_t>(vehicle));
    v.throttle = std::clamp(throttle, -1.0f, 1.0f), v.steer = std::clamp(yaw, -1.0f, 1.0f);
    v.pitch = std::clamp(pitch, -1.0f, 1.0f), v.roll = std::clamp(roll, -1.0f, 1.0f);
}

void Physics::set_velocity(int vehicle, float x, float y, float z) {
    p_->sys.GetBodyInterface().SetLinearVelocity(p_->vehicles.at(static_cast<size_t>(vehicle)).id, Vec3(x, y, z));
}

// Our Refractor parts. Units of the template values are unverified (docs/formats/vehicles.md):
// strength and damping are read per unit of the mass each wheel carries (s^-2, s^-1), which puts a Willys
// (25, 5) at 0.8 Hz, damping ratio 0.5, resting where the model places its wheels.
void Physics::Impl::forces(Vehicle& v) {
    BodyInterface& bi = sys.GetBodyInterface();
    const VehicleDef& d = v.def;
    RMat44 T = bi.GetWorldTransform(v.id);
    Vec3 up = T.GetAxisY(), lin = bi.GetLinearVelocity(v.id), ang = bi.GetAngularVelocity(v.id);
    RVec3 com = bi.GetCenterOfMassPosition(v.id);
    // Springs with strength 0 (tank "dummy" rollers) carry nothing; the weight is shared by the others.
    int carrying = static_cast<int>(std::count_if(d.wheels.begin(), d.wheels.end(), [](const WheelDef& w) { return w.strength > 0; }));
    int driven = static_cast<int>(std::count_if(d.wheels.begin(), d.wheels.end(), [](const WheelDef& w) { return w.strength > 0 && w.driven; }));
    float mw = d.mass / static_cast<float>(std::max(carrying, 1));
    float yaw_rate = ang.Dot(up);
    int contacts = 0;
    // shortcut: drive and drag scales are placeholders (Willys: 3.7 m/s^2, ~70 km/h); calibrate against the original.
    float a_drive = d.torque * d.differential / 20.0f;
    const float mu = 1.0f;  // shortcut: c_PGF* grip models and materialFriction not applied yet
    IgnoreSingleBodyFilter self(v.id);
    RayCastSettings rs;
    rs.SetBackFaceMode(EBackFaceMode::CollideWithBackFaces);  // collision mesh winding is unverified
    for (size_t i = 0; i < d.wheels.size(); ++i) {
        const WheelDef& w = d.wheels[i];
        // Drawing: wheels roll with the ground; unloaded (dummy) rollers with the hull's forward speed.
        v.wheel_spin[i] = std::fmod(v.wheel_spin[i] + lin.Dot(T.GetAxisZ()) / w.radius * kStep * 57.29578f, 360.0f);
        if (w.strength <= 0) continue;
        float target = v.steer * w.steer_deg, step = w.steer_rate > 0 ? w.steer_rate * kStep : 1e9f;
        v.wheel_steer[i] += std::clamp(target - v.wheel_steer[i], -step, step);
        float rest = std::clamp(w.strength > 0 ? kGravity / w.strength : 0.3f, 0.05f, 1.0f);
        // Ray from rest above the wheel centre; compression is rest (load = weight) with the wheel at its model position.
        float len = 2 * rest + w.radius;
        RRayCast ray{T * Vec3(w.pos[0], w.pos[1] + rest, w.pos[2]), -up * len};
        ClosestHitCollisionCollector<CastRayCollector> hit;
        sys.GetNarrowPhaseQuery().CastRay(ray, rs, hit, {}, {}, self);
        v.wheel_offset[i] = -std::min(rest, 0.2f);  // hanging free; shortcut: drawn droop capped, real travel unknown
        if (!hit.HadHit()) continue;
        float x = len - hit.mHit.mFraction * len;
        v.wheel_offset[i] = x - rest;
        if (x <= 0) continue;
        RVec3 p = ray.GetPointOnRay(hit.mHit.mFraction);
        Vec3 n = up;
        {
            BodyLockRead lock(sys.GetBodyLockInterface(), hit.mHit.mBodyID);
            if (lock.Succeeded()) n = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mHit.mSubShapeID2, p);
        }
        ++contacts;
        // Tracked: grip resists sliding, not the commanded turn, so the yaw part of the spin is left out of the slip.
        Vec3 vp = lin + (d.tracked ? ang - up * yaw_rate : ang).Cross(Vec3(p - com));
        float N = std::max(0.0f, mw * (w.strength * x - w.damping * vp.Dot(up)));
        float ang_rad = v.wheel_steer[i] * 3.14159265f / 180.0f;
        Vec3 f = T.Multiply3x3(Vec3(std::sin(ang_rad), 0, std::cos(ang_rad)));
        f = f - n * f.Dot(n);
        if (f.LengthSq() < 1e-6f) continue;
        f = f.Normalized();
        Vec3 side = n.Cross(f).Normalized();
        float vs = vp.Dot(side), vf = vp.Dot(f);
        float Fs = -0.5f * vs * mw / kStep;  // cancel half the sideways slip per step
        float Ff = 0;
        if (v.throttle != 0 && v.throttle * vf < -1.0f) Ff = (vf > 0 ? -1.0f : 1.0f) * 0.8f * mu * N;  // braking
        else if (w.driven && driven > 0) Ff = v.throttle * a_drive * d.mass / static_cast<float>(driven);
        // No throttle: engine braking (0.5 s time constant), full hold below 0.5 m/s so parked vehicles do not creep.
        // Aircraft roll out gently: hard brakes nose a tail-dragger over.
        if (v.throttle == 0) Ff = std::fabs(vf) < 0.5f ? -0.5f * vf * mw / kStep : -(d.plane ? 0.3f : 2.0f) * vf * mw;
        float lim = mu * N, tot = std::sqrt(Fs * Fs + Ff * Ff);
        if (tot > lim && tot > 0) Fs *= lim / tot, Ff *= lim / tot;
        // Along the ground normal: along the tilted body axis a pitched vehicle would push itself backwards.
        bi.AddForce(v.id, n * N, p);
        // shortcut: grip acts near centre-of-mass height (arcade roll resistance); tune once the original is measured.
        bi.AddForce(v.id, side * Fs + f * Ff, p + up * (0.8f * Vec3(com - p).Dot(up)));
    }
    if (d.tracked && contacts > 0) {  // turn on the spot: blend the yaw rate towards the command
        float want = v.steer * d.turn_rate * 3.14159265f / 180.0f;
        bi.SetAngularVelocity(v.id, ang + up * (0.2f * (want - yaw_rate)));
    }
    bool wet = floats(v, T, lin, ang, com);
    wings(v, T, lin, ang, com);
    // Propellers push along the body at the centre; a ship's only while it floats. Ships reverse at half power.
    float th = d.ship && !d.plane ? (v.throttle < 0 ? 0.5f * v.throttle : v.throttle) : std::max(0.0f, v.throttle);
    if (d.plane || (d.ship && wet)) bi.AddForce(v.id, T.GetAxisZ() * (th * d.thrust * d.mass));
    float sp = lin.Length();
    bi.AddForce(v.id, -lin * (d.drag * d.mass * sp / 150.0f));
}

// Wings (unverified, docs/formats/vehicles.md): per wing, as acceleration x mass along the wing normal:
//   -kLift * setWingLift * v_n * |v|           angle-of-attack lift and damping
//   -kFlap * setFlapLift * deflection * v_f^2   control surface, deflection in [-1, 1] = input x sign(setAcceleration);
//                                               a positive pitch rotation raises the trailing edge (D3D convention), so it pushes down.
//   This gives c_PIRoll +1 = roll right, c_PIYaw +1 = nose right (as cars), c_PIPitch +1 = nose down (stick forward).
//   +g / n_regulated * min(1, (v_f / kRegulateSpeed)^2)   self-levelling main wings. Every retail regulated wing says 4.91
//      (g / 2), but the Il-2 has one such wing and the SBD three; reading it per wing gives them 0.5 g and 1.5 g, so the
//      regulated wings are taken to hold 1 g together. Unverified.
void Physics::Impl::wings(Vehicle& v, const RMat44& T, Vec3 lin, Vec3 ang, RVec3 com) {
    // shortcut: placeholder constants, tuned so a Mustang climbs out at ~30 m/s and rolls/pitches briskly.
    const float kLift = 0.01f, kFlap = 0.001f, kRegulateSpeed = 30.0f;
    BodyInterface& bi = sys.GetBodyInterface();
    const VehicleDef& d = v.def;
    Vec3 fwd = T.GetAxisZ();
    float n_reg = static_cast<float>(std::count_if(d.wings.begin(), d.wings.end(), [](const WingDef& w) { return w.regulate > 0; }));
    Mat44 inv_inertia = Mat44::sZero();
    {
        BodyLockRead lock(sys.GetBodyLockInterface(), v.id);
        if (lock.Succeeded()) inv_inertia = lock.GetBody().GetInverseInertia();
    }
    for (size_t i = 0; i < d.wings.size(); ++i) {
        const WingDef& w = d.wings[i];
        float in = w.input == 1 ? v.pitch : w.input == 2 ? v.roll : w.input == 3 ? v.steer : 0.0f;
        float step = w.rate * kStep;
        v.deflection[i] += std::clamp(in * w.sign - v.deflection[i], -step, step);
        RVec3 p = T * Vec3(w.pos[0], w.pos[1], w.pos[2]);
        Vec3 n = T.Multiply3x3(Vec3(w.normal[0], w.normal[1], w.normal[2]));
        Vec3 vp = lin + ang.Cross(Vec3(p - com));
        float s = vp.Length(), vn = vp.Dot(n), vf = std::max(0.0f, vp.Dot(fwd));
        // Explicit damping must not overshoot: cap the angle-of-attack force at half of what stops v_n in one step,
        // using the effective mass at the wing (a narrow hull has little roll inertia; uncapped it oscillates).
        Vec3 rn = Vec3(p - com).Cross(n);
        float m_eff = 1.0f / (1.0f / d.mass + rn.Dot(inv_inertia.Multiply3x3(rn)));
        float aoa = -kLift * w.lift * vn * s, cap = 0.5f * std::fabs(vn) / kStep * m_eff / d.mass;
        float a = std::clamp(aoa, -cap, cap) - kFlap * w.flap * v.deflection[i] * vf * vf;
        if (w.regulate > 0) a += kGravity / n_reg * std::min(1.0f, vf * vf / (kRegulateSpeed * kRegulateSpeed));
        bi.AddForce(v.id, n * (a * d.mass), p);
    }
}

// Floats (unverified, docs/formats/vehicles.md): each lifts setFloatMaxLift x mass x the submerged fraction of its
// column, straight up, and damps vertical motion. Sideways slip of the hull is damped at the centre (a keel), so long
// hulls can still turn on their rudders; forward drag is the template's drag (forces()). Returns whether any float is wet.
bool Physics::Impl::floats(Vehicle& v, const RMat44& T, Vec3 lin, Vec3 ang, RVec3 com) {
    // shortcut: drag rates (1/s) are placeholders; the Refractor drag values only set the top speed (forces()).
    const float kSide = 2.0f, kVertical = 4.0f;
    BodyInterface& bi = sys.GetBodyInterface();
    const VehicleDef& d = v.def;
    if (d.floats.empty()) return false;
    Vec3 side = T.GetAxisX();
    float share = d.mass / static_cast<float>(d.floats.size()), wet_share = 0;
    for (const FloatDef& f : d.floats) {
        RVec3 p = T * Vec3(f.pos[0], f.pos[1], f.pos[2]);
        float sub = std::clamp((water - (static_cast<float>(p.GetY()) - f.height)) / f.height, 0.0f, 1.0f);
        if (sub <= 0) continue;
        wet_share += sub / static_cast<float>(d.floats.size());
        Vec3 vp = lin + ang.Cross(Vec3(p - com));
        bi.AddForce(v.id, Vec3(0, f.lift * sub * d.mass - kVertical * vp.GetY() * sub * share, 0), p);
    }
    if (wet_share > 0) bi.AddForce(v.id, side * (-kSide * lin.Dot(side) * wet_share * d.mass));
    return wet_share > 0;
}

// Moving parts (drawing only): generic joints, propellers. Absolute inputs drive towards setMin/MaxRotation at
// setMaxSpeed deg/s (back to 0 with setAutomaticReset); mouse-look inputs turn at setMaxSpeed and stop at the limits
// when there are any (turret yaw usually has none).
void Physics::Impl::animate(Vehicle& v) {
    v.prop_angle = std::fmod(v.prop_angle + (300.0f + 1500.0f * std::max(0.0f, v.throttle)) * kStep, 360.0f);  // shortcut: rpm guessed
    for (auto& [key, j] : v.def.joints) {
        if (!j.input[0] && !j.input[1] && !j.input[2]) continue;
        auto& a = v.angle[key];
        for (int i = 0; i < 3; ++i) {
            float in = 0;
            switch (j.input[i]) {
                case JointDef::None: continue;
                case JointDef::Pitch: in = v.pitch; break;
                case JointDef::Roll: in = v.roll; break;
                case JointDef::Yaw: in = v.steer; break;
                case JointDef::Throttle: in = v.throttle; break;
                case JointDef::LookX: in = v.look_x; break;
                case JointDef::LookY: in = v.look_y; break;
            }
            in *= j.sign[i];
            float lo = std::min(j.mn[i], j.mx[i]), hi = std::max(j.mn[i], j.mx[i]), step = j.speed[i] * kStep;
            if (j.input[i] == JointDef::LookX || j.input[i] == JointDef::LookY) {
                a[i] += in * step;
                if (lo < hi) a[i] = std::clamp(a[i], lo, hi);
                else a[i] = std::remainder(a[i], 360.0f);
                continue;
            }
            if (in == 0 && !j.reset) continue;
            float target = in > 0 ? in * hi : -in * lo;
            a[i] += std::clamp(target - a[i], -step, step);
        }
    }
}

void Physics::look(int vehicle, float x, float y) {
    auto& v = p_->vehicles.at(static_cast<size_t>(vehicle));
    v.look_x = std::clamp(x, -1.0f, 1.0f), v.look_y = std::clamp(y, -1.0f, 1.0f);
}

Mat4 Physics::joint(int vehicle, const std::string& name, int occurrence) const {
    const auto& v = p_->vehicles.at(static_cast<size_t>(vehicle));
    auto it = v.def.joints.find({name, occurrence});
    if (it == v.def.joints.end()) return Mat4::identity();
    const JointDef& j = it->second;
    Mat4 m = Mat4::identity();
    if (j.wheel >= 0 && static_cast<size_t>(j.wheel) < v.wheel_spin.size())
        m = translate(0, v.wheel_offset[static_cast<size_t>(j.wheel)], 0) * rotation_ypr(0, v.wheel_spin[static_cast<size_t>(j.wheel)], 0);
    if (j.wing >= 0 && static_cast<size_t>(j.wing) < v.deflection.size())
        m = m * rotation_ypr(0, v.deflection[static_cast<size_t>(j.wing)] * v.def.wings[static_cast<size_t>(j.wing)].range, 0);
    if (j.propeller) m = m * rotation_ypr(0, 0, v.prop_angle);
    auto a = v.angle.find(it->first);
    if (a != v.angle.end()) m = m * rotation_ypr(a->second[0], a->second[1], a->second[2]);
    return m;
}

// Nearest armour triangle along a ray (vehicle space, Moller-Trumbore); its material, or -1.
int Physics::Impl::armor(const Vehicle& v, RVec3 from, Vec3 dir) const {
    RMat44 inv = sys.GetBodyInterface().GetWorldTransform(v.id).InversedRotationTranslation();
    Vec3 o = Vec3(inv * from), d = inv.Multiply3x3(dir);
    const std::vector<float>& a = v.def.armor;
    float best = 1e30f;
    int mat = -1;
    for (size_t t = 0; t + 8 < a.size(); t += 9) {
        Vec3 p0(a[t], a[t + 1], a[t + 2]), e1 = Vec3(a[t + 3], a[t + 4], a[t + 5]) - p0, e2 = Vec3(a[t + 6], a[t + 7], a[t + 8]) - p0;
        Vec3 h = d.Cross(e2);
        float det = e1.Dot(h);
        if (std::fabs(det) < 1e-9f) continue;
        Vec3 s = o - p0;
        float u = s.Dot(h) / det;
        if (u < 0 || u > 1) continue;
        Vec3 q = s.Cross(e1);
        float w = d.Dot(q) / det, dist = e2.Dot(q) / det;
        if (w < 0 || u + w > 1 || dist < 0 || dist >= best) continue;
        best = dist, mat = v.def.armor_material[t / 9];
    }
    return mat;
}

// shortcut: no drag, full gravity; shots stop at the first contact (grenades do not bounce).
void Physics::Impl::projectiles() {
    Vec3 g = sys.GetGravity();
    for (size_t i = 0; i < shots.size();) {
        Shot& s = shots[i];
        s.ttl -= kStep;
        Vec3 v1 = s.v + g * kStep, step = (s.v + v1) * (0.5f * kStep);
        bool done = s.ttl <= 0;
        if (!done && s.resting) {
            ++i;
            continue;
        }
        if (!done) {
            BodyID ignore = s.ignore >= 0 && static_cast<size_t>(s.ignore) < vehicles.size() ? vehicles[static_cast<size_t>(s.ignore)].id : BodyID();
            BodyID ignore_c = s.ignore_character >= 0 && static_cast<size_t>(s.ignore_character) < soldiers.size()
                                  ? soldiers[static_cast<size_t>(s.ignore_character)].ch->GetInnerBodyID() : BodyID();
            struct Two final : BodyFilter {
                BodyID a, b;
                bool ShouldCollide(const BodyID& id) const override { return id != a && id != b; }
                bool ShouldCollideLocked(const Body& body) const override { return ShouldCollide(body.GetID()); }
            } filter;
            filter.a = ignore, filter.b = ignore_c;
            RRayCast ray{RVec3(s.p), step};
            RayCastResult hit;
            Physics::Hit h;
            h.material = s.material, h.material2 = s.material2, h.tag = s.tag, h.radius = s.radius;
            if (s.bounce && sys.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter)) {
                // shortcut: restitution 0.3, tangential speed kept at 60 %; rests below 1 m/s (on moving vehicles too)
                RVec3 at = ray.GetPointOnRay(hit.mFraction);
                Vec3 n = -step.Normalized();
                {
                    BodyLockRead lock(sys.GetBodyLockInterface(), hit.mBodyID);
                    if (lock.Succeeded()) n = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, at);
                }
                Vec3 vn = n * v1.Dot(n), vt = v1 - vn;
                s.p = Vec3(at) + n * 0.05f, s.v = vt * 0.6f - vn * 0.3f, s.ignore = -1, s.ignore_character = -1;
                if (s.v.Length() < 1.0f) s.resting = true, s.v = Vec3::sZero();
                ++i;
                continue;
            } else if (s.bounce && s.p.GetY() + step.GetY() < water) {
                s.resting = true, s.v = Vec3::sZero(), s.p.SetY(water);  // shortcut: floats on the surface
                ++i;
                continue;
            } else if (!s.bounce && sys.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter)) {
                RVec3 at = ray.GetPointOnRay(hit.mFraction);
                for (size_t k = 0; k < soldiers.size(); ++k)
                    if (soldiers[k].ch->GetInnerBodyID() == hit.mBodyID) {
                        h.character = static_cast<int>(k);
                        h.height = static_cast<float>(at.GetY() - soldiers[k].ch->GetPosition().GetY());
                    }
                for (size_t k = 0; k < vehicles.size(); ++k)
                    if (vehicles[k].id == hit.mBodyID) {
                        h.vehicle = static_cast<int>(k);
                        // From just outside the hull through it: the armour face the shell meets.
                        Vec3 dir = step.Normalized();
                        h.armor = armor(vehicles[k], at - dir * 1.0f, dir);
                    }
                for (int k = 0; k < 3; ++k) h.pos[k] = static_cast<float>(at[static_cast<uint>(k)]);
                hits.push_back(h), done = true;
            } else if (s.p.GetY() + step.GetY() < water) {
                Vec3 at = s.p + step * ((s.p.GetY() - water) / std::max(1e-6f, -step.GetY()));
                h.pos[0] = at.GetX(), h.pos[1] = water, h.pos[2] = at.GetZ();
                hits.push_back(h), done = true;
            }
        }
        if (done && s.ttl <= 0 && s.radius > 0) {  // fuse ran out (grenades): explodes where it is
            Physics::Hit h;
            h.material = s.material, h.material2 = s.material2, h.tag = s.tag, h.radius = s.radius;
            h.pos[0] = s.p.GetX(), h.pos[1] = s.p.GetY(), h.pos[2] = s.p.GetZ();
            hits.push_back(h);
        }
        if (done) {
            shots[i] = shots.back(), shots.pop_back();
            continue;
        }
        s.p += step, s.v = v1;
        ++i;
    }
}

int Physics::fire(const float pos[3], const float vel[3], int material, int material2, float ttl, int ignore_vehicle,
                  int ignore_character, int tag, float radius, bool bounce) {
    for (int k = 0; k < 3; ++k)
        if (!std::isfinite(pos[k]) || !std::isfinite(vel[k])) return -1;
    int id = p_->next_shot++;
    // Lifetimes up to 6 minutes: mines (360 s) and explosive packs (240 s) wait that long.
    p_->shots.push_back({Vec3(pos[0], pos[1], pos[2]), Vec3(vel[0], vel[1], vel[2]), material, material2, ignore_vehicle,
                         ignore_character, std::clamp(ttl, 0.0f, 360.0f), tag, std::max(0.0f, radius), bounce, id});
    return id;
}

bool Physics::shot_position(int id, float out[3]) const {
    for (auto& s : p_->shots)
        if (s.id == id) {
            out[0] = s.p.GetX(), out[1] = s.p.GetY(), out[2] = s.p.GetZ();
            return true;
        }
    return false;
}

void Physics::explode(int id) {
    for (auto& s : p_->shots)
        if (s.id == id) s.ttl = 0, s.resting = false;
}

std::vector<Physics::Hit> Physics::take_hits() { return std::exchange(p_->hits, {}); }

bool Physics::line_of_sight(const float a[3], const float b[3], int ignore_character, int target_character, int target_vehicle) const {
    auto inner = [&](int c) {
        return c >= 0 && static_cast<size_t>(c) < p_->soldiers.size() ? p_->soldiers[static_cast<size_t>(c)].ch->GetInnerBodyID() : BodyID();
    };
    struct Three final : BodyFilter {
        BodyID a, b, c;
        bool ShouldCollide(const BodyID& id) const override { return id != a && id != b && id != c; }
        bool ShouldCollideLocked(const Body& body) const override { return ShouldCollide(body.GetID()); }
    } filter;
    filter.a = inner(ignore_character), filter.b = inner(target_character);
    filter.c = target_vehicle >= 0 && static_cast<size_t>(target_vehicle) < p_->vehicles.size() ? p_->vehicles[static_cast<size_t>(target_vehicle)].id : BodyID();
    RRayCast ray{RVec3(a[0], a[1], a[2]), Vec3(b[0] - a[0], b[1] - a[1], b[2] - a[2])};
    RayCastResult hit;
    return !p_->sys.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter);
}

void Physics::projectiles(std::vector<float>& out) const {
    out.clear();
    for (auto& s : p_->shots) out.insert(out.end(), {s.p.GetX(), s.p.GetY(), s.p.GetZ(), s.v.GetX(), s.v.GetY(), s.v.GetZ()});
}

// shortcut: jump speed and swimming depth are placeholders (no soldier speeds in the templates).
void Physics::Impl::soldier(Soldier& s) {
    CharacterVirtual& ch = *s.ch;
    Vec3 g = sys.GetGravity(), vel = ch.GetLinearVelocity();
    Vec3 v = Vec3(0, vel.GetY(), 0);
    if (ch.GetGroundState() == CharacterBase::EGroundState::OnGround && vel.GetY() - ch.GetGroundVelocity().GetY() < 0.1f) {
        if (s.fall > 0) s.landing = s.fall, s.fall = 0;
        s.chute = 0;
        v = ch.GetGroundVelocity();
        if (s.jump) v += Vec3(0, 4.5f, 0);
    } else {
        v += g * kStep;
        if (s.chute > 0 && v.GetY() < 0) v.SetY(v.GetY() + s.chute * v.GetY() * v.GetY() * kStep);  // drag up, against the fall
        s.fall = std::max(0.0f, -v.GetY());
    }
    float feet = static_cast<float>(ch.GetPosition().GetY());
    if (feet < water - 1.4f) v.SetY(std::max(v.GetY(), (water - 1.4f - feet) * 2.0f)), s.fall = 0, s.chute = 0;  // swim: head above the surface
    s.jump = false;
    ch.SetLinearVelocity(v + s.want);
    CharacterVirtual::ExtendedUpdateSettings es;
    ch.ExtendedUpdate(kStep, g, es, sys.GetDefaultBroadPhaseLayerFilter(kMoving), sys.GetDefaultLayerFilter(kMoving), {}, {}, temp);
}

namespace {
Ref<Shape> soldier_shape(int posture) {  // a capsule of radius 0.3 standing on the feet
    float h = posture == 2 ? 0.7f : posture == 1 ? 1.2f : 1.8f;
    return new RotatedTranslatedShape(Vec3(0, h / 2, 0), Quat::sIdentity(), new CapsuleShape(h / 2 - 0.3f, 0.3f));
}
}  // namespace

void Physics::open_parachute(int character, float k) { p_->soldiers.at(static_cast<size_t>(character)).chute = std::max(0.0f, k); }
bool Physics::parachute_open(int character) const { return p_->soldiers.at(static_cast<size_t>(character)).chute > 0; }
float Physics::take_landing(int character) { return std::exchange(p_->soldiers.at(static_cast<size_t>(character)).landing, 0.0f); }

bool Physics::set_posture(int character, int posture) {
    auto& s = p_->soldiers.at(static_cast<size_t>(character));
    posture = std::clamp(posture, 0, 2);
    if (posture == s.posture) return true;
    Ref<Shape> sh = soldier_shape(posture);
    IgnoreSingleBodyFilter self(s.ch->GetInnerBodyID());
    // Getting lower always fits; getting up must not push into anything.
    if (!s.ch->SetShape(sh, posture > s.posture ? 1e6f : 0.01f, p_->sys.GetDefaultBroadPhaseLayerFilter(kMoving),
                        p_->sys.GetDefaultLayerFilter(kMoving), self, {}, p_->temp))
        return false;
    s.ch->SetInnerBodyShape(sh);
    s.posture = posture;
    return true;
}

int Physics::add_character(float x, float y, float z) {
    Ref<CharacterVirtualSettings> cs = new CharacterVirtualSettings();
    cs->mShape = soldier_shape(0);
    cs->mMaxSlopeAngle = DegreesToRadians(50.0f);
    cs->mInnerBodyShape = cs->mShape;  // a body in the broad phase, so projectiles can hit soldiers
    cs->mInnerBodyLayer = kMoving;
    Impl::Soldier s;
    s.ch = new CharacterVirtual(cs, RVec3(x, y, z), Quat::sIdentity(), &p_->sys);
    p_->soldiers.push_back(s);
    return static_cast<int>(p_->soldiers.size() - 1);
}

void Physics::walk(int character, float vx, float vz, bool jump) {
    auto& s = p_->soldiers.at(static_cast<size_t>(character));
    s.want = Vec3(std::isfinite(vx) ? vx : 0.0f, 0, std::isfinite(vz) ? vz : 0.0f);
    s.jump = s.jump || jump;
}

void Physics::place_character(int character, float x, float y, float z) {
    auto& s = p_->soldiers.at(static_cast<size_t>(character));
    s.ch->SetPosition(RVec3(x, y, z));
    s.ch->SetLinearVelocity(Vec3::sZero());
}

void Physics::set_character_active(int character, bool active) {
    auto& s = p_->soldiers.at(static_cast<size_t>(character));
    s.active = active;
    // Its inner body would still stop shots and vehicles: park it with the character far below the world.
    if (!active) s.ch->SetPosition(RVec3(s.ch->GetPosition().GetX(), -10000, s.ch->GetPosition().GetZ()));
}

void Physics::kill_character(int character) { set_character_active(character, false); }

void Physics::character_position(int character, float out[3]) const {
    RVec3 p = p_->soldiers.at(static_cast<size_t>(character)).ch->GetPosition();
    out[0] = static_cast<float>(p.GetX()), out[1] = static_cast<float>(p.GetY()), out[2] = static_cast<float>(p.GetZ());
}

bool Physics::character_on_ground(int character) const { return p_->soldiers.at(static_cast<size_t>(character)).ch->IsSupported(); }

void Physics::step(float dt) {
    p_->acc = std::min(p_->acc + dt, 8 * kStep);  // a long frame drops time rather than spiralling
    while (p_->acc >= kStep) {
        for (auto& v : p_->vehicles) p_->forces(v), p_->animate(v);
        for (auto& s : p_->soldiers)
            if (s.active) p_->soldier(s);
        p_->projectiles();
        p_->sys.Update(kStep, 1, &p_->temp, &p_->jobs);
        p_->acc -= kStep;
    }
}

Mat4 Physics::transform(int vehicle) const {
    RMat44 m = p_->sys.GetBodyInterface().GetWorldTransform(p_->vehicles.at(static_cast<size_t>(vehicle)).id);
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        Vec4 col = m.GetColumn4(static_cast<uint>(c));
        for (int i = 0; i < 4; ++i) r.m[c * 4 + i] = col[static_cast<uint>(i)];
    }
    return r;
}

float Physics::speed(int vehicle) const {
    return p_->sys.GetBodyInterface().GetLinearVelocity(p_->vehicles.at(static_cast<size_t>(vehicle)).id).Length();
}

}  // namespace obf
