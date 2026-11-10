// Conquest rules: flags (ControlPoint) changing hands, tickets. Spec: docs/formats/weapons.md "Conquest"
#pragma once
#include <array>
#include <string>
#include <vector>
#include "con.h"

namespace obf {

struct ConquestFlag {
    std::string name;      // setControlPointName
    float pos[3] = {};
    float radius = 10, to_get = 10, to_lose = 10;  // radius, timeToGetControl, timeToLoseControl (s)
    int team = 0, group = -1;                      // owner (0 nobody), spawnGroupId
    bool locked = false;                           // unableToChangeTeam (main bases)
    float progress = 0;                            // towards neutral (owned) or towards the attacker (neutral)
    int attacker = 0;
};

struct Conquest {
    std::vector<ConquestFlag> flags;
    std::array<float, 3> tickets{0, 100, 100}, bleed{0, 0, 0};  // [team]; bleed: tickets lost per minute
    int winner = 0;

    // present[i][team]: living soldiers of each team inside flag i's radius. Returns what changed, for display.
    std::vector<std::string> update(float dt, const std::vector<std::array<int, 3>>& present);
    void death(int team);  // a soldier of `team` died: one ticket
    int owned(int team) const;
};

// Flags: ControlPoint instances of the level; tickets: game.setNumberOfTickets / setTicketLostPerMin.
Conquest conquest_from(const Con& con);

}  // namespace obf
