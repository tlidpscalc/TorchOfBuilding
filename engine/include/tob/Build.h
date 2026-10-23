#pragma once

#include "tob/Calc.h"
#include "tob/GameData.h"
#include "tob/ModDB.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace tob {

// One modifier line from the build and what the parser made of it (feeds the coverage report).
struct LineReport {
    std::string source;
    std::string line;
    bool ok = false;
    bool inert = false;
    std::string error;
};

// The damaging skill being calculated, with the facts read from its tlidb card.
struct MainSkill {
    std::string name;
    TagMask tags = 0;
    int level = 20;
    DamageType damageType = DamageType::Physical;
    double baseMin = 0.0;           // level-20 base damage; higher levels use the per-level multiplier
    double baseMax = 0.0;
    double baseCastSpeed = 1.0;
    double addedDamageEffectiveness = 1.0;  // multiplies flat "Adds X - Y Damage" (level-20 card value)
    std::vector<std::string> mainStats;
    bool clockworkCore = false;     // supported by Clockwork Core: hits are multiplied by Tangle count

    // Attacks: one entry per hit component ("Icy Lances: Deals 407% Weapon Attack Damage"), plus conversions.
    struct AttackComponent {
        std::string name;           // lowercase, e.g. "icy lances", "explosion"
        double weaponDamagePct = 0.0;
    };
    std::vector<AttackComponent> attackComponents;
    std::vector<Conversion> conversions;

    // Homing Frost Bolt style multi-projectile casts (0 = not used).
    double extraProjectilePer = 0.0;    // +1 projectile per this much Projectile Quantity
    int extraProjectileCap = 0;
    double shotgunFalloffPct = 0.0;     // later projectiles hitting the same target deal (100 - this)%
    double condensationTime = 0.0;      // seconds; > 0 enables the unfired-bolt model
    double condensationCastSpeedEfficiency = 0.0;
};

// A weapon after its own (local) modifiers, per the confirmed rules:
//   damage  = (base + "Adds ... to the gear") x (1 + "% Gear <type> Damage") per damage type
//   speed   = base attack speed x (1 + "% gear Attack Speed")
//   crit    = base crit rating x (1 + "% Critical Strike Rating for this gear")
// Global modifiers apply on top of these in the DPS calculation.
struct WeaponStats {
    std::string slot;
    DamageByType damage{};
    double attackSpeed = 0.0;
    double critRating = 0.0;
};

struct BuildState {
    ModDB mods;
    Config config;
    MainSkill main;
    std::vector<WeaponStats> weapons;        // main hand first
    std::vector<std::string> activeSkills;   // names, including the main skill
    std::vector<std::string> externalAilments;
    double enemyResistance = 30.0;
    double enemyArmorPhysical = 50.0;
    double enemyArmorNonPhysical = 30.0;
    std::vector<LineReport> lines;
};

BuildState loadBuild(const GameData& data, const nlohmann::json& build);
BuildState loadBuildFile(const GameData& data, const std::string& path);

} // namespace tob
