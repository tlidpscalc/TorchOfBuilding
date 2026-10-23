#pragma once

#include "tob/ModDB.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace tob {

// ---------------------------------------------------------------------------
// Game constants. Anything marked UNVERIFIED still needs confirming in-game.
// ---------------------------------------------------------------------------
namespace constants {
inline constexpr double kCritChancePerRating = 0.0001;   // 100 rating = 1% (confirmed)
inline constexpr double kMaxCritChance = 1.0;
// Crits deal 150% by default; "+X% Critical Strike Damage" adds onto this (confirmed).
inline constexpr double kBaseCritDamagePct = 50.0;
} // namespace constants

struct DamageRange {
    double min = 0.0;
    double max = 0.0;
    double avg() const { return (min + max) / 2.0; }
};

using DamageByType = std::array<DamageRange, kDamageTypeCount>;

struct Weapon {
    DamageByType damage{};          // the weapon's own damage, incl. its local flat mods
    double attacksPerSecond = 1.0;
    double critRating = 0.0;        // base crit rating of the weapon
};

// One damaging part of a skill, e.g. Ice Shot's "Icy Lances" and "Explosion".
struct HitComponent {
    std::string name;
    double weaponDamagePct = 0.0;           // "Deals 407% Weapon Attack Damage" -> 407
    DamageByType baseDamage{};              // spell base damage at the skill's level
    double addedDamageEffectivenessPct = 100.0;
    double hitsPerUse = 1.0;                // how many times this component hits per skill use
};

struct Conversion {
    DamageType from;
    DamageType to;
    double pct;                             // 0..100 of the `from` damage
};

struct SkillSpec {
    std::string name;
    TagMask tags = 0;
    std::vector<HitComponent> components;
    std::vector<Conversion> conversions;
    // Attacks use weapon speed. Non-attacks use the skill's base cast speed (casts/s, the
    // "Cast Speed" on tlidb skill cards; cast time = 1 / cast speed), scaled by cast speed mods.
    double baseCastSpeed = 1.0;
    double baseCritRating = 0.0;            // spells: skill base crit rating; attacks use the weapon
};

// Defaults are the in-game target dummy (as described by the project owner).
struct Enemy {
    int level = 85;
    std::array<double, kDamageTypeCount> resistancePct{0.0, 30.0, 30.0, 30.0, 30.0};  // Physical unused
    double armorMitigationPhysicalPct = 50.0;
    double armorMitigationNonPhysicalPct = 30.0;
};

// Lines explaining each calculated number, grouped by section, shown in the UI.
struct Breakdown {
    std::map<std::string, std::vector<std::string>> sections;
    void add(const std::string& section, std::string line) { sections[section].push_back(std::move(line)); }
};

struct ComponentResult {
    std::string name;
    std::array<double, kDamageTypeCount> avgByType{};   // after scaling and enemy mitigation, before crit
    double avgHit = 0.0;                                // non-crit average
    double avgHitWithCrit = 0.0;
    double damagePerUse = 0.0;                          // avgHitWithCrit * hitsPerUse
};

struct CalcResult {
    std::vector<ComponentResult> components;
    double critChance = 0.0;            // 0..1
    double critMultiplier = 1.0;        // e.g. 1.5
    double usesPerSecond = 0.0;
    double damagePerUse = 0.0;
    double hitDps = 0.0;
    Breakdown breakdown;
};

struct CalcInput {
    SkillSpec skill;
    Weapon weapon;                      // ignored for non-attacks
    Enemy enemy;
    const ModDB* mods = nullptr;
    const Config* config = nullptr;
};

// A slice of a hit's damage. After conversion, `history` holds every type the slice has been.
// Confirmed TLI rule: Increased mods only use the final type (`type`), while other mods
// (additional, buckets) still apply for any type in the history.
struct DamagePortion {
    DamageType type;
    DamageTypeMask history;
    DamageRange damage;
};

// Splits raw damage into portions after conversions (over-conversion is scaled down to 100%).
std::vector<DamagePortion> applyConversions(const DamageByType& raw, const std::vector<Conversion>& conversions);

// Hit DPS of a single skill. DoT, ailments, Spell Burst, Multistrike etc. come later.
CalcResult calculateHit(const CalcInput& in);

} // namespace tob
