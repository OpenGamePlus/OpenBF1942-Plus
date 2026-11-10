// Map viewer and game window: arguments, window, input; the game is engine/game.*, drawing engine/render.*.
// Usage in docs/ai/SYSTEM_SUMMARY.md.
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "con.h"
#include "game.h"
#include "math.h"
#include "render.h"
#include "scene.h"
#include "terrain.h"
#include "vfs.h"

using namespace obf;

namespace {

std::string text(const Vfs& vfs, const std::string& path) {
    auto b = vfs.read(path);
    return std::string(b.begin(), b.end());
}

int run(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: obf_mapview <BF1942_DIR> <mod> <level> [--shot out.ppm] [--cam x y z yaw pitch] [--mode conquest|ctf|tdm|coop] [--soldier <template>] [--drive <vehicle>] [--play <team 1|2 or soldier>] [--kit 0-4] [--sim s] [--fire-ahead] [--collision] [--no-objects]\n"
                     "  right mouse: look, WASD/QE: move (or drive), shift: fast, C: collision meshes, esc: quit\n"
                     "  aircraft: W/S throttle (held), arrows pitch (up = nose down) and roll, A/D rudder; right mouse aims turrets\n"
                     "  --shot with --drive: settles 2 s, drives 4 s (last 2 s steering right), prints the track;\n"
                     "  aircraft: 20 s take-off run, pulling up from 12 s to 16 s; the vehicle fires from 2 s (--fire-ahead: gun\n"
                     "  straight ahead, no steering)\n"
                     "  --play: walk a soldier (WASD, shift walks, space jumps, right mouse looks, left mouse fires), E gets into / out of the nearest vehicle\n"
                     "  (parked vehicles are drivable with --play or --drive); with --shot: runs 2 s, gets into the nearest vehicle,\n"
                     "  drives 2 s, gets out, then reports how far the parked vehicles moved while settling, then plays --sim seconds\n"
                     "  (default 10) of conquest and prints flag changes and tickets\n");
        return 2;
    }
    std::string shot;
    float cx = -1, cy = 0, cz = 0, yaw = 0.6f, pitch = -0.5f;
    bool objects = true, show_collision = false;
    std::string mode = "conquest", soldier, drive, play;
    bool look_demo = false;  // --shot vehicle demo: keep the gun straight ahead (--fire-ahead)
    int kit = 1;             // --kit: 0 scout, 1 assault, 2 anti-tank, 3 medic, 4 engineer
    float sim_seconds = 10;  // --shot with --play: seconds of AI after the demo (--sim)
    for (int i = 4; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--no-objects")) objects = false;
        else if (!std::strcmp(argv[i], "--collision")) show_collision = true;
        else if (!std::strcmp(argv[i], "--mode") && i + 1 < argc) mode = argv[++i];
        else if (!std::strcmp(argv[i], "--soldier") && i + 1 < argc) soldier = argv[++i];
        else if (!std::strcmp(argv[i], "--drive") && i + 1 < argc) drive = argv[++i];
        else if (!std::strcmp(argv[i], "--play") && i + 1 < argc) play = argv[++i];
        else if (!std::strcmp(argv[i], "--fire-ahead")) look_demo = true;
        else if (!std::strcmp(argv[i], "--kit") && i + 1 < argc) kit = std::clamp(std::atoi(argv[++i]), 0, 4);
        else if (!std::strcmp(argv[i], "--sim") && i + 1 < argc) sim_seconds = std::clamp(static_cast<float>(std::atof(argv[++i])), 0.0f, 3600.0f);
        else if (!std::strcmp(argv[i], "--cam") && i + 5 < argc) {
            cx = std::stof(argv[i + 1]), cy = std::stof(argv[i + 2]), cz = std::stof(argv[i + 3]);
            yaw = std::stof(argv[i + 4]), pitch = std::stof(argv[i + 5]);
            i += 5;
        }
    }

    Vfs vfs;
    vfs.mount_mod(argv[1], argv[2]);
    std::string lvl = "bf1942/levels/" + lower(argv[3]) + "/";
    TerrainInfo info = parse_terrain_con(text(vfs, lvl + "init/terrain.con"));
    Heightmap hm = load_heightmap(vfs.read(info.heightmap + ".raw"), info.y_scale);
    std::printf("%s: %zu files, heightmap %dx%d, world %.0f m\n", argv[3], vfs.size(), hm.n, hm.n, info.world_size);

    Con con(vfs);
    std::vector<DrawItem> scene;
    std::vector<SpawnPoint> spawns;
    std::map<std::string, int> skipped;
    if (objects) {
        con.load_game();
        con.run("bf1942/game/materialmanagersettings.con");  // shortcut: assumed engine-loaded; damage tables
        // shortcut: assumed engine-loaded like objects/; defines AnimationStateMachine states.
        con.run("animations/animationstates.con");
        con.load_level(argv[3], mode);
        if (!soldier.empty()) {  // demo: place one soldier 6 m in front of the start camera, on the ground
            float sx = cx < 0 ? info.world_size * 0.15f : cx, sz = cx < 0 ? info.world_size * 0.15f : cz;
            sx += std::sin(yaw) * 6, sz += std::cos(yaw) * 6;
            float cell = info.world_size / static_cast<float>(hm.n);
            int r = std::clamp(static_cast<int>(sz / cell), 0, hm.n - 1), c = std::clamp(static_cast<int>(sx / cell), 0, hm.n - 1);
            char pos[96];
            std::snprintf(pos, sizeof pos, "%g/%g/%g", sx, hm.height(r, c), sz);
            con.instances.push_back({"", soldier, {{"absoluteposition", {pos}}, {"rotation", {std::to_string(yaw * 57.29578f + 180) + "/0/0"}}}});
        }
        // Third-person view only: the engine swaps these parts by camera and state at runtime.
        scene = build_scene(con, vfs, skipped, [](const std::string& t) {
            return t == "parachute" || t.rfind("1p", 0) == 0 || t.find("1pbody") != std::string::npos;
        }, &spawns);
        std::printf("objects: %zu instances -> %zu meshes drawn; skipped:", con.instances.size(), scene.size());
        for (auto& [why, n] : skipped) std::printf(" %s %d,", why.c_str(), n);
        std::printf("\n");
    }

    GameOptions opt;
    opt.drive = drive, opt.play = play, opt.kit = kit, opt.fire_ahead = look_demo, opt.shot = !shot.empty();
    Game game(vfs, con, hm, info, scene, spawns, opt, Camera{cx, cy, cz, yaw, pitch}, objects);
    for (auto& [why, n] : game.skipped) skipped[why] += n;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    int W = 1280, H = 720;
    SDL_Window* win = SDL_CreateWindow("OpenBF1942-Plus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) throw std::runtime_error(SDL_GetError());
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetSwapInterval(1);
    Renderer renderer(vfs, con, hm, info, scene, objects);
    game.print_weapon();

    bool running = true, look = false;
    Uint64 last = SDL_GetPerformanceCounter();
    std::vector<float> projectiles;
    while (running) {
        SDL_Event ev;
        float mouse_x = 0, mouse_y = 0;  // this frame's look motion (turrets when driving)
        bool toggle_vehicle = false;     // E: get in / out
        Camera& cam = game.cam;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_c) show_collision = !show_collision;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_e && !ev.key.repeat) toggle_vehicle = true;
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_RIGHT) look = true;
            if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_RIGHT) look = false;
            if (ev.type == SDL_MOUSEMOTION && look) {
                mouse_x += static_cast<float>(ev.motion.xrel), mouse_y += static_cast<float>(ev.motion.yrel);
                cam.yaw += static_cast<float>(ev.motion.xrel) * 0.004f;
                cam.pitch = std::clamp(cam.pitch - static_cast<float>(ev.motion.yrel) * 0.004f, -1.5f, 1.5f);
            }
        }
        SDL_SetRelativeMouseMode(look ? SDL_TRUE : SDL_FALSE);
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - last) / static_cast<float>(SDL_GetPerformanceFrequency());
        last = now;
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        auto key = [&](SDL_Scancode a, SDL_Scancode b) { return static_cast<float>(k[a] - k[b]); };
        if (shot.empty()) {
            GameInput in;
            in.throttle = in.forward = key(SDL_SCANCODE_W, SDL_SCANCODE_S);
            in.yaw = in.right = key(SDL_SCANCODE_D, SDL_SCANCODE_A);
            in.pitch = key(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN), in.roll = key(SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT);
            in.look_x = mouse_x / 20.0f, in.look_y = mouse_y / 20.0f;  // mouse right / down = +1 at ~20 px per frame
            in.jump = k[SDL_SCANCODE_SPACE], in.slow = k[SDL_SCANCODE_LSHIFT], in.use = toggle_vehicle;
            in.fire = SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT);
            game.step(static_cast<float>(SDL_GetTicks()) / 1000.0f, dt, in);
        } else {
            game.demo(sim_seconds);
        }
        game.pose();
        std::string title;
        if (game.hud(title)) SDL_SetWindowTitle(win, title.c_str());
        float speed = (k[SDL_SCANCODE_LSHIFT] ? 400.f : 60.f) * dt * !game.controls_camera();  // free camera
        float fx = std::sin(cam.yaw), fz = std::cos(cam.yaw);
        if (k[SDL_SCANCODE_W]) cam.x += fx * speed, cam.z += fz * speed;
        if (k[SDL_SCANCODE_S]) cam.x -= fx * speed, cam.z -= fz * speed;
        if (k[SDL_SCANCODE_D]) cam.x += fz * speed, cam.z -= fx * speed;
        if (k[SDL_SCANCODE_A]) cam.x -= fz * speed, cam.z += fx * speed;
        if (k[SDL_SCANCODE_E]) cam.y += speed;
        if (k[SDL_SCANCODE_Q]) cam.y -= speed;

        SDL_GL_GetDrawableSize(win, &W, &H);
        game.phys.projectiles(projectiles);
        // shortcut: the .baf frame rate is a guess; --shot renders at t = 1 s so captures are repeatable.
        float seconds = shot.empty() ? static_cast<float>(SDL_GetTicks()) / 1000.0f : 1.0f;
        renderer.draw(scene, cam, W, H, seconds, show_collision, projectiles, game.soldiers);
        if (!shot.empty()) {
            renderer.save_ppm(shot, W, H);
            running = false;
        }
        SDL_GL_SwapWindow(win);
    }
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
