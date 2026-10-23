#pragma once

#include "tob/Build.h"
#include "tob/Calc.h"

#include <string>
#include <vector>

namespace tob {

namespace constants {
inline constexpr double kBaseSpellCritRating = 500.0;     // confirmed via the character sheet
inline constexpr double kMainStatDamagePerPoint = 0.5;    // % additional damage per main-stat point (confirmed)
inline constexpr double kLevelDamageTo30 = 1.10;          // per level 21..30 (confirmed)
inline constexpr double kLevelDamageAbove30 = 1.08;       // per level above 30 (confirmed)
inline constexpr double kBaseFrostbiteRating = 10.0;
inline constexpr double kFrostbiteRatingCap = 120.0;
inline constexpr double kFrozenAbove = 100.0;
inline constexpr double kBaseMaxBlessings = 4.0;
inline constexpr double kBaseMaxTangles = 2.0;
inline constexpr double kMaxTangleCap = 4.0;
inline constexpr double kDualWieldAttackBlock = 30.0;
inline constexpr int kUnfiredBoltHoldCap = 10;
} // namespace constants

struct DpsFactor {
    std::string name;
    double value;
};

// One part of a multi-part attack (e.g. Ice Shot's lances and explosion).
struct ComponentDps {
    std::string name;
    double dps = 0.0;
    bool included = true;   // config flag "exclude <name>" leaves it out of the total
};

// Damage over time from a damaging ailment (Ignite, Wilt, Trauma).
struct AilmentDps {
    std::string name;
    double dps = 0.0;
    double stacks = 0.0;        // average active stacks (Ignite/Trauma: uptime of their single stack)
    double baseDamage = 0.0;    // per stack per second, before scaling
};

struct SkillDpsResult {
    std::vector<ComponentDps> components;
    std::vector<AilmentDps> ailments;
    double hitDps = 0.0;        // dps = hitDps + ailment DPS + reapingDps + deteriorationDps
    double reapingDps = 0.0;
    double deteriorationDps = 0.0;
    double dps = 0.0;
    double averageHit = 0.0;        // one projectile, after every factor
    double castsPerSecond = 0.0;
    double hitsPerCast = 0.0;       // after shotgun falloff
    double tangles = 1.0;
    double critChance = 0.0;
    double critMultiplier = 1.0;
    double unfiredBolts = 0.0;
    double frostbiteRating = 0.0;
    std::vector<DpsFactor> factors; // every multiplier on the hit, for breakdowns and tests
    Breakdown breakdown;
};

// Hit DPS of the build's main skill against the configured enemy.
SkillDpsResult calculateMainSkillDps(const BuildState& build);

// Whether calculateMainSkillDps reads this stat. Parsed mods of other stats are recognised but not
// modelled yet (e.g. weapon-local stats until attack DPS exists); the coverage report shows them.
bool isModelledStat(std::string_view stat);

// x1.10 per level 21..30 and x1.08 per level above 30.
double skillLevelDamageMultiplier(int level);

} // namespace tob
