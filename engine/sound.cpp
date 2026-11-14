#include "sound.h"

#include <algorithm>

#include <cctype>
#include <functional>

namespace obf {

namespace {
std::string low(std::string s) {
    for (char& c : s) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

std::string Sounds::script(const std::string& tmpl, const std::string& type, bool children) const {
    auto& ts = con_.templates.at("objecttemplate");
    std::function<std::string(const std::string&, int)> find = [&](const std::string& n, int depth) -> std::string {
        auto it = ts.find(low(n));
        if (it == ts.end() || depth > 6) return "";
        const ConObject& o = it->second;
        if (type.empty() || low(o.type) == low(type))
            for (auto& [k, a] : o.props)
                if (k == "loadsoundscript" && !a.empty()) return low(o.dir.empty() ? a[0] : o.dir + "/" + a[0]);
        if (!children) return "";
        for (auto& [k, a] : o.props)
            if (k == "addtemplate" && !a.empty())
                if (std::string s = find(a[0], depth + 1); !s.empty()) return s;
        return "";
    };
    return con_.templates.count("objecttemplate") ? find(tmpl, 0) : "";
}

const SoundPatch* Sounds::patch(const std::string& path) {
    auto it = patches_.find(path);
    if (it != patches_.end()) return it->second.get();
    auto read = [&](const std::string& p) {
        if (!vfs_.has(p)) return std::string();
        auto d = vfs_.read(p);
        return std::string(d.begin(), d.end());
    };
    SoundPatch sp = ssc_parse(path, read);
    auto& slot = patches_[path];
    if (!sp.layers.empty()) slot = std::make_unique<SoundPatch>(std::move(sp));
    if (slot)
        for (auto& l : slot->layers)
            if (!wavs_.count(l.wav)) {
                std::shared_ptr<const Wav> w;
                try {
                    if (vfs_.has(l.wav)) w = std::make_shared<Wav>(wav_parse(vfs_.read(l.wav)));
                } catch (const std::exception&) {  // a bad file stays silent
                }
                wavs_[l.wav] = w;
            }
    return slot.get();
}

const SoundPatch* Sounds::weapon_patch(const std::string& tmpl, size_t index) {
    auto it = weapon_.find(tmpl);
    if (it == weapon_.end()) {
        std::string path = script(tmpl);
        std::vector<SoundPatch> ps;
        if (!path.empty()) {
            auto read = [&](const std::string& p) {
                if (!vfs_.has(p)) return std::string();
                auto d = vfs_.read(p);
                return std::string(d.begin(), d.end());
            };
            ps = ssc_patches(path, read);
        }
        it = weapon_.emplace(tmpl, std::move(ps)).first;
    }
    return index < it->second.size() && !it->second[index].layers.empty() ? &it->second[index] : nullptr;
}

int Sounds::play_patch(const SoundPatch& p, const float pos[3], float control, bool once) {
    SoundPatch q = p;
    std::vector<std::shared_ptr<const Wav>> w;
    for (auto& l : q.layers) {
        if (once) l.loop = false;
        if (!wavs_.count(l.wav)) {
            std::shared_ptr<const Wav> wav;
            try {
                if (vfs_.has(l.wav)) wav = std::make_shared<Wav>(wav_parse(vfs_.read(l.wav)));
            } catch (const std::exception&) {  // a bad file stays silent
            }
            wavs_[l.wav] = wav;
        }
        w.push_back(wavs_[l.wav]);
    }
    return mixer.play(q, w, pos, control);
}

void Sounds::shot(const std::string& tmpl, const float pos[3], int source, float now) {
    const SoundPatch* burst = weapon_patch(tmpl, 5);
    bool automatic = burst && std::any_of(burst->layers.begin(), burst->layers.end(), [](const SoundLayer& l) { return l.loop; });
    if (automatic) {  // one loop per shooter while it fires (single-shot weapons keep silence.wav there)
        auto b = bursts_.find(source);
        if (b == bursts_.end()) b = bursts_.emplace(source, Burst{play_patch(*burst, pos, 0, false), now}).first;
        else mixer.update(b->second.handle, pos, 0);
        b->second.last = now;
    } else if (const SoundPatch* single = weapon_patch(tmpl, 0)) {
        play_patch(*single, pos, 0, true);
    }
    if (const SoundPatch* far = weapon_patch(tmpl, 2)) play_patch(*far, pos, 0, true);
}

void Sounds::reload(const std::string& tmpl, const float pos[3]) {
    if (const SoundPatch* r = weapon_patch(tmpl, 1)) play_patch(*r, pos, 0, true);
}

void Sounds::effect(const std::string& tmpl, const float pos[3]) {
    for (size_t i = 0; i < 16; ++i)  // patches left empty (commented out) are skipped, not the end
        if (const SoundPatch* p = weapon_patch(tmpl, i)) play_patch(*p, pos, 0, true);
}

// A soldier template's setRadioLanguage ("German"), lower-cased; "english" if it has none.
std::string Sounds::language(const std::string& tmpl) const {
    if (con_.templates.count("objecttemplate")) {
        auto& ts = con_.templates.at("objecttemplate");
        if (auto it = ts.find(low(tmpl)); it != ts.end())
            for (auto& [k, a] : it->second.props)
                if (k == "setradiolanguage" && !a.empty()) {
                    std::string l = low(a[0]);
                    l.erase(std::remove(l.begin(), l.end(), '"'), l.end());
                    return l;
                }
    }
    return "english";
}

SoundPatch Sounds::soldier_patch(const std::string& script, int patch, const std::string& tmpl) {
    // shortcut: the soldiers' shared script folder, where SoldierSound.inc's relative names point; paths with a
    // folder (the radio's menu/menuradiosound.ssc, bf1942/game/gameplay.ssc) are taken as they are
    const std::string key = script.find('/') != std::string::npos ? script : "objects/soldiers/common/sounds/" + script;
    auto it = weapon_.find(key);
    if (it == weapon_.end()) {
        auto read = [&](const std::string& p) {
            if (!vfs_.has(p)) return std::string();
            auto d = vfs_.read(p);
            return std::string(d.begin(), d.end());
        };
        it = weapon_.emplace(key, vfs_.has(key) ? ssc_patches(key, read) : std::vector<SoundPatch>{}).first;
    }
    SoundPatch one;
    if (patch < 0 || static_cast<size_t>(patch) >= it->second.size() || it->second[static_cast<size_t>(patch)].layers.empty()) return one;
    const SoundPatch& all = it->second[static_cast<size_t>(patch)];
    // A patch mixing triggered and untriggered layers is a chain (posture changes, the fall's wind): it plays whole.
    // Otherwise its layers are variants (footsteps; the fall's screams, all triggered at 3.3 s): the next one plays.
    auto triggered = std::count_if(all.layers.begin(), all.layers.end(), [](const SoundLayer& l) { return l.trigger; });
    if (triggered > 0 && static_cast<size_t>(triggered) < all.layers.size()) one = all;
    else one.layers.push_back(all.layers[variant_++ % all.layers.size()]);
    // Voices: @Language is the soldier's radio language folder; one the archives lack (Canadian) falls back to English.
    for (SoundLayer& l : one.layers)
        if (auto at = l.wav.find("@language"); at != std::string::npos) {
            std::string wav = l.wav;
            l.wav.replace(at, 9, language(tmpl));
            if (!vfs_.has(l.wav)) l.wav = wav.replace(at, 9, "english");
        }
    return one;
}

void Sounds::soldier(const std::string& script, int patch, const float pos[3], const std::string& tmpl) {
    SoundPatch one = soldier_patch(script, patch, tmpl);
    if (!one.layers.empty()) play_patch(one, pos, 0, true);
}

void Sounds::tick(float now) {
    for (auto it = bursts_.begin(); it != bursts_.end();)
        if (now - it->second.last > 0.25f) mixer.release(it->second.handle), it = bursts_.erase(it);
        else ++it;
}

int Sounds::play(const std::string& path, const float pos[3], float control) {
    const SoundPatch* p = path.empty() ? nullptr : patch(path);
    if (!p) return -1;
    std::vector<std::shared_ptr<const Wav>> w;
    for (auto& l : p->layers) w.push_back(wavs_[l.wav]);
    return mixer.play(*p, w, pos, control);
}


void Sounds::loops(const std::vector<Running>& running) {
    std::map<long long, int> next;
    for (const Running& r : running) {
        auto it = loops_.find(r.key);
        if (it == loops_.end()) {
            auto key = r.tmpl + "\n own";
            auto sc = scripts_.find(key);
            if (sc == scripts_.end()) sc = scripts_.emplace(key, script(r.tmpl, "", false)).first;
            int h = -1;
            if (!r.script.empty()) {  // a soldier's (falling): that patch of its SoldierSound.inc script
                SoundPatch one = soldier_patch(r.script, r.patch, r.tmpl);
                if (!one.layers.empty()) h = play_patch(one, r.pos, r.control, false);
            } else if (r.patch >= 0) {
                if (const SoundPatch* one = weapon_patch(r.tmpl, static_cast<size_t>(r.patch))) h = play_patch(*one, r.pos, r.control, false);
            } else {
                h = play(sc->second, r.pos, r.control);
            }
            if (h >= 0) mixer.update(h, r.pos, r.control, r.speed, r.accel);
            next[r.key] = h;
        } else {
            if (it->second >= 0) mixer.update(it->second, r.pos, r.control, r.speed, r.accel);
            next[r.key] = it->second;
            loops_.erase(it);
        }
    }
    for (auto& [key, handle] : loops_)  // parts no longer running
        if (handle >= 0) mixer.release(handle);
    loops_ = std::move(next);
}

}  // namespace obf
