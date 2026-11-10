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
    void shot(const std::string& tmpl, const float pos[3]);  // one-shot script of a FireArms template
    // The looping Engine script under a vehicle template; tmpl "" stops it. control: 0 idle .. 1 full.
    void engine(const std::string& tmpl, const float pos[3], float control);
    // Script path of a template, or of its first child (addTemplate, depth-first) of the given type; "" if none.
    std::string script(const std::string& tmpl, const std::string& type = "") const;

    Mixer mixer;

private:
    const SoundPatch* patch(const std::string& path);
    int play(const std::string& path, const float pos[3], float control);

    const Vfs& vfs_;
    const Con& con_;
    std::map<std::string, std::unique_ptr<SoundPatch>> patches_;  // by script path; null: none
    std::map<std::string, std::shared_ptr<const Wav>> wavs_;      // by path; null: missing or bad
    std::map<std::string, std::string> scripts_;                  // template (+ type) -> script
    std::string engine_tmpl_;
    int engine_ = -1;
};

}  // namespace obf
