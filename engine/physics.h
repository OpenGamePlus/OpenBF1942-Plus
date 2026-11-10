// Physics (ADR 0004): Jolt owns rigid bodies, contacts and collision queries; the Refractor vehicle parts
// (Spring wheels, Engine) are ours and act on Jolt bodies as forces. Spec: docs/formats/vehicles.md
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "con.h"
#include "math.h"
#include "scene.h"
#include "sm.h"
#include "terrain.h"

namespace obf {

struct WheelDef {
    float pos[3] = {};          // wheel centre at rest, vehicle space
    float radius = 0.4f;
    float strength = 25, damping = 5;  // setStrength / setDamping
    float steer_deg = 0, steer_rate = 0;  // RotationalBundle on c_PIYaw above the spring: max angle, deg/s
    bool driven = false;        // under the Engine
};

struct VehicleDef {
    float mass = 1000, drag = 0;
    float torque = 0, differential = 1;  // Engine setTorque / setDifferential
    std::vector<WheelDef> wheels;
    std::vector<float> hull;    // xyz points, vehicle space; the chassis is their convex hull
};

// Walks an object template tree (first LodObject child only). Throws if it has no hull geometry.
VehicleDef vehicle_def(const Con& con, const Vfs& vfs, const std::string& name);

class Physics {
public:
    Physics(const Heightmap& hm, float world_size);
    ~Physics();
    // Static collision for placed .sm meshes (first collision block); returns bodies added.
    size_t add_scene(const std::vector<DrawItem>& items, const Vfs& vfs);
    void add_static(const std::string& key, const CollisionMesh& mesh, const Mat4& world);
    int add_vehicle(const VehicleDef& def, const Mat4& world);
    void drive(int vehicle, float throttle, float steer);  // both in [-1, 1]
    void step(float dt);  // fixed 60 Hz substeps
    Mat4 transform(int vehicle) const;
    float speed(int vehicle) const;  // m/s

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace obf
