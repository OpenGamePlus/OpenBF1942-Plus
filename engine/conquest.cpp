#include "conquest.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace obf {

int Conquest::owned(int team) const {
    return static_cast<int>(std::count_if(flags.begin(), flags.end(), [&](const ConquestFlag& f) { return f.team == team; }));
}

void Conquest::death(int team) {
    if (team >= 1 && team <= 2 && !winner) tickets[static_cast<size_t>(team)] = std::max(0.0f, tickets[static_cast<size_t>(team)] - 1);
}

// shortcut: the retail rules are not documented; these follow the template fields (unverified):
// one team alone in the radius first neutralises an enemy flag (timeToLoseControl), then takes a neutral one
// (timeToGetControl); the owner alone undoes progress; both teams present freeze it. A team holding fewer flags than the
// other loses its setTicketLostPerMin; a team at 0 tickets loses.
std::vector<std::string> Conquest::update(float dt, const std::vector<std::array<int, 3>>& present) {
    std::vector<std::string> events;
    if (winner || !(dt > 0)) return events;
    for (size_t i = 0; i < flags.size() && i < present.size(); ++i) {
        ConquestFlag& f = flags[i];
        int t1 = present[i][1], t2 = present[i][2];
        if (f.locked || (t1 > 0 && t2 > 0)) continue;
        int alone = t1 > 0 ? 1 : t2 > 0 ? 2 : 0;
        if (!alone) continue;
        if (alone == f.team) {
            f.progress = std::max(0.0f, f.progress - dt / std::max(f.to_lose, 0.1f)), f.attacker = 0;
            continue;
        }
        if (f.attacker != alone) f.attacker = alone, f.progress = 0;
        f.progress += dt / std::max(f.team ? f.to_lose : f.to_get, 0.1f);
        if (f.progress < 1) continue;
        f.progress = 0;
        if (f.team) {
            events.push_back(f.name + " neutralised by team " + std::to_string(alone));
            f.team = 0;
        } else {
            events.push_back(f.name + " captured by team " + std::to_string(alone));
            f.team = alone, f.attacker = 0;
        }
    }
    int o1 = owned(1), o2 = owned(2);
    if (o1 < o2) tickets[1] = std::max(0.0f, tickets[1] - bleed[1] * dt / 60);
    if (o2 < o1) tickets[2] = std::max(0.0f, tickets[2] - bleed[2] * dt / 60);
    if (tickets[1] <= 0 || tickets[2] <= 0) {
        winner = tickets[1] <= 0 ? 2 : 1;
        events.push_back("team " + std::to_string(winner) + " wins");
    }
    return events;
}

Conquest conquest_from(const Con& con) {
    Conquest q;
    auto ot = con.templates.find("objecttemplate");
    for (auto& inst : con.instances) {
        if (inst.deleted || ot == con.templates.end()) continue;
        auto t = ot->second.find(lower(inst.name));
        if (t == ot->second.end() || lower(t->second.type) != "controlpoint") continue;
        ConquestFlag f;
        f.name = inst.name;
        auto num = [](const std::vector<std::string>& a) {
            float v = static_cast<float>(std::atof(a[0].c_str()));
            return std::isfinite(v) ? v : 0.0f;
        };
        for (auto& [k, a] : t->second.props) {
            if (a.empty()) continue;
            if (k == "setcontrolpointname") f.name = a[0];
            else if (k == "radius") f.radius = std::max(1.0f, num(a));
            else if (k == "team") f.team = std::clamp(std::atoi(a[0].c_str()), 0, 2);
            else if (k == "spawngroupid") f.group = std::atoi(a[0].c_str());
            else if (k == "timetogetcontrol") f.to_get = num(a);
            else if (k == "timetolosecontrol") f.to_lose = num(a);
            else if (k == "unabletochangeteam") f.locked = num(a) != 0;
        }
        for (auto& [k, a] : inst.props)
            if (k == "absoluteposition" && !a.empty()) {
                std::string s = a[0];
                for (int i = 0; i < 3; ++i) {
                    f.pos[i] = static_cast<float>(std::atof(s.c_str()));
                    auto slash = s.find('/');
                    s = slash == std::string::npos ? "" : s.substr(slash + 1);
                }
            }
        q.flags.push_back(f);
    }
    auto g = con.settings.find("game");
    if (g != con.settings.end())
        for (auto& [m, a] : g->second) {
            if (a.size() < 2) continue;
            int team = std::atoi(a[0].c_str());
            float v = static_cast<float>(std::atof(a[1].c_str()));
            if (team < 1 || team > 2 || !std::isfinite(v)) continue;
            if (m == "setnumberoftickets") q.tickets[static_cast<size_t>(team)] = v;
            if (m == "setticketlostpermin") q.bleed[static_cast<size_t>(team)] = v;
        }
    return q;
}

}  // namespace obf
