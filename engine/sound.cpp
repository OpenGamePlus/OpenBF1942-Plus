#include "sound.h"

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

int Sounds::play(const std::string& path, const float pos[3], float control) {
    const SoundPatch* p = path.empty() ? nullptr : patch(path);
    if (!p) return -1;
    std::vector<std::shared_ptr<const Wav>> w;
    for (auto& l : p->layers) w.push_back(wavs_[l.wav]);
    return mixer.play(*p, w, pos, control);
}

void Sounds::shot(const std::string& tmpl, const float pos[3]) {
    auto it = scripts_.find(tmpl);
    if (it == scripts_.end()) it = scripts_.emplace(tmpl, script(tmpl)).first;
    play(it->second, pos, 0);
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
