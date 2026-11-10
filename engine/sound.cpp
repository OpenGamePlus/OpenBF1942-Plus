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

std::string Sounds::script(const std::string& tmpl, const std::string& type) const {
    auto& ts = con_.templates.at("objecttemplate");
    std::function<std::string(const std::string&, int)> find = [&](const std::string& n, int depth) -> std::string {
        auto it = ts.find(low(n));
        if (it == ts.end() || depth > 6) return "";
        const ConObject& o = it->second;
        if (type.empty() || low(o.type) == low(type))
            for (auto& [k, a] : o.props)
                if (k == "loadsoundscript" && !a.empty()) return low(o.dir.empty() ? a[0] : o.dir + "/" + a[0]);
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


void Sounds::engine(const std::string& tmpl, const float pos[3], float control) {
    if (tmpl != engine_tmpl_) {
        if (engine_ >= 0) mixer.release(engine_);
        engine_tmpl_ = tmpl, engine_ = -1;
        if (!tmpl.empty()) {
            auto key = tmpl + "\n engine";
            auto it = scripts_.find(key);
            if (it == scripts_.end()) it = scripts_.emplace(key, script(tmpl, "Engine")).first;
            engine_ = play(it->second, pos, control);
        }
    } else if (engine_ >= 0) {
        mixer.update(engine_, pos, control);
    }
}

}  // namespace obf
