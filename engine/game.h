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
    bool crouch = false;    // held
    bool prone = false;     // pressed this frame: lie down / get up
    bool view = false;      // pressed this frame: first / third person
    int seat = -1;          // F1..F8 pressed: move to that seat of the vehicle (0-based)
};

// The lower / upper body animation states a soldier's skinned parts should play; empty: as loaded.
// hold != -1: play once from that time and stay on the last frame.
struct SoldierView {
    size_t first = 0, parts = 0;  // its items in the scene
    std::string lower, upper;
    float hold = -1;
    size_t held_first = 0, held_parts = 0;  // the weapon in its right hand (items with a bone); the renderer places them
    bool first_person = false;               // play the states' first-person animations
    float fov = 0;                           // first person: field of view of the arms (set1pFov), radians
    float upper_hold = -1;                   // >= 0: the upper state plays once from this time (reloads)
    std::string weapon;                      // state animating the held weapon's skeleton (WeaponReloadThompson)
    float weapon_hold = -1;                  // as upper_hold, for the weapon state
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
    struct Seat {
        WeaponDef weapon;
        Mat4 muzzle;  // posed FireArms part, for firing
        bool has_muzzle = false;
        float next_fire = 0, exit[3] = {-2, 0, 0}, pos[3] = {};  // pos: the SeatObject, vehicle space
        Mat4 at;                     // the SeatObject's transform, vehicle space
        int show = 0;                // as SeatDef::show
        std::string lower, upper;    // the seated soldier's animation states
        int who = -2;  // -2 empty, -1 the player, else a bot
    };
    struct Veh {
        int id = -1;
        std::string name;
        size_t first = 0, parts = 0;
        bool plane = false, ship = false;
        int team = 0;  // of its crew; 0 empty
        float len = 0, throttle = 0, muzzles_at = -1;
        float hp = 100;
        int material = 0;
        bool wrecked = false;
        size_t wreck_first = 0, wreck_parts = 0;  // the wreck detail level, drawn once destroyed
        std::vector<Seat> seats;                  // [0] the driver
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
        int veh = -1, team = 0;  // on a ship (moves with vehicle `veh`, local offset `local`), owned by `team`
        float local[3] = {};
    };
    void move_spawns();  // spawn points on vehicles follow them
    struct Bot {
        int ch = -1, team = 0;
        std::string tmpl;
        size_t first = 0, parts = 0;
        size_t held_first = 0, held_parts = 0;  // its primary weapon's draw items
        float hp = 30, yaw = 0, pos[3] = {};
        bool dead = false, moving = false;
        float died_at = 0;
        WeaponDef weapon;
        Ammo ammo;
        int kit = 1, posture = 0;  // posture as Physics::set_posture
        WeaponDef grenade;         // the kit's hand grenade, if any
        Ammo grenades;
        float next_grenade = 0;
        float next_fire = 0, seen_at = -1, next_look = 0, last_shot = -10;
        int target = -2;  // -2 nobody, -1 the player, else a bot
        float check_at = 0, check_pos[2] = {}, detour_until = 0, detour_side = 1;  // getting unstuck
        int veh = -1, vseat = -1;  // the vehicle and seat it sits in
        float board_at = 0;        // next look for a vehicle to board
        float reverse_until = 0;   // driving: backing out of a jam
        int jams = 0;              // stuck checks in a row while driving; gives up the vehicle at 4
        std::vector<float> path;  // waypoints (x, z) to the flag it runs for
        size_t waypoint = 0;
        int path_goal = -1;       // flag index the path leads to
        float replan_at = 0;
    };

    const VehicleDef* def_for(const std::string& name);
    float ground(float x, float z) const;
    void add_vehicle(const std::string& name, Mat4 world, int team = 0);
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
    int seat = 0;  // the player's seat in vehicle `cur`
    void take_seat(int s);
    size_t sol_first = 0, sol_parts = 0;
    std::map<int, std::string> team_skins;
    std::map<std::pair<int, int>, std::string> kits;
    std::string soldier_tmpl, weapon_name = "M1Garand", lower_state = "lb_stand";
    float move_f = 0, move_r = 0;

    Conquest game;
    std::vector<SpawnPt> spawn_points;
    std::vector<Bot> bots;
    NavGrid nav;  // infantry map of the level, 2 m cells; empty: bots run straight
    NavGrid tank_nav;  // tank map, 4 m cells; empty: bot drivers go straight
    NavGrid boat_nav;  // landing craft (else boat) map, 4 m cells; empty: boats go straight
    bool troop_boat(const Veh& v) const { return v.ship && !v.plane && v.len < 8; }  // LCVP, Higgins, Daihatsu
    void disembark(Veh& v, float t);
    void board(Bot& b, float t);
    void crew(Bot& b, float t, float step, const float* target, int target_veh);
    void leave(Bot& b);
    void refresh_muzzles(Veh& v);
    void seat_point(const Veh& v, size_t s, float out[3]) const;
    const Seat* shown_seat(int veh, int s) const;  // the seat if its soldier is drawn (seatFlags), else null
    Mat4 seated(int veh, const Seat& st) const;     // where a seated soldier's model goes
    std::array<int, 3> deaths{0, 0, 0};
    int detours = 0;  // sidesteps taken by stuck bots
    int grenades_thrown = 0, boardings = 0, wrecks = 0;
    DamageTable damage;
    WeaponDef hand;  // the weapon in hand, and its ammunition
    Ammo hand_ammo;
    std::vector<std::pair<WeaponDef, Ammo>> loadout;  // the kit's weapons that fire; the held one's ammo lives in hand_ammo
    size_t slot = 0;
    std::vector<std::pair<size_t, size_t>> loadout_items;  // per slot: first draw item, count (held weapon models)
    size_t add_weapon_items(const std::string& weapon);    // appends a hand weapon's parts far below; returns the count
    void select(size_t i);
    void refill_loadout();
    bool fire_held = false;  // trigger held last step (fireOnce weapons)
    float last_shot = -10;   // the player's, game time
    // Upper-body and weapon states for a soldier holding `w`: firing (shot within the last moment), reloading.
    void arm_states(SoldierView& v, const std::string& w, const WeaponDef& def, const Ammo& ammo, float shot, int pose) const;
    int posture = 0;         // the player's: 0 stand, 1 crouch, 2 prone
    size_t chute_first = 0, chute_parts = 0;  // the parachute's draw items (far below while closed)
    size_t fp_first = 0, fp_parts = 0;        // first-person arms and body (far below in third person)
    bool first_person = false;
    float hands[3] = {-0.12f, -1.56f, 0.1f};  // center1pHands: the 1p rig's origin from the camera
    float fov1p = 0;                            // set1pFov (0.47): the arms' field of view
    float chute_k = 0;                         // setParachuteDrag / mass of the player's soldier
    float fall_damage(int character, float full);  // hit points lost on the last landing
    bool lying = false;      // prone toggled on
    float eye_height() const { return posture == 2 ? 0.35f : posture == 1 ? 1.0f : 1.5f; }
    static float body_height(int p) { return p == 2 ? 0.7f : p == 1 ? 1.2f : 1.8f; }  // as the physics capsules
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
