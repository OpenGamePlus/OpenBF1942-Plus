// The game layer: vehicles, soldiers, bots, weapons, damage, conquest, AI, the scripted --shot demos and the
// camera that follows the player. No SDL or GL: renderers read the scene and the soldier views.
// Rules and their status: docs/formats/vehicles.md, docs/formats/weapons.md.
#pragma once
#include <array>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "con.h"
#include "conquest.h"
#include "damage.h"
#include "hud.h"
#include "math.h"
#include "nav.h"
#include "physics.h"
#include "scene.h"
#include "terrain.h"
#include "vfs.h"

namespace obf {

struct GameOptions {
    std::string drive;       // --drive: a vehicle in front of the camera, driven unless playing
    std::string play;        // --play: team "1" / "2" or a soldier template
    int kit = 1;             // 0 scout, 1 assault, 2 anti-tank, 3 medic, 4 engineer
    bool fire_ahead = false; // vehicle demo: gun straight ahead, no steering
    bool shot = false;       // --shot: scripted demos, game time is simulated
};

// One frame of player input (keys and mouse, already mapped).
struct GameInput {
    float throttle = 0, yaw = 0, pitch = 0, roll = 0;  // vehicles, [-1, 1]
    float look_x = 0, look_y = 0;                      // turrets, mouse motion scaled
    float forward = 0, right = 0;                      // on foot
    bool jump = false, slow = false, fire = false, use = false, reload = false;
    int select = -1;  // number key: kit weapon slot (0-based), -1 none
    bool detonate = false;  // explosive packs placed by the player
};

// The lower / upper body animation states a soldier's skinned parts should play; empty: as loaded.
// hold != -1: play once from that time and stay on the last frame.
struct SoldierView {
    size_t first = 0, parts = 0;  // its items in the scene
    std::string lower, upper;
    float hold = -1;
};

// Rounds in the weapon and spare magazines. numOfMag read as all magazines, the loaded one included (unverified).
struct Ammo {
    int rounds = 0, spare = 0;
    float reload_until = -1;  // reloading while game time < this
    bool unlimited = true;
    static Ammo full(const WeaponDef& w);
    bool take(float t);    // one round; an empty magazine starts the reload
    void reload(float t, const WeaponDef& w);  // starts one if a spare magazine is left and none is running
    void tick(float t, const WeaponDef& w);    // a finished reload fills the magazine
    bool reloading(float t) const { return t < reload_until; }
    float refill = 0;  // rounds received from a supply depot towards the next spare magazine
    void supply(float rounds, const WeaponDef& w);  // adds rounds, as spare magazines up to numOfMag
};

struct Camera {
    float x = -1, y = 0, z = 0, yaw = 0.6f, pitch = -0.5f;  // x < 0: not set (default corner)
};

class Game {
public:
    // scene: the level's draw items (spawner-placed movable vehicles are removed); vehicles and soldiers are appended.
    Game(const Vfs& vfs, Con& con, const Heightmap& hm, const TerrainInfo& info, std::vector<DrawItem>& scene,
         const std::vector<SpawnPoint>& spawns, const GameOptions& opt, Camera cam, bool objects);

    void step(float t, float dt, const GameInput& in);  // live play: t is the wall clock in seconds
    void demo(float sim_seconds);                        // --shot: the scripted run for --play or --drive
    void pose();                                         // scene matrices, soldier views, camera
    bool controls_camera() const { return cur >= 0 || player >= 0; }
    HudInfo hud_info() const;                            // what the on-screen HUD shows now
    void print_weapon() const;

    // The vehicle under control: its template, position and engine load 0..1; "" on foot.
    std::string driving(float pos[3], float& control) const;

    struct SoundEvent {
        std::string tmpl;  // FireArms template that fired
        float pos[3];
    };
    std::vector<SoundEvent> sounds;  // since the frontend last cleared it

    Physics phys;
    Camera cam;
    std::vector<SoldierView> soldiers;
    std::map<std::string, int> skipped;

private:
    struct Veh {
        int id = -1;
        std::string name;
        size_t first = 0, parts = 0;
        bool plane = false;
        float len = 0, exit[3] = {-2, 0, 0}, throttle = 0;
        float hp = 100, next_fire = 0;
        int material = 0;
        bool wrecked = false;
        size_t wreck_first = 0, wreck_parts = 0;  // the wreck detail level, drawn once destroyed
        WeaponDef weapon;
        Mat4 muzzle;  // posed FireArms part, for firing
        bool has_muzzle = false;
        Mat4 start;  // where it was placed
    };
    struct Depot {  // SupplyDepot working on soldiers (ammo boxes, medic lockers, half-tracks are not placed by levels)
        float pos[3] = {}, radius = 0, health = 0;  // health: hit points per second (setHealth second value; unverified)
        int team = 0;                                // 0: both
        std::map<int, float> ammo;                   // ammoType -> rounds per second (addAmmoType third value; unverified)
    };
    std::vector<Depot> depots;
    void supply(float step);
    int nearest_depot(const float pos[3], int team, const WeaponDef& w) const;
    struct SpawnPt {
        float pos[3] = {}, yaw = 0;
        int group = -1;
    };
    struct Bot {
        int ch = -1, team = 0;
        std::string tmpl;
        size_t first = 0, parts = 0;
        float hp = 30, yaw = 0, pos[3] = {};
        bool dead = false, moving = false;
        float died_at = 0;
        WeaponDef weapon;
        Ammo ammo;
        float next_fire = 0, seen_at = -1, next_look = 0;
        int target = -2;  // -2 nobody, -1 the player, else a bot
        float check_at = 0, check_pos[2] = {}, detour_until = 0, detour_side = 1;  // getting unstuck
        std::vector<float> path;  // waypoints (x, z) to the flag it runs for
        size_t waypoint = 0;
        int path_goal = -1;       // flag index the path leads to
        float replan_at = 0;
    };

    const VehicleDef* def_for(const std::string& name);
    float ground(float x, float z) const;
    void add_vehicle(const std::string& name, Mat4 world);
    const ConObject* object_template(const std::string& n) const;
    float hitpoints(const std::string& n) const;
    std::string primary(int tm, int kt) const;
    std::vector<std::string> kit_weapons(int tm, int kt) const;  // kit order, those with a projectile
    int spawn_team(const SpawnPt& sp) const;
    void get_in_out();
    void walk(float forward, float right, bool jump, bool slow);
    void fire(float t);
    float game_time() const { return now; }
    void player_dies();
    void apply_hits();
    void think(float t, float step);
    void demo_play(float sim_seconds);
    void demo_vehicle();

    const Vfs& vfs;
    Con& con;
    const Heightmap& hm;
    const TerrainInfo& info;
    std::vector<DrawItem>& scene;
    GameOptions opt;

    std::vector<Veh> vehicles;
    int cur = -1;  // the vehicle under control
    std::map<std::string, std::unique_ptr<VehicleDef>> defs;  // by template; null if it cannot move

    int player = -1, team = 2;
    size_t sol_first = 0, sol_parts = 0;
    std::map<int, std::string> team_skins;
    std::map<std::pair<int, int>, std::string> kits;
    std::string soldier_tmpl, weapon_name = "M1Garand", lower_state = "lb_stand";
    float move_f = 0, move_r = 0;

    Conquest game;
    std::vector<SpawnPt> spawn_points;
    std::vector<Bot> bots;
    NavGrid nav;  // infantry map of the level, 2 m cells; empty: bots run straight
    std::array<int, 3> deaths{0, 0, 0};
    int detours = 0;  // sidesteps taken by stuck bots
    DamageTable damage;
    WeaponDef hand;  // the weapon in hand, and its ammunition
    Ammo hand_ammo;
    std::vector<std::pair<WeaponDef, Ammo>> loadout;  // the kit's weapons that fire; the held one's ammo lives in hand_ammo
    size_t slot = 0;
    void select(size_t i);
    void refill_loadout();
    bool fire_held = false;  // trigger held last step (fireOnce weapons)
    struct Placed {  // the player's mines (go off near a vehicle) and explosive packs (go off on detonate)
        int shot = -1;
        float near = 0;
    };
    std::vector<Placed> placed;
    void placed_charges(bool detonate);
    float hand_next = 0, player_hp = 30, respawn_at = -1, now = 0;
    bool player_dead = false;
    std::vector<std::pair<float, std::string>> feed;  // (game time, message): kills, captures, destroyed vehicles
    void note(std::string m) {
        while (!feed.empty() && now - feed.front().first > 6) feed.erase(feed.begin());
        feed.emplace_back(now, std::move(m));
    }
    std::mt19937 rng{1942};  // AI aim error; fixed seed: --shot runs repeat
};

}  // namespace obf
