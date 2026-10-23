#include "tob/SkillDps.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <set>

namespace tob {

namespace {

Mod builtin(std::string stat, ModType type, double value, std::string source) {
    Mod m;
    m.stat = std::move(stat);
    m.type = type;
    m.value = value;
    m.source = std::move(source);
    return m;
}

PerStat perVar(std::string var, bool multiplies = false) {
    PerStat p;
    p.var = std::move(var);
    p.multiplies = multiplies;
    return p;
}

} // namespace

bool isModelledStat(std::string_view stat) {
    static const std::set<std::string, std::less<>> kUsed = {
        "Damage", "HitDamage", "DamageTaken", "CritRating", "CritDamage", "CritHitDamage", "CastSpeed", "Speed",
        "Penetration", "ArmorPenetration", "DoubleDamageChance", "Intelligence", "Strength", "Dexterity", "SkillLevel",
        "ProjectileQuantity", "ProjectileSpeed", "SkillEffectDuration", "CooldownRecoverySpeed", "SealedManaCompensation",
        "AuraEffect", "CurseEffect", "FrostbiteEffect", "FrostbiteRating", "ColdInfiltrationEffect",
        "SpellAggressionEffect", "NightFeatherEffect", "SelvesEffect", "AttackBlock", "SpellBlock", "MaxTangle",
        "MaxFocusBlessing", "AddedDamageMin", "AddedDamageMax", "GrantSkill", "InflictFrostbite",
        "InflictColdInfiltration", "InflictFrail", "InflictParalysis", "GainSpellAggression", "Hasten",
        "NightFeatherSource", "FocusBlessingSource", "LuckyCrit", "Conspire", "JoinedForces", "AttackSpeed",
        // Weapon-local stats build the weapon used by attack skills.
        "WeaponBaseMin", "WeaponBaseMax", "WeaponAddedMin", "WeaponAddedMax", "WeaponDamage", "WeaponAttackSpeed",
        "WeaponCritRating",
        // Ailments and damage over time.
        "AilmentBaseMin", "AilmentBaseMax", "AilmentFromFlatMin", "AilmentFromFlatMax", "AilmentChance", "AilmentDuration",
        "AfflictionPerSecond", "AfflictionEffect", "DotDamage", "AilmentDamage", "IgniteDamage", "WiltDamage", "TraumaDamage",
        "ReapSource", "ReapingDuration", "ReapingRecoverySpeed",
        "DeteriorationChance", "DeteriorationExtraStacks", "DeteriorationDamage", "DeteriorationDuration",
    };
    return kUsed.contains(stat);
}

double skillLevelDamageMultiplier(int level) {
    const int to30 = std::clamp(level, 20, 30) - 20;
    const int above30 = std::max(0, level - 30);
    return std::pow(constants::kLevelDamageTo30, to30) * std::pow(constants::kLevelDamageAbove30, above30);
}

SkillDpsResult calculateMainSkillDps(const BuildState& build) {
    using namespace constants;
    SkillDpsResult r;
    const MainSkill& s = build.main;
    ModDB db = build.mods;          // copy: the engine's built-in buffs and debuffs are added below
    Config cfg = build.config;

    VarResolver resolver;
    resolver = [&](const std::string& name, const EvalContext& ctx) -> std::optional<double> {
        if (auto plus = name.find(" + "); plus != std::string::npos)
            return db.varValue(name.substr(0, plus), ctx) + db.varValue(name.substr(plus + 3), ctx);
        if (name == "projectile speed") return db.sum(ModType::Increased, {"ProjectileSpeed"}, ctx);
        if (name == "attack or spell block chance") return db.sum(ModType::Base, {"AttackBlock", "SpellBlock"}, ctx);
        if (name == "cooldown recovery speed") return db.sum(ModType::Increased, {"CooldownRecoverySpeed"}, ctx);
        if (name == "skill effect duration") return db.sum(ModType::Increased, {"SkillEffectDuration"}, ctx);
        if (name == "sealed mana compensation") return db.sum(ModType::Increased, {"SealedManaCompensation"}, ctx);
        return std::nullopt;
    };
    const EvalContext ctx{s.tags, 0, &cfg, s.name, &resolver};
    auto base = [&](std::string_view stat) { return db.sum(ModType::Base, {stat}, ctx); };
    auto flagSet = [&](std::string_view stat) { return db.flag(stat, ctx); };
    auto attribute = [&](std::string_view stat) {
        return db.sum(ModType::Base, {stat}, ctx) * (1.0 + db.sum(ModType::Increased, {stat}, ctx) / 100.0);
    };

    // --- Situational state derived from the build ------------------------------------------------
    if (cfg.flag("dual wielding")) db.add(builtin("AttackBlock", ModType::Base, kDualWieldAttackBlock, "Dual wielding"));

    r.tangles = s.clockworkCore ? std::min(kMaxTangleCap, kBaseMaxTangles + base("MaxTangle")) : 1.0;
    if (s.clockworkCore) cfg.vars["activated tangles"] = r.tangles;

    // Focus Blessing: +5% additional damage per stack (confirmed); assumed at max when it is generated.
    const double maxFocus = kBaseMaxBlessings + base("MaxFocusBlessing");
    double focus = cfg.var("focus blessing").value_or(flagSet("FocusBlessingSource") ? maxFocus : 0.0);
    focus = std::min(focus, maxFocus);
    cfg.vars["focus blessing"] = focus;
    if (focus > 0) {
        cfg.flags["focus blessing active"] = true;
        Mod m = builtin("Damage", ModType::Additional, 5.0, "Focus Blessing");
        m.per = perVar("focus blessing");
        db.add(m);
    }

    // Night Feather Entanglement (Witch of the Night): one stack per active Tangle, max 2.
    if (flagSet("NightFeatherSource")) {
        const double stacks = std::min(2.0, r.tangles);
        cfg.vars["night feather"] = stacks;
        cfg.flags["night feather"] = stacks > 0;
        Mod dmg = builtin("Damage", ModType::Additional, 7.0, "Night Feather Entanglement");
        dmg.requiredTags = bit(SkillTag::Tangle);
        dmg.per = perVar("night feather");
        dmg.effectStat = "NightFeatherEffect";
        db.add(dmg);
        Mod cdr = builtin("CooldownRecoverySpeed", ModType::Increased, 3.0, "Night Feather Entanglement");
        cdr.per = perVar("night feather");
        cdr.effectStat = "NightFeatherEffect";
        db.add(cdr);
    }

    // Spell Aggression: additionally +7% cast speed and spell damage, scaled by its effect.
    if (flagSet("GainSpellAggression") || cfg.flag("spell aggression")) {
        cfg.flags["spell aggression"] = true;
        Mod speed = builtin("CastSpeed", ModType::Additional, 7.0, "Spell Aggression");
        speed.effectStat = "SpellAggressionEffect";
        db.add(speed);
        Mod dmg = builtin("Damage", ModType::Additional, 7.0, "Spell Aggression");
        dmg.requiredTags = bit(SkillTag::Spell);
        dmg.effectStat = "SpellAggressionEffect";
        db.add(dmg);
    }
    // Hasten (tlidb "Quickness"): additionally +8% attack and cast speed.
    if (flagSet("Hasten") || cfg.flag("hasten")) {
        db.add(builtin("CastSpeed", ModType::Additional, 8.0, "Hasten"));
        db.add(builtin("AttackSpeed", ModType::Additional, 8.0, "Hasten"));
    }
    // Projectile Amplification (follower): +1.4% additional projectile damage per stack (multiplies).
    if (cfg.var("projectile amplification").value_or(0.0) > 0) {
        Mod m = builtin("Damage", ModType::Additional, 1.4, "Projectile Amplification");
        m.requiredTags = bit(SkillTag::Projectile);
        m.per = perVar("projectile amplification", /*multiplies=*/true);
        db.add(m);
    }

    // Frostbite: the max rating is applied at once (confirmed); Frozen above 100.
    if (flagSet("InflictFrostbite")) {
        r.frostbiteRating = std::min(kFrostbiteRatingCap, kBaseFrostbiteRating + base("FrostbiteRating"));
        cfg.flags["frostbitten"] = true;
        cfg.flags["frozen"] = r.frostbiteRating > kFrozenAbove;
    }
    // Enemy debuffs.
    if (flagSet("InflictFrail")) {
        Mod m = builtin("DamageTaken", ModType::Additional, 15.0, "Frail");
        m.requiredTags = bit(SkillTag::Spell);
        db.add(m);
    }
    if (flagSet("InflictColdInfiltration")) {
        Mod m = builtin("DamageTaken", ModType::Additional, 13.0, "Cold Infiltration");
        m.damageTypes = bit(DamageType::Cold);
        m.effectStat = "ColdInfiltrationEffect";
        db.add(m);
    }
    if (flagSet("InflictParalysis"))  // ASSUMED: Paralysis' +15% damage taken is its own multiplier
        db.add(builtin("DamageTaken", ModType::Additional, 15.0, "Paralysis"));

    std::set<std::string> ailments(build.externalAilments.begin(), build.externalAilments.end());
    if (cfg.flag("frostbitten")) ailments.insert("Frostbite");
    if (cfg.flag("frozen")) ailments.insert("Freeze");
    cfg.vars["ailment types"] = static_cast<double>(ailments.size());
    cfg.flags["affected by ailments"] = !ailments.empty();

    // --- Speed and projectiles ---------------------------------------------------------------------
    const double castInc = db.sum(ModType::Increased, {"CastSpeed", "Speed"}, ctx);
    const double castMore = db.more({"CastSpeed", "Speed"}, ctx);
    r.castsPerSecond = s.baseCastSpeed * (1.0 + castInc / 100.0) * castMore;

    double projectiles = 1.0;
    if (s.extraProjectilePer > 0)
        projectiles += std::min<double>(s.extraProjectileCap, std::floor(base("ProjectileQuantity") / s.extraProjectilePer));

    // Unfired bolts: casts still condensing when a bolt launches (see docs/mechanics/homing_frost_bolt.md).
    if (s.condensationTime > 0 && !cfg.var("unfired bolts")) {
        const double condense = s.condensationTime / (1.0 + s.condensationCastSpeedEfficiency * castInc / 100.0);
        const double laterCasts = std::floor(r.castsPerSecond * condense);
        cfg.vars["unfired bolts"] = std::min<double>(kUnfiredBoltHoldCap, projectiles * laterCasts);
    }
    r.unfiredBolts = cfg.var("unfired bolts").value_or(0.0);
    r.hitsPerCast = 1.0 + (projectiles - 1.0) * (1.0 - s.shotgunFalloffPct / 100.0);

    // --- Ailments: damage over time from Ignite, Wilt and Trauma ------------------------------------
    // tlidb: base damage comes from "Adds X - Y Base <Ailment> Damage" and from a share of the added
    // flat damage; it ignores the skill damage multiplier. Owner (confirmed): ailments cannot crit or
    // deal Double Damage; resistance applies, armor does not; damage-taken debuffs of the matching
    // type apply; chance above 100% does nothing; Ignite/Trauma refresh a single stack (by default)
    // while Wilt stacks independently up to 30, limited by hit rate; Affliction sits at max.
    struct AilmentDef {
        const char* name;
        const char* damageStat;
        DamageType type;
        double duration;
        double maxStacks;
        bool independentStacks;
    };
    static constexpr AilmentDef kAilments[] = {
        {"ignite", "IgniteDamage", DamageType::Fire, 4.0, 1.0, false},
        {"wilt", "WiltDamage", DamageType::Erosion, 1.5, 30.0, true},
        {"trauma", "TraumaDamage", DamageType::Physical, 4.0, 1.0, false},
    };
    auto groupSum = [&](std::string_view stat, const std::string& group) {
        double total = 0.0;
        for (const Mod& m : db.mods())
            if (m.stat == stat && m.group == group && db.applies(m, ctx)) total += db.effectiveValue(m, ctx);
        return total;
    };
    // Optional fight length (config "fight duration", seconds). Unset = steady state. When set, effects
    // that ramp up are averaged over the fight: something that grows linearly from 0 to full over `rampTime`
    // averages to this fraction of its full value.
    const double fight = cfg.var("fight duration").value_or(0.0);
    auto rampAverage = [fight](double rampTime) {
        if (fight <= 0.0 || rampTime <= 0.0) return 1.0;
        return fight >= rampTime ? 1.0 - rampTime / (2.0 * fight) : fight / (2.0 * rampTime);
    };

    auto addAilments = [&](double hitsPerSecond, double flatAddedAvg) {
        // Affliction: +1% additional damage over time taken per point, max 100, scaled by Affliction Effect.
        double affliction = 0.0;
        if (base("AfflictionPerSecond") > 0 || cfg.var("affliction"))
            affliction = std::clamp(cfg.var("affliction").value_or(100.0), 0.0, 100.0);
        // Over a timed fight Affliction builds from 0 at its per-second rate, unless a value is set.
        if (fight > 0.0 && !cfg.var("affliction") && base("AfflictionPerSecond") > 0)
            affliction *= rampAverage(affliction / base("AfflictionPerSecond"));
        const double afflictionFactor =
            1.0 + affliction / 100.0 * (1.0 + db.sum(ModType::Increased, {"AfflictionEffect"}, ctx) / 100.0);
        const double durationInc = 1.0 + db.sum(ModType::Increased, {"AilmentDuration"}, ctx) / 100.0;
        // Skill level and main stats scale ailment damage like hits (confirmed). The flat-damage share
        // is not scaled by added damage effectiveness (owner's best guess, untested).
        double mainStat = 0.0;
        for (const std::string& st : s.mainStats) mainStat += attribute(st);
        const double skillFactor =
            skillLevelDamageMultiplier(s.level) * (1.0 + kMainStatDamagePerPoint * mainStat / 100.0);

        for (const AilmentDef& a : kAilments) {
            const double baseAvg = (groupSum("AilmentBaseMin", a.name) + groupSum("AilmentBaseMax", a.name)) / 2.0 +
                                   flatAddedAvg * (groupSum("AilmentFromFlatMin", a.name) +
                                                   groupSum("AilmentFromFlatMax", a.name)) / 200.0;   // average of the A%-B% range (confirmed)
            const double chance = std::clamp(groupSum("AilmentChance", a.name) / 100.0, 0.0, 1.0);
            if (baseAvg <= 0.0 || chance <= 0.0) continue;

            const double applied = hitsPerSecond * chance * a.duration * durationInc;
            double stacks = a.independentStacks ? std::min(a.maxStacks, applied) : std::min(1.0, applied);
            // Independent stacks (Wilt) build up linearly until steady: one duration, or sooner at the cap.
            if (a.independentStacks && applied > 0.0)
                stacks *= rampAverage(a.duration * durationInc * std::min(1.0, a.maxStacks / applied));

            EvalContext dotCtx = ctx;
            dotCtx.damageTypes = bit(a.type);
            // Spell damage mods (increased and additional) do not apply to ailments (confirmed by testing).
            dotCtx.skillTags &= ~bit(SkillTag::Spell);
            // Hit-only mods ("HitDamage") are left out; generic, type, DoT, ailment and per-ailment mods apply.
            const double inc = db.sum(ModType::Increased, {"Damage", "DotDamage", "AilmentDamage", a.damageStat}, dotCtx);
            const double more = db.more({"Damage", "DotDamage", "AilmentDamage", a.damageStat}, dotCtx);
            double v = baseAvg * stacks * skillFactor * (1.0 + inc / 100.0) * more * db.more({"DamageTaken"}, dotCtx) *
                       afflictionFactor;
            if (a.type != DamageType::Physical)
                v *= 1.0 - (build.enemyResistance - db.sum(ModType::Base, {"Penetration"}, dotCtx)) / 100.0;
            r.ailments.push_back({a.name, v, stacks, baseAvg});
            r.breakdown.add("Ailments", std::format("{}: base {:.4g}/s, {:.2f} stacks, +{:.0f}% inc, x{:.3f} additional, "
                                                    "affliction x{:.2f} -> {:.4g} DPS",
                                                    a.name, baseAvg, stacks, inc, more, afflictionFactor, v));
            r.dps += v;
        }

        // --- Reaping: True Damage equal to N seconds of the DoT currently on the target -------------
        // Confirmed: uses the DoT's final DPS (after resistance, Affliction and every multiplier); only
        // Reaping Duration (seconds reaped) and Reaping Recovery Speed (cooldown) scale it; each source
        // has its own cooldown per target.
        double dotDps = 0.0;
        for (const AilmentDps& a : r.ailments) dotDps += a.dps;
        if (dotDps <= 0.0) return;
        const double durationScale = (1.0 + db.sum(ModType::Increased, {"ReapingDuration"}, ctx) / 100.0) *
                                     db.more({"ReapingDuration"}, ctx);
        const double recoveryScale = (1.0 + db.sum(ModType::Increased, {"ReapingRecoverySpeed"}, ctx) / 100.0) *
                                     db.more({"ReapingRecoverySpeed"}, ctx);
        for (const Mod& m : db.mods()) {
            if (m.stat != "ReapSource" || !db.applies(m, ctx)) continue;
            const std::string trigger = m.group.substr(0, m.group.find('|'));
            const std::string rec = m.group.substr(m.group.find('|') + 1);
            // How often the trigger happens on its own (per target).
            double triggerRate = 0.0;
            if (trigger == "hit") triggerRate = hitsPerSecond;
            else if (trigger == "dot") triggerRate = std::numeric_limits<double>::infinity();   // DoT ticks continuously
            else if (trigger == "affliction") triggerRate = affliction >= 100.0 ? std::numeric_limits<double>::infinity() : 0.0;
            // Beam reaps happen once per channel cycle (stacks build to 4-5, then reset; confirmed). Channelling
            // is not modelled yet, so the rate comes from config and defaults to none.
            else if (trigger == "channel") triggerRate = cfg.var("beam reaps per second").value_or(0.0);
            // ASSUMED: a source without a stated recovery time reaps on every trigger.
            const double cooldownRate = rec.empty() ? std::numeric_limits<double>::infinity()
                                                    : recoveryScale / std::stod(rec);
            const double rate = std::min(triggerRate, cooldownRate);
            if (!std::isfinite(rate) || rate <= 0.0) continue;
            const double seconds = m.value * durationScale;
            const double v = seconds * dotDps * rate;
            r.reapingDps += v;
            r.dps += v;
            r.breakdown.add("Reaping", std::format("{}: {:.2f} s of {:.4g} DoT/s, {:.3f} reaps/s -> {:.4g} DPS",
                                                   m.source.empty() ? trigger : m.source, seconds, dotDps, rate, v));
        }
    };

    // --- Deterioration: True Damage equal to 60% of each hit's erosion damage, 1 s later -----------
    // Confirmed: base chance 0%; uses the hit's erosion damage after mitigation and all multipliers;
    // only Deterioration-specific mods scale it; each stack (up to 99) is independent.
    auto addDeterioration = [&](double erosionHitDps, double hitsPerSecond) {
        const double chance = std::clamp(base("DeteriorationChance") / 100.0, 0.0, 1.0);
        if (erosionHitDps <= 0.0 || chance <= 0.0) return;
        const double stacksPerHit = chance * (1.0 + base("DeteriorationExtraStacks"));
        const double duration = 1.0 * (1.0 + db.sum(ModType::Increased, {"DeteriorationDuration"}, ctx) / 100.0) *
                                db.more({"DeteriorationDuration"}, ctx);
        const double active = hitsPerSecond * stacksPerHit * duration;
        const double capFactor = active > 99.0 ? 99.0 / active : 1.0;   // stacks beyond 99 are not applied
        const double scale = (1.0 + db.sum(ModType::Increased, {"DeteriorationDamage"}, ctx) / 100.0) *
                             db.more({"DeteriorationDamage"}, ctx);
        // Each stack pays out when it expires, so a timed fight loses its first `duration` seconds.
        const double delayFactor = fight > 0.0 ? std::max(0.0, fight - duration) / fight : 1.0;
        r.deteriorationDps = erosionHitDps * stacksPerHit * capFactor * 0.6 * scale * delayFactor;
        r.dps += r.deteriorationDps;
        r.breakdown.add("Deterioration", std::format("{:.4g} erosion hit DPS x {:.3f} stacks/hit x 60% x {:.3f} = {:.4g} DPS ({:.1f} active stacks)",
                                                     erosionHitDps, stacksPerHit * capFactor, scale, r.deteriorationDps,
                                                     std::min(active, 99.0)));
    };

    // --- Attacks: weapon damage, per-weapon crit and speed, weapons alternate ------------------------
    if ((s.tags & bit(SkillTag::Attack)) && !build.weapons.empty()) {
        std::vector<WeaponStats> weapons = build.weapons;
        // Conspire (100%) / Joined Forces (60%): the off-hand does not attack; that share of its
        // damage is added to the main-hand base damage instead (confirmed).
        const double merge = flagSet("Conspire") ? 1.0 : flagSet("JoinedForces") ? 0.6 : 0.0;
        if (merge > 0 && weapons.size() > 1) {
            for (std::size_t i = 0; i < kDamageTypeCount; ++i) {
                weapons[0].damage[i].min += merge * weapons[1].damage[i].min;
                weapons[0].damage[i].max += merge * weapons[1].damage[i].max;
            }
            weapons.resize(1);
        }
        // Hits are computed at 100% weapon damage and scaled per component; damage is linear in it.
        const double pct = 100.0;

        const double speedInc = db.sum(ModType::Increased, {"AttackSpeed", "Speed"}, ctx);
        const double speedMore = db.more({"AttackSpeed", "Speed"}, ctx);
        double mainStat = 0.0;
        for (const std::string& st : s.mainStats) mainStat += attribute(st);
        const double common = skillLevelDamageMultiplier(s.level) * (1.0 + kMainStatDamagePerPoint * mainStat / 100.0) *
                              (1.0 + std::min(1.0, base("DoubleDamageChance") / 100.0));
        const double critInc = 1.0 + db.sum(ModType::Increased, {"CritRating"}, ctx) / 100.0;
        const double critMore = db.more({"CritRating"}, ctx);
        r.critMultiplier = 1.0 + (constants::kBaseCritDamagePct + base("CritDamage")) / 100.0;
        const double onCrit = db.more({"CritHitDamage"}, ctx);

        double damagePerCycle = 0.0, timePerCycle = 0.0, erosionPerCycle = 0.0;
        for (const WeaponStats& w : weapons) {
            DamageByType raw{};
            for (DamageType t : kAllDamageTypeList) {
                EvalContext tctx = ctx;
                tctx.damageTypes = bit(t);
                const double addMin = db.sum(ModType::Base, {"AddedDamageMin"}, tctx);
                const double addMax = db.sum(ModType::Base, {"AddedDamageMax"}, tctx);
                // Weapon attacks: added damage effectiveness equals the hit component % (confirmed).
                raw[index(t)] = {w.damage[index(t)].min * pct / 100.0 + addMin * pct / 100.0,
                                 w.damage[index(t)].max * pct / 100.0 + addMax * pct / 100.0};
            }
            double hit = 0.0, erosion = 0.0;
            for (const DamagePortion& p : applyConversions(raw, s.conversions)) {
                EvalContext finalCtx = ctx, historyCtx = ctx;
                finalCtx.damageTypes = bit(p.type);
                historyCtx.damageTypes = p.history;
                double v = p.damage.avg() * (1.0 + db.sum(ModType::Increased, {"Damage", "HitDamage"}, finalCtx) / 100.0) *
                           db.more({"Damage", "HitDamage"}, historyCtx) * db.more({"DamageTaken"}, finalCtx);
                if (cfg.flag("frostbitten") && p.type == DamageType::Cold)
                    v *= 1.0 + r.frostbiteRating / 100.0 *
                                   (1.0 + db.sum(ModType::Increased, {"FrostbiteEffect"}, finalCtx) / 100.0);
                if (p.type != DamageType::Physical)
                    v *= 1.0 - (build.enemyResistance - db.sum(ModType::Base, {"Penetration"}, finalCtx)) / 100.0;
                v *= 1.0 - ((p.type == DamageType::Physical ? build.enemyArmorPhysical : build.enemyArmorNonPhysical) -
                            db.sum(ModType::Base, {"ArmorPenetration"}, finalCtx)) / 100.0;
                hit += v;
                if (p.type == DamageType::Erosion) erosion += v;
            }
            // Attacks start from the weapon's crit rating; global crit rating applies on top (confirmed).
            double chance = std::clamp((w.critRating + base("CritRating")) * critInc * critMore * constants::kCritChancePerRating,
                                       0.0, constants::kMaxCritChance);
            if (flagSet("LuckyCrit")) chance = 1.0 - (1.0 - chance) * (1.0 - chance);
            const double hitScale = common * ((1.0 - chance) + chance * r.critMultiplier * onCrit);
            hit *= hitScale;
            erosion *= hitScale;

            const double aps = w.attackSpeed * (1.0 + speedInc / 100.0) * speedMore;
            r.breakdown.add("Attack", std::format("{}: {:.4g} per hit, {:.2f} attacks/s, {:.1f}% crit", w.slot, hit, aps, chance * 100.0));
            if (&w == &weapons.front()) r.critChance = chance;
            damagePerCycle += hit;
            erosionPerCycle += erosion;
            timePerCycle += aps > 0 ? 1.0 / aps : 0.0;
        }
        // Skills alternate weapons by default, one hit per weapon in turn (confirmed); each swing
        // takes its own weapon's time (ASSUMED; the owner is not sure yet).
        r.castsPerSecond = timePerCycle > 0 ? static_cast<double>(weapons.size()) / timePerCycle : 0.0;
        const double dpsAt100 = (damagePerCycle / static_cast<double>(weapons.size())) * r.hitsPerCast *
                                r.castsPerSecond * r.tangles;

        // Multi-part attacks (e.g. Ice Shot's lances and explosion) each get their own DPS; whether a
        // part hits the main target depends on the situation, so each can be excluded from the total.
        std::vector<MainSkill::AttackComponent> parts = s.attackComponents;
        if (parts.empty()) parts.push_back({"hit", 100.0});
        double includedPct = 0.0;
        for (const auto& part : parts) {
            const bool included = !cfg.flag("exclude " + part.name);
            const double dps = dpsAt100 * part.weaponDamagePct / 100.0;
            r.components.push_back({part.name, dps, included});
            r.breakdown.add("DPS", std::format("{}: {:.0f}% weapon damage, {:.4g} DPS{}", part.name, part.weaponDamagePct,
                                               dps, included ? "" : " (excluded)"));
            if (included) {
                r.dps += dps;
                includedPct += part.weaponDamagePct;
            }
        }
        r.averageHit = damagePerCycle / static_cast<double>(weapons.size()) * includedPct / 100.0;
        r.hitDps = r.dps;
        addDeterioration(damagePerCycle > 0 ? r.hitDps * erosionPerCycle / damagePerCycle : 0.0,
                         r.castsPerSecond * projectiles * r.tangles);

        // Every included part's hit rolls for ailments. Added flat damage for attacks is the weapon's
        // damage plus flat added damage (tlidb); the main-hand weapon is used.
        int includedParts = 0;
        for (const ComponentDps& c : r.components) includedParts += c.included ? 1 : 0;
        EvalContext allTypes = ctx;
        allTypes.damageTypes = kAllDamageTypesMask;
        double flat = (db.sum(ModType::Base, {"AddedDamageMin"}, allTypes) + db.sum(ModType::Base, {"AddedDamageMax"}, allTypes)) / 2.0;
        for (const DamageRange& d : weapons.front().damage) flat += d.avg();
        addAilments(r.castsPerSecond * projectiles * r.tangles * includedParts, flat);

        r.breakdown.add("DPS", std::format("{:.2f} attacks/s, hits {:.4g}, total {:.4g}", r.castsPerSecond, r.hitDps, r.dps));
        return r;
    }

    // --- One hit -------------------------------------------------------------------------------------
    EvalContext hitCtx = ctx;
    hitCtx.damageTypes = bit(s.damageType);
    auto factor = [&](std::string name, double v) { r.factors.push_back({std::move(name), v}); };

    // Flat added damage scales with the skill's added damage effectiveness (confirmed).
    const double added = (db.sum(ModType::Base, {"AddedDamageMin"}, hitCtx) + db.sum(ModType::Base, {"AddedDamageMax"}, hitCtx)) / 2.0;
    const double baseAvg = (s.baseMin + s.baseMax) / 2.0 + added * s.addedDamageEffectiveness;
    factor(std::format("Skill level {}", s.level), skillLevelDamageMultiplier(s.level));
    double mainStat = 0.0;
    for (const std::string& st : s.mainStats) mainStat += attribute(st);
    factor(std::format("Main stats ({:.0f})", mainStat), 1.0 + kMainStatDamagePerPoint * mainStat / 100.0);
    const double inc = db.sum(ModType::Increased, {"Damage", "HitDamage"}, hitCtx);
    factor(std::format("Increased damage (+{:.1f}%)", inc), 1.0 + inc / 100.0);
    factor("Additional damage", db.more({"Damage", "HitDamage"}, hitCtx));
    factor("Enemy damage taken", db.more({"DamageTaken"}, hitCtx));

    if (cfg.flag("frostbitten") && s.damageType == DamageType::Cold) {
        const double effect = db.sum(ModType::Increased, {"FrostbiteEffect"}, hitCtx);
        factor(std::format("Frostbite ({:.0f} rating, +{:.0f}% effect)", r.frostbiteRating, effect),
               1.0 + r.frostbiteRating / 100.0 * (1.0 + effect / 100.0));
    }
    if (s.damageType != DamageType::Physical) {
        const double res = build.enemyResistance - db.sum(ModType::Base, {"Penetration"}, hitCtx);
        factor(std::format("Enemy resistance {:.1f}%", res), 1.0 - res / 100.0);
    }
    const double armor = (s.damageType == DamageType::Physical ? build.enemyArmorPhysical : build.enemyArmorNonPhysical) -
                         db.sum(ModType::Base, {"ArmorPenetration"}, hitCtx);
    factor(std::format("Enemy armor mitigation {:.1f}%", armor), 1.0 - armor / 100.0);

    const double flatCrit = ((s.tags & bit(SkillTag::Spell)) ? kBaseSpellCritRating : 0.0) + base("CritRating");
    const double critRating = flatCrit * (1.0 + db.sum(ModType::Increased, {"CritRating"}, ctx) / 100.0) *
                              db.more({"CritRating"}, ctx);
    r.critChance = std::clamp(critRating * constants::kCritChancePerRating, 0.0, constants::kMaxCritChance);
    // Lucky (tlidb): the check is rolled twice and the better result is kept.
    if (flagSet("LuckyCrit")) r.critChance = 1.0 - (1.0 - r.critChance) * (1.0 - r.critChance);
    r.critMultiplier = 1.0 + (constants::kBaseCritDamagePct + base("CritDamage")) / 100.0;
    const double onCrit = db.more({"CritHitDamage"}, ctx);
    factor(std::format("Crit ({:.1f}% chance, x{:.2f}, x{:.2f} on crit)", r.critChance * 100.0, r.critMultiplier, onCrit),
           (1.0 - r.critChance) + r.critChance * r.critMultiplier * onCrit);
    factor("Double Damage", 1.0 + std::min(1.0, base("DoubleDamageChance") / 100.0));

    r.averageHit = baseAvg;
    for (const DpsFactor& f : r.factors) r.averageHit *= f.value;
    r.dps = r.averageHit * r.hitsPerCast * r.castsPerSecond * r.tangles;
    r.hitDps = r.dps;
    addDeterioration(s.damageType == DamageType::Erosion ? r.hitDps : 0.0, r.castsPerSecond * projectiles * r.tangles);
    {
        // Spells: added flat damage is the flat added damage only (tlidb). Every projectile hit rolls.
        EvalContext allTypes = ctx;
        allTypes.damageTypes = kAllDamageTypesMask;
        const double flat =
            (db.sum(ModType::Base, {"AddedDamageMin"}, allTypes) + db.sum(ModType::Base, {"AddedDamageMax"}, allTypes)) / 2.0;
        addAilments(r.castsPerSecond * projectiles * r.tangles, flat);
    }

    for (const DpsFactor& f : r.factors) r.breakdown.add("Hit", std::format("{}: x{:.3f}", f.name, f.value));
    r.breakdown.add("DPS", std::format("{:.0f} base x factors = {:.4g} per hit", baseAvg, r.averageHit));
    r.breakdown.add("DPS", std::format("{:.2f} hits/cast ({:.0f} projectiles), {:.2f} casts/s, {:.0f} Tangles, {:.0f} unfired bolts",
                                       r.hitsPerCast, projectiles, r.castsPerSecond, r.tangles, r.unfiredBolts));
    r.breakdown.add("DPS", std::format("DPS {:.4g}", r.dps));
    return r;
}

} // namespace tob
