#include "audio.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace obf {

Wav wav_parse(const std::vector<uint8_t>& d) {
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) throw std::runtime_error("wav: not RIFF WAVE");
    Wav w;
    int bits = 0;
    bool fmt = false;
    for (size_t o = 12; o + 8 <= d.size();) {
        uint32_t len;
        std::memcpy(&len, d.data() + o + 4, 4);
        size_t body = o + 8;
        if (len > d.size() - body) throw std::runtime_error("wav: chunk past the end");
        if (!std::memcmp(d.data() + o, "fmt ", 4)) {
            if (len < 16) throw std::runtime_error("wav: short fmt");
            uint16_t format, ch, b;
            uint32_t rate;
            std::memcpy(&format, d.data() + body, 2), std::memcpy(&ch, d.data() + body + 2, 2);
            std::memcpy(&rate, d.data() + body + 4, 4), std::memcpy(&b, d.data() + body + 14, 2);
            if (format != 1 || (ch != 1 && ch != 2) || (b != 8 && b != 16) || rate < 1000 || rate > 192000) throw std::runtime_error("wav: unsupported format");
            w.channels = ch, w.rate = static_cast<int>(rate), bits = b, fmt = true;
        } else if (!std::memcmp(d.data() + o, "data", 4)) {
            if (!fmt) throw std::runtime_error("wav: data before fmt");
            if (bits == 16) {
                w.samples.resize(len / 2);
                std::memcpy(w.samples.data(), d.data() + body, w.samples.size() * 2);
            } else {
                w.samples.resize(len);
                for (size_t i = 0; i < len; ++i) w.samples[i] = static_cast<int16_t>((d[body + i] - 128) * 256);
            }
            w.samples.resize(w.samples.size() / static_cast<size_t>(w.channels) * static_cast<size_t>(w.channels));
            return w;
        }
        o = body + len + (len & 1);
    }
    throw std::runtime_error("wav: no data chunk");
}

float SoundEffect::eval(float x) const {
    if (shape == Linear) return params.size() >= 2 ? params[0] + params[1] * x : 1.0f;
    if (params.size() < 4) return 1.0f;
    // shortcut: ramps with more than four params (two segments?) use the first four.
    float x0 = params[0], x1 = params[1], y0 = params[2], dy = params[3];
    if (x1 == x0) return x < x0 ? y0 : y0 + dy;
    return y0 + dy * std::clamp((x - x0) / (x1 - x0), 0.0f, 1.0f);
}

namespace {

std::string lowered(std::string s) {
    for (char& c : s) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string dir_of(const std::string& p) {
    auto s = p.rfind('/');
    return s == std::string::npos ? "" : p.substr(0, s);
}

std::string join(const std::string& dir, const std::string& rel) {
    std::vector<std::string> parts;
    std::stringstream ss(dir.empty() ? rel : dir + "/" + rel);
    for (std::string part; std::getline(ss, part, '/');) {
        if (part.empty() || part == ".") continue;
        if (part == ".." && !parts.empty()) parts.pop_back();
        else if (part != "..") parts.push_back(part);
    }
    std::string out;
    for (auto& p : parts) out += (out.empty() ? "" : "/") + p;
    return out;
}

void parse_into(std::vector<SoundPatch>& patches, const std::string& path, const std::function<std::string(const std::string&)>& read,
                const std::string& level, int depth) {
    if (depth > 8) return;
    std::string text = read(path);
    for (size_t a; (a = text.find("/*")) != std::string::npos;) {  // block comments
        size_t b = text.find("*/", a + 2);
        text.erase(a, b == std::string::npos ? std::string::npos : b + 2 - a);
    }
    std::istringstream in(text);
    bool active = true;  // lines before any #templateLevel belong to every level
    SoundEffect* fx = nullptr;
    for (std::string line; std::getline(in, line);) {
        std::istringstream ls(line);
        std::string kw;
        if (!(ls >> kw)) continue;
        std::string k = lowered(kw);
        if (k == "#templatelevel") {
            std::string lv;
            ls >> lv;
            active = lowered(lv) == lowered(level);
            continue;
        }
        if (!active) continue;
        if (k == "#include") {
            std::string rel;
            ls >> rel;
            if (!rel.empty()) parse_into(patches, join(dir_of(path), lowered(rel)), read, level, depth + 1);
            fx = nullptr;  // the include may have moved the patches
            continue;
        }
        if (k[0] == '#' || k[0] == '*') continue;  // comments
        std::string arg;
        ls >> arg;
        float v = static_cast<float>(std::atof(arg.c_str()));
        if (!std::isfinite(v)) v = 0;
        if (k == "newpatch") {
            patches.emplace_back();
            fx = nullptr;
            continue;
        }
        if (patches.empty()) patches.emplace_back();  // layers before any newPatch
        SoundPatch& patch = patches.back();
        SoundLayer* layer = patch.layers.empty() ? nullptr : &patch.layers.back();
        if (k == "load") {
            // @ROOT/Sound/@RTD/x.wav -> sound/44khz/x.wav (@RTD: the sample-rate folder; 44 kHz chosen)
            std::string p = lowered(arg);
            if (p.rfind("@root/", 0) == 0) p = p.substr(6);
            else p = join(dir_of(path), p);
            for (size_t r; (r = p.find("@rtd")) != std::string::npos;) p.replace(r, 4, "44khz");
            patch.layers.push_back({p, false, false, false, false, false, 1, 1, {}});
            fx = nullptr;
        } else if (!layer) {
            continue;
        } else if (k == "loop") {
            layer->loop = true;
        } else if (k == "stop") {
            layer->finish = lowered(arg) == "finishsample";
        } else if (k == "stereo") {
            layer->stereo = true;
        } else if (k == "trigger") {
            layer->trigger = lowered(arg) == "volume", layer->on_release = lowered(arg) == "release";
        } else if (k == "volume") {
            layer->volume = v;
        } else if (k == "mindistance") {
            layer->min_distance = std::max(0.01f, v);
        } else if (k == "begineffect") {
            layer->effects.push_back({});
            fx = &layer->effects.back();
        } else if (k == "endeffect") {
            fx = nullptr;
        } else if (fx && k == "controldestination") {
            fx->dest = lowered(arg) == "pitch" ? SoundEffect::Pitch : SoundEffect::Volume;
        } else if (fx && k == "controlsource") {
            std::string s = lowered(arg);
            fx->source = s == "distance" ? SoundEffect::Distance : s == "time" ? SoundEffect::Time : s == "timerelease" ? SoundEffect::TimeRelease
                       : s == "default" ? SoundEffect::Control : s == "speed" ? SoundEffect::Speed
                       : s == "acceleration" ? SoundEffect::Acceleration : SoundEffect::Unknown;
        } else if (fx && k == "envelope") {
            fx->shape = lowered(arg) == "linear" ? SoundEffect::Linear : SoundEffect::Ramp;
        } else if (fx && k == "param" && !arg.empty()) {
            fx->params.push_back(v);
        }
    }
}

}  // namespace

std::vector<SoundPatch> ssc_patches(const std::string& path, const std::function<std::string(const std::string&)>& read,
                                    const std::string& level) {
    std::vector<SoundPatch> patches;
    parse_into(patches, join("", lowered(path)), read, level, 0);  // ".." from loadSoundScript paths resolved
    return patches;
}

SoundPatch ssc_parse(const std::string& path, const std::function<std::string(const std::string&)>& read, const std::string& level) {
    SoundPatch all;
    for (SoundPatch& p : ssc_patches(path, read, level)) all.layers.insert(all.layers.end(), p.layers.begin(), p.layers.end());
    return all;
}

int Mixer::play(const SoundPatch& patch, const std::vector<std::shared_ptr<const Wav>>& wavs, const float pos[3], float control) {
    std::lock_guard<std::mutex> g(m_);
    // A stalled device (WSLg's PulseAudio paused callbacks for seconds) stops one-shots from ending, and they pile up:
    // past the cap the oldest one-shots go; loops stay.
    if (voices_.size() >= kMaxVoices) {
        size_t drop = voices_.size() - kMaxVoices + patch.layers.size();
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [&](const Voice& v) { return !v.layer.loop && drop && drop--; }), voices_.end());
    }
    int h = next_++;
    for (size_t i = 0; i < patch.layers.size() && i < wavs.size(); ++i) {
        if (!wavs[i] || !wavs[i]->frames()) continue;
        Voice v{h, patch.layers[i], wavs[i], {pos[0], pos[1], pos[2]}, control, 0, -1, 0};
        voices_.push_back(std::move(v));
    }
    return h;
}

void Mixer::update(int handle, const float pos[3], float control, float speed, float accel) {
    std::lock_guard<std::mutex> g(m_);
    for (auto& v : voices_)
        if (v.handle == handle) std::copy(pos, pos + 3, v.pos), v.control = control, v.speed = speed, v.accel = accel;
}

void Mixer::release(int handle) {
    std::lock_guard<std::mutex> g(m_);
    for (auto& v : voices_) {
        if (v.handle != handle || v.release_time >= 0) continue;
        if (v.layer.finish) v.layer.loop = false;  // stop FinishSample: this pass to its end
        else v.release_time = v.time;
    }
}

void Mixer::listen(const Listener& l) {
    std::lock_guard<std::mutex> g(m_);
    listener_ = l;
}

void Mixer::mix(float* out, int frames) {
    Listener l;
    {
        std::lock_guard<std::mutex> g(m_);
        l = listener_;
    }
    mix(out, frames, l);
}

void Mixer::mix(float* out, int frames, const Listener& l) {
    std::lock_guard<std::mutex> g(m_);
    const float dt = static_cast<float>(frames) / static_cast<float>(rate_);
    for (auto& v : voices_) {
        float d[3] = {v.pos[0] - l.pos[0], v.pos[1] - l.pos[1], v.pos[2] - l.pos[2]};
        float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        float gain = std::min(v.layer.volume, 1.0f), pitch = 1;  // shortcut: volumes above 1 (first-person shots say 10) capped
        float timed = 1;  // the volume from time and control curves alone: what starts a trigger layer
        bool has_release = false;
        for (const SoundEffect& e : v.layer.effects) {
            float x = e.source == SoundEffect::Distance ? dist : e.source == SoundEffect::Time ? v.time
                    : e.source == SoundEffect::TimeRelease ? (v.release_time < 0 ? 0.0f : v.time - v.release_time)
                    : e.source == SoundEffect::Control ? v.control : e.source == SoundEffect::Speed ? v.speed
                    : e.source == SoundEffect::Acceleration ? v.accel : 0.0f;
            if (e.source == SoundEffect::Unknown) continue;
            has_release |= e.source == SoundEffect::TimeRelease;
            float y = e.eval(x);
            if (e.dest == SoundEffect::Volume) gain *= std::max(0.0f, y), timed *= e.source == SoundEffect::Distance ? 1.0f : std::max(0.0f, y);
            else pitch *= std::max(0.05f, y);
        }
        if (v.layer.loop && v.release_time >= 0 && (!has_release || v.time - v.release_time > 10)) v.release_time = -2;  // done
        float lg = 1, rg = 1;
        if (!v.layer.stereo) {  // DirectSound-style roll-off beyond minDistance, equal-power pan
            gain *= std::min(1.0f, v.layer.min_distance / std::max(dist, 1e-3f));
            float pan = dist > 1e-3f ? std::clamp((d[0] * l.right[0] + d[1] * l.right[1] + d[2] * l.right[2]) / dist, -1.0f, 1.0f) : 0.0f;
            lg = std::sqrt((1 - pan) / 2) * 1.41421356f, rg = std::sqrt((1 + pan) / 2) * 1.41421356f;
        }
        if ((v.layer.trigger || v.layer.on_release) && !v.started) {  // waits silent, without reading its sample, until its moment
            if (v.layer.on_release ? v.release_time < 0 : timed <= 1e-4f) {
                v.time += dt;
                continue;
            }
            v.started = true;
        }
        const Wav& w = *v.wav;
        double step = static_cast<double>(w.rate) / rate_ * pitch;
        size_t n = w.frames();
        for (int i = 0; i < frames && v.release_time != -2; ++i) {
            if (v.phase >= static_cast<double>(n)) {
                if (!v.layer.loop) {
                    v.release_time = -2;
                    break;
                }
                v.phase = std::fmod(v.phase, static_cast<double>(n));
            }
            if (gain > 1e-4f) {
                size_t i0 = static_cast<size_t>(v.phase), i1 = i0 + 1 < n ? i0 + 1 : (v.layer.loop ? 0 : i0);
                float f = static_cast<float>(v.phase - static_cast<double>(i0));
                auto at = [&](size_t idx, int ch) { return static_cast<float>(w.samples[idx * static_cast<size_t>(w.channels) + static_cast<size_t>(ch)]) / 32768.0f; };
                float left = at(i0, 0) * (1 - f) + at(i1, 0) * f;
                float right = w.channels == 2 ? at(i0, 1) * (1 - f) + at(i1, 1) * f : left;
                if (!v.layer.stereo) left = right = (left + right) * 0.5f;
                out[2 * i] += left * gain * lg;
                out[2 * i + 1] += right * gain * rg;
            }
            v.phase += step;
        }
        v.time += dt;
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.release_time == -2; }), voices_.end());
}

}  // namespace obf
