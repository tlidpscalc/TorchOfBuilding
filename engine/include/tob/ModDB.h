#pragma once

#include "tob/Mod.h"

#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tob {

// User-controlled situational state, like PoB's Configuration tab.
struct Config {
    std::unordered_map<std::string, bool> flags;     // "frozen" -> true
    std::unordered_map<std::string, double> vars;    // "focus blessing" -> 8
    // "Stacks up to N times" mods use N when their variable is not set (sustained DPS assumption).
    bool assumeMaxStacks = true;

    bool flag(const std::string& name) const;
    std::optional<double> var(const std::string& name) const;
};

struct EvalContext;

// Supplies computed variables (e.g. "projectile speed" = total Projectile Speed bonus) that are
// not plain config values. Returns nullopt for names it does not know.
using VarResolver = std::function<std::optional<double>(const std::string&, const EvalContext&)>;

// What is being asked about when querying the database.
struct EvalContext {
    TagMask skillTags = 0;
    // Damage types this portion of damage counts as. Converted damage carries both its
    // original and final type here. 0 = not a damage-typed query (e.g. crit rating).
    DamageTypeMask damageTypes = 0;
    const Config* config = nullptr;
    std::string skillName;                  // matches Mod::skillScope
    const VarResolver* resolver = nullptr;
};

class ModDB {
public:
    void add(Mod mod);
    void add(const std::vector<Mod>& mods);
    void clear() { mods_.clear(); }

    const std::vector<Mod>& mods() const { return mods_; }

    // Sum of Base or Increased values over the listed stats.
    double sum(ModType type, std::initializer_list<std::string_view> stats, const EvalContext& ctx) const;

    // Combined multiplier from Additional and Bucket mods over the listed stats (1.0 = none).
    double more(std::initializer_list<std::string_view> stats, const EvalContext& ctx) const;

    bool flag(std::string_view stat, const EvalContext& ctx) const;

    const Mod* findOverride(std::string_view stat, const EvalContext& ctx) const;

    // Every applicable mod (with its effective value) for breakdown display.
    struct Contribution {
        const Mod* mod;
        double value;
    };
    std::vector<Contribution> contributions(ModType type, std::initializer_list<std::string_view> stats,
                                            const EvalContext& ctx) const;

    // Value after PerStat scaling and effect scaling. Assumes applies() already passed.
    double effectiveValue(const Mod& mod, const EvalContext& ctx) const;
    bool applies(const Mod& mod, const EvalContext& ctx) const;
    double varValue(const std::string& name, const EvalContext& ctx, const PerStat* per = nullptr) const;

private:
    double perSteps(const Mod& mod, const EvalContext& ctx) const;
    double effectScale(const Mod& mod, const EvalContext& ctx) const;

    std::vector<Mod> mods_;
};

} // namespace tob
