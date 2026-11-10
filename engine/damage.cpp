#include "damage.h"

#include <cmath>
#include <cstdlib>

namespace obf {

float DamageTable::damage(int attacker, int target) const {
    auto a = materials.find(attacker), t = materials.find(target);
    if (a == materials.end() || t == materials.end()) return 0;
    auto m = mods.find({a->second.att, t->second.def});
    return m == mods.end() ? 0 : a->second.damage * m->second;
}

DamageTable damage_table(const std::vector<std::pair<std::string, std::vector<std::string>>>& commands) {
    DamageTable t;
    Material* cur = nullptr;
    int att = 0, def = 0;
    for (auto& [method, args] : commands) {
        if (args.empty()) continue;
        float v = static_cast<float>(std::atof(args[0].c_str()));  // atof: retail has values like "0.1.0"
        if (!std::isfinite(v)) continue;
        int i = std::atoi(args[0].c_str());
        if (method == "material") cur = &(t.materials[i] = Material{i, i, 0});
        else if (method == "materialattgroup" && cur) cur->att = i;
        else if (method == "materialdefgroup" && cur) cur->def = i;
        else if (method == "materialdamage" && cur) cur->damage = v;
        else if (method == "attgroup") att = i;
        else if (method == "defgroup") def = i;
        else if (method == "damagemod") t.mods[{att, def}] = v;
    }
    return t;
}

}  // namespace obf
