// Headless game runner: loads a level and runs the scripted demo of obf_mapview --shot without a window or GL.
// usage: obf_sim <BF1942_DIR> <mod> <level> (--play <team or soldier> [--kit 0-4] | --drive <vehicle> [--fire-ahead])
//        [--cam x y z yaw pitch] [--mode conquest|ctf|tdm|coop] [--sim seconds]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "con.h"
#include "game.h"
#include "scene.h"
#include "terrain.h"
#include "vfs.h"

using namespace obf;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: obf_sim <BF1942_DIR> <mod> <level> (--play <team or soldier> [--kit 0-4] | --drive <vehicle> [--fire-ahead])\n"
                             "       [--cam x y z yaw pitch] [--mode conquest|ctf|tdm|coop] [--sim seconds]\n");
        return 2;
    }
    try {
        GameOptions opt;
        opt.shot = true;
        Camera cam;
        std::string mode = "conquest";
        float sim_seconds = 10;
        for (int i = 4; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--play") && i + 1 < argc) opt.play = argv[++i];
            else if (!std::strcmp(argv[i], "--drive") && i + 1 < argc) opt.drive = argv[++i];
            else if (!std::strcmp(argv[i], "--kit") && i + 1 < argc) opt.kit = std::clamp(std::atoi(argv[++i]), 0, 4);
            else if (!std::strcmp(argv[i], "--fire-ahead")) opt.fire_ahead = true;
            else if (!std::strcmp(argv[i], "--mode") && i + 1 < argc) mode = argv[++i];
            else if (!std::strcmp(argv[i], "--sim") && i + 1 < argc) sim_seconds = std::clamp(static_cast<float>(std::atof(argv[++i])), 0.0f, 36000.0f);
            else if (!std::strcmp(argv[i], "--cam") && i + 5 < argc) {
                cam = {std::stof(argv[i + 1]), std::stof(argv[i + 2]), std::stof(argv[i + 3]), std::stof(argv[i + 4]), std::stof(argv[i + 5])};
                i += 5;
            }
        }
        Vfs vfs;
        vfs.mount_mod(argv[1], argv[2]);
        std::string lvl = "bf1942/levels/" + lower(argv[3]) + "/";
        auto t = vfs.read(lvl + "init/terrain.con");
        TerrainInfo info = parse_terrain_con(std::string(t.begin(), t.end()));
        Heightmap hm = load_heightmap(vfs.read(info.heightmap + ".raw"), info.y_scale);
        Con con(vfs);
        con.load_game();
        con.run("bf1942/game/materialmanagersettings.con");  // as obf_mapview
        con.run("animations/animationstates.con");
        con.load_level(argv[3], mode);
        std::map<std::string, int> skipped;
        std::vector<SpawnPoint> spawns;
        std::vector<DrawItem> scene = build_scene(con, vfs, skipped, [](const std::string& t) {  // as obf_mapview
            return t == "parachute" || t.rfind("1p", 0) == 0 || t.find("1pbody") != std::string::npos;
        }, &spawns);
        Game game(vfs, con, hm, info, scene, spawns, opt, cam, true);
        game.print_weapon();
        game.demo(sim_seconds);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
