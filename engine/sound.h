// Game sounds: finds a template's sound script (loadSoundScript), loads its .wav files once, plays them on a Mixer.
// Spec: docs/formats/sound.md
#pragma once
#include <map>
#include <memory>
#include <string>

#include "audio.h"
#include "con.h"
#include "vfs.h"

namespace obf {

class Sounds {
public:
    Sounds(const Vfs& vfs, const Con& con, int rate) : mixer(rate), vfs_(vfs), con_(con) {}
    // A FireArms template fired at `pos` by `source` (any id stable per shooter) at time `now` (s). Single shots play
    // patch 0 and the distant patch 2; automatic weapons (a looping patch 5) loop it while their shooter keeps firing.
    void shot(const std::string& tmpl, const float pos[3], int source, float now);
    void reload(const std::string& tmpl, const float pos[3]);  // patch 1
    void tick(float now);  // ends automatic fire that stopped (no shot for 0.25 s)
    // The looping Engine script under a vehicle template; tmpl "" stops it. control: 0 idle .. 1 full.
    void engine(const std::string& tmpl, const float pos[3], float control);
    // Script path of a template, or of its first child (addTemplate, depth-first) of the given type; "" if none.
    std::string script(const std::string& tmpl, const std::string& type = "") const;

    Mixer mixer;

private:
    const SoundPatch* patch(const std::string& path);
    const SoundPatch* weapon_patch(const std::string& tmpl, size_t index);
    int play_patch(const SoundPatch& p, const float pos[3], float control, bool once);
    int play(const std::string& path, const float pos[3], float control);

    const Vfs& vfs_;
    const Con& con_;
    std::map<std::string, std::unique_ptr<SoundPatch>> patches_;  // by script path; null: none
    std::map<std::string, std::shared_ptr<const Wav>> wavs_;      // by path; null: missing or bad
    std::map<std::string, std::string> scripts_;                  // template (+ type) -> script
    std::map<std::string, std::vector<SoundPatch>> weapon_;  // by template: its script's patches
    struct Burst {
        int handle;
        float last;
    };
    std::map<int, Burst> bursts_;  // automatic fire by source
    std::string engine_tmpl_;
    int engine_ = -1;
};

}  // namespace obf
