#include "tob/Calc.h"
#include "tob/ModParser.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tob;
using Catch::Approx;

namespace {

// 100 physical weapon, 2 attacks/s, skill deals 200% weapon damage.
CalcInput basicAttack() {
    CalcInput in;
    in.weapon.damage[index(DamageType::Physical)] = {100, 100};
    in.weapon.attacksPerSecond = 2.0;
    in.skill.name = "Test Attack";
    in.skill.tags = bit(SkillTag::Attack) | bit(SkillTag::Projectile);
    in.skill.components.push_back({"Hit", 200.0, {}, 200.0, 1.0});
    return in;
}

void addMods(ModDB& db, std::string_view text) {
    for (const ParseResult& r : parseModText(text)) {
        REQUIRE(r.ok);
        db.add(r.mods);
    }
}

} // namespace

TEST_CASE("Physical attack against the target dummy loses 50% to armor") {
    CalcInput in = basicAttack();
    CalcResult r = calculateHit(in);
    CHECK(r.components.at(0).avgHit == Approx(100));   // 200 * (1 - 0.5)
    CHECK(r.hitDps == Approx(200));
}

TEST_CASE("Armor mitigation penetration subtracts from the mitigation percentage") {
    CalcInput in = basicAttack();
    ModDB db;
    addMods(db, "+10% Armor Mitigation Penetration");
    in.mods = &db;
    CHECK(calculateHit(in).components.at(0).avgHit == Approx(200 * 0.6));
}

TEST_CASE("Converted cold damage is reduced by resistance and non-physical armor mitigation") {
    CalcInput in = basicAttack();
    in.skill.conversions.push_back({DamageType::Physical, DamageType::Cold, 100});
    CHECK(calculateHit(in).components.at(0).avgHit == Approx(200 * 0.7 * 0.7));
}

TEST_CASE("Penetration lowers resistance directly and can go negative") {
    CalcInput in = basicAttack();
    in.skill.conversions.push_back({DamageType::Physical, DamageType::Cold, 100});
    ModDB db;
    in.mods = &db;

    addMods(db, "+20% Elemental Penetration");                     // 30% -> 10%
    CHECK(calculateHit(in).components.at(0).avgHit == Approx(200 * 0.9 * 0.7));

    addMods(db, "+30% Cold Penetration");                          // 30% -> -20%
    CHECK(calculateHit(in).components.at(0).avgHit == Approx(200 * 1.2 * 0.7));
}

TEST_CASE("100 critical strike rating is 1% crit chance") {
    CalcInput in = basicAttack();
    in.weapon.critRating = 500;
    ModDB db;
    addMods(db, "+500 Critical Strike Rating");
    in.mods = &db;
    CalcResult r = calculateHit(in);
    CHECK(r.critChance == Approx(0.10));
    CHECK(r.critMultiplier == Approx(1.5));
    CHECK(r.components.at(0).avgHitWithCrit == Approx(100 * (1 + 0.10 * 0.5)));
}

TEST_CASE("Critical strike damage adds onto the base 150%") {
    CalcInput in = basicAttack();
    in.weapon.critRating = 1000;                                   // 10%
    ModDB db;
    addMods(db, "+50% Critical Strike Damage\n+25% Critical Strike Damage");
    in.mods = &db;
    CalcResult r = calculateHit(in);
    CHECK(r.critMultiplier == Approx(2.25));                       // 150 + 50 + 25
    CHECK(r.components.at(0).avgHitWithCrit == Approx(100 * (1 + 0.10 * 1.25)));
}

TEST_CASE("Converted damage: increased mods use the final type, other mods keep the original type") {
    CalcInput in = basicAttack();
    in.skill.conversions.push_back({DamageType::Physical, DamageType::Cold, 100});
    ModDB db;
    addMods(db,
            "+50% Physical Damage\n"                               // original type, increased: no effect
            "+50% Cold Damage\n"                                   // final type, increased: applies
            "+20% Damage\n"                                        // generic: adds with cold
            "+30% additional Physical Damage\n");                  // original type, additional: applies
    in.mods = &db;
    double expected = 200 * 1.7 * 1.3 * 0.7 * 0.7;
    CHECK(calculateHit(in).components.at(0).avgHit == Approx(expected));
}

TEST_CASE("Converted damage: additional mods of the original AND the final type both apply") {
    CalcInput in = basicAttack();
    in.skill.conversions.push_back({DamageType::Physical, DamageType::Cold, 100});
    ModDB db;
    addMods(db, "+30% additional Physical Damage\n+20% additional Cold Damage\n+50% additional Fire Damage");
    in.mods = &db;
    CHECK(calculateHit(in).components.at(0).avgHit == Approx(200 * 1.3 * 1.2 * 0.7 * 0.7));   // fire does not apply
}

TEST_CASE("Increased and additional damage combine per the bucket rules") {
    CalcInput in = basicAttack();
    ModDB db;
    addMods(db,
            "+30% Attack Damage\n"
            "+20% Physical Damage\n"
            "+25% additional Damage\n"
            "+20% additional Attack Damage\n"
            "+10% Spell Damage\n");                                // does not apply
    in.mods = &db;
    double expected = 200 * 1.5 * 1.25 * 1.2 * 0.5;
    CHECK(calculateHit(in).components.at(0).avgHit == Approx(expected));
}

TEST_CASE("Cast speed mods scale a spell's base cast speed") {
    CalcInput in;
    in.skill.tags = bit(SkillTag::Spell);
    in.skill.baseCastSpeed = 1.25;                                 // 0.8 s cast time
    ModDB db;
    addMods(db, "+25% Cast Speed\n+10% additional Cast Speed");
    in.mods = &db;
    CHECK(calculateHit(in).usesPerSecond == Approx(1.25 * 1.25 * 1.1));
}

TEST_CASE("Tangle damage is increased damage and adds with generic damage") {
    CalcInput in;
    in.skill.tags = bit(SkillTag::Spell) | bit(SkillTag::Tangle);
    HitComponent hit;
    hit.name = "Hit";
    hit.baseDamage[index(DamageType::Lightning)] = {100, 100};
    in.skill.components.push_back(hit);
    ModDB db;
    addMods(db, "+30% Tangle Damage\n+20% Damage\n+50% Elemental Damage");
    in.mods = &db;
    CHECK(calculateHit(in).components.at(0).avgHit == Approx(100 * 2.0 * 0.7 * 0.7));

    in.skill.tags = bit(SkillTag::Spell);                          // not tangled: tangle mod drops out
    CHECK(calculateHit(in).components.at(0).avgHit == Approx(100 * 1.7 * 0.7 * 0.7));
}

TEST_CASE("Added flat damage scales with added damage effectiveness on spells") {
    CalcInput in;
    in.skill.name = "Test Spell";
    in.skill.tags = bit(SkillTag::Spell) | bit(SkillTag::Fire);
    HitComponent hit;
    hit.name = "Hit";
    hit.baseDamage[index(DamageType::Fire)] = {100, 100};
    hit.addedDamageEffectivenessPct = 150;
    in.skill.components.push_back(hit);

    ModDB db;
    addMods(db, "Adds 10 - 10 Fire Damage to Spells\nAdds 50 - 50 Cold Damage to Attacks");
    in.mods = &db;
    CalcResult r = calculateHit(in);
    CHECK(r.components.at(0).avgHit == Approx(115 * 0.7 * 0.7));
    CHECK(r.usesPerSecond == Approx(1.0));
}
