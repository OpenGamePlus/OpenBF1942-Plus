#include "effects.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "scene.h"

namespace obf {

namespace {
std::string low(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string p; std::getline(in, p, sep);) out.push_back(p);
    return out;
}
float num(const std::string& s) {
    float v = static_cast<float>(std::atof(s.c_str()));
    return std::isfinite(v) ? v : 0.0f;
}
const ConObject* find(const Con& con, const std::string& name) {
    auto s = con.templates.find("objecttemplate");
    if (s == con.templates.end()) return nullptr;
    auto t = s->second.find(low(name));
    return t == s->second.end() ? nullptr : &t->second;
}
}  // namespace

Crd crd_parse(const std::string& s) {
    Crd c;
    auto p = split(s, '/');
    if (p.size() < 2) return c;
    c.uniform = low(p[0]) != "crd_none";
    c.a = num(p[1]), c.b = p.size() > 2 ? num(p[2]) : c.a, c.sym = p.size() > 3 && num(p[3]) != 0;
    return c;
}

float Crd::sample(std::mt19937& rng) const {
    float v = a;
    if (uniform) v = a + (b - a) * std::uniform_real_distribution<float>(0, 1)(rng);
    if (sym && std::uniform_int_distribution<int>(0, 1)(rng)) v = -v;
    return v;
}

Curve curve_parse(const std::string& s) {
    Curve c;
    for (const std::string& k : split(s, '|'))
        if (auto p = split(k, '/'); p.size() >= 2) c.keys.push_back({num(p[0]) / 100, num(p[1])});
    std::sort(c.keys.begin(), c.keys.end());
    return c;
}

float Curve::at(float t) const {
    if (keys.empty()) return 1;
    if (t <= keys.front().first) return keys.front().second;
    for (size_t i = 1; i < keys.size(); ++i)
        if (t <= keys[i].first) {
            float span = keys[i].first - keys[i - 1].first, f = span > 0 ? (t - keys[i - 1].first) / span : 1;
            return keys[i - 1].second + (keys[i].second - keys[i - 1].second) * f;
        }
    return keys.back().second;
}

std::shared_ptr<const ParticleDef> Effects::particle(const std::string& name) {
    auto it = particles_.find(low(name));
    if (it != particles_.end()) return it->second;
    std::shared_ptr<ParticleDef> p;
    const ConObject* t = find(con_, name);
    std::string type = t ? low(t->type) : "";
    if (type == "spriteparticle" || type == "particle") {
        p = std::make_shared<ParticleDef>();
        p->ttl.a = p->ttl.b = 1, p->size.a = p->size.b = 1, p->ratio.a = p->ratio.b = 1;
        for (auto& [k, a] : t->props) {
            if (a.empty()) continue;
            const std::string& v = a[0];
            if (k == "texture") p->texture = "texture/" + low(v);
            else if (k == "geometry" && type == "particle") {  // GeometryTemplate <name>: its file
                if (auto gs = con_.templates.find("geometrytemplate"); gs != con_.templates.end())
                    if (auto g = gs->second.find(low(v)); g != gs->second.end())
                        for (auto& [gk, ga] : g->second.props)
                            if (gk == "file" && !ga.empty()) p->mesh = "standardmesh/" + low(ga[0]) + ".sm";
            }
            else if (k == "timetolive") p->ttl = crd_parse(v);
            else if (k == "size") p->size = crd_parse(v);
            else if (k == "gravitymodifier") p->gravity = crd_parse(v);
            else if (k == "drag") p->drag = crd_parse(v);
            else if (k == "initrotation") p->rotation = crd_parse(v);
            else if (k == "rotationspeed") p->rotation_speed = crd_parse(v);
            else if (k == "initanimationframe") p->frame = crd_parse(v);
            else if (k == "animationspeed") p->anim_speed = crd_parse(v);
            else if (k == "xysizeratio") p->ratio = crd_parse(v);
            else if (k == "sizeovertime") p->size_over = curve_parse(v);
            else if (k == "gravitymodifierovertime") p->gravity_t = curve_parse(v);
            else if (k == "rotationspeedovertime") p->rotation_speed_t = curve_parse(v);
            else if (k == "alphaovertime") p->alpha_t = curve_parse(v);
            else if (k == "destblendmode") p->additive = low(v) == "bmone";
            else if (k == "numanimationframes") p->frames = std::max(1, std::atoi(v.c_str()));
            else if (k == "colorrgbaovertime")
                for (const std::string& key : split(v, '|'))
                    if (auto c = split(key, '/'); c.size() >= 5) p->color.push_back({num(c[0]), num(c[1]), num(c[2]), num(c[3]), num(c[4])});
        }
        std::sort(p->color.begin(), p->color.end());
        if (type == "particle" && p->mesh.empty()) p = nullptr;
    }
    return particles_[low(name)] = p;
}

const EffectDef* Effects::def(const std::string& name) {
    auto it = defs_.find(low(name));
    if (it != defs_.end()) return it->second.get();
    auto d = std::make_shared<EffectDef>();
    if (const ConObject* t = find(con_, name)) {
        auto emitter = [&](const std::string& en, Mat4 local) {
            const ConObject* e = find(con_, en);
            if (!e || low(e->type) != "emitter") return;
            EmitterDef ed;
            ed.local = local, ed.ttl.a = ed.ttl.b = -1;

            for (auto& [k, a] : e->props) {
                if (a.empty()) continue;
                const std::string& v = a[0];
                if (k == "template") ed.particle = particle(v);
                else if (k == "timetolive") ed.ttl = crd_parse(v);
                else if (k == "intensity") ed.intensity = crd_parse(v);
                else if (k == "delay") ed.delay = crd_parse(v);
                else if (k == "looping") ed.looping = num(v) != 0;
                else if (k == "addemitterspeed") ed.add_speed = num(v) != 0;
                else if (k == "emitterspeedscale") ed.speed_scale = num(v);
                else if (k == "relativepositioninright") ed.pos[0] = crd_parse(v);
                else if (k == "relativepositioninup") ed.pos[1] = crd_parse(v);
                else if (k == "relativepositionindof") ed.pos[2] = crd_parse(v);
                else if (k == "positionalspeedinright") ed.speed[0] = crd_parse(v);
                else if (k == "positionalspeedinup") ed.speed[1] = crd_parse(v);
                else if (k == "positionalspeedindof") ed.speed[2] = crd_parse(v);
                else if (k == "intensityatspeed") ed.at_speed = std::max(0.0f, num(v));
                else if (k == "movetowatersurface") ed.to_water = num(v) != 0;
                else if (k == "showinfirstperson") ed.first = num(v) != 0;
                else if (k == "showinthirdperson") ed.third = num(v) != 0;
            }
            if (ed.particle) d->emitters.push_back(std::move(ed));
        };
        if (low(t->type) == "emitter") emitter(name, Mat4::identity());
        // children: addTemplate, then setPosition / setRotation for the one just added
        std::vector<std::pair<std::string, std::array<float, 6>>> kids;
        for (auto& [k, a] : t->props) {
            if (a.empty()) continue;
            if (k == "addtemplate") kids.push_back({a[0], {}});
            else if (k == "setposition" && !kids.empty()) parse_vec3(a[0], kids.back().second.data());
            else if (k == "setrotation" && !kids.empty()) parse_vec3(a[0], kids.back().second.data() + 3);
            else if (k == "timetolive") d->ttl = crd_parse(a[0]).a;
            else if (k == "addworkonmaterial") d->materials.push_back(std::atoi(a[0].c_str()));
        }
        for (auto& [kn, pr] : kids) emitter(kn, translate(pr[0], pr[1], pr[2]) * rotation_ypr(pr[3], pr[4], pr[5]));
    }
    if (d->emitters.empty()) d = nullptr;
    return (defs_[low(name)] = d).get();
}

int Effects::spawn(const std::string& name, const Mat4& at, const float vel[3], int view) {
    const EffectDef* d = name.empty() ? nullptr : def(name);
    if (!d) return -1;
    Run r;
    r.def = defs_[low(name)], r.at = at;
    if (vel) std::copy(vel, vel + 3, r.vel);
    for (const EmitterDef& e : d->emitters)
        if (view == 1 ? e.first || !e.third : !(e.first && !e.third))
            r.emitters.push_back({&e, e.ttl.sample(rng_), e.delay.sample(rng_), 1, {}});
    if (r.emitters.empty()) return -1;
    live_[next_] = std::move(r);
    return next_++;
}

void Effects::move(int id, const Mat4& at, const float vel[3]) {
    auto it = live_.find(id);
    if (it == live_.end()) return;
    it->second.at = at;
    if (vel) std::copy(vel, vel + 3, it->second.vel);
}

void Effects::stop(int id) {
    if (auto it = live_.find(id); it != live_.end()) it->second.stopped = true;
}

void Effects::step(float dt) {
    if (!(dt > 0)) return;
    for (auto it = live_.begin(); it != live_.end();) {
        Run& r = it->second;
        r.age += dt;
        bool over = r.stopped || (r.def->ttl >= 0 && r.age > r.def->ttl), busy = false;
        for (EmitterRun& er : r.emitters) {
            const EmitterDef& e = *er.def;
            const ParticleDef& pd = *e.particle;
            float t = r.age - er.delay;
            bool on = !over && t >= 0 && (e.looping || er.ttl < 0 || t < er.ttl);
            if (on) {
                float k = 1;
                if (e.at_speed > 0) k = std::min(1.0f, std::sqrt(r.vel[0] * r.vel[0] + r.vel[1] * r.vel[1] + r.vel[2] * r.vel[2]) / e.at_speed);
                er.acc += std::max(0.0f, e.intensity.sample(rng_)) * k * dt;
                for (; er.acc >= 1 && er.ps.size() < 400; er.acc -= 1) {  // 400: a cap per emitter, guessed
                    Mat4 m = r.at * e.local;
                    float o[3] = {e.pos[0].sample(rng_), e.pos[1].sample(rng_), e.pos[2].sample(rng_)};
                    float s[3] = {e.speed[0].sample(rng_), e.speed[1].sample(rng_), e.speed[2].sample(rng_)};
                    Particle p{};
                    for (int i = 0; i < 3; ++i) {
                        p.pos[i] = m.m[12 + i] + m.m[i] * o[0] + m.m[4 + i] * o[1] + m.m[8 + i] * o[2];
                        p.vel[i] = m.m[i] * s[0] + m.m[4 + i] * s[1] + m.m[8 + i] * s[2] + (e.add_speed ? r.vel[i] * e.speed_scale : 0.0f);
                    }
                    if (e.to_water) p.pos[1] = water_level;
                    for (int i = 0; i < 3; ++i) p.axes[i] = m.m[i], p.axes[3 + i] = m.m[4 + i], p.axes[6 + i] = m.m[8 + i];
                    p.ttl = std::max(0.01f, pd.ttl.sample(rng_)), p.size = pd.size.sample(rng_), p.gravity = pd.gravity.sample(rng_);
                    p.drag = pd.drag.sample(rng_), p.rot = pd.rotation.sample(rng_), p.rot_speed = pd.rotation_speed.sample(rng_);
                    p.frame = pd.frame.sample(rng_), p.anim_speed = pd.anim_speed.sample(rng_), p.ratio = pd.ratio.sample(rng_);
                    er.ps.push_back(p);
                }
            }
            for (Particle& p : er.ps) {
                p.age += dt;
                float f = p.age / p.ttl;
                p.vel[1] -= 9.81f * p.gravity * pd.gravity_t.at(f) * dt;
                float keep = std::max(0.0f, 1 - p.drag * dt);
                for (int i = 0; i < 3; ++i) p.vel[i] *= keep, p.pos[i] += p.vel[i] * dt;
                p.rot += p.rot_speed * pd.rotation_speed_t.at(f) * dt, p.frame += p.anim_speed * dt;
            }
            er.ps.erase(std::remove_if(er.ps.begin(), er.ps.end(), [](const Particle& p) { return p.age >= p.ttl; }), er.ps.end());
            busy = busy || !er.ps.empty() || (!over && (e.looping || er.ttl < 0 || t < er.ttl));
        }
        it = busy ? std::next(it) : live_.erase(it);
    }
}

std::vector<Sprite> Effects::sprites() const {
    std::vector<Sprite> out;
    for (auto& [id, r] : live_)
        for (const EmitterRun& er : r.emitters) {
            const ParticleDef& pd = *er.def->particle;
            if (!pd.mesh.empty()) continue;
            for (const Particle& p : er.ps) {
                float f = std::clamp(p.age / p.ttl, 0.0f, 1.0f) * 100;
                float c[4] = {255, 255, 255, 255};
                if (!pd.color.empty()) {
                    size_t i = 0;
                    while (i + 1 < pd.color.size() && pd.color[i + 1][0] < f) ++i;
                    const auto& a = pd.color[i];
                    const auto& b = pd.color[std::min(i + 1, pd.color.size() - 1)];
                    float span = b[0] - a[0], k = span > 0 ? std::clamp((f - a[0]) / span, 0.0f, 1.0f) : 0.0f;
                    for (int j = 0; j < 4; ++j) c[j] = a[1 + j] + (b[1 + j] - a[1 + j]) * k;
                }
                Sprite s{};
                std::copy(p.pos, p.pos + 3, s.pos);
                s.size = p.size * pd.size_over.at(f / 100), s.ratio = p.ratio, s.rotation = p.rot;
                for (int j = 0; j < 4; ++j) s.rgba[j] = std::clamp(c[j] / 255, 0.0f, 1.0f);
                s.rgba[3] *= std::clamp(pd.alpha_t.at(f / 100), 0.0f, 1.0f);
                s.texture = &pd.texture, s.frames = pd.frames, s.additive = pd.additive;
                s.frame = static_cast<int>(std::floor(p.frame)) % pd.frames;
                if (s.frame < 0) s.frame += pd.frames;
                if (s.size > 0 && s.rgba[3] > 0) out.push_back(s);
            }
        }
    return out;
}

// Mesh particles: the emitter's axes at emission, turned about its z ("Dof") by the particle's rotation, scaled by
// size x sizeOverTime (unverified); coloured by colorRGBAOverTime when it has one.
std::vector<MeshParticle> Effects::meshes() const {
    std::vector<MeshParticle> out;
    for (auto& [id, r] : live_)
        for (const EmitterRun& er : r.emitters) {
            const ParticleDef& pd = *er.def->particle;
            if (pd.mesh.empty()) continue;
            for (const Particle& p : er.ps) {
                float f = std::clamp(p.age / p.ttl, 0.0f, 1.0f);
                float sz = p.size * pd.size_over.at(f), a = p.rot * 3.14159265f / 180, ca = std::cos(a), sa = std::sin(a);
                MeshParticle mp{};
                mp.world = Mat4::identity();
                for (int i = 0; i < 3; ++i) {
                    float x = p.axes[i], y = p.axes[3 + i];
                    mp.world.m[i] = (x * ca + y * sa) * sz, mp.world.m[4 + i] = (y * ca - x * sa) * sz;
                    mp.world.m[8 + i] = p.axes[6 + i] * sz, mp.world.m[12 + i] = p.pos[i];
                }
                mp.mesh = &pd.mesh;
                for (float& c : mp.rgba) c = 1;
                if (!pd.color.empty()) {
                    size_t i = 0;
                    while (i + 1 < pd.color.size() && pd.color[i + 1][0] < f * 100) ++i;
                    for (int j = 0; j < 4; ++j) mp.rgba[j] = std::clamp(pd.color[i][1 + j] / 255, 0.0f, 1.0f);
                }
                mp.rgba[3] *= std::clamp(pd.alpha_t.at(f), 0.0f, 1.0f);
                if (sz > 0) out.push_back(mp);
            }
        }
    return out;
}

size_t Effects::particles() const {
    size_t n = 0;
    for (auto& [id, r] : live_)
        for (const EmitterRun& er : r.emitters) n += er.ps.size();
    return n;
}

}  // namespace obf
