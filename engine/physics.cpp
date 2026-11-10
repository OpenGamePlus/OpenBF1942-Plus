#include "physics.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>
#include <stdexcept>

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
    int visits = 0;

    void walk(const std::string& name, const Mat4& world, int depth, WheelDef ctx) {
        const ConObject* t = find(con, name);
        if (!t || depth > 16 || ++visits > 10000) return;
        std::string type = lower(t->type);
        if (type == "engine") ctx.driven = true;
        bool yaw_input = false;
        float max_yaw = 0, rate = 0;
        for (auto& [k, a] : t->props) {
            if (a.empty()) continue;
            if (depth == 0 && k == "mass") def.mass = num(a);
            if (depth == 0 && k == "drag") def.drag = num(a);
            if (type == "engine" && k == "settorque") def.torque = num(a);
            if (type == "engine" && k == "setdifferential") def.differential = num(a);
            if (type == "rotationalbundle" && k == "setinputtoyaw" && lower(a[0]) == "c_piyaw") yaw_input = true;
            if (k == "setmaxrotation" || k == "setmaxspeed") {
                float v[3];
                parse_vec3(a[0], v);
                (k == "setmaxrotation" ? max_yaw : rate) = std::fabs(v[0]);
            }
            if (type == "spring" && k == "setstrength") ctx.strength = num(a);
            if (type == "spring" && k == "setdamping") ctx.damping = num(a);
        }
        if (yaw_input) ctx.steer_deg = max_yaw, ctx.steer_rate = rate;
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
        for (auto& c : children)
            walk(c.name, world * translate(c.pos[0], c.pos[1], c.pos[2]) * rotation_ypr(c.rot[0], c.rot[1], c.rot[2]), depth + 1, ctx);
    }
};

}  // namespace

VehicleDef vehicle_def(const Con& con, const Vfs& vfs, const std::string& name) {
    VehicleDef def;
    Walker w{con, def, {}, 0};
    w.walk(name, Mat4::identity(), 0, WheelDef{});
    std::map<std::string, int> skipped;
    std::map<std::string, SmMesh> cache;
    for (const DrawItem& item : build_object(con, vfs, name, Mat4::identity(), skipped)) {
        if (item.mesh.size() < 3 || item.mesh.compare(item.mesh.size() - 3, 3, ".sm") != 0) continue;
        auto it = cache.find(item.mesh);
        if (it == cache.end()) it = cache.emplace(item.mesh, sm_parse(vfs.read(item.mesh))).first;
        const SmMesh& m = it->second;
        if (std::find(w.owner.begin(), w.owner.end(), item.owner) != w.owner.end()) {  // wheel radius from mesh height
            float r = (m.bbox[4] - m.bbox[1]) / 2;
            if (std::isfinite(r) && r > 0.05f && r < 5)
                for (size_t i = 0; i < def.wheels.size(); ++i)
                    if (w.owner[i] == item.owner) def.wheels[i].radius = r;
            continue;
        }
        float p[3];
        bool any = false;
        for (auto& c : m.collision)
            for (size_t v = 0; v + 2 < c.vertices.size(); v += 3) {
                xform(item.world, c.vertices[v], c.vertices[v + 1], c.vertices[v + 2], p);
                def.hull.insert(def.hull.end(), p, p + 3), any = true;
            }
        if (!any)  // no collision block: bounding box corners
            for (int i = 0; i < 8; ++i) {
                xform(item.world, m.bbox[i & 1 ? 3 : 0], m.bbox[i & 2 ? 4 : 1], m.bbox[i & 4 ? 5 : 2], p);
                def.hull.insert(def.hull.end(), p, p + 3);
            }
    }
    def.hull.erase(std::remove_if(def.hull.begin(), def.hull.end(), [](float f) { return !std::isfinite(f); }), def.hull.end());
    def.hull.resize(def.hull.size() / 3 * 3);
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
        float throttle = 0, steer = 0;
        std::vector<float> wheel_steer;
    };
    std::vector<Vehicle> vehicles;
    float acc = 0;

    void forces(Vehicle& v);
};

// Jolt's allocator hooks must exist before Impl's members (TempAllocator) are built.
Physics::Physics(const Heightmap& hm, float world_size) : p_((jolt_init(), std::make_unique<Impl>())) {
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
        // shortcut: trees (.tm) and animated meshes get no collision; spawned vehicles are static obstacles.
        if (!item.skin.empty() || item.mesh.size() < 3 || item.mesh.compare(item.mesh.size() - 3, 3, ".sm") != 0) continue;
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
    Array<Vec3> pts;
    for (size_t i = 0; i + 2 < def.hull.size(); i += 3) pts.push_back(Vec3(def.hull[i], def.hull[i + 1], def.hull[i + 2]));
    ConvexHullShapeSettings hs(pts, 0.05f);
    auto res = hs.Create();
    if (res.HasError()) throw std::runtime_error(std::string("vehicle hull: ") + res.GetError().c_str());
    Vec3 c0(world.m[0], world.m[1], world.m[2]), c1(world.m[4], world.m[5], world.m[6]), c2(world.m[8], world.m[9], world.m[10]);
    Quat rot = Mat44(Vec4(c0.Normalized(), 0), Vec4(c1.Normalized(), 0), Vec4(c2.Normalized(), 0), Vec4(0, 0, 0, 1)).GetQuaternion().Normalized();
    BodyCreationSettings bs(res.Get(), RVec3(world.m[12], world.m[13], world.m[14]), rot, EMotionType::Dynamic, kMoving);
    bs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
    bs.mMassPropertiesOverride.mMass = def.mass;
    bs.mLinearDamping = 0;  // drag is ours
    bs.mAngularDamping = 0.5f;
    bs.mAllowSleeping = false;
    Impl::Vehicle v{def, p_->sys.GetBodyInterface().CreateAndAddBody(bs, EActivation::Activate), 0, 0, {}};
    v.wheel_steer.assign(def.wheels.size(), 0.0f);
    p_->vehicles.push_back(std::move(v));
    return static_cast<int>(p_->vehicles.size() - 1);
}

void Physics::drive(int vehicle, float throttle, float steer) {
    auto& v = p_->vehicles.at(static_cast<size_t>(vehicle));
    v.throttle = std::clamp(throttle, -1.0f, 1.0f), v.steer = std::clamp(steer, -1.0f, 1.0f);
}

// Our Refractor parts. Units of the template values are unverified (docs/formats/vehicles.md):
// strength and damping are read per unit of the mass each wheel carries (s^-2, s^-1), which puts a Willys
// (25, 5) at 0.8 Hz, damping ratio 0.5, resting where the model places its wheels.
void Physics::Impl::forces(Vehicle& v) {
    BodyInterface& bi = sys.GetBodyInterface();
    const VehicleDef& d = v.def;
    if (d.wheels.empty()) return;
    RMat44 T = bi.GetWorldTransform(v.id);
    Vec3 up = T.GetAxisY(), lin = bi.GetLinearVelocity(v.id), ang = bi.GetAngularVelocity(v.id);
    RVec3 com = bi.GetCenterOfMassPosition(v.id);
    float mw = d.mass / static_cast<float>(d.wheels.size());
    int driven = static_cast<int>(std::count_if(d.wheels.begin(), d.wheels.end(), [](const WheelDef& w) { return w.driven; }));
    // shortcut: drive and drag scales are placeholders (Willys: 3.7 m/s^2, ~70 km/h); calibrate against the original.
    float a_drive = d.torque * d.differential / 20.0f;
    const float mu = 1.0f;  // shortcut: c_PGF* grip models and materialFriction not applied yet
    IgnoreSingleBodyFilter self(v.id);
    RayCastSettings rs;
    rs.SetBackFaceMode(EBackFaceMode::CollideWithBackFaces);  // collision mesh winding is unverified
    for (size_t i = 0; i < d.wheels.size(); ++i) {
        const WheelDef& w = d.wheels[i];
        float target = v.steer * w.steer_deg, step = w.steer_rate > 0 ? w.steer_rate * kStep : 1e9f;
        v.wheel_steer[i] += std::clamp(target - v.wheel_steer[i], -step, step);
        float rest = std::clamp(w.strength > 0 ? kGravity / w.strength : 0.3f, 0.05f, 1.0f);
        // Ray from rest above the wheel centre; compression is rest (load = weight) with the wheel at its model position.
        float len = 2 * rest + w.radius;
        RRayCast ray{T * Vec3(w.pos[0], w.pos[1] + rest, w.pos[2]), -up * len};
        ClosestHitCollisionCollector<CastRayCollector> hit;
        sys.GetNarrowPhaseQuery().CastRay(ray, rs, hit, {}, {}, self);
        if (!hit.HadHit()) continue;
        float x = len - hit.mHit.mFraction * len;
        if (x <= 0) continue;
        RVec3 p = ray.GetPointOnRay(hit.mHit.mFraction);
        Vec3 n = up;
        {
            BodyLockRead lock(sys.GetBodyLockInterface(), hit.mHit.mBodyID);
            if (lock.Succeeded()) n = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mHit.mSubShapeID2, p);
        }
        Vec3 vp = lin + ang.Cross(Vec3(p - com));
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
        if (v.throttle == 0) Ff = std::fabs(vf) < 0.5f ? -0.5f * vf * mw / kStep : -2.0f * vf * mw;
        float lim = mu * N, tot = std::sqrt(Fs * Fs + Ff * Ff);
        if (tot > lim && tot > 0) Fs *= lim / tot, Ff *= lim / tot;
        bi.AddForce(v.id, up * N, p);
        // shortcut: grip acts near centre-of-mass height (arcade roll resistance); tune once the original is measured.
        bi.AddForce(v.id, side * Fs + f * Ff, p + up * (0.8f * Vec3(com - p).Dot(up)));
    }
    float sp = lin.Length();
    bi.AddForce(v.id, -lin * (d.drag * d.mass * sp / 150.0f));
}

void Physics::step(float dt) {
    p_->acc = std::min(p_->acc + dt, 8 * kStep);  // a long frame drops time rather than spiralling
    while (p_->acc >= kStep) {
        for (auto& v : p_->vehicles) p_->forces(v);
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
