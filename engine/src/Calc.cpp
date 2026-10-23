#include "tob/Calc.h"

#include <algorithm>
#include <format>

namespace tob {

// A slice of a component's damage. After conversion, `history` holds every type the slice has
// been. Confirmed TLI rule: Increased mods only use the final type (`type`), while other mods
// (additional, buckets) still apply for any type in the history.
std::vector<DamagePortion> applyConversions(const DamageByType& raw, const std::vector<Conversion>& conversions) {
    std::vector<DamagePortion> portions;
    for (DamageType t : kAllDamageTypeList) {
        if (raw[index(t)].max > 0.0) portions.push_back({t, bit(t), raw[index(t)]});
    }

    for (DamageType from : kAllDamageTypeList) {
        double total = 0.0;
        for (const Conversion& c : conversions) {
            if (c.from == from) total += c.pct;
        }
        if (total <= 0.0) continue;
        // Over-conversion is scaled down to 100%, as in most ARPGs.
        double scale = total > 100.0 ? 100.0 / total : 1.0;

        std::vector<DamagePortion> added;
        for (DamagePortion& p : portions) {
            if (p.type != from) continue;
            for (const Conversion& c : conversions) {
                if (c.from != from) continue;
                double f = c.pct * scale / 100.0;
                added.push_back({c.to, static_cast<DamageTypeMask>(p.history | bit(c.to)),
                                 {p.damage.min * f, p.damage.max * f}});
            }
            double keep = 1.0 - std::min(total, 100.0) / 100.0;
            p.damage.min *= keep;
            p.damage.max *= keep;
        }
        portions.insert(portions.end(), added.begin(), added.end());
    }

    std::erase_if(portions, [](const DamagePortion& p) { return p.damage.max <= 0.0; });
    return portions;
}

CalcResult calculateHit(const CalcInput& in) {
    static const ModDB kEmptyDb;
    const ModDB& db = in.mods ? *in.mods : kEmptyDb;
    const SkillSpec& skill = in.skill;
    const bool isAttack = (skill.tags & bit(SkillTag::Attack)) != 0;

    CalcResult r;
    const EvalContext skillCtx{skill.tags, 0, in.config};

    // --- Crit -------------------------------------------------------------
    {
        double baseRating = (isAttack ? in.weapon.critRating : skill.baseCritRating) +
                            db.sum(ModType::Base, {"CritRating"}, skillCtx);
        double inc = db.sum(ModType::Increased, {"CritRating"}, skillCtx);
        double more = db.more({"CritRating"}, skillCtx);
        double rating = baseRating * (1.0 + inc / 100.0) * more;
        r.critChance = std::clamp(rating * constants::kCritChancePerRating, 0.0, constants::kMaxCritChance);

        double critBonusPct = constants::kBaseCritDamagePct + db.sum(ModType::Base, {"CritDamage"}, skillCtx);
        r.critMultiplier = 1.0 + critBonusPct / 100.0;

        r.breakdown.add("Crit", std::format("Crit rating: {:.0f} base x {:.2f} inc x {:.3f} additional = {:.0f}",
                                            baseRating, 1.0 + inc / 100.0, more, rating));
        r.breakdown.add("Crit", std::format("Crit chance: {:.2f}%", r.critChance * 100.0));
        r.breakdown.add("Crit", std::format("Crit damage multiplier: {:.1f}%", r.critMultiplier * 100.0));
    }

    // --- Speed ------------------------------------------------------------
    {
        const std::string_view speedStat = isAttack ? "AttackSpeed" : "CastSpeed";
        double base = isAttack ? in.weapon.attacksPerSecond : skill.baseCastSpeed;
        double inc = db.sum(ModType::Increased, {speedStat, "Speed"}, skillCtx);
        double more = db.more({speedStat, "Speed"}, skillCtx);
        r.usesPerSecond = base * (1.0 + inc / 100.0) * more;
        r.breakdown.add("Speed", std::format("{}: {:.3f} base x {:.2f} inc x {:.3f} additional = {:.3f}/s",
                                             speedStat, base, 1.0 + inc / 100.0, more, r.usesPerSecond));
    }

    // --- Hit components ---------------------------------------------------
    for (const HitComponent& comp : skill.components) {
        ComponentResult cr;
        cr.name = comp.name;
        const std::string section = "Hit: " + comp.name;

        DamageByType raw{};
        const double weaponMult = isAttack ? comp.weaponDamagePct / 100.0 : 0.0;
        const double ade = comp.addedDamageEffectivenessPct / 100.0;
        for (DamageType t : kAllDamageTypeList) {
            EvalContext tctx = skillCtx;
            tctx.damageTypes = bit(t);
            double addMin = db.sum(ModType::Base, {"AddedDamageMin"}, tctx);
            double addMax = db.sum(ModType::Base, {"AddedDamageMax"}, tctx);
            DamageRange& d = raw[index(t)];
            d.min = in.weapon.damage[index(t)].min * weaponMult + comp.baseDamage[index(t)].min + addMin * ade;
            d.max = in.weapon.damage[index(t)].max * weaponMult + comp.baseDamage[index(t)].max + addMax * ade;
            if (d.max > 0.0) {
                r.breakdown.add(section, std::format("{} base: {:.1f}-{:.1f} (weapon x {:.2f}, added x {:.2f})",
                                                     toString(t), d.min, d.max, weaponMult, ade));
            }
        }

        for (const DamagePortion& p : applyConversions(raw, skill.conversions)) {
            EvalContext finalCtx = skillCtx;
            finalCtx.damageTypes = bit(p.type);
            EvalContext historyCtx = skillCtx;
            historyCtx.damageTypes = p.history;
            double inc = db.sum(ModType::Increased, {"Damage", "HitDamage"}, finalCtx);
            double more = db.more({"Damage", "HitDamage"}, historyCtx);
            double scaled = p.damage.avg() * (1.0 + inc / 100.0) * more;

            // Enemy mitigation. Penetration can push values below zero (more damage taken).
            double resist = 0.0;
            if (p.type != DamageType::Physical) {
                resist = in.enemy.resistancePct[index(p.type)] - db.sum(ModType::Base, {"Penetration"}, finalCtx);
            }
            double armorMit = (p.type == DamageType::Physical ? in.enemy.armorMitigationPhysicalPct
                                                              : in.enemy.armorMitigationNonPhysicalPct) -
                              db.sum(ModType::Base, {"ArmorPenetration"}, finalCtx);
            double dealt = scaled * (1.0 - resist / 100.0) * (1.0 - armorMit / 100.0);
            cr.avgByType[index(p.type)] += dealt;

            r.breakdown.add(section,
                            std::format("{} avg {:.1f} x {:.2f} inc x {:.3f} additional x {:.2f} (res {:.0f}%) "
                                        "x {:.2f} (armor {:.0f}%) = {:.1f}",
                                        toString(p.type), p.damage.avg(), 1.0 + inc / 100.0, more,
                                        1.0 - resist / 100.0, resist, 1.0 - armorMit / 100.0, armorMit, dealt));
        }

        for (double v : cr.avgByType) cr.avgHit += v;
        cr.avgHitWithCrit = cr.avgHit * (1.0 + r.critChance * (r.critMultiplier - 1.0));
        cr.damagePerUse = cr.avgHitWithCrit * comp.hitsPerUse;
        r.damagePerUse += cr.damagePerUse;

        r.breakdown.add(section, std::format("Average hit {:.1f}, with crit {:.1f}, x{:.2f} hits = {:.1f} per use",
                                             cr.avgHit, cr.avgHitWithCrit, comp.hitsPerUse, cr.damagePerUse));
        r.components.push_back(std::move(cr));
    }

    r.hitDps = r.damagePerUse * r.usesPerSecond;
    r.breakdown.add("DPS", std::format("{:.1f} per use x {:.3f}/s = {:.1f}", r.damagePerUse, r.usesPerSecond, r.hitDps));
    return r;
}

} // namespace tob
