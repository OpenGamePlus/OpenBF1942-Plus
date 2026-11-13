// Audio: RIFF PCM .wav, .ssc sound scripts, and a software mixer (no SDL; the executable feeds a device).
// Spec: docs/formats/sound.md
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace obf {

struct Wav {
    int rate = 0, channels = 1;
    std::vector<int16_t> samples;  // interleaved
    size_t frames() const { return channels ? samples.size() / static_cast<size_t>(channels) : 0; }
};
Wav wav_parse(const std::vector<uint8_t>& data);  // PCM 8 / 16-bit, mono or stereo; throws std::runtime_error

// beginEffect ... endEffect: one control curve on a layer.
struct SoundEffect {
    enum Dest { Volume, Pitch } dest = Volume;
    enum Source { Distance, Time, TimeRelease, Control, Speed, Acceleration, Unknown } source = Unknown;  // Control: "Default"
    enum Shape { Ramp, Linear } shape = Ramp;
    std::vector<float> params;
    float eval(float x) const;  // Ramp: x0 x1 y0 dy -> y0 + dy * clamp((x - x0) / (x1 - x0)); Linear: a + b * x
};

// One "load" in a patch.
struct SoundLayer {
    std::string wav;  // VFS path, @ROOT / @RTD resolved (sound/44khz/...)
    bool loop = false, stereo = false;  // stereo: not positioned (first-person sounds)
    bool finish = false;                // stop FinishSample: a released loop plays its pass to the end
    bool trigger = false;               // trigger Volume: starts when its time / control volume first rises above 0
    float volume = 1, min_distance = 1;
    std::vector<SoundEffect> effects;
};

struct SoundPatch {
    std::vector<SoundLayer> layers;
};

// Parses one template level (HIGH / MEDIUM / LOW) of a script; read(path) returns a file's text or "" if missing.
// #include paths are relative to the including file.
SoundPatch ssc_parse(const std::string& path, const std::function<std::string(const std::string&)>& read,
                     const std::string& level = "HIGH");  // some scripts ship only HIGH (Elco torpedoes)
// The same, one patch per newPatch. Hand weapons: 0 single shot, 1 reload, 2 distant shot, 5 automatic fire (loop).
std::vector<SoundPatch> ssc_patches(const std::string& path, const std::function<std::string(const std::string&)>& read,
                                    const std::string& level = "HIGH");

struct Listener {
    float pos[3] = {}, right[3] = {1, 0, 0};
};

// Mixes playing patches into interleaved stereo float. Thread-safe: the game thread adds and moves voices while the
// audio thread mixes; the lock covers only the voice list, so callers load files and parse scripts outside it.
class Mixer {
public:
    explicit Mixer(int rate) : rate_(rate) {}
    // wavs: one per layer (null skips the layer). Returns a handle for update / release.
    int play(const SoundPatch& patch, const std::vector<std::shared_ptr<const Wav>>& wavs, const float pos[3], float control = 0);
    void update(int handle, const float pos[3], float control, float speed = 0, float accel = 0);
    void release(int handle);  // loops fade by their TimeRelease effects (or stop), one-shots finish
    void mix(float* out, int frames, const Listener& l);  // adds into out (2 * frames)
    void listen(const Listener& l);                       // the listener mix(out, frames) uses
    void mix(float* out, int frames);
    size_t voices() const {
        std::lock_guard<std::mutex> g(m_);
        return voices_.size();
    }

private:
    struct Voice {
        int handle;
        SoundLayer layer;
        std::shared_ptr<const Wav> wav;
        float pos[3], control = 0, time = 0, release_time = -1, speed = 0, accel = 0;
        double phase = 0;
        bool started = false;  // a trigger layer that has begun
    };
    static constexpr size_t kMaxVoices = 256;
    int rate_, next_ = 1;
    std::vector<Voice> voices_;
    Listener listener_;
    mutable std::mutex m_;
};

}  // namespace obf
