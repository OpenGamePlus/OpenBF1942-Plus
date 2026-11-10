// Physics (ADR 0004): Jolt owns rigid bodies, contacts and collision queries; the Refractor vehicle parts
// (Spring wheels, Engine) are ours and act on Jolt bodies as forces. Spec: docs/formats/vehicles.md
#pragma once
#include <map>
#include <memory>
#include <utility>
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

// Wing: a flat surface pushing along its normal (local y). Control input deflects it (flap lift).
struct WingDef {
    float pos[3] = {}, normal[3] = {0, 1, 0};  // force point (position + setPositionOffset), vehicle space
    float lift = 0, flap = 0, regulate = 0;    // setWingLift, setFlapLift, setRegulateToLift (always 4.91 in retail)
    int input = 0;                             // setInputToPitch: 0 none, 1 c_PIPitch, 2 c_PIRoll, 3 c_PIYaw
    float sign = 1, rate = 4;                  // direction (sign of setAcceleration), full deflections per second
    float range = 0;                           // degrees at full deflection (drawing)
};

// FloatingBundle: a buoyant column from pos down setHullHeight; lifts setFloatMaxLift x mass x submerged fraction.
struct FloatDef {
    float pos[3] = {};
    float height = 1, lift = 0;
};

// A moving part, drawn through build_object's JointFn. Generic joints follow an input per axis (yaw, pitch, roll):
// absolute inputs drive towards min/max, mouse-look inputs move at setMaxSpeed and stop at the limits.
struct JointDef {
    enum Input { None, Pitch, Roll, Yaw, Throttle, LookX, LookY };
    Input input[3] = {None, None, None};
    float mn[3] = {}, mx[3] = {}, speed[3] = {}, sign[3] = {1, 1, 1};
    bool reset = false;
    int wheel = -1, wing = -1;  // wheels spin and ride their springs; wings show their deflection
    bool propeller = false;     // spins with the throttle
};

struct VehicleDef {
    float mass = 1000, drag = 0;
    float torque = 0, differential = 1;  // Engine setTorque / setDifferential
    bool tracked = false;       // c_ETTank: the engine turns the hull (setInputToYaw) instead of steering wheels
    float turn_rate = 0;        // deg/s at full steer, tracked only
    bool plane = false;         // c_ETPlane: the engine is thrust, wheels roll freely
    bool ship = false;          // has a c_ETShip engine: propeller thrust only in water
    float thrust = 0;           // aircraft and ships: m/s^2 at full throttle, summed over engines
    std::vector<FloatDef> floats;
    std::map<std::pair<std::string, int>, JointDef> joints;  // (lower template name, occurrence) as JointFn
    float exit[3] = {-2, 0, 0};  // setSoldierExitLocation, vehicle space
    std::vector<WingDef> wings;
    std::vector<WheelDef> wheels;
    std::vector<float> hull;    // xyz points, vehicle space; the chassis is their convex hull
};

// Walks an object template tree (first LodObject child only). Throws if it has no hull geometry.
VehicleDef vehicle_def(const Con& con, const Vfs& vfs, const std::string& name);

class Physics {
public:
    // water_level: sea surface height (Terrain.con waterLevel); floats need it.
    Physics(const Heightmap& hm, float world_size, float water_level = -1e9f);
    ~Physics();
    // Static collision for placed .sm meshes (first collision block); returns bodies added.
    size_t add_scene(const std::vector<DrawItem>& items, const Vfs& vfs);
    void add_static(const std::string& key, const CollisionMesh& mesh, const Mat4& world);
    int add_vehicle(const VehicleDef& def, const Mat4& world);
    // Inputs in [-1, 1], named after Refractor's c_PI*: yaw +1 = right (cars steer, tanks turn, rudders); roll +1 = right
    // wing down; pitch +1 = nose down (stick forward).
    void drive(int vehicle, float throttle, float yaw, float pitch = 0, float roll = 0);
    void set_velocity(int vehicle, float x, float y, float z);
    void look(int vehicle, float x, float y);  // c_PIMouseLookX / Y in [-1, 1] (turrets, guns)
    // Local transform of a moving part for build_object (identity for parts that do not move).
    Mat4 joint(int vehicle, const std::string& name, int occurrence) const;
    void step(float dt);  // fixed 60 Hz substeps
    Mat4 transform(int vehicle) const;
    float speed(int vehicle) const;  // m/s

    // Soldiers: a 1.8 m capsule (Jolt CharacterVirtual) standing at feet position (x, y, z). It walks on the terrain,
    // static meshes and vehicles, steps up 0.4 m, falls, jumps, and swims at the surface.
    int add_character(float x, float y, float z);
    void walk(int character, float vx, float vz, bool jump);  // wanted horizontal velocity, m/s
    void place_character(int character, float x, float y, float z);
    void set_character_active(int character, bool active);    // inactive: not simulated (in a vehicle)
    void character_position(int character, float out[3]) const;
    bool character_on_ground(int character) const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace obf
