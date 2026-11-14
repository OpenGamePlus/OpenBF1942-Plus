#pragma once
// Effects: EffectBundle -> Emitter -> SpriteParticle templates (objects/effects/*/effects.con), simulated on the CPU.
// Readings of the fields are unverified (docs/formats/effects.md). No GL here: the renderer draws sprites().

#include <array>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "con.h"
#include "mat.h"

namespace obf {

// CRD_<kind>/a/b/sym: NONE a; UNIFORM between a and b (sym 1: either sign); NORMAL / EXPONENTIAL read as UNIFORM.
struct Crd {
    float a = 0, b = 0;
    bool uniform = false, sym = false;
    float sample(std::mt19937& rng) const;
};
Crd crd_parse(const std::string& s);
// "t/v|t/v|...": t in percent of the particle's life; at(): linear between keys, 1 with no keys.
struct Curve {
    std::vector<std::pair<float, float>> keys;
    float at(float t01) const;
};
Curve curve_parse(const std::string& s);

struct ParticleDef {
    std::string texture;  // texture/<name>
    std::string mesh;     // a Particle's geometry: standardmesh/<file>.sm (drawn as a mesh, not a sprite)
    Crd ttl, size, gravity, drag, rotation, rotation_speed, frame, anim_speed, ratio;
    Curve size_over, gravity_t, rotation_speed_t, alpha_t;  // multipliers over the life
    std::vector<std::array<float, 5>> color;  // colorRGBAOverTime: percent, r, g, b, a (0-255)
    bool additive = false;                    // destBlendMode BMOne
    int frames = 1;                           // numAnimationFrames: a square grid of frames in the texture
};

struct EmitterDef {
    std::shared_ptr<const ParticleDef> particle;
    Mat4 local = Mat4::identity();  // the bundle's setPosition / setRotation for it
    Crd ttl, intensity, delay;      // seconds active, particles per second, wait before starting
    Crd pos[3], speed[3];           // relativePositionIn / positionalSpeedIn Right, Up, Dof
    bool looping = false, add_speed = false;
    float speed_scale = 1;
    bool first = false, third = false;  // showInFirstPerson / showInThirdPerson
    float at_speed = 0;     // IntensityAtSpeed: full intensity from this carrier speed (m/s), less below; 0: always
    bool to_water = false;  // moveToWaterSurface: particles start at the water level
};

struct EffectDef {
    std::vector<EmitterDef> emitters;
    float ttl = -1;              // the bundle's timeToLive; -1 until stopped
    std::vector<int> materials;  // addWorkOnMaterial: only over these (1: water); empty: anywhere
};

struct Sprite {
    float pos[3], size, ratio, rotation, rgba[4];  // size: edge in metres; rotation: degrees; rgba 0-1
    const std::string* texture;
    int frames, frame;
    bool additive;
};

struct MeshParticle {
    Mat4 world;               // the emitter's frame turned by the particle's rotation, scaled by its size
    const std::string* mesh;  // standardmesh/<file>.sm
    float rgba[4];
};

class Effects {
public:
    explicit Effects(const Con& con) : con_(con) {}
    // An effect at a transform (its z axis is the emitters' "Dof", y "Up", x "Right"); vel: the carrier's velocity.
    // Returns an id, or -1 for a template with nothing to draw.
    // view 3: the world's emitters (first-person-only ones left out); 1: the first-person view's (with no flag or
    // showInFirstPerson).
    int spawn(const std::string& name, const Mat4& at, const float vel[3] = nullptr, int view = 3);
    void move(int id, const Mat4& at, const float vel[3] = nullptr);  // an attached effect follows its carrier
    bool alive(int id) const { return live_.count(id) > 0; }
    const EffectDef* lookup(const std::string& name) { return name.empty() ? nullptr : def(name); }
    float water_level = -1e9f;
    void stop(int id);                                               // emitters end; particles live out their time
    void step(float dt);
    std::vector<Sprite> sprites() const;
    std::vector<MeshParticle> meshes() const;
    size_t particles() const;
    size_t running() const { return live_.size(); }

private:
    struct Particle {
        float pos[3], vel[3], age, ttl, size, gravity, drag, rot, rot_speed, frame, anim_speed, ratio;
        float axes[9];  // mesh particles: the emitter's x, y, z axes when emitted
    };
    struct EmitterRun {
        const EmitterDef* def;
        float ttl, delay, acc = 1;  // acc: particles owed; the first leaves at once (a muzzle flash cannot wait)
        std::vector<Particle> ps;
    };
    struct Run {
        std::shared_ptr<const EffectDef> def;
        Mat4 at;
        float vel[3] = {}, age = 0;
        bool stopped = false;
        std::vector<EmitterRun> emitters;
    };
    const EffectDef* def(const std::string& name);
    std::shared_ptr<const ParticleDef> particle(const std::string& name);
    const Con& con_;
    std::map<std::string, std::shared_ptr<const EffectDef>> defs_;
    std::map<std::string, std::shared_ptr<const ParticleDef>> particles_;
    std::map<int, Run> live_;
    int next_ = 0;
    std::mt19937 rng_{1942};
};

}  // namespace obf
