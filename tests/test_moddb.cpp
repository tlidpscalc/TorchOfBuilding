#include "tob/ModDB.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tob;
using Catch::Approx;

namespace {

Mod mod(ModType type, double value, std::string group = {}) {
    Mod m;
    m.stat = "Damage";
    m.type = type;
    m.value = value;
    m.group = std::move(group);
    return m;
}

} // namespace

TEST_CASE("Increased mods add together") {
    ModDB db;
    db.add(mod(ModType::Increased, 50));
    db.add(mod(ModType::Increased, 50));
    EvalContext ctx;
    CHECK(db.sum(ModType::Increased, {"Damage"}, ctx) == Approx(100));
}

TEST_CASE("Each additional mod multiplies separately") {
    ModDB db;
    db.add(mod(ModType::Additional, 50));
    db.add(mod(ModType::Additional, 50));
    EvalContext ctx;
    CHECK(db.more({"Damage"}, ctx) == Approx(2.25));
}

TEST_CASE("Additional mods in the same group add before multiplying") {
    ModDB db;
    db.add(mod(ModType::Additional, 50, "helmet:damage"));
    db.add(mod(ModType::Additional, 50, "helmet:damage"));
    EvalContext ctx;
    CHECK(db.more({"Damage"}, ctx) == Approx(2.0));
}

TEST_CASE("Bucket mods add within the bucket and multiply with everything else") {
    ModDB db;
    db.add(mod(ModType::Bucket, 10, "tangle damage enhancement"));
    db.add(mod(ModType::Bucket, 10, "tangle damage enhancement"));
    db.add(mod(ModType::Additional, 50));
    EvalContext ctx;
    CHECK(db.more({"Damage"}, ctx) == Approx(1.2 * 1.5));
}

TEST_CASE("Tag and damage type requirements filter mods") {
    ModDB db;
    Mod attackFire = mod(ModType::Increased, 30);
    attackFire.requiredTags = bit(SkillTag::Attack);
    attackFire.damageTypes = bit(DamageType::Fire);
    db.add(attackFire);

    EvalContext spellFire{bit(SkillTag::Spell), bit(DamageType::Fire), nullptr};
    EvalContext attackCold{bit(SkillTag::Attack), bit(DamageType::Cold), nullptr};
    EvalContext attackFireCtx{bit(SkillTag::Attack) | bit(SkillTag::Projectile), bit(DamageType::Fire), nullptr};
    CHECK(db.sum(ModType::Increased, {"Damage"}, spellFire) == 0);
    CHECK(db.sum(ModType::Increased, {"Damage"}, attackCold) == 0);
    CHECK(db.sum(ModType::Increased, {"Damage"}, attackFireCtx) == Approx(30));
}

TEST_CASE("Per-stack steps of one mod add up unless the mod multiplies") {
    Config cfg;
    cfg.vars["focus blessing"] = 6;
    cfg.vars["matching resistances"] = 4;
    EvalContext ctx{0, 0, &cfg};

    ModDB additive;
    Mod bunch = mod(ModType::Additional, 3);
    bunch.per = PerStat{"focus blessing", 1, std::nullopt, false};
    additive.add(bunch);
    CHECK(additive.more({"Damage"}, ctx) == Approx(1.18));          // 1 + 6 x 3%

    ModDB compound;
    Mod rainbow = mod(ModType::Additional, 17);
    rainbow.per = PerStat{"matching resistances", 2, std::nullopt, true};
    compound.add(rainbow);
    CHECK(compound.more({"Damage"}, ctx) == Approx(1.17 * 1.17));   // 2 steps, compounding
}

TEST_CASE("Conditions and per-stat scaling use the config") {
    Config cfg;
    ModDB db;
    Mod m = mod(ModType::Increased, 2);
    m.conditions.push_back({"berserk", false});
    m.per = PerStat{"rage", 10, 15.0};
    db.add(m);

    EvalContext ctx{0, 0, &cfg};
    CHECK(db.sum(ModType::Increased, {"Damage"}, ctx) == 0);   // not berserk

    cfg.flags["berserk"] = true;
    cfg.vars["rage"] = 55;                                       // 5 steps -> 10
    CHECK(db.sum(ModType::Increased, {"Damage"}, ctx) == Approx(10));

    cfg.vars["rage"] = 200;                                      // 20 steps -> 40, capped at 15
    CHECK(db.sum(ModType::Increased, {"Damage"}, ctx) == Approx(15));
}
