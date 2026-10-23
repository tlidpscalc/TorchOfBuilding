#include "tob/ModParser.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tob;
using Catch::Approx;

TEST_CASE("Additional damage with qualifiers") {
    ParseResult r = parseModLine("+30% additional Attack Damage");
    REQUIRE(r.ok);
    REQUIRE(r.mods.size() == 1);
    CHECK(r.mods[0].type == ModType::Additional);
    CHECK(r.mods[0].stat == "Damage");
    CHECK(r.mods[0].requiredTags == bit(SkillTag::Attack));
    CHECK(r.mods[0].value == Approx(30));
}

TEST_CASE("Hit damage and elemental qualifiers") {
    ParseResult r = parseModLine("+5% additional Elemental Hit Damage");
    REQUIRE(r.ok);
    CHECK(r.mods[0].stat == "HitDamage");
    CHECK(r.mods[0].damageTypes == kElementalMask);
}

TEST_CASE("Plain percentage damage is increased") {
    ParseResult r = parseModLine("+45% Cold Damage");
    REQUIRE(r.ok);
    CHECK(r.mods[0].type == ModType::Increased);
    CHECK(r.mods[0].damageTypes == bit(DamageType::Cold));
}

TEST_CASE("Tangle damage without 'enhancement' is increased damage") {
    ParseResult r = parseModLine("+30% Tangle Damage");
    REQUIRE(r.ok);
    CHECK(r.mods[0].type == ModType::Increased);
    CHECK(r.mods[0].stat == "Damage");
    CHECK(r.mods[0].requiredTags == bit(SkillTag::Tangle));
}

TEST_CASE("Damage enhancement becomes a named bucket") {
    ParseResult r = parseModLine("+12% Tangle Damage Enhancement");
    REQUIRE(r.ok);
    CHECK(r.mods[0].type == ModType::Bucket);
    CHECK(r.mods[0].group == "tangle damage enhancement");
}

TEST_CASE("Crit rating flat vs percentage") {
    ParseResult flat = parseModLine("+250 Critical Strike Rating");
    ParseResult pct = parseModLine("+40% Critical Strike Rating");
    REQUIRE(flat.ok);
    REQUIRE(pct.ok);
    CHECK(flat.mods[0].type == ModType::Base);
    CHECK(pct.mods[0].type == ModType::Increased);
}

TEST_CASE("'Cold Skill' means the skill's Cold tag, not cold damage") {
    ParseResult r = parseModLine("+65% Cold Skill Critical Strike Damage");
    REQUIRE(r.ok);
    CHECK(r.mods[0].stat == "CritDamage");
    CHECK(r.mods[0].requiredTags == bit(SkillTag::Cold));
    CHECK(r.mods[0].damageTypes == 0);
}

TEST_CASE("Youga's Hero Memory offensive lines all parse") {
    for (const char* line : {"+80% Critical Strike Rating", "+65% Cold Skill Critical Strike Damage",
                             "+47% Cold Skill Critical Strike Damage", "+44% Spell Critical Strike Damage"}) {
        INFO(line);
        CHECK(parseModLine(line).ok);
    }
}

TEST_CASE("Crit damage is not swallowed by the generic damage rule") {
    ParseResult r = parseModLine("+50% Critical Strike Damage");
    REQUIRE(r.ok);
    CHECK(r.mods[0].stat == "CritDamage");
}

TEST_CASE("Penetration with and without 'resistance'") {
    ParseResult a = parseModLine("+20% Elemental Resistance Penetration");
    ParseResult b = parseModLine("+15% Erosion Penetration");
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    CHECK(a.mods[0].damageTypes == kElementalMask);
    CHECK(b.mods[0].damageTypes == bit(DamageType::Erosion));
}

TEST_CASE("Additional crit damage is not a real stat and stays unsupported") {
    CHECK_FALSE(parseModLine("+20% additional Critical Strike Damage").ok);
}

TEST_CASE("Per-stat scaling with a cap") {
    ParseResult r = parseModLine("+33% additional Damage for every +1 Penetration, up to +132%");
    REQUIRE(r.ok);
    REQUIRE(r.mods[0].per.has_value());
    CHECK(r.mods[0].per->var == "penetration");
    CHECK(r.mods[0].per->per == Approx(1));
    CHECK(r.mods[0].per->limit.value() == Approx(132));
}

TEST_CASE("While conditions") {
    ParseResult r = parseModLine("+40% Attack Speed while Berserk");
    REQUIRE(r.ok);
    REQUIRE(r.mods[0].conditions.size() == 1);
    CHECK(r.mods[0].conditions[0].flag == "berserk");
    CHECK_FALSE(r.mods[0].conditions[0].negate);
}

TEST_CASE("Per-stack mods: additive by default, compounding with (multiplies)") {
    ParseResult bunch = parseModLine("+3% additional Spell Damage per stack of Focus Blessing owned");
    REQUIRE(bunch.ok);
    REQUIRE(bunch.mods[0].per.has_value());
    CHECK(bunch.mods[0].per->var == "focus blessing");   // canonical name shared by all wordings
    CHECK_FALSE(bunch.mods[0].per->multiplies);

    ParseResult rainbow = parseModLine(
        "+17% additional damage for every 2 types of Elemental Resistance with identical values (multiplies)");
    REQUIRE(rainbow.ok);
    REQUIRE(rainbow.mods[0].per.has_value());
    CHECK(rainbow.mods[0].per->per == Approx(2));
    CHECK(rainbow.mods[0].per->multiplies);
}

TEST_CASE("Legendary wordings: attributes, main skill level, labels, flags") {
    ParseResult all = parseModLine("+12% All Stats");
    REQUIRE(all.ok);
    CHECK(all.mods.size() == 3);
    CHECK(all.mods[0].type == ModType::Increased);

    ParseResult two = parseModLine("+30 Strength and Intelligence");
    REQUIRE(two.ok);
    REQUIRE(two.mods.size() == 2);
    CHECK(two.mods[0].stat == "Strength");
    CHECK(two.mods[1].stat == "Intelligence");

    ParseResult main = parseModLine("+4 Main Skill Level");
    REQUIRE(main.ok);
    CHECK(main.mods[0].stat == "SkillLevel");
    CHECK(main.mods[0].requiredTags == bit(SkillTag::Main));

    ParseResult beacon = parseModLine("[Beacon] +2 Max Spell Burst");
    REQUIRE(beacon.ok);
    CHECK(beacon.mods[0].stat == "MaxSpellBurst");

    ParseResult both = parseModLine("Adds 28 - 35 Physical Damage to Attacks and Spells");
    REQUIRE(both.ok);
    CHECK(both.mods[0].requiredTags == 0);

    CHECK(parseModLine("Lucky Critical Strike").mods.at(0).stat == "LuckyCrit");
}

TEST_CASE("Weapon base and local gear lines become weapon stats") {
    ParseResult base = parseModLine("190 - 190 Physical Damage");
    REQUIRE(base.ok);
    CHECK(base.mods[0].stat == "WeaponBaseMin");
    CHECK(base.mods[0].damageTypes == bit(DamageType::Physical));
    CHECK(parseModLine("1.5 Attack Speed").mods.at(0).stat == "WeaponAttackSpeed");
    CHECK(parseModLine("+155% Gear Physical Damage").mods.at(0).stat == "WeaponDamage");
    CHECK(parseModLine("+27% gear Attack Speed").mods.at(0).type == ModType::Increased);
    CHECK(parseModLine("Adds 40 - 60 Physical Damage to the gear").mods.at(0).stat == "WeaponAddedMin");
    CHECK(parseModLine("+60% Attack Critical Strike Rating for this gear").mods.at(0).stat == "WeaponCritRating");
}

TEST_CASE("Support buffs granted to the player are not limited to the supported skill") {
    ParseResult r = parseModLine(
        "Gains a stack of buff when using the supported skill every 6s: +34.5 % Attack and Cast Speed after using a Mobility Skill");
    REQUIRE(r.ok);
    CHECK(r.playerBuff);
    CHECK_FALSE(r.supportScoped);
    REQUIRE(r.mods.size() == 2);
    CHECK(r.mods[1].stat == "CastSpeed");
    CHECK(r.mods[1].value == Approx(34.5));
    REQUIRE(r.mods[1].conditions.size() == 1);
    CHECK(r.mods[1].conditions[0].flag == "used mobility skill recently");
}

TEST_CASE("Ailment and damage-over-time wordings") {
    ParseResult base = parseModLine("Adds 10 - 30 Base Wilt Damage");
    REQUIRE(base.ok);
    CHECK(base.mods[0].stat == "AilmentBaseMin");
    CHECK(base.mods[0].group == "wilt");

    ParseResult all = parseModLine("+3% chance to inflict Damaging Ailments");
    REQUIRE(all.ok);
    CHECK(all.mods.size() == 3);

    CHECK(parseModLine("+3% Wilt chance").mods.at(0).group == "wilt");
    CHECK(parseModLine("+10% chance to Ignite targets").mods.at(0).group == "ignite");

    ParseResult proj = parseModLine("+62% additional Ailment Damage dealt by Projectiles");
    REQUIRE(proj.ok);
    CHECK(proj.mods[0].stat == "AilmentDamage");
    CHECK(proj.mods[0].type == ModType::Additional);
    CHECK(proj.mods[0].requiredTags == bit(SkillTag::Projectile));

    CHECK(parseModLine("+9% Damage Over Time").mods.at(0).stat == "DotDamage");
    CHECK(parseModLine("+41% additional Ignite Damage for this skill").mods.at(0).stat == "IgniteDamage");
    CHECK(parseModLine("+30 Affliction inflicted per second").mods.at(0).stat == "AfflictionPerSecond");
    CHECK(parseModLine("+43% Affliction Effect").mods.at(0).stat == "AfflictionEffect");
    CHECK(parseModLine("Gains 10% - 20% of the added Flat Damage's value as Base Ignite Damage").mods.at(0).stat ==
          "AilmentFromFlatMin");
    // Still reads the original order for spells: "+19% additional Elemental Damage dealt by Spell Skills".
    ParseResult dream = parseModLine("+19% additional Elemental Damage dealt by Spell Skills");
    REQUIRE(dream.ok);
    CHECK(dream.mods[0].requiredTags == bit(SkillTag::Spell));
    CHECK(dream.mods[0].damageTypes == kElementalMask);
}

TEST_CASE("Reap sources and reaping modifiers") {
    ParseResult hit = parseModLine("Reaps 0.3 s of Damage Over Time when the skill hits");
    REQUIRE(hit.ok);
    CHECK(hit.mods[0].stat == "ReapSource");
    CHECK(hit.mods[0].value == Approx(0.3));
    CHECK(hit.mods[0].group == "hit|");

    ParseResult dot = parseModLine(
        "Reaps 0.3 s of Damage Over Time when dealing Damage Over Time. The effect has a 6 s Recovery Time against the same target");
    REQUIRE(dot.ok);
    CHECK(dot.mods[0].group == "dot|6");
    CHECK(dot.mods[0].conditions.empty());

    CHECK(parseModLine("Reaps enemies within the Beam Area, dealing 0.5s of Damage Over Time.").mods.at(0).group == "channel|");
    CHECK(parseModLine("+20% Reaping Duration").mods.at(0).stat == "ReapingDuration");
    CHECK(parseModLine("+25% additional Reaping Recovery Speed").mods.at(0).type == ModType::Additional);
}

TEST_CASE("Spell Burst wordings") {
    ParseResult speed = parseModLine("+20% Spell Burst Charge Speed");
    REQUIRE(speed.ok);
    CHECK(speed.mods[0].stat == "SpellBurstChargeSpeed");
    CHECK(parseModLine("-30% additional Spell Burst Charge Speed").mods.at(0).type == ModType::Additional);

    ParseResult support = parseModLine("+25% additional Hit Damage for the supported skills cast by Spell Burst");
    REQUIRE(support.ok);
    CHECK(support.supportScoped);
    CHECK(support.mods[0].stat == "HitDamage");
    REQUIRE(support.mods[0].conditions.size() == 1);
    CHECK(support.mods[0].conditions[0].flag == "cast by spell burst");

    ParseResult next = parseModLine("+30% additional Hit Damage for skills cast by Spell Burst during the next 2 Spell "
                                    "Burst(s) activated after casting the supported skill");
    REQUIRE(next.ok);
    CHECK(next.mods[0].conditions.at(0).flag == "cast by spell burst");
}

TEST_CASE("Multistrike wordings") {
    ParseResult support = parseModLine("+101% chance for the supported skill to trigger Multistrike");
    REQUIRE(support.ok);
    CHECK(support.supportScoped);
    CHECK(support.mods[0].stat == "MultistrikeChance");
    CHECK(support.mods[0].value == Approx(101));
    CHECK(parseModLine("+30% Multistrike Chance for this skill").mods.at(0).stat == "MultistrikeChance");
    CHECK(parseModLine("Multistrikes of the supported skill deal 12% increasing damage").mods.at(0).stat ==
          "MultistrikeIncrement");
    CHECK(parseModLine("+40% additional Multistrike damage increment").mods.at(0).type == ModType::Additional);
    ParseResult speed = parseModLine("+8% Attack Speed for this skill for every attack made during Multistrike");
    REQUIRE(speed.ok);
    CHECK(speed.mods[0].stat == "MultistrikeAttackSpeed");
    CHECK_FALSE(speed.mods[0].per.has_value());
}

TEST_CASE("Fervor, Blessing, Blur, Intimidated, Lucky and gained-damage wordings") {
    CHECK(parseModLine("Gains 10 Fervor Rating when the supported skill hits an enemy").mods.at(0).stat == "FervorSource");
    ParseResult buff = parseModLine("Fervor gains an additional base effect: +3% additional Damage for every 4 Fervor Rating");
    REQUIRE(buff.ok);
    CHECK(buff.playerBuff);
    CHECK(buff.mods[0].effectStat == "FervorEffect");
    CHECK(buff.mods[0].conditions.at(0).flag == "fervor");
    CHECK(buff.mods[0].per->var == "fervor rating");

    ParseResult blur = parseModLine("Blur gains an additional effect: +0.75% additional Damage Over Time for each Blur Rating");
    REQUIRE(blur.ok);
    CHECK(blur.mods[0].stat == "DotDamage");
    CHECK(blur.mods[0].effectStat == "BlurEffect");

    ParseResult divine = parseModLine(
        "Gain a Divine Blessing every 2s in the following order: Focus Blessing - Agility Blessing - Tenacity Blessing");
    REQUIRE(divine.mods.size() == 2);
    CHECK(divine.mods[1].stat == "AgilityBlessingSource");
    CHECK(parseModLine("+2% additional damage for the supported skill for each stack of Agility Blessing").mods.at(0).per->var ==
          "agility blessing");

    CHECK(parseModLine("Enemies within 12m are Intimidated").mods.at(0).stat == "InflictIntimidated");
    CHECK(parseModLine("+20% Intimidating effect").mods.at(0).stat == "IntimidatingEffect");
    CHECK(parseModLine("The supported skill deals Lucky Damage").mods.at(0).stat == "LuckyDamage");
    CHECK(parseModLine("+30% additional Max Damage").mods.at(0).stat == "MaxDamage");
    CHECK(parseModLine("-20% additional min damage").mods.at(0).value == Approx(-20));

    ParseResult gain = parseModLine("Adds 30% of Physical Damage as Fire Damage to the supported skill");
    REQUIRE(gain.ok);
    CHECK(gain.supportScoped);
    CHECK(gain.mods[0].group == "physical>fire");
    CHECK(parseModLine("Adds 10% of Physical Damage to Cold Damage").mods.at(0).group == "physical>cold");
}

TEST_CASE("(multiplies) is reported") {
    ParseResult r = parseModLine("+10% additional Spell Damage (multiplies)");
    REQUIRE(r.ok);
    CHECK(r.multiplies);
}

TEST_CASE("Unsupported text fails with a reason instead of being dropped") {
    ParseResult r = parseModLine("Summons a dragon whenever you sneeze");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
}
