#include "tob/ModDB.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace tob {

std::string_view toString(ModType t) {
    switch (t) {
        case ModType::Base:       return "Base";
        case ModType::Increased:  return "Increased";
        case ModType::Additional: return "Additional";
        case ModType::Bucket:     return "Bucket";
        case ModType::Override:   return "Override";
        case ModType::Flag:       return "Flag";
    }
    return "?";
}

bool Config::flag(const std::string& name) const {
    auto it = flags.find(name);
    return it != flags.end() && it->second;
}

std::optional<double> Config::var(const std::string& name) const {
    auto it = vars.find(name);
    if (it == vars.end()) return std::nullopt;
    return it->second;
}

void ModDB::add(Mod mod) {
    mods_.push_back(std::move(mod));
}

void ModDB::add(const std::vector<Mod>& mods) {
    mods_.insert(mods_.end(), mods.begin(), mods.end());
}

namespace {

bool statMatches(const Mod& mod, std::initializer_list<std::string_view> stats) {
    return std::any_of(stats.begin(), stats.end(), [&](std::string_view s) { return mod.stat == s; });
}

} // namespace

double ModDB::varValue(const std::string& name, const EvalContext& ctx, const PerStat* per) const {
    if (ctx.resolver) {
        if (auto v = (*ctx.resolver)(name, ctx)) return *v;
    }
    if (ctx.config) {
        if (auto v = ctx.config->var(name)) return *v;
    }
    if (per && per->maxStacks && (!ctx.config || ctx.config->assumeMaxStacks)) return *per->maxStacks * per->per;
    return 0.0;
}

bool ModDB::applies(const Mod& mod, const EvalContext& ctx) const {
    if (!mod.skillScope.empty() && mod.skillScope != ctx.skillName) return false;
    if ((mod.requiredTags & ctx.skillTags) != mod.requiredTags) return false;
    if (mod.damageTypes != 0 && (mod.damageTypes & ctx.damageTypes) == 0) return false;

    for (const Condition& c : mod.conditions) {
        bool holds = c.var.empty() ? (ctx.config && ctx.config->flag(c.flag)) : varValue(c.var, ctx) >= c.atLeast;
        if (holds == c.negate) return false;
    }
    return true;
}

double ModDB::perSteps(const Mod& mod, const EvalContext& ctx) const {
    const PerStat& p = *mod.per;
    double var = varValue(p.var, ctx, &p);
    return p.per > 0 ? std::floor(var / p.per + 1e-9) : 0.0;
}

double ModDB::effectiveValue(const Mod& mod, const EvalContext& ctx) const {
    double v = mod.value;
    if (mod.per) {
        v *= perSteps(mod, ctx);
        if (mod.per->limit) v = mod.value >= 0 ? std::min(v, *mod.per->limit) : std::max(v, -*mod.per->limit);
    }
    return v * effectScale(mod, ctx);
}

double ModDB::effectScale(const Mod& mod, const EvalContext& ctx) const {
    if (mod.effectStat.empty()) return 1.0;
    EvalContext ectx = ctx;
    if (!mod.effectScope.empty()) ectx.skillName = mod.effectScope;
    ectx.damageTypes = 0;
    return 1.0 + sum(ModType::Increased, {mod.effectStat}, ectx) / 100.0;
}

double ModDB::sum(ModType type, std::initializer_list<std::string_view> stats, const EvalContext& ctx) const {
    double total = 0.0;
    for (const Mod& m : mods_) {
        if (m.type == type && statMatches(m, stats) && applies(m, ctx)) total += effectiveValue(m, ctx);
    }
    return total;
}

double ModDB::more(std::initializer_list<std::string_view> stats, const EvalContext& ctx) const {
    double product = 1.0;
    std::map<std::string, double> additionalGroups;
    std::map<std::string, double> buckets;

    for (const Mod& m : mods_) {
        if ((m.type != ModType::Additional && m.type != ModType::Bucket) || !statMatches(m, stats) ||
            !applies(m, ctx))
            continue;

        if (m.type == ModType::Additional && m.per && m.per->multiplies) {
            double step = 1.0 + m.value * effectScale(m, ctx) / 100.0;
            product *= std::pow(step, perSteps(m, ctx));
            continue;
        }

        double v = effectiveValue(m, ctx);
        if (m.type == ModType::Bucket) {
            buckets[m.group] += v;
        } else if (!m.group.empty()) {
            additionalGroups[m.group] += v;
        } else {
            product *= 1.0 + v / 100.0;
        }
    }
    for (const auto& [_, v] : additionalGroups) product *= 1.0 + v / 100.0;
    for (const auto& [_, v] : buckets) product *= 1.0 + v / 100.0;
    return product;
}

bool ModDB::flag(std::string_view stat, const EvalContext& ctx) const {
    return std::any_of(mods_.begin(), mods_.end(), [&](const Mod& m) {
        return m.type == ModType::Flag && m.stat == stat && m.value != 0.0 && applies(m, ctx);
    });
}

const Mod* ModDB::findOverride(std::string_view stat, const EvalContext& ctx) const {
    for (const Mod& m : mods_) {
        if (m.type == ModType::Override && m.stat == stat && applies(m, ctx)) return &m;
    }
    return nullptr;
}

std::vector<ModDB::Contribution> ModDB::contributions(ModType type, std::initializer_list<std::string_view> stats,
                                                      const EvalContext& ctx) const {
    std::vector<Contribution> out;
    for (const Mod& m : mods_) {
        if (m.type == type && statMatches(m, stats) && applies(m, ctx)) out.push_back({&m, effectiveValue(m, ctx)});
    }
    return out;
}

} // namespace tob
