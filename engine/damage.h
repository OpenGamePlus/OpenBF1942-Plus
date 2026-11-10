// Damage table from MaterialManager commands (bf1942/game/materialmanagerdefine.con, materialmanagersettings.con and the
// damage_system/ files it runs). Spec: docs/formats/weapons.md
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace obf {

struct Material {
    int att = 0, def = 0;  // materialAttGroup / materialDefGroup
    float damage = 0;      // materialDamage
};

struct DamageTable {
    std::map<int, Material> materials;
    std::map<std::pair<int, int>, float> mods;  // (attGroup, defGroup) -> damageMod
    // materialDamage(attacker) x damageMod(attacker's attGroup, target's defGroup); 0 if either is unknown.
    float damage(int attacker, int target) const;
};

// commands: (method, args) in order, as Con::settings["materialmanager"].
DamageTable damage_table(const std::vector<std::pair<std::string, std::vector<std::string>>>& commands);

}  // namespace obf
