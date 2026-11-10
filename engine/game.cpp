#include "game.h"

#include <functional>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <tuple>

namespace obf {

namespace {

// Third-person view only: the engine swaps these parts by camera and state at runtime.
bool hide_1p(const std::string& t) { return t == "parachute" || t.rfind("1p", 0) == 0 || t.find("1pbody") != std::string::npos; }
bool hide_3p(const std::string& t) { return t == "parachute" || !(t.rfind("1p", 0) == 0 || t.find("1pbody") != std::string::npos); }

}  // namespace

Game::Game(const Vfs& vfs_, Con& con_, const Heightmap& hm_, const TerrainInfo& info_, std::vector<DrawItem>& scene_,
           const std::vector<SpawnPoint>& spawns, const GameOptions& opt_, Camera cam_, bool objects)
    : phys(hm_, info_.world_size, info_.water_level), cam(cam_), vfs(vfs_), con(con_), hm(hm_), info(info_), scene(scene_), opt(opt_) {
    if (objects) std::printf("physics: %zu static bodies\n", phys.add_scene(scene, vfs));
    const std::string& drive = opt.drive;
    const std::string& play = opt.play;
    float cx = cam.x, cz = cam.z, yaw = cam.yaw;

    // Vehicles: the --drive one in front of the camera and, when driving or playing, every parked vehicle that can move
    // (wheels, wings or floats) from the level's spawners. Their parts are appended to the scene and posed each frame.
    if (objects && (!drive.empty() || !play.empty())) {
        std::vector<char> movable(spawns.size());
        for (size_t i = 0; i < spawns.size(); ++i) movable[i] = def_for(spawns[i].name) != nullptr;
        scene.erase(std::remove_if(scene.begin(), scene.end(), [&](const DrawItem& d) { return d.spawn >= 0 && movable[static_cast<size_t>(d.spawn)]; }),
                    scene.end());
        for (size_t i = 0; i < spawns.size(); ++i)
            if (movable[i]) add_vehicle(spawns[i].name, spawns[i].world, spawns[i].team);
        std::printf("vehicles: %zu of %zu spawners\n", vehicles.size(), spawns.size());
    }
    if (objects && !drive.empty()) {
        float sx = cx < 0 ? info.world_size * 0.15f : cx, sz = cx < 0 ? info.world_size * 0.15f : cz;
        sx += std::sin(yaw) * 10, sz += std::cos(yaw) * 10;
        float cell = info.world_size / static_cast<float>(hm.n);
        int r = std::clamp(static_cast<int>(sz / cell), 0, hm.n - 1), c = std::clamp(static_cast<int>(sx / cell), 0, hm.n - 1);
        size_t before = vehicles.size();
        add_vehicle(drive, translate(sx, std::max(hm.height(r, c), info.water_level) + 1, sz) * rotation_ypr(yaw * 57.29578f, 0, 0));
        if (vehicles.size() == before) throw std::runtime_error("--drive " + drive + ": not a vehicle that can move");
        if (play.empty()) cur = static_cast<int>(before);
        const VehicleDef& def = *def_for(drive);
        std::printf("drive %s: mass %.0f, %zu wheels, %zu wings, %zu floats, %zu hull points, %zu parts\n", drive.c_str(), def.mass,
                    def.wheels.size(), def.wings.size(), def.floats.size(), def.hull.size() / 3, vehicles.back().parts);
    }

    // Playable soldier (--play): a character capsule at the camera spot; its parts are appended like the vehicle's.
    // Teams (level Init.con): soldier template per team (game.setTeamSkin), kit per team and class (game.setKit).
    // --play 1 / 2 picks a team; --play <template> a soldier, team by its skin (else 2).
    sol_first = scene.size();
    for (auto& [m, a] : con.settings["game"]) {
        if (m == "setteamskin" && a.size() >= 2) team_skins[std::atoi(a[0].c_str())] = a[1];
        if (m == "setkit" && a.size() >= 3) kits[{std::atoi(a[0].c_str()), std::atoi(a[1].c_str())}] = a[2];
    }
    soldier_tmpl = play;
    if (play == "1" || play == "2") team = std::atoi(play.c_str()), soldier_tmpl = team_skins.count(team) ? team_skins[team] : "";
    else
        for (auto& [t, sk] : team_skins)
            if (lower(sk) == lower(play)) team = t;
    if (!play.empty() && soldier_tmpl.empty()) throw std::runtime_error("--play " + play + ": the level sets no soldier for that team");
    if (!primary(team, opt.kit).empty()) weapon_name = primary(team, opt.kit);
    if (objects && !play.empty()) {
        float sx = cx < 0 ? info.world_size * 0.15f : cx, sz = cx < 0 ? info.world_size * 0.15f : cz;
        float cell = info.world_size / static_cast<float>(hm.n);
        int r = std::clamp(static_cast<int>(sz / cell), 0, hm.n - 1), c = std::clamp(static_cast<int>(sx / cell), 0, hm.n - 1);
        player = phys.add_character(sx, std::max(hm.height(r, c), info.water_level - 1.4f) + 0.5f, sz);
        for (DrawItem& item : build_object(con, vfs, soldier_tmpl, Mat4::identity(), skipped, nullptr, hide_1p)) {
            ++sol_parts;
            scene.push_back(std::move(item));
        }
        fp_first = scene.size();
        for (DrawItem& item : build_object(con, vfs, soldier_tmpl, translate(0, -10000, 0), skipped, nullptr, hide_3p))
            ++fp_parts, scene.push_back(std::move(item));
        first_person = !opt.shot && fp_parts > 0;  // live play starts in first person, as the original
        chute_first = scene.size();
        for (DrawItem& item : build_object(con, vfs, "Parachute", translate(0, -10000, 0), skipped)) ++chute_parts, scene.push_back(std::move(item));
        if (const ConObject* st = object_template(soldier_tmpl)) {
            float drag = 0, mass = 100;
            for (auto& [k, a] : st->props)
                if (k == "setparachutedrag" && !a.empty()) drag = static_cast<float>(std::atof(a[0].c_str()));
                else if (k == "mass" && !a.empty()) mass = std::max(1.0f, static_cast<float>(std::atof(a[0].c_str())));
                else if (k == "center1phands" && !a.empty()) parse_vec3(a[0], hands);
            chute_k = std::isfinite(drag) ? std::max(0.0f, drag) / mass : 0.0f;
        }
        std::printf("play %s (team %d, kit %s): %zu parts\n", soldier_tmpl.c_str(), team,
                    kits.count({team, opt.kit}) ? kits[{team, opt.kit}].c_str() : "-", sol_parts);
    }
    // Conquest (engine/conquest.*): flags change hands, tickets run down. Bots of both teams start on the spawn points
    // their team owns: a SpawnPoint's setGroup is a ControlPoint's spawnGroupId, owned by that flag's team (0: nobody).
    game = conquest_from(con);
    if (player >= 0) {
        // Infantry navigation: the level's <Map>/Pathfinding/*infantr*level0map.raw (the name varies by level).
        for (const std::string& p : vfs.paths(con.level_dir + "/pathfinding/")) {
            if (p.find("infantr") == std::string::npos || p.size() < 13 || p.compare(p.size() - 13, 13, "level0map.raw") != 0) continue;
            try {
                NavGrid g = nav_parse(vfs.read(p), info.world_size);
                nav = nav_coarse(g, std::max(1, static_cast<int>(2.0f / g.cell + 0.5f)));
            } catch (const std::exception& e) {
                std::printf("navigation %s: %s\n", p.c_str(), e.what());
            }
            break;
        }
        // Vehicle maps: Tank0level0map.raw; LandingCraft3level0map.raw, else Boat2level0map.raw (4 levels of 78).
        for (const char* kind : {"tank", "boat", "landingcraft"})
            for (const std::string& p : vfs.paths(con.level_dir + "/pathfinding/")) {
                std::string f = p.substr(p.rfind('/') + 1);
                if (f.rfind(kind, 0) != 0 || f.size() < 13 || f.compare(f.size() - 13, 13, "level0map.raw") != 0) continue;
                try {
                    NavGrid g = nav_parse(vfs.read(p), info.world_size);
                    (std::string(kind) == "tank" ? tank_nav : boat_nav) = nav_coarse(g, std::max(1, static_cast<int>(4.0f / g.cell + 0.5f)));
                } catch (const std::exception& e) {
                    std::printf("navigation %s: %s\n", p.c_str(), e.what());
                }
                break;
            }
        // Supply depots under placed objects (Ammobox -> AmmoboxSupplyDepot); shortcut: at the root's position.
        std::function<const ConObject*(const std::string&, int)> depot_of = [&](const std::string& n, int depth) -> const ConObject* {
            const ConObject* t = object_template(n);
            if (!t || depth > 6) return nullptr;
            if (lower(t->type) == "supplydepot") return t;
            for (auto& [k, a] : t->props)
                if (k == "addtemplate" && !a.empty())
                    if (const ConObject* d = depot_of(a[0], depth + 1)) return d;
            return nullptr;
        };
        for (auto& inst : con.instances) {
            const ConObject* t = inst.deleted ? nullptr : depot_of(inst.name, 0);
            if (!t) continue;
            Depot d;
            bool soldiers = true;
            for (auto& [k, a] : t->props) {
                if (a.empty()) continue;
                if (k == "radius") d.radius = static_cast<float>(std::atof(a[0].c_str()));
                else if (k == "team") d.team = std::atoi(a[0].c_str());
                else if (k == "workonsoldiers") soldiers = std::atoi(a[0].c_str()) != 0;
                else if (k == "sethealth" && a.size() >= 2) d.health = std::max(0.0f, static_cast<float>(std::atof(a[1].c_str())));
                else if (k == "addammotype" && a.size() >= 3) d.ammo[std::atoi(a[0].c_str())] = static_cast<float>(std::atof(a[2].c_str()));
            }
            for (auto& [k, a] : inst.props)
                if (k == "absoluteposition" && !a.empty()) parse_vec3(a[0], d.pos);
            if (soldiers && d.radius > 0 && (d.health > 0 || !d.ammo.empty())) depots.push_back(d);
        }
        for (auto& inst : con.instances) {
            const ConObject* t = inst.deleted ? nullptr : object_template(inst.name);
            if (!t || lower(t->type) != "spawnpoint") continue;
            SpawnPt sp;
            for (auto& [k, a] : t->props)
                if (k == "setgroup" && !a.empty()) sp.group = std::atoi(a[0].c_str());
            float rot[3] = {};
            for (auto& [k, a] : inst.props)
                if (k == "absoluteposition" && !a.empty()) parse_vec3(a[0], sp.pos);
                else if (k == "rotation" && !a.empty()) parse_vec3(a[0], rot);
            sp.yaw = rot[0];
            spawn_points.push_back(sp);
        }
        move_spawns();
        bool skipped_one = false;  // the player takes one of its team's places
        for (const SpawnPt& sp : spawn_points) {
            int tm = spawn_team(sp);
            if (tm == 0 || !team_skins.count(tm)) continue;
            if (tm == team && !skipped_one) {
                skipped_one = true;
                continue;
            }
            Bot b;
            b.team = tm, b.tmpl = team_skins[tm], b.yaw = sp.yaw, b.hp = hitpoints(b.tmpl), b.first = scene.size();
            b.kit = static_cast<int>(bots.size() % 5);  // the five classes in turn
            b.weapon = weapon_def(con, primary(tm, b.kit));
            b.ammo = Ammo::full(b.weapon);
            for (const std::string& w : kit_weapons(tm, b.kit))
                if (WeaponDef g = weapon_def(con, w); g.bounce && g.radius > 0 && g.ttl < 60 && b.grenade.name.empty()) b.grenade = g;
            b.grenades = Ammo::full(b.grenade);
            std::copy(sp.pos, sp.pos + 3, b.pos);
            b.ch = phys.add_character(sp.pos[0], sp.pos[1] + 0.1f, sp.pos[2]);
            for (DrawItem& item : build_object(con, vfs, b.tmpl, Mat4::identity(), skipped, nullptr, hide_1p)) ++b.parts, scene.push_back(std::move(item));
            b.held_first = scene.size(), b.held_parts = add_weapon_items(b.weapon.name);
            bots.push_back(std::move(b));
        }
        int n1 = static_cast<int>(std::count_if(bots.begin(), bots.end(), [](const Bot& b) { return b.team == 1; }));
        std::printf("conquest: %zu flags, tickets %.0f / %.0f; bots: %d team 1, %zu team 2; %zu spawn points; navigation %d x %d; %zu supply depots\n",
                    game.flags.size(), game.tickets[1], game.tickets[2], n1, bots.size() - static_cast<size_t>(n1), spawn_points.size(), nav.n, nav.n, depots.size());
    }
    // Combat: damage tables, the soldier's kit weapon.
    damage = damage_table(con.settings["materialmanager"]);
    hand = weapon_def(con, weapon_name);
    for (const std::string& w : kit_weapons(team, opt.kit)) loadout.push_back({weapon_def(con, w), Ammo::full(weapon_def(con, w))});
    if (loadout.empty() && !hand.name.empty()) loadout.push_back({hand, Ammo::full(hand)});
    if (!loadout.empty()) hand = loadout[0].first;
    if (player >= 0)
        for (auto& [w, a] : loadout) loadout_items.push_back({scene.size(), add_weapon_items(w.name)});
    hand_ammo = Ammo::full(hand);
    player_hp = hitpoints(soldier_tmpl);
    if (cam.x < 0) cam.x = info.world_size * 0.15f, cam.y = 300, cam.z = info.world_size * 0.15f;  // default: corner, looking across
}

void Game::print_weapon() const {
    if (player >= 0)
        std::printf("weapon %s: %.0f m/s every %.2f s, material %d; damage table: %zu materials, %zu modifiers\n", hand.name.c_str(),
                    hand.velocity, hand.interval, hand.material, damage.materials.size(), damage.mods.size());
    if (player >= 0 && !loadout.empty()) {
        std::printf("kit:");
        for (size_t i = 0; i < loadout.size(); ++i) {
            const WeaponDef& w = loadout[i].first;
            std::printf(" %zu %s (%d x %d%s)", i + 1, w.name.c_str(), w.mag_size, w.mags, w.radius > 0 ? ", explodes" : "");
        }
        std::printf("\n");
    }
}

const VehicleDef* Game::def_for(const std::string& name) {
    auto it = defs.find(lower(name));
    if (it == defs.end()) {
        std::unique_ptr<VehicleDef> d;
        try {
            VehicleDef v = vehicle_def(con, vfs, name);
            if (!v.wheels.empty() || !v.wings.empty() || !v.floats.empty()) d = std::make_unique<VehicleDef>(std::move(v));
        } catch (const std::exception&) {
        }
        it = defs.emplace(lower(name), std::move(d)).first;
    }
    return it->second.get();
}

// Terrain height under (x, z), bilinear between samples.
float Game::ground(float x, float z) const {
    float cell = info.world_size / static_cast<float>(hm.n), fx = std::clamp(x / cell, 0.0f, static_cast<float>(hm.n - 1)),
          fz = std::clamp(z / cell, 0.0f, static_cast<float>(hm.n - 1));
    int c = std::min(static_cast<int>(fx), hm.n - 2), r = std::min(static_cast<int>(fz), hm.n - 2);
    float u = fx - static_cast<float>(c), w = fz - static_cast<float>(r);
    return (hm.height(r, c) * (1 - u) + hm.height(r, c + 1) * u) * (1 - w) + (hm.height(r + 1, c) * (1 - u) + hm.height(r + 1, c + 1) * u) * w;
}

void Game::add_vehicle(const std::string& name, Mat4 world, int team_on) {
    const VehicleDef* def = def_for(name);
    if (!def) return;
    // Some retail spawns sit a little under our terrain (M3A1, T95, Sexton fell through it): lift the vehicle until no
    // hull point or wheel bottom is below the ground under it. Vehicles on bridges and decks are above it already.
    float lift = 0;
    auto check = [&](float x, float y, float z) {
        float wx = world.m[0] * x + world.m[4] * y + world.m[8] * z + world.m[12], wy = world.m[1] * x + world.m[5] * y + world.m[9] * z + world.m[13],
              wz = world.m[2] * x + world.m[6] * y + world.m[10] * z + world.m[14];
        lift = std::max(lift, ground(wx, wz) - wy);
    };
    for (size_t i = 0; i + 2 < def->hull.size(); i += 3) check(def->hull[i], def->hull[i + 1], def->hull[i + 2]);
    for (auto& w : def->wheels)
        if (w.strength > 0) check(w.pos[0], w.pos[1] - w.radius, w.pos[2]);
    if (lift > 0) world = translate(0, lift + 0.05f, 0) * world;
    Veh v;
    v.id = phys.add_vehicle(*def, world), v.name = name, v.plane = def->plane, v.ship = def->ship, v.first = scene.size(), v.start = world;
    for (size_t i = 2; i < def->hull.size(); i += 3) v.len = std::max(v.len, std::fabs(def->hull[i]));
    for (DrawItem& item : build_object(con, vfs, name, Mat4::identity(), skipped)) ++v.parts, scene.push_back(std::move(item));
    v.wreck_first = scene.size();
    for (DrawItem& item : build_object(con, vfs, name, Mat4::identity(), skipped, nullptr, nullptr, true))
        ++v.wreck_parts, scene.push_back(std::move(item));
    v.hp = def->hitpoints, v.material = def->material;
    for (const SeatDef& sd : def->seats) {
        Seat st;
        st.weapon = sd.weapon;
        std::copy(sd.exit, sd.exit + 3, st.exit), std::copy(sd.pos, sd.pos + 3, st.pos);
        st.at = sd.at, st.show = sd.show;
        // Seat poses (animationStatesVehicle.con): a seat's own states, else sitting / standing by its flags.
        st.lower = !sd.lower.empty() ? sd.lower : sd.show == 3 || sd.show == 2 ? "lb_standinvehicle" : "lb_sitinvehicle";
        st.upper = !sd.upper.empty() ? sd.upper : "ub_sitinvehicle";
        v.seats.push_back(st);
    }
    if (v.seats.empty()) v.seats.push_back(Seat{}), v.seats[0].weapon = def->weapon, std::copy(def->exit, def->exit + 3, v.seats[0].exit);
    // Ships carry spawn points (FletcherSoldierSpawn): owned by the spawner's team when it has teamOnVehicle 1.
    if (team_on > 0) {
        std::function<void(const std::string&, Mat4, int)> find = [&](const std::string& n, Mat4 at, int depth) {
            const ConObject* t = object_template(n);
            if (!t || depth > 12) return;
            if (lower(t->type) == "spawnpoint") {
                SpawnPt sp;
                sp.veh = static_cast<int>(vehicles.size()), sp.team = team_on;
                sp.local[0] = at.m[12], sp.local[1] = at.m[13], sp.local[2] = at.m[14];
                spawn_points.push_back(sp);
                return;
            }
            std::string child;
            float pos[3] = {}, rot[3] = {};
            auto flush = [&] {
                if (!child.empty()) find(child, at * translate(pos[0], pos[1], pos[2]) * rotation_ypr(rot[0], rot[1], rot[2]), depth + 1);
                child.clear(), pos[0] = pos[1] = pos[2] = rot[0] = rot[1] = rot[2] = 0;
            };
            for (auto& [k, a] : t->props) {
                if (a.empty()) continue;
                if (k == "addtemplate") flush(), child = a[0];
                else if (k == "setposition" && !child.empty()) parse_vec3(a[0], pos);
                else if (k == "setrotation" && !child.empty()) parse_vec3(a[0], rot);
            }
            flush();
        };
        find(name, Mat4::identity(), 0);
    }
    vehicles.push_back(v);
}

const ConObject* Game::object_template(const std::string& n) const {
    auto& ot = con.templates["objecttemplate"];
    auto it = ot.find(lower(n));
    return it == ot.end() ? nullptr : &it->second;
}

float Game::hitpoints(const std::string& n) const {
    const ConObject* t = object_template(n);
    float hp = 30;
    if (t)
        for (auto& [k, a] : t->props)
            if (k == "hitpoints" && !a.empty()) hp = static_cast<float>(std::atof(a[0].c_str()));
    return std::isfinite(hp) && hp > 0 ? hp : 30.0f;
}

size_t Game::add_weapon_items(const std::string& weapon) {
    if (weapon.empty()) return 0;
    size_t n = 0;
    for (DrawItem& item : build_object(con, vfs, weapon, translate(0, -10000, 0), skipped))
        if (!item.bone.empty()) ++n, scene.push_back(std::move(item));  // parts on a bone only: the held model
    return n;
}

std::vector<std::string> Game::kit_weapons(int tm, int kt) const {
    std::vector<std::string> w;
    auto k = kits.find({tm, kt});
    if (const ConObject* t = k != kits.end() ? object_template(k->second) : nullptr)
        for (auto& [m, a] : t->props)
            if (m == "addtemplate" && !a.empty() && !weapon_def(con, a[0]).name.empty()) w.push_back(a[0]);
    return w;
}

std::string Game::primary(int tm, int kt) const {  // the kit's first weapon with a projectile
    auto w = kit_weapons(tm, kt);
    return w.empty() ? "" : w[0];
}

void Game::select(size_t i) {
    if (i >= loadout.size() || i == slot) return;
    loadout[slot].second = hand_ammo;
    hand_ammo.reload_until = -1;  // switching away cancels a reload (shortcut: no switch time)
    slot = i, hand = loadout[i].first, hand_ammo = loadout[i].second, hand_next = now + 0.3f;
}

void Game::refill_loadout() {
    for (auto& [w, a] : loadout) a = Ammo::full(w);
    if (slot < loadout.size()) hand_ammo = loadout[slot].second;
}

void Game::move_spawns() {
    for (SpawnPt& sp : spawn_points) {
        if (sp.veh < 0) continue;
        Mat4 m = phys.transform(vehicles[static_cast<size_t>(sp.veh)].id);
        for (int i = 0; i < 3; ++i) sp.pos[i] = m.m[i] * sp.local[0] + m.m[4 + i] * sp.local[1] + m.m[8 + i] * sp.local[2] + m.m[12 + i];
        sp.yaw = std::atan2(m.m[8], m.m[10]) * 57.29578f;
    }
}

int Game::spawn_team(const SpawnPt& sp) const {
    if (sp.veh >= 0) return vehicles[static_cast<size_t>(sp.veh)].wrecked ? 0 : sp.team;
    for (auto& f : game.flags)
        if (f.group == sp.group) return f.team;
    return 0;
}

// E: get into the nearest vehicle within reach (shortcut: 5 m + half its length, not the EntryPoint radius);
// again: out at its setSoldierExitLocation.
void Game::get_in_out() {
    if (player < 0) return;
    if (cur >= 0) {
        Veh& v = vehicles[static_cast<size_t>(cur)];
        Mat4 t = phys.transform(v.id);
        const float* x = v.seats[static_cast<size_t>(seat)].exit;
        float e[3];
        for (int i = 0; i < 3; ++i) e[i] = t.m[i] * x[0] + t.m[4 + i] * x[1] + t.m[8 + i] * x[2] + t.m[12 + i];
        if (seat == 0) phys.drive(v.id, 0, 0);
        phys.look(v.id, 0, 0, seat);
        v.seats[static_cast<size_t>(seat)].who = -2;
        bool empty = true;
        for (const Seat& s : v.seats) empty = empty && s.who == -2;
        if (empty) v.team = 0;
        phys.place_character(player, e[0], e[1] + 0.5f, e[2]);
        phys.set_character_active(player, true);
        std::printf("out of %s at %.1f %.1f %.1f\n", v.name.c_str(), e[0], e[1], e[2]);
        cur = -1;
        return;
    }
    float p[3], best = 0;
    phys.character_position(player, p);
    for (size_t i = 0; i < vehicles.size(); ++i) {
        Mat4 t = phys.transform(vehicles[i].id);
        float dx = t.m[12] - p[0], dz = t.m[14] - p[2], margin = 5 + vehicles[i].len * 0.5f - std::sqrt(dx * dx + dz * dz);
        if (margin >= 0 && (cur < 0 || margin > best)) cur = static_cast<int>(i), best = margin;
    }
    if (cur < 0) return;
    // The driver's seat if free, else the first free one (shortcut: any seat from any entry point, by distance).
    Veh& v = vehicles[static_cast<size_t>(cur)];
    seat = -1;
    for (size_t i = 0; i < v.seats.size() && seat < 0; ++i)
        if (v.seats[i].who == -2) seat = static_cast<int>(i);
    if (seat < 0 || (v.team && v.team != team)) return void(cur = -1);  // full, or crewed by the other team
    v.seats[static_cast<size_t>(seat)].who = -1, v.team = team;
    phys.set_character_active(player, false);
    std::printf("in %s, seat %d of %zu\n", v.name.c_str(), seat + 1, v.seats.size());
}

// F1..F8: move to another free seat of the same vehicle.
void Game::take_seat(int s) {
    if (cur < 0) return;
    Veh& v = vehicles[static_cast<size_t>(cur)];
    if (s < 0 || static_cast<size_t>(s) >= v.seats.size() || s == seat || v.seats[static_cast<size_t>(s)].who != -2) return;
    if (seat == 0) phys.drive(v.id, 0, 0);
    phys.look(v.id, 0, 0, seat);
    v.seats[static_cast<size_t>(seat)].who = -2, v.seats[static_cast<size_t>(s)].who = -1, seat = s;
    std::printf("seat %d of %zu\n", seat + 1, v.seats.size());
}

// On foot: run (5 m/s) or walk with shift (2.5 m/s) relative to the camera; shortcut: speeds guessed.
void Game::walk(float forward, float right, bool jump, bool slow) {
    move_f = forward, move_r = right;
    if (player < 0) return;
    // shortcut: crouching at half speed, crawling at a quarter (no soldier speeds in the templates)
    float l = std::sqrt(forward * forward + right * right),
          sp = (slow ? 2.5f : 5.0f) * (posture == 2 ? 0.25f : posture == 1 ? 0.5f : 1.0f) / std::max(l, 1.0f);
    jump = jump && posture == 0;
    float fx = std::sin(cam.yaw), fz = std::cos(cam.yaw);
    phys.walk(player, (fx * forward + fz * right) * sp, (fz * forward - fx * right) * sp, jump);
}

// Fire the controlled vehicle's weapon from its posed barrel, or the soldier's along the camera. t: game time.
Ammo Ammo::full(const WeaponDef& w) {
    Ammo a;
    a.unlimited = w.mag_size <= 0;
    a.rounds = w.mag_size, a.spare = std::max(0, w.mags - 1);
    return a;
}

void Ammo::supply(float r, const WeaponDef& w) {
    if (unlimited || w.mag_size <= 0) return;
    int most = std::max(0, w.mags - 1);
    if (spare >= most) {
        refill = 0;
        return;
    }
    refill += r;
    while (refill >= static_cast<float>(w.mag_size) && spare < most) refill -= static_cast<float>(w.mag_size), ++spare;
}

bool Ammo::take(float t) {
    if (unlimited) return true;
    if (reloading(t) || rounds <= 0) return false;
    --rounds;
    return true;
}

void Ammo::reload(float t, const WeaponDef& w) {
    if (!unlimited && !reloading(t) && reload_until < 0 && spare > 0 && rounds < w.mag_size) reload_until = t + w.reload;
}

void Ammo::tick(float t, const WeaponDef& w) {
    if (unlimited) return;
    if (reload_until >= 0 && t >= reload_until) {
        // shortcut: rounds left in the old magazine are lost, as with a clip; per-weapon rules (allowReloadOnEmptyClipOnly) unread
        rounds = w.mag_size, --spare, reload_until = -1;
    }
    if (rounds <= 0) reload(t, w);  // autoReload is 0 on many weapons; reloading on empty spares a key press
}

// Soldiers inside a depot's radius get ammunition (spare magazines) and hit points.
void Game::supply(float step) {
    auto at = [&](const float p[3], int tm, Ammo& ammo, const WeaponDef& w, float& hp, float full) {
        for (const Depot& d : depots) {
            float dx = p[0] - d.pos[0], dy = p[1] - d.pos[1], dz = p[2] - d.pos[2];
            if ((d.team && d.team != tm) || dx * dx + dy * dy + dz * dz > d.radius * d.radius) continue;
            auto r = d.ammo.find(w.ammo_type);
            if (r != d.ammo.end()) ammo.supply(r->second * step, w);
            hp = std::min(full, hp + d.health * step);
        }
    };
    if (player >= 0 && !player_dead && cur < 0) {
        float p[3];
        phys.character_position(player, p);
        at(p, team, hand_ammo, hand, player_hp, hitpoints(soldier_tmpl));
    }
    for (Bot& b : bots)
        if (!b.dead) at(b.pos, b.team, b.ammo, b.weapon, b.hp, hitpoints(b.tmpl));
}

int Game::nearest_depot(const float pos[3], int tm, const WeaponDef& w) const {
    int best = -1;
    float bd = 1e30f;
    for (size_t i = 0; i < depots.size(); ++i) {
        const Depot& d = depots[i];
        float dx = pos[0] - d.pos[0], dz = pos[2] - d.pos[2];
        if ((d.team && d.team != tm) || !d.ammo.count(w.ammo_type) || dx * dx + dz * dz >= bd) continue;
        bd = dx * dx + dz * dz, best = static_cast<int>(i);
    }
    return best;
}

// Mines go off when a vehicle comes within explodeNearEnemyDistance (any vehicle: friendly ones too); packs on detonate.
void Game::placed_charges(bool detonate) {
    for (size_t i = 0; i < placed.size();) {
        float p[3];
        if (!phys.shot_position(placed[i].shot, p)) {
            placed[i] = placed.back(), placed.pop_back();
            continue;
        }
        bool go = detonate && placed[i].near <= 0;
        for (const Veh& v : vehicles) {
            if (go || placed[i].near <= 0 || v.wrecked) continue;
            Mat4 m = phys.transform(v.id);
            float dx = m.m[12] - p[0], dy = m.m[13] - p[1], dz = m.m[14] - p[2];
            // shortcut: from the vehicle's centre
            float r = placed[i].near + v.len;  // len: the hull's half length
            go = dx * dx + dy * dy + dz * dz < r * r;
        }
        if (go) phys.explode(placed[i].shot);
        ++i;
    }
}

void Game::fire(float t) {
    if (cur >= 0) {
        Veh& v = vehicles[static_cast<size_t>(cur)];
        Seat& st = v.seats[static_cast<size_t>(seat)];
        const WeaponDef& w = st.weapon;
        if (v.wrecked || w.name.empty() || t < st.next_fire) return;
        st.next_fire = t + w.interval;
        Mat4 m = st.has_muzzle ? st.muzzle : phys.transform(v.id);
        float d[3] = {m.m[8], m.m[9], m.m[10]}, l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!(l > 1e-6f)) return;
        float pos[3], vel[3];
        for (int i = 0; i < 3; ++i) d[i] /= l, pos[i] = m.m[12 + i] + d[i] * (st.has_muzzle ? 1.0f : v.len), vel[i] = d[i] * w.velocity;
        phys.fire(pos, vel, w.material, w.material2, w.ttl, v.id, -1, team, w.radius, w.bounce);
        sounds.push_back({w.name, {pos[0], pos[1], pos[2]}, 100000 + 16 * v.id + seat});
    } else if (player >= 0 && !hand.name.empty() && t >= hand_next && hand_ammo.take(t)) {
        hand_next = t + hand.interval;
        float p[3], d[3] = {std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), std::cos(cam.yaw) * std::cos(cam.pitch)}, pos[3], vel[3];
        phys.character_position(player, p);
        for (int i = 0; i < 3; ++i) pos[i] = p[i] + (i == 1 ? eye_height() : 0.0f) + d[i] * 0.6f, vel[i] = d[i] * hand.velocity;
        int id = phys.fire(pos, vel, hand.material, hand.material2, hand.ttl, -1, player, team, hand.radius, hand.bounce);
        last_shot = t;
        if (id >= 0 && hand.bounce && hand.ttl > 60) placed.push_back({id, hand.near});  // not a grenade: waits for a trigger
        sounds.push_back({hand.name, {pos[0], pos[1], pos[2]}, -1});
    }
}

std::string Game::driving(float pos[3], float& control) const {
    if (cur < 0) return "";
    const Veh& v = vehicles[static_cast<size_t>(cur)];
    if (v.wrecked) return "";
    Mat4 m = phys.transform(v.id);
    for (int i = 0; i < 3; ++i) pos[i] = m.m[12 + i];
    // shortcut: engine load from speed (30 m/s = full; aircraft 80), until engines expose their rpm
    control = std::clamp(std::fabs(phys.speed(v.id)) / (v.plane ? 80.0f : 30.0f), 0.0f, 1.0f);
    return v.name;
}

// shortcut: guessed; no damage landing under 10 m/s (a 5 m drop), a full soldier's hit points at 20 m/s (20 m)
float Game::fall_damage(int character, float full) {
    float v = phys.take_landing(character);
    return v > 10 ? full * (v - 10) / 10 : 0.0f;
}

void Game::arm_states(SoldierView& v, const std::string& w, const WeaponDef& def, const Ammo& ammo, float shot, int pose) const {
    if (ammo.reloading(now)) {
        v.upper = (pose == 2 ? "ub_liereload" : "ub_standreload") + w, v.upper_hold = ammo.reload_until - def.reload;
        v.weapon = "weaponreload" + w, v.weapon_hold = v.upper_hold, v.span = def.reload;
    } else if (now - shot < std::max(0.25f, def.interval * 1.5f)) {  // looping fire states while shots keep coming
        v.upper = (pose == 2 ? "ub_liefire" : "ub_fire") + w;
        v.weapon = "weaponfire" + w;
    }
}

void Game::player_dies() {
    player_dead = true, respawn_at = game_time() + 3;
    note("You were killed");
    phys.set_character_active(player, false);
    game.death(team);
    std::printf("you died\n");
}

// Hits: damage = hit material vs the armour face met (else the vehicle's material). The explosion material (material2)
// hits everything within the projectile's radius, falling off linearly to 0 at its edge (unverified); without a radius
// it adds to the direct hit. Soldiers take no damage from their own team (shots carry the shooter's team as their tag).
void Game::apply_hits() {
    auto hurt_player = [&](float d) {
        player_hp -= d;
        std::printf("you are hit: %.1f hp left\n", std::max(0.0f, player_hp));
        if (player_hp <= 0) player_dies();
    };
    auto hurt_bot = [&](Bot& b, float d, int tag) {
        b.hp -= d;
        if (b.hp > 0) return;
        b.dead = true, b.died_at = game_time();
        phys.character_position(b.ch, b.pos);
        phys.kill_character(b.ch);
        game.death(b.team);
        ++deaths[static_cast<size_t>(std::clamp(b.team, 0, 2))];
        if (tag == team) std::printf("%s (team %d) killed\n", b.tmpl.c_str(), b.team);
        note(b.tmpl + (tag == team ? " killed by your team" : " killed"));
    };
    auto hurt_vehicle = [&](size_t i, float d, int armour, int tag) {
        Veh& v = vehicles[i];
        v.hp -= d;
        std::printf("hit %s (armour %d): %.1f damage, %.1f hp left\n", v.name.c_str(), armour, d, std::max(0.0f, v.hp));
        if (v.hp > 0) return;
        v.wrecked = true, ++wrecks;
        std::printf("%s destroyed\n", v.name.c_str());
        for (Seat& st : v.seats)
            if (st.who >= 0) {
                Bot& cb = bots[static_cast<size_t>(st.who)];
                leave(cb);
                hurt_bot(cb, 1e6f, tag);
            }
        note(v.name + " destroyed");
        if (static_cast<int>(i) == cur && player >= 0) get_in_out(), player_dies();  // the crew dies with it
        phys.drive(v.id, 0, 0);
    };
    for (const Physics::Hit& h : phys.take_hits()) {
        bool splash = h.radius > 0 && h.material2 >= 0;
        auto direct = [&](int target) {
            return damage.damage(h.material, target) + (h.material2 >= 0 && !splash ? damage.damage(h.material2, target) : 0.0f);
        };
        // Body part by hit height: head 40, chest 41, limbs 42 (setSkeletonCollisionBone materials).
        // shortcut: heights stand in for the bones; arms count as chest.
        int part = h.height > 1.45f ? 40 : h.height > 0.85f ? 41 : 42;
        if (h.character >= 0 && h.character == player) {
            float hh = h.height * 1.8f / (posture == 2 ? 0.7f : posture == 1 ? 1.2f : 1.8f);  // as if standing
            part = hh > 1.45f ? 40 : hh > 0.85f ? 41 : 42;
            if (!player_dead && h.tag != team) hurt_player(direct(part));
        } else if (h.character >= 0) {
            for (Bot& b : bots)
                if (b.ch == h.character && !b.dead && h.tag != b.team) {
                    float hh = h.height * 1.8f / body_height(b.posture);  // as if standing
                    hurt_bot(b, direct(hh > 1.45f ? 40 : hh > 0.85f ? 41 : 42), h.tag);
                }
        } else if (h.vehicle >= 0) {
            for (size_t i = 0; i < vehicles.size(); ++i)
                if (vehicles[i].id == h.vehicle && !vehicles[i].wrecked) {
                    int target = h.armor >= 0 ? h.armor : vehicles[i].material;
                    hurt_vehicle(i, direct(target), target, h.tag);
                }
        }
        if (!splash) continue;
        auto falloff = [&](const float q[3], float lift) {
            float dx = q[0] - h.pos[0], dy = q[1] + lift - h.pos[1], dz = q[2] - h.pos[2];
            return std::max(0.0f, 1 - std::sqrt(dx * dx + dy * dy + dz * dz) / h.radius);
        };
        if (player >= 0 && !player_dead && cur < 0 && h.tag != team) {
            float q[3];
            phys.character_position(player, q);
            if (float f = falloff(q, 1); f > 0) hurt_player(damage.damage(h.material2, 41) * f);
        }
        for (Bot& b : bots)
            if (!b.dead && b.veh < 0 && h.tag != b.team)  // crews are inside their vehicle
                if (float f = falloff(b.pos, 1); f > 0) hurt_bot(b, damage.damage(h.material2, 41) * f, h.tag);
        for (size_t i = 0; i < vehicles.size(); ++i) {
            if (vehicles[i].wrecked) continue;
            Mat4 m = phys.transform(vehicles[i].id);
            float q[3] = {m.m[12], m.m[13], m.m[14]};
            // shortcut: distance to the vehicle's centre of mass, not its hull
            if (float f = falloff(q, 0); f > 0) hurt_vehicle(i, damage.damage(h.material2, vehicles[i].material) * f, vehicles[i].material, h.tag);
        }
    }
}

// AI (shortcut: no path finding, no squads, no vehicles; numbers guessed). Every 0.3 s a bot picks the nearest
// opponent (bot or player) it can see within 120 m; with one it turns, waits 0.7 s and fires its kit weapon at
// half the weapon's rate with ~2 deg of aim error. Otherwise it runs at 4 m/s straight for the nearest flag its
// team does not hold (main bases cannot change hands) and waits there. The dead come back after 10 s (the
// player after 3) on a spawn point their team owns. Then the flags and tickets move on. t: game time.
// Bots as crew. shortcut: all numbers guessed; land vehicles only (no flying, no boats); seats by order, not EntryPoint.
void Game::seat_point(const Veh& v, size_t s, float out[3]) const {
    Mat4 m = phys.transform(v.id);
    const float* p = v.seats[s].pos;
    for (int i = 0; i < 3; ++i) out[i] = m.m[i] * p[0] + m.m[4 + i] * p[1] + m.m[8 + i] * p[2] + m.m[12 + i];
    out[1] += 1.0f;  // the head over the seat
}

const Game::Seat* Game::shown_seat(int veh, int s) const {
    if (veh < 0 || s < 0) return nullptr;
    const Veh& v = vehicles[static_cast<size_t>(veh)];
    const Seat& st = v.seats[static_cast<size_t>(s)];
    return v.wrecked || !st.show ? nullptr : &st;
}

// The SeatObject marks the pelvis: the model's origin goes the pose's pelvis height below it (sitting 0.83 m, standing
// 1.0 m, measured on 3PWillySitLower / 3PStandLower); checked on a Willys: both seated, facing forward.
Mat4 Game::seated(int veh, const Seat& st) const {
    return phys.transform(vehicles[static_cast<size_t>(veh)].id) * st.at * translate(0, st.show >= 2 ? -1.0f : -0.83f, 0);
}

void Game::refresh_muzzles(Veh& v) {  // as pose() does, for runs that do not draw
    std::vector<DrawItem> parts = build_object(con, vfs, v.name, phys.transform(v.id), skipped,
                                               [&](const std::string& n, int o) { return phys.joint(v.id, n, o); });
    for (const DrawItem& d : parts)
        for (Seat& st : v.seats)
            if (!st.weapon.name.empty() && d.owner == st.weapon.name) st.muzzle = d.world, st.has_muzzle = true;
}

// A bot with nothing to shoot boards a vehicle within 10 m: an empty land vehicle's driver seat, or a free armed
// seat of one its team already crews. Checked every 3 s.
void Game::board(Bot& b, float t) {
    if (t < b.board_at) return;
    b.board_at = t + 3;
    for (size_t vi = 0; vi < vehicles.size(); ++vi) {
        Veh& v = vehicles[vi];
        bool boat = troop_boat(v);
        if (v.wrecked || v.plane || (v.ship && !boat) || (v.team && v.team != b.team)) continue;
        Mat4 m = phys.transform(v.id);
        float dx = m.m[12] - b.pos[0], dz = m.m[14] - b.pos[2];
        if (dx * dx + dz * dz > (10 + v.len) * (10 + v.len)) continue;
        int s = -1;
        if (v.seats[0].who == -2 && !v.team) s = 0;
        for (size_t k = 1; k < v.seats.size() && s < 0 && v.team; ++k)  // landing craft also take passengers
            if (v.seats[k].who == -2 && (boat || !v.seats[k].weapon.name.empty())) s = static_cast<int>(k);
        if (s < 0) continue;
        v.seats[static_cast<size_t>(s)].who = static_cast<int>(&b - bots.data()), v.team = b.team;
        b.veh = static_cast<int>(vi), b.vseat = s, b.path.clear(), b.path_goal = -1, ++boardings;
        phys.set_character_active(b.ch, false), phys.walk(b.ch, 0, 0, false);
        if (phys.set_posture(b.ch, 0)) b.posture = 0;
        return;
    }
}

void Game::leave(Bot& b) {
    if (b.veh < 0) return;
    Veh& v = vehicles[static_cast<size_t>(b.veh)];
    Seat& st = v.seats[static_cast<size_t>(b.vseat)];
    st.who = -2;
    if (b.vseat == 0) phys.drive(v.id, 0, 0);
    phys.look(v.id, 0, 0, b.vseat);
    Mat4 m = phys.transform(v.id);
    float e[3];
    for (int i = 0; i < 3; ++i) e[i] = m.m[i] * st.exit[0] + m.m[4 + i] * st.exit[1] + m.m[8 + i] * st.exit[2] + m.m[12 + i];
    phys.place_character(b.ch, e[0], e[1] + 0.5f, e[2]);
    phys.set_character_active(b.ch, true);
    bool empty = true;
    for (const Seat& s : v.seats) empty = empty && s.who == -2;
    if (empty) v.team = 0;
    b.veh = b.vseat = -1;
}

// Landing craft at the beach: everyone gets out (and keeps away from boats for a minute).
void Game::disembark(Veh& v, float t) {
    phys.drive(v.id, 0, 0);
    for (Seat& st : v.seats)
        if (st.who >= 0) {
            Bot& cb = bots[static_cast<size_t>(st.who)];
            leave(cb);
            cb.board_at = t + 60;
        }
}

// A crew member: the driver heads for the flag (tank map route), every armed seat turns its gun onto the target and fires.
void Game::crew(Bot& b, float t, float step, const float* target, int target_veh) {
    Veh& v = vehicles[static_cast<size_t>(b.veh)];
    Seat& st = v.seats[static_cast<size_t>(b.vseat)];
    Mat4 m = phys.transform(v.id);
    std::copy(m.m + 12, m.m + 15, b.pos);
    b.moving = false;
    if (target && !st.weapon.name.empty()) {
        if (t >= v.muzzles_at) refresh_muzzles(v), v.muzzles_at = t + 0.1f;
        Mat4 mz = st.has_muzzle ? st.muzzle : m;
        float f[3] = {mz.m[8], mz.m[9], mz.m[10]}, o[3] = {mz.m[12], mz.m[13], mz.m[14]};
        float d[3] = {target[0] - o[0], target[1] - o[1], target[2] - o[2]};
        float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        if (dist > 1 && fl > 1e-4f) {
            float tof = dist / std::max(st.weapon.velocity, 1.0f);
            d[1] += 0.5f * 9.81f * tof * tof;  // lead the drop
            float yaw_err = std::remainder(std::atan2(d[0], d[2]) - std::atan2(f[0], f[2]), 6.2831853f) * 57.29578f;
            float pitch_err = (std::atan2(d[1], std::sqrt(d[0] * d[0] + d[2] * d[2])) - std::asin(std::clamp(f[1] / fl, -1.0f, 1.0f))) * 57.29578f;
            // Mouse right turns right, mouse down lowers the gun (the player's mapping).
            phys.look(v.id, std::clamp(yaw_err / 10, -1.0f, 1.0f), std::clamp(-pitch_err / 5, -1.0f, 1.0f), b.vseat);
            if (std::fabs(yaw_err) < 3 && std::fabs(pitch_err) < 3 && dist < 250 && t >= st.next_fire) {
                st.next_fire = t + st.weapon.interval * (st.weapon.interval > 1 ? 1.0f : 2.0f);
                float l = dist;
                std::normal_distribution<float> err(0.0f, 0.01f);
                float dir[3] = {f[0] / fl + err(rng), f[1] / fl + err(rng), f[2] / fl + err(rng)}, pos[3], vel[3];
                l = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                for (int i = 0; i < 3; ++i) pos[i] = o[i] + dir[i] / l * 1.0f, vel[i] = dir[i] / l * st.weapon.velocity;
                const WeaponDef& w = st.weapon;
                phys.fire(pos, vel, w.material, w.material2, w.ttl, v.id, -1, b.team, w.radius, w.bounce);
                sounds.push_back({w.name, {pos[0], pos[1], pos[2]}, 100000 + 16 * v.id + b.vseat});
                b.last_shot = t;
            }
        }
    } else {
        phys.look(v.id, 0, 0, b.vseat);
    }
    (void)target_veh;
    if (b.vseat != 0) return;
    // Driving: the nearest flag its team does not hold; stop 15 m from it. Stuck: back out for 2 s.
    const ConquestFlag* goal = nullptr;
    float best = 1e30f;
    for (const ConquestFlag& fl : game.flags) {
        float g = (fl.pos[0] - b.pos[0]) * (fl.pos[0] - b.pos[0]) + (fl.pos[2] - b.pos[2]) * (fl.pos[2] - b.pos[2]);
        if (fl.team != b.team && !fl.locked && g < best) best = g, goal = &fl;
    }
    bool boat = troop_boat(v);
    if (boat && goal && best < 40 * 40) return disembark(v, t);
    if (!goal || (!boat && (best < 15 * 15 || target))) {  // there, or fighting: hold (landing craft keep going)
        phys.drive(v.id, 0, 0);
        return;
    }
    int gi = static_cast<int>(goal - game.flags.data());
    bool stuck = false;
    if (t >= b.check_at && t >= b.reverse_until) {  // under 2 m in 3 s
        float mx = b.pos[0] - b.check_pos[0], mz = b.pos[2] - b.check_pos[1];
        stuck = mx * mx + mz * mz < 4;
        b.jams = stuck ? b.jams + 1 : 0;
        b.check_at = t + 3, b.check_pos[0] = b.pos[0], b.check_pos[1] = b.pos[2];
    }
    if (boat && b.jams >= 2) return disembark(v, t);  // beached (or aground): everybody out
    if (b.jams >= 4) {  // hopeless (parked vehicles, rocks): on foot, and no boarding for a minute
        phys.drive(v.id, 0, 0), leave(b), b.board_at = t + 60, b.jams = 0;
        return;
    }
    if (stuck) b.reverse_until = t + 3, b.replan_at = 0;
    const NavGrid& grid = boat ? boat_nav : tank_nav;
    if (grid.n > 0 && (gi != b.path_goal || (stuck && t >= b.replan_at))) {
        b.path = nav_path(grid, b.pos[0], b.pos[2], goal->pos[0], goal->pos[2]);
        b.waypoint = 0, b.path_goal = gi, b.replan_at = t + 5;
    }
    float tx = goal->pos[0], tz = goal->pos[2];
    while (b.waypoint * 2 + 1 < b.path.size()) {
        float wx = b.path[b.waypoint * 2], wz = b.path[b.waypoint * 2 + 1];
        if ((wx - b.pos[0]) * (wx - b.pos[0]) + (wz - b.pos[2]) * (wz - b.pos[2]) > 6 * 6) {
            tx = wx, tz = wz;
            break;
        }
        ++b.waypoint;
    }
    float heading = std::atan2(m.m[8], m.m[10]), want = std::atan2(tx - b.pos[0], tz - b.pos[2]);
    float err = std::remainder(want - heading, 6.2831853f) * 57.29578f;
    if (t < b.reverse_until) phys.drive(v.id, -1.0f, t < b.reverse_until - 1.5f ? 0.0f : (err > 0 ? -1.0f : 1.0f));  // straight back, then turning
    else phys.drive(v.id, std::fabs(err) < 45 ? 1.0f : 0.3f, std::clamp(err / 30, -1.0f, 1.0f));
    (void)step;
}

void Game::think(float t, float step) {
    move_spawns();
    auto owned_spawn = [&](int tm) -> const SpawnPt* {
        std::vector<const SpawnPt*> c;
        for (const SpawnPt& sp : spawn_points)
            if (spawn_team(sp) == tm) c.push_back(&sp);
        return c.empty() ? nullptr : c[std::uniform_int_distribution<size_t>(0, c.size() - 1)(rng)];
    };
    if (player >= 0 && player_dead && t >= respawn_at && !game.winner)
        if (const SpawnPt* sp = owned_spawn(team)) {
            phys.place_character(player, sp->pos[0], sp->pos[1] + 0.2f, sp->pos[2]);
            phys.set_character_active(player, true);
            player_dead = false, player_hp = hitpoints(soldier_tmpl), cam.yaw = sp->yaw * 3.14159265f / 180.0f;
            refill_loadout();
            if (phys.set_posture(player, 0)) posture = 0, lying = false;
            std::printf("respawned\n");
        }
    for (Bot& b : bots) {
        if (!b.dead && b.veh < 0) phys.character_position(b.ch, b.pos);
        if (!b.dead || t - b.died_at < 10 || game.winner) continue;
        const SpawnPt* sp = owned_spawn(b.team);
        if (!sp) continue;
        phys.place_character(b.ch, sp->pos[0], sp->pos[1] + 0.2f, sp->pos[2]);
        phys.set_character_active(b.ch, true);
        b.dead = false, b.hp = hitpoints(b.tmpl), b.yaw = sp->yaw, b.target = -2, b.seen_at = -1;
        b.ammo = Ammo::full(b.weapon), b.grenades = Ammo::full(b.grenade);
        if (phys.set_posture(b.ch, 0)) b.posture = 0;
        b.veh = b.vseat = -1, b.path.clear(), b.path_goal = -1;
        std::copy(sp->pos, sp->pos + 3, b.pos);
    }
    float ppos[3] = {};
    int pveh = -1;
    bool palive = player >= 0 && !player_dead;
    if (palive && cur >= 0) {
        Mat4 m = phys.transform(vehicles[static_cast<size_t>(cur)].id);
        ppos[0] = m.m[12], ppos[1] = m.m[13] + 1, ppos[2] = m.m[14], pveh = vehicles[static_cast<size_t>(cur)].id;
    } else if (palive) {
        phys.character_position(player, ppos);
        ppos[1] += posture == 2 ? 0.3f : posture == 1 ? 0.8f : 1.2f;
    }
    std::normal_distribution<float> err(0.0f, 0.035f);
    for (size_t bi = 0; bi < bots.size(); ++bi) {
        Bot& b = bots[bi];
        if (b.dead) continue;
        if (game.winner) {
            phys.walk(b.ch, 0, 0, false), b.moving = false;
            continue;
        }
        float eye[3] = {b.pos[0], b.pos[1] + body_height(b.posture) - 0.2f, b.pos[2]};
        int own_veh = b.veh >= 0 ? vehicles[static_cast<size_t>(b.veh)].id : -1;
        if (b.veh >= 0) seat_point(vehicles[static_cast<size_t>(b.veh)], static_cast<size_t>(b.vseat), eye);
        auto aim_point = [&](int who, float out[3], int& ch, int& veh) {
            if (who == -1) return std::copy(ppos, ppos + 3, out), ch = cur >= 0 ? -1 : player, veh = pveh, void();
            const Bot& o = bots[static_cast<size_t>(who)];
            if (o.veh >= 0) {  // crew: aim at the vehicle
                const Veh& ov = vehicles[static_cast<size_t>(o.veh)];
                Mat4 om = phys.transform(ov.id);
                return out[0] = om.m[12], out[1] = om.m[13] + 1, out[2] = om.m[14], ch = -1, veh = ov.id, void();
            }
            out[0] = o.pos[0], out[1] = o.pos[1] + body_height(o.posture) * 0.65f, out[2] = o.pos[2], ch = o.ch, veh = -1;
        };
        if (t >= b.next_look) {  // nearest visible opponent; line of sight checked for the three nearest
            b.next_look = t + 0.3f, b.target = -2;
            std::vector<std::pair<float, int>> cand;
            auto d2 = [&](const float* q) {
                return (q[0] - b.pos[0]) * (q[0] - b.pos[0]) + (q[1] - b.pos[1]) * (q[1] - b.pos[1]) + (q[2] - b.pos[2]) * (q[2] - b.pos[2]);
            };
            if (palive && team != b.team) cand.push_back({d2(ppos), -1});
            for (size_t oi = 0; oi < bots.size(); ++oi)
                if (!bots[oi].dead && bots[oi].team != b.team) cand.push_back({d2(bots[oi].pos), static_cast<int>(oi)});
            std::sort(cand.begin(), cand.end());
            for (size_t c = 0; c < cand.size() && c < 3 && cand[c].first < 120 * 120; ++c) {
                float q[3];
                int ch, veh;
                aim_point(cand[c].second, q, ch, veh);
                if (phys.line_of_sight(eye, q, b.ch, ch, veh, own_veh)) {
                    b.target = cand[c].second;
                    break;
                }
            }
        }
        if (b.target == -1 && !palive) b.target = -2;
        if (b.target >= 0 && bots[static_cast<size_t>(b.target)].dead) b.target = -2;
        if (b.veh >= 0) {
            float q[3];
            int ch = -1, veh = -1;
            if (b.target != -2) aim_point(b.target, q, ch, veh);
            crew(b, t, step, b.target != -2 ? q : nullptr, veh);
            continue;
        }
        if (b.target != -2) {
            float q[3];
            int ch, veh;
            aim_point(b.target, q, ch, veh);
            float d[3] = {q[0] - eye[0], q[1] - eye[1], q[2] - eye[2]}, dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (b.seen_at < 0) b.seen_at = t;
            b.yaw = std::atan2(d[0], d[2]) * 57.29578f, b.moving = false;
            phys.walk(b.ch, 0, 0, false);
            // shortcut: in a fight scouts lie down and the others crouch (the original AI's choice is unread)
            if (int want = b.kit == 0 ? 2 : 1; b.posture != want && phys.set_posture(b.ch, want)) b.posture = want;
            b.ammo.tick(t, b.weapon);
            if (bool r = b.ammo.reloading(t); r && !b.was_reloading) sounds.push_back({b.weapon.name, {b.pos[0], b.pos[1] + 1.5f, b.pos[2]}, static_cast<int>(bi), true});
            b.was_reloading = b.ammo.reloading(t);
            // A grenade at soldiers 12-35 m away, at most every 8 s (guessed): the low arc that lands on them.
            float hd = std::sqrt(d[0] * d[0] + d[2] * d[2]), v = b.grenade.velocity, gr = 9.81f;
            float disc = v * v * v * v - gr * (gr * hd * hd + 2 * d[1] * v * v);
            if (!b.grenade.name.empty() && veh < 0 && hd > 12 && hd < 35 && disc > 0 && t >= b.next_grenade && t - b.seen_at > 1.5f &&
                b.grenades.take(t)) {
                b.next_grenade = t + 8;
                float tan_a = (v * v - std::sqrt(disc)) / (gr * hd), ca = 1 / std::sqrt(1 + tan_a * tan_a), sa = tan_a * ca;
                float dir[3] = {d[0] / hd * ca + err(rng), sa + err(rng), d[2] / hd * ca + err(rng)}, pos[3], vel[3];
                for (int i = 0; i < 3; ++i) pos[i] = eye[i] + dir[i] * 0.5f, vel[i] = dir[i] * v;
                phys.fire(pos, vel, b.grenade.material, b.grenade.material2, b.grenade.ttl, -1, b.ch, b.team, b.grenade.radius, true);
                sounds.push_back({b.grenade.name, {pos[0], pos[1], pos[2]}, static_cast<int>(bi)});
                ++grenades_thrown;
                continue;
            }
            if (dist > 0.5f && t - b.seen_at > 0.7f && t >= b.next_fire && !b.weapon.name.empty() && b.ammo.take(t)) {
                b.next_fire = t + 2 * b.weapon.interval;
                float dir[3] = {d[0] / dist + err(rng), d[1] / dist + err(rng), d[2] / dist + err(rng)};
                float l = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]), pos[3], vel[3];
                for (int i = 0; i < 3; ++i) pos[i] = eye[i] + dir[i] / l * 0.6f, vel[i] = dir[i] / l * b.weapon.velocity;
                phys.fire(pos, vel, b.weapon.material, b.weapon.material2, b.weapon.ttl, -1, b.ch, b.team, b.weapon.radius, b.weapon.bounce);
                b.last_shot = t;
                sounds.push_back({b.weapon.name, {pos[0], pos[1], pos[2]}, static_cast<int>(bi)});
            }
            continue;
        }
        b.seen_at = -1;
        if (b.posture != 0 && phys.set_posture(b.ch, 0)) b.posture = 0;  // up again to move on
        board(b, t);
        if (b.veh >= 0) continue;
        // The flag it already runs for, while it can still take it; else the nearest one its team does not hold
        // (two flags at about the same distance would otherwise swap goals every step).
        const ConquestFlag* goal = nullptr;
        float best = 1e30f;
        if (b.path_goal >= 0 && static_cast<size_t>(b.path_goal) < game.flags.size()) {
            const ConquestFlag& f = game.flags[static_cast<size_t>(b.path_goal)];
            if (f.team != b.team && !f.locked)
                goal = &f, best = (f.pos[0] - b.pos[0]) * (f.pos[0] - b.pos[0]) + (f.pos[2] - b.pos[2]) * (f.pos[2] - b.pos[2]);
        }
        for (const ConquestFlag& f : game.flags) {
            float g = (f.pos[0] - b.pos[0]) * (f.pos[0] - b.pos[0]) + (f.pos[2] - b.pos[2]) * (f.pos[2] - b.pos[2]);
            if (!goal && f.team != b.team && !f.locked && g < best) best = g, goal = &f;
        }
        float gpos[3] = {}, arrive = 8;
        int gi = -1;
        if (goal) std::copy(goal->pos, goal->pos + 3, gpos), gi = static_cast<int>(goal - game.flags.data());
        // Out of spare magazines: to the nearest depot of its team that has its ammunition, and wait there until full.
        bool dry = !b.ammo.unlimited && b.ammo.spare == 0 && b.ammo.rounds < std::max(1, b.weapon.mag_size / 2);
        if (int di = dry ? nearest_depot(b.pos, b.team, b.weapon) : -1; di >= 0) {
            const Depot& d = depots[static_cast<size_t>(di)];
            std::copy(d.pos, d.pos + 3, gpos), gi = static_cast<int>(game.flags.size()) + di, arrive = d.radius * 0.7f;
            best = (gpos[0] - b.pos[0]) * (gpos[0] - b.pos[0]) + (gpos[2] - b.pos[2]) * (gpos[2] - b.pos[2]);
        }
        b.moving = gi >= 0 && best > arrive * arrive;
        if (!b.moving) {
            phys.walk(b.ch, 0, 0, false);
            continue;
        }
        // Route on the infantry map: planned when the goal changes, and again when stuck (at most every 5 s).
        bool stuck = false;
        if (t >= b.check_at) {  // under 2 m in 2 s
            float mx = b.pos[0] - b.check_pos[0], mz = b.pos[2] - b.check_pos[1];
            stuck = mx * mx + mz * mz < 4;
            b.check_at = t + 2, b.check_pos[0] = b.pos[0], b.check_pos[1] = b.pos[2];
        }
        if (nav.n > 0 && (gi != b.path_goal || (stuck && t >= b.replan_at))) {
            b.path = nav_path(nav, b.pos[0], b.pos[2], gpos[0], gpos[2]);
            b.waypoint = 0, b.path_goal = gi, b.replan_at = t + 5;
            stuck = stuck && b.path.empty();
        }
        float tx = gpos[0], tz = gpos[2];
        while (b.waypoint * 2 + 1 < b.path.size()) {
            float wx = b.path[b.waypoint * 2], wz = b.path[b.waypoint * 2 + 1];
            if ((wx - b.pos[0]) * (wx - b.pos[0]) + (wz - b.pos[2]) * (wz - b.pos[2]) > 2.5f * 2.5f) {
                tx = wx, tz = wz;
                break;
            }
            ++b.waypoint;
        }
        float gx = tx - b.pos[0], gz = tz - b.pos[2], gl = std::sqrt(gx * gx + gz * gz);
        if (gl < 1e-3f) gx = 0, gz = 1, gl = 1;
        gx /= gl, gz /= gl;
        // Still stuck: sidestep at right angles for 1.5 s, a random side.
        if (stuck && t > b.detour_until)
            ++detours, b.detour_until = t + 1.5f, b.detour_side = std::uniform_int_distribution<int>(0, 1)(rng) ? 1.0f : -1.0f;
        if (t < b.detour_until) std::tie(gx, gz) = std::make_pair(gz * b.detour_side, -gx * b.detour_side);
        b.yaw = std::atan2(gx, gz) * 57.29578f;
        phys.walk(b.ch, gx * 4, gz * 4, t < b.detour_until);  // jumping while stuck helps over low walls
    }
    supply(step);
    // Who stands at each flag.
    std::vector<std::array<int, 3>> present(game.flags.size(), std::array<int, 3>{0, 0, 0});
    for (size_t i = 0; i < game.flags.size(); ++i) {
        const ConquestFlag& f = game.flags[i];
        auto near = [&](const float* q) {
            float dx = q[0] - f.pos[0], dy = q[1] - f.pos[1], dz = q[2] - f.pos[2];
            return dx * dx + dy * dy + dz * dz <= f.radius * f.radius;
        };
        for (const Bot& b : bots)
            if (!b.dead && near(b.pos)) ++present[i][static_cast<size_t>(std::clamp(b.team, 0, 2))];
        if (palive && near(ppos)) ++present[i][static_cast<size_t>(std::clamp(team, 0, 2))];
    }
    for (const std::string& ev : game.update(step, present)) std::printf("[%.0f s] %s\n", t, ev.c_str()), note(ev);
}

void Game::step(float t, float dt, const GameInput& in) {
    now = t;
    sounds.clear();
    move_f = move_r = 0;
    if (in.use) get_in_out();
    if (player >= 0) {
        if (in.select >= 0 && cur < 0) select(static_cast<size_t>(in.select));
        if (in.prone) lying = !lying;
        if (in.view && fp_parts) first_person = !first_person;
        // Space while falling opens the parachute (shortcut: the original's key and opening rules unread).
        if (in.jump && cur < 0 && !player_dead && !phys.character_on_ground(player) && chute_k > 0) phys.open_parachute(player, chute_k);
        if (float d = fall_damage(player, hitpoints(soldier_tmpl)); d > 0 && !player_dead) {
            player_hp -= d;
            std::printf("fall: %.1f damage\n", d);
            if (player_hp <= 0) player_dies();
        }
        if (int want = lying ? 2 : in.crouch ? 1 : 0; cur < 0 && !player_dead && want != posture && phys.set_posture(player, want)) posture = want;
        placed_charges(in.detonate);
        hand_ammo.tick(t, hand);
        if (in.reload) hand_ammo.reload(t, hand);
        if (bool r = hand_ammo.reloading(t); r && !was_reloading && !player_dead) {
            float p[3];
            phys.character_position(player, p);
            sounds.push_back({hand.name, {p[0], p[1] + 1.5f, p[2]}, -1, true});
        }
        was_reloading = hand_ammo.reloading(t);
    }
    bool pull = in.fire && !(cur < 0 && hand.fire_once && fire_held);  // fireOnce: one round per click on foot
    fire_held = in.fire;
    if (pull && !player_dead) fire(t);
    if (cur >= 0 && !vehicles[static_cast<size_t>(cur)].wrecked) {
        Veh& v = vehicles[static_cast<size_t>(cur)];
        float th = in.throttle;
        if (v.plane) v.throttle = std::clamp(v.throttle + th * 0.5f * dt, 0.0f, 1.0f), th = v.throttle;
        if (in.seat >= 0) take_seat(in.seat);
        if (seat == 0) phys.drive(v.id, th, in.yaw, in.pitch, in.roll);
        phys.look(v.id, in.look_x, in.look_y, seat);
        walk(0, 0, false, false);
    } else {
        walk(in.forward, in.right, in.jump, in.slow);
    }
    think(t, dt);
    phys.step(dt);
    apply_hits();
}

void Game::demo(float sim_seconds) {
    move_f = move_r = 0;
    sounds.clear();
    if (player >= 0) demo_play(sim_seconds);
    else if (cur >= 0) demo_vehicle();
}

// --shot with --play: settle, shoot at the nearest enemy, run 2 s, get into the nearest vehicle, drive 2 s, get out,
// then sim_seconds of conquest; prints the track, the game and how far the parked vehicles moved while settling.
void Game::demo_play(float sim_seconds) {
    auto tick = [&] {
        hand_ammo.tick(now, hand);
        think(now, 1.0f / 60);
        phys.step(1.0f / 60);
        apply_hits();
        now += 1.0f / 60;
    };
    auto track = [&](const char* what) {
        float p[3];
        phys.character_position(player, p);
        std::printf("%s: soldier %.1f %.1f %.1f%s\n", what, p[0], p[1], p[2], cur >= 0 ? " (in a vehicle)" : "");
    };
    for (int i = 0; i < 60; ++i) walk(0, 0, false, false), tick();
    track("start");
    for (int i = 0; i < 120; ++i) {  // aim at the nearest living enemy within 150 m and fire
        float p[3], best = 150 * 150;
        phys.character_position(player, p);
        const Bot* tgt = nullptr;
        for (const Bot& b : bots) {
            float d = (b.pos[0] - p[0]) * (b.pos[0] - p[0]) + (b.pos[2] - p[2]) * (b.pos[2] - p[2]);
            if (!b.dead && b.team != team && d < best) best = d, tgt = &b;
        }
        if (!tgt) break;
        float dx = tgt->pos[0] - p[0], dy = tgt->pos[1] + 1.2f - (p[1] + 1.5f), dz = tgt->pos[2] - p[2];
        cam.yaw = std::atan2(dx, dz), cam.pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
        walk(0, 0, false, false);
        fire(now);
        tick();
    }
    for (int i = 0; i < 120; ++i) walk(1, 0, false, false), tick();
    walk(0, 0, false, false);
    track("ran 2 s");
    get_in_out();
    for (int i = 0; i < 120 && cur >= 0; ++i) {
        phys.drive(vehicles[static_cast<size_t>(cur)].id, 1, 0);
        fire(now);
        tick();
    }
    get_in_out();
    for (int i = 0; i < 60; ++i) tick();
    track("end");
    for (int i = 0; i < static_cast<int>(sim_seconds * 60) && !game.winner; ++i) walk(0, 0, false, false), tick();
    int alive = 0, running = 0;
    for (const Bot& b : bots) alive += !b.dead, running += !b.dead && b.moving;
    std::printf("after %.0f s: %d of %zu bots alive, %d running for a flag; flags held %d / %d; tickets %.1f / %.1f%s; player %s; "
                "bot deaths %d / %d; stuck sidesteps %d; grenades %d; boardings %d, wrecks %d\n",
                now, alive, bots.size(), running, game.owned(1), game.owned(2), game.tickets[1], game.tickets[2],
                game.winner ? (game.winner == 1 ? ", team 1 won" : ", team 2 won") : "", player_dead ? "dead" : "alive", deaths[1], deaths[2],
                detours, grenades_thrown, boardings, wrecks);
    // Parked vehicles after 5 s: those pushed away (spawned inside something) or tipped over.
    int moved = 0, tipped = 0;
    for (size_t i = 0; i < vehicles.size(); ++i) {
        if (static_cast<int>(i) == cur) continue;
        Mat4 t = phys.transform(vehicles[i].id);
        const Mat4& w = vehicles[i].start;
        float dx = t.m[12] - w.m[12], dy = t.m[13] - w.m[13], dz = t.m[14] - w.m[14];
        if (dx * dx + dy * dy + dz * dz > 4) ++moved, std::printf("  moved: %s %.1f %.1f %.1f\n", vehicles[i].name.c_str(), dx, dy, dz);
        if (t.m[5] < 0.7f) ++tipped, std::printf("  tipped: %s up %.2f\n", vehicles[i].name.c_str(), t.m[5]);
    }
    std::printf("parked vehicles: %zu, moved > 2 m: %d, tipped: %d\n", vehicles.size(), moved, tipped);
}

// --shot with --drive: settles 2 s, drives 4 s (last 2 s steering right) firing from 2 s; aircraft: 20 s take-off,
// pulling up from 12 s to 16 s. Prints the track.
void Game::demo_vehicle() {
    const Veh& v = vehicles[static_cast<size_t>(cur)];
    int steps = v.plane ? 1200 : 360;
    for (int i = 0; i < steps; ++i) {
        if (v.plane) phys.drive(v.id, i < 120 ? 0.0f : 1.0f, 0, i >= 720 && i < 960 ? -0.4f : 0.0f);
        else phys.drive(v.id, i < 120 ? 0.0f : 1.0f, i < 240 || opt.fire_ahead ? 0.0f : 1.0f);
        if (!opt.fire_ahead) phys.look(v.id, i >= 120 ? 1.0f : 0.0f, i >= 120 ? -1.0f : 0.0f);  // turrets swing round, guns up
        if (i >= 120 && i % 20 == 0) fire(static_cast<float>(i) / 60.0f);
        phys.step(1.0f / 60);
        apply_hits();
        Mat4 t = phys.transform(v.id);
        if (i % 30 == 29) std::printf("t %.1f s: pos %.1f %.1f %.1f, up %.2f %.2f %.2f, speed %.1f m/s\n", (i + 1) / 60.0, t.m[12], t.m[13], t.m[14], t.m[4], t.m[5], t.m[6], phys.speed(v.id));
    }
}

void Game::pose() {
    // Every vehicle's parts (wheels, turrets, control surfaces, propellers); destroyed ones show their wreck.
    const Mat4 hidden = translate(0, -10000, 0);
    for (Veh& v : vehicles) {
        Mat4 t = phys.transform(v.id);
        std::vector<DrawItem> parts = build_object(con, vfs, v.name, v.wrecked ? hidden : t, skipped,
                                                   [&](const std::string& n, int o) { return phys.joint(v.id, n, o); });
        for (size_t i = 0; i < parts.size() && i < v.parts; ++i) {
            scene[v.first + i].world = parts[i].world;
            for (Seat& st : v.seats)
                if (!st.weapon.name.empty() && parts[i].owner == st.weapon.name) st.muzzle = parts[i].world, st.has_muzzle = true;
        }
        std::vector<DrawItem> wreck = build_object(con, vfs, v.name, v.wrecked ? t : hidden, skipped, nullptr, nullptr, true);
        for (size_t i = 0; i < wreck.size() && i < v.wreck_parts; ++i) scene[v.wreck_first + i].world = wreck[i].world;
    }
    if (cur >= 0) {  // chase camera behind the vehicle, distance from its length (Fletcher ~110 m)
        const Veh& v = vehicles[static_cast<size_t>(cur)];
        Mat4 t = phys.transform(v.id);
        float hx = t.m[8], hz = t.m[10], hl = std::sqrt(hx * hx + hz * hz);
        if (hl > 1e-3f) hx /= hl, hz /= hl;
        float back = v.plane ? 16.0f : std::max(9.0f, v.len * 1.2f);
        cam.x = t.m[12] - hx * back, cam.y = t.m[13] + (v.plane ? 4.5f : std::max(3.5f, back * 0.35f)), cam.z = t.m[14] - hz * back;
        cam.yaw = std::atan2(hx, hz), cam.pitch = -0.25f;
    }
    soldiers.clear();
    if (player >= 0) {
        float p[3];
        phys.character_position(player, p);
        // The soldier faces the camera direction; hidden far below while in a vehicle or seen in first person.
        bool fp = first_person && cur < 0;
        const Seat* in_seat = shown_seat(cur, seat);  // seated where the seat shows its soldier (Willys, gunners on top)
        Mat4 w = in_seat ? seated(cur, *in_seat)
                 : cur >= 0 || fp ? translate(0, -10000, 0) : translate(p[0], p[1], p[2]) * rotation_ypr(cam.yaw * 57.29578f, 0, 0);
        std::vector<DrawItem> parts = build_object(con, vfs, soldier_tmpl, w, skipped, nullptr, hide_1p);
        for (size_t i = 0; i < parts.size() && i < sol_parts; ++i) scene[sol_first + i].world = parts[i].world;
        if (phys.character_on_ground(player) || cur >= 0) {  // in the air: keep the last state
            const char* base = posture == 2 ? "lb_lie" : posture == 1 ? "lb_crouch" : "";
            if (*base)
                lower_state = std::string(base) + (move_f > 0.1f ? "forward" : move_f < -0.1f ? "backward"
                                                 : move_r > 0.1f ? "straferight" : move_r < -0.1f ? "strafeleft" : "");
            else
                lower_state = move_f > 0.1f ? "lb_runforward" : move_f < -0.1f ? "lb_runbackward"
                            : move_r > 0.1f ? "lb_straferight" : move_r < -0.1f ? "lb_strafeleft" : "lb_stand";
        }
        // Upper body: the held weapon's state (copyState2 names them by weapon: Ub_StandAimBar1918).
        std::string held = cur < 0 ? hand.name : "";
        std::string upper = posture == 2 ? "ub_lie" + held : posture == 1 ? "ub_crouch" + held
                          : std::fabs(move_f) > 0.1f || std::fabs(move_r) > 0.1f ? "ub_runforward" + held : "ub_standaim" + held;
        SoldierView me;
        me.first = sol_first, me.parts = sol_parts, me.lower = lower_state, me.upper = held.empty() ? "" : upper;
        if (!held.empty()) arm_states(me, held, hand, hand_ammo, last_shot, posture);
        if (in_seat) me.lower = in_seat->lower, me.upper = in_seat->upper, me.weapon.clear();
        if (cur < 0 && slot < loadout_items.size()) me.held_first = loadout_items[slot].first, me.held_parts = loadout_items[slot].second;
        // First person: the 1p arms and body hang from the camera at the eye (center1pHands), with the states'
        // first-person animations; the held weapon follows their right hand.
        Mat4 eye = translate(p[0], p[1] + eye_height(), p[2]) * rotation_ypr(cam.yaw * 57.29578f, -cam.pitch * 57.29578f, 0);
        // shortcut: center1pHands' x (-0.12) is left out and the arms' field of view is 0.75 rad, both fitted by eye to
        // an original-client screenshot (El Alamein, Thompson); set1pFov (0.47) drew them nearly twice as large.
        Mat4 rig = fp ? eye * translate(0, hands[1], hands[2]) : translate(0, -10000, 0);
        std::vector<DrawItem> fparts = build_object(con, vfs, soldier_tmpl, rig, skipped, nullptr, hide_3p);
        for (size_t i = 0; i < fparts.size() && i < fp_parts; ++i) scene[fp_first + i].world = fparts[i].world;
        if (fp) {
            SoldierView mine = me;
            mine.first = fp_first, mine.parts = fp_parts, mine.first_person = true, mine.fov = 0.75f;
            soldiers.push_back(mine);
        } else {
            soldiers.push_back(me);
        }
        if (chute_parts) {
            bool open = cur < 0 && phys.parachute_open(player);
            // The model's origin is its harness: at the shoulders (1.3 m, fitted by eye on a screenshot).
            Mat4 cw = open ? translate(p[0], p[1] + 1.3f, p[2]) * rotation_ypr(cam.yaw * 57.29578f, 0, 0) : translate(0, -10000, 0);
            std::vector<DrawItem> cp = build_object(con, vfs, "Parachute", cw, skipped);
            for (size_t i = 0; i < cp.size() && i < chute_parts; ++i) scene[chute_first + i].world = cp[i].world;
        }
        if (fp) {
            cam.x = eye.m[12], cam.y = eye.m[13], cam.z = eye.m[14];
        } else if (cur < 0) {  // third-person camera over the shoulder
            float dx = std::sin(cam.yaw) * std::cos(cam.pitch), dy = std::sin(cam.pitch), dz = std::cos(cam.yaw) * std::cos(cam.pitch);
            cam.x = p[0] - dx * 4.5f, cam.y = p[1] + eye_height() + 0.2f - dy * 4.5f, cam.z = p[2] - dz * 4.5f;
        }
    }
    for (Bot& b : bots) {  // running, standing or fallen, facing where they go or aim; crews are not drawn (shortcut)
        if (!b.dead && b.veh < 0) phys.character_position(b.ch, b.pos);
        const Seat* bseat = b.dead ? nullptr : shown_seat(b.veh, b.vseat);
        Mat4 bw = bseat ? seated(b.veh, *bseat)
                  : b.veh >= 0 ? translate(0, -10000, 0) : translate(b.pos[0], b.pos[1], b.pos[2]) * rotation_ypr(b.yaw, 0, 0);
        std::vector<DrawItem> parts = build_object(con, vfs, b.tmpl, bw,
                                                   skipped, nullptr, hide_1p);
        for (size_t i = 0; i < parts.size() && i < b.parts; ++i) scene[b.first + i].world = parts[i].world;
        SoldierView v;
        v.first = b.first, v.parts = b.parts, v.held_first = b.held_first, v.held_parts = b.held_parts;
        if (b.dead) v.lower = "lb_diecheststand", v.upper = "ub_diecheststand", v.hold = opt.shot ? -1000.0f : b.died_at, soldiers.push_back(v);
        else {
            const std::string& w = b.weapon.name;
            const char* lower_b = b.posture == 2 ? "lb_lie" : b.posture == 1 ? "lb_crouch" : b.moving ? "lb_runforward" : "lb_stand";
            std::string upper = b.posture == 2 ? "ub_lie" + w : b.posture == 1 ? "ub_crouch" + w : b.moving ? "ub_runforward" + w : "ub_standaim" + w;
            v.lower = lower_b, v.upper = w.empty() ? "" : upper;
            if (!w.empty()) arm_states(v, w, b.weapon, b.ammo, b.last_shot, b.posture);
            if (bseat) v.lower = bseat->lower, v.upper = bseat->upper, v.weapon.clear(), v.held_parts = 0;  // c_AsmHideWeapon
            soldiers.push_back(v);
        }
    }
}

HudInfo Game::hud_info() const {
    HudInfo h;
    if (player < 0) return h;
    float full = std::max(1.0f, hitpoints(soldier_tmpl));
    h.on = true, h.team = team, h.hp = static_cast<int>(std::ceil(std::max(0.0f, player_hp) * 100 / full));  // percent
    h.winner = game.winner;
    for (int t = 0; t < 2; ++t) h.tickets[t] = static_cast<int>(game.tickets[t + 1]), h.flags[t] = game.owned(t + 1);
    h.weapon = hand.name;
    if (!hand_ammo.unlimited) h.rounds = hand_ammo.rounds, h.spare = hand_ammo.spare, h.reloading = hand_ammo.reloading(now);
    if (cur >= 0) {
        const Veh& v = vehicles[static_cast<size_t>(cur)];
        h.vehicle = v.name, h.vehicle_hp = static_cast<int>(std::ceil(std::max(0.0f, v.hp)));
        h.weapon = v.seats[static_cast<size_t>(seat)].weapon.name, h.rounds = -1, h.reloading = false;
        if (v.seats.size() > 1) h.vehicle += "  seat " + std::to_string(seat + 1) + "/" + std::to_string(v.seats.size());
    }
    for (const auto& [t, m] : feed)
        if (now - t < 6) h.feed.push_back(m);
    if (h.feed.size() > 5) h.feed.erase(h.feed.begin(), h.feed.end() - 5);
    return h;
}

}  // namespace obf
