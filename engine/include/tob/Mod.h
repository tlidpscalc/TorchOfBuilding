#pragma once

#include "tob/Tags.h"

#include <optional>
#include <string>
#include <vector>

namespace tob {

// How a modifier combines with others of the same stat.
//
// Torchlight Infinite rules (confirmed by the project owner):
//   Base       - flat values, summed.
//   Increased  - all "increased" (written as "+X% Foo") summed into one bucket: (1 + sum/100).
//   Additional - every "+X% additional Foo" is its own multiplier: product of (1 + v/100).
//                Exception: mods sharing a non-empty `group` add together first and then
//                multiply as one (used for same-item stacking without "(multiplies)").
//   Bucket     - named special buckets such as "Tangle damage enhancement": summed within the
//                bucket named by `group`, then each bucket multiplies: product of (1 + sum/100).
//   Override   - replaces the computed value (first matching wins).
//   Flag       - boolean switch; value != 0 means set.
enum class ModType : std::uint8_t { Base, Increased, Additional, Bucket, Override, Flag };

std::string_view toString(ModType t);

// Value scales with a variable: value * floor(var / per), optionally capped at `limit`
// (the cap applies to the scaled result, matching "up to +X%" wording).
// Confirmed TLI rule: the steps of one mod add together into a single bonus. Only when the line
// says "(multiplies)" does each step compound, e.g. Double Rainbow's
// "+17% additional damage for every 2 types ... (multiplies)" -> (1.17)^steps.
struct PerStat {
    std::string var;            // canonical variable name, e.g. "focus blessing", "projectile speed"
    double per = 1.0;
    std::optional<double> limit;
    bool multiplies = false;    // compound per step (Additional mods only)
    // "Stacks up to N times": when the variable is not set, the config may assume full stacks.
    std::optional<double> maxStacks;
};

// A mod only applies while its conditions hold. Either a named flag (set or, when negated, not
// set) or a variable threshold ("when having at least 6 unfired Frost Bolts").
struct Condition {
    std::string flag;           // canonical flag name, e.g. "frozen", "cursed", "spell aggression"
    bool negate = false;
    std::string var;            // when non-empty: condition is var >= atLeast
    double atLeast = 0.0;
};

struct Mod {
    std::string stat;           // e.g. "Damage", "HitDamage", "CritRating", "Penetration"
    ModType type = ModType::Base;
    double value = 0.0;

    TagMask requiredTags = 0;           // skill must have ALL of these tags
    DamageTypeMask damageTypes = 0;     // 0 = not damage-type specific
    std::string group;                  // see ModType comment

    std::vector<Condition> conditions;
    std::optional<PerStat> per;

    // Only applies when evaluating this skill (supports, a skill's own lines). Empty = global.
    std::string skillScope;
    // The value is scaled by (1 + Increased(effectStat)/100), evaluated in `effectScope`'s skill
    // context: curse values by CurseEffect, auras by AuraEffect, buffs by their own Effect stat.
    std::string effectStat;
    std::string effectScope;

    std::string source;                 // human readable origin, e.g. "Talent: Elimination"
};

} // namespace tob
