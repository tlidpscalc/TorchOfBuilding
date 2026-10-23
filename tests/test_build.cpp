#include "tob/Build.h"
#include "tob/SkillDps.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>

using namespace tob;
using Catch::Approx;

namespace {

const GameData& gameData() {
    static const GameData data = GameData::load(TOB_DATA_DIR);
    return data;
}

nlohmann::json youga() {
    std::ifstream in(std::string(TOB_DATA_DIR) + "/builds/youga_ultimate_one.json");
    return nlohmann::json::parse(in);
}

void describe(const SkillDpsResult& r) {
    for (const DpsFactor& f : r.factors) UNSCOPED_INFO(f.name << ": x" << f.value);
    UNSCOPED_INFO("casts/s " << r.castsPerSecond << ", hits/cast " << r.hitsPerCast << ", tangles " << r.tangles
                             << ", unfired " << r.unfiredBolts << ", DPS " << r.dps);
}

} // namespace

TEST_CASE("Youga build: every line parses") {
    BuildState b = loadBuild(gameData(), youga());
    int failures = 0;
    for (const LineReport& l : b.lines) {
        if (l.ok) continue;
        UNSCOPED_INFO(l.source << ": \"" << l.line << "\" -> " << l.error);
        ++failures;
    }
    CHECK(failures == 0);
}

TEST_CASE("Activation Mediums use the chosen tier, bonus and rolls") {
    nlohmann::json build = youga();
    for (auto& skill : build["skills"]["active"])
        if (skill["name"] == "Selves of All Realms")
            skill["supports"][0] = {{"name", "Activation Medium: Preparation"}, {"tier", "1"}, {"bonus", "duration"},
                                    {"values", {5, 15}}};
    BuildState b = loadBuild(gameData(), build);
    std::vector<std::string> lines;
    for (const LineReport& l : b.lines)
        if (l.source == "Support: Activation Medium: Preparation (Selves of All Realms)") lines.push_back(l.line);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "Prepares the supported skill every 5 s");
    CHECK(lines[1] == "+15% additional Duration for the supported skill");   // the Cooldown bonus is left out

    // Defaults: the best tier with the Cooldown bonus at the top of its range.
    lines.clear();
    for (const LineReport& l : loadBuild(gameData(), youga()).lines)
        if (l.source == "Support: Activation Medium: Preparation (Biting Cold)") lines.push_back(l.line);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "The supported skill +25% additional Cooldown Recovery Speed");
}

// Reference values: tools/verify/youga_handcalc.py with no blinking (Periodic Burst uptime 0),
// which matched four 60 s dummy runs within +-5%.
TEST_CASE("Youga build reproduces the verified hand calculation") {
    BuildState b = loadBuild(gameData(), youga());
    SkillDpsResult r = calculateMainSkillDps(b);
    describe(r);

    CHECK(b.main.level == 36);
    CHECK(r.castsPerSecond == Approx(5.515).epsilon(0.005));
    CHECK(r.unfiredBolts == 6);
    CHECK(r.hitsPerCast == Approx(1.8));
    CHECK(r.tangles == 4);
    CHECK(r.frostbiteRating == 115);
    CHECK(r.critChance == Approx(0.7641).epsilon(0.002));
    CHECK(r.dps == Approx(7.941e12).epsilon(0.005));
}

TEST_CASE("Lucky Critical Strike rolls crit twice") {
    nlohmann::json build = youga();
    const double base = calculateMainSkillDps(loadBuild(gameData(), build)).critChance;
    build["items"][0]["lines"].push_back("Lucky Critical Strike");
    const double lucky = calculateMainSkillDps(loadBuild(gameData(), build)).critChance;
    CHECK(lucky == Approx(1.0 - (1.0 - base) * (1.0 - base)));
}

TEST_CASE("Main Skill Level only raises the main skill") {
    nlohmann::json build = youga();
    build["items"][0]["lines"].push_back("+4 Main Skill Level");
    CHECK(loadBuild(gameData(), build).main.level == 40);
}

namespace {

// A tiny game data set with one attack skill, so weapon tests do not depend on scraped data.
GameData attackData() {
    GameData d;
    d.boards = nlohmann::json::array();
    d.traits = nlohmann::json::array();
    d.alternateYouga = nlohmann::json::object();
    nlohmann::json levels = nlohmann::json::array();
    for (int i = 1; i <= 20; ++i)
        levels.push_back({{"level", i}, {"text", {"Deals 200% Weapon Attack Damage."}}});
    d.skills = {{"Test Strike", {{"name", "Test Strike"}, {"tags", {"Attack", "Melee", "Physical"}},
                                 {"stats", nlohmann::json::object()}, {"levels", levels}}}};
    nlohmann::json twoPart = nlohmann::json::array();
    for (int i = 1; i <= 20; ++i)
        twoPart.push_back({{"level", i},
                           {"text", {"Lance:", "Deals 300% Weapon Attack Damage.", "Blast:", "Deals 100% Weapon Attack Damage."}}});
    d.skills["Test Shot"] = {{"name", "Test Shot"}, {"tags", {"Attack", "Projectile"}},
                             {"stats", nlohmann::json::object()}, {"levels", twoPart}};
    nlohmann::json spell = nlohmann::json::array();
    for (int i = 1; i <= 20; ++i) spell.push_back({{"level", i}, {"text", {"Deals 100-100 Spell Cold Damage"}}});
    nlohmann::json erosion = nlohmann::json::array();
    for (int i = 1; i <= 20; ++i) erosion.push_back({{"level", i}, {"text", {"Deals 100-100 Spell Erosion Damage"}}});
    d.skills["Test Rot"] = {{"name", "Test Rot"}, {"tags", {"Spell", "Erosion"}},
                            {"stats", {{"Cast Speed", "2"}}}, {"levels", erosion}};
    nlohmann::json ranged = nlohmann::json::array();
    for (int i = 1; i <= 20; ++i) ranged.push_back({{"level", i}, {"text", {"Deals 0-300 Spell Lightning Damage"}}});
    d.skills["Test Spark"] = {{"name", "Test Spark"}, {"tags", {"Spell", "Lightning"}},
                              {"stats", {{"Cast Speed", "1"}}}, {"levels", ranged}};
    d.skills["Test Bolt"] = {{"name", "Test Bolt"}, {"tags", {"Spell", "Cold"}},
                             {"stats", {{"Cast Speed", "2"}, {"Main Stat", "Intelligence"}}}, {"levels", spell}};
    return d;
}

nlohmann::json attackBuild(bool offHand, bool conspire) {
    nlohmann::json skill = nlohmann::json::object();
    skill["name"] = "Test Strike";
    nlohmann::json b = nlohmann::json::object();
    b["skills"]["main"] = "Test Strike";
    b["skills"]["active"] = nlohmann::json::array({skill});
    b["items"] = {{{"slot", "Main Hand"}, {"name", "Test Axe"},
                   {"lines", {"100 - 100 Physical Damage", "500 Critical Strike Rating", "1.5 Attack Speed",
                              "Adds 20 - 20 Physical Damage to the gear", "+100% Gear Physical Damage",
                              "+20% gear Attack Speed", "+50% Critical Strike Rating for this gear"}}}};
    if (offHand)
        b["items"].push_back({{"slot", "Off Hand"}, {"name", "Test Dagger"},
                              {"lines", {"50 - 50 Physical Damage", "500 Critical Strike Rating", "1.0 Attack Speed"}}});
    if (conspire) b["items"].push_back({{"slot", "Gloves"}, {"name", "Demons' Touch"}, {"lines", {"Conspire"}}});
    return b;
}

} // namespace

TEST_CASE("Weapons are built from base plus local gear modifiers") {
    BuildState b = loadBuild(attackData(), attackBuild(false, false));
    REQUIRE(b.weapons.size() == 1);
    CHECK(b.weapons[0].damage[index(DamageType::Physical)].avg() == Approx(240));   // (100 + 20) x 2
    CHECK(b.weapons[0].attackSpeed == Approx(1.8));                                  // 1.5 x 1.2
    CHECK(b.weapons[0].critRating == Approx(750));                                   // 500 x 1.5
}

TEST_CASE("Attack DPS uses the weapon, skill weapon %, and enemy mitigation") {
    SkillDpsResult r = calculateMainSkillDps(loadBuild(attackData(), attackBuild(false, false)));
    // 240 x 200% = 480 physical, x0.5 armor = 240; crit 7.5% at 150% -> x1.0375; 1.8 attacks/s
    CHECK(r.averageHit == Approx(240 * 1.0375));
    CHECK(r.castsPerSecond == Approx(1.8));
    CHECK(r.dps == Approx(240 * 1.0375 * 1.8));
}

TEST_CASE("Dual wielding alternates weapons; Conspire merges the off-hand into the main hand") {
    SkillDpsResult dual = calculateMainSkillDps(loadBuild(attackData(), attackBuild(true, false)));
    const double mainHit = 240 * 1.0375, offHit = 50 * 1.025;   // off hand: 50 x 200% x 0.5 armor, 5% crit
    CHECK(dual.dps == Approx((mainHit + offHit) / (1 / 1.8 + 1 / 1.0)));

    SkillDpsResult merged = calculateMainSkillDps(loadBuild(attackData(), attackBuild(true, true)));
    CHECK(merged.dps == Approx(290 * 1.0375 * 1.8));   // (240 + 50) x 200% x 0.5, main-hand crit and speed
}

TEST_CASE("Joined Forces adds 60% of the off-hand to the main hand") {
    nlohmann::json build = attackBuild(true, false);
    build["items"].push_back({{"slot", "Gloves"}, {"name", "Test Gloves"}, {"lines", {"Joined Forces"}}});
    SkillDpsResult r = calculateMainSkillDps(loadBuild(attackData(), build));
    CHECK(r.dps == Approx((240 + 0.6 * 50) * 1.0375 * 1.8));   // (240 + 30) x 200% x 0.5 armor
}

TEST_CASE("Multi-part attacks report each part and can leave parts out of the total") {
    nlohmann::json build = attackBuild(false, false);
    build["skills"]["main"] = "Test Shot";
    build["skills"]["active"][0]["name"] = "Test Shot";
    const double per100 = 120 * 1.0375 * 1.8;   // 240 at 100% weapon damage x 0.5 armor, crit, speed

    SkillDpsResult all = calculateMainSkillDps(loadBuild(attackData(), build));
    REQUIRE(all.components.size() == 2);
    CHECK(all.components[0].name == "lance");
    CHECK(all.components[0].dps == Approx(per100 * 3));
    CHECK(all.components[1].name == "blast");
    CHECK(all.components[1].dps == Approx(per100));
    CHECK(all.dps == Approx(per100 * 4));

    build["config"]["flags"] = {"exclude blast"};
    SkillDpsResult lanceOnly = calculateMainSkillDps(loadBuild(attackData(), build));
    CHECK_FALSE(lanceOnly.components[1].included);
    CHECK(lanceOnly.dps == Approx(per100 * 3));
}

namespace {

SkillDpsResult withLines(std::initializer_list<const char*> lines) {
    nlohmann::json build = attackBuild(false, false);
    nlohmann::json gloves = {{"slot", "Gloves"}, {"name", "Test Gloves"}, {"lines", nlohmann::json::array()}};
    for (const char* l : lines) gloves["lines"].push_back(l);
    build["items"].push_back(gloves);
    return calculateMainSkillDps(loadBuild(attackData(), build));
}

double ailmentDps(const SkillDpsResult& r, const std::string& name) {
    for (const AilmentDps& a : r.ailments)
        if (a.name == name) return a.dps;
    return 0.0;
}

} // namespace

TEST_CASE("Wilt stacks are limited by hit rate and scale with damage over time") {
    // 1.8 hits/s x 100% chance x 1.5 s = 2.7 stacks of 20 erosion per second, x0.7 for 30% resistance.
    SkillDpsResult r = withLines({"Adds 10 - 30 Base Wilt Damage", "+100% Wilt chance"});
    CHECK(ailmentDps(r, "wilt") == Approx(20 * 2.7 * 0.7));
    CHECK(r.dps == Approx(r.hitDps + 20 * 2.7 * 0.7));

    // Chance above 100% does nothing; "+50% Damage Over Time" is increased damage for the ailment only.
    SkillDpsResult more = withLines({"Adds 10 - 30 Base Wilt Damage", "+150% Wilt chance", "+50% Damage Over Time"});
    CHECK(ailmentDps(more, "wilt") == Approx(20 * 2.7 * 0.7 * 1.5));
    CHECK(more.hitDps == Approx(r.hitDps));
}

TEST_CASE("Ignite keeps one refreshed stack; Affliction sits at max and scales with its effect") {
    SkillDpsResult r = withLines({"Adds 100 - 100 Base Ignite Damage", "+50% Ignite chance"});
    CHECK(ailmentDps(r, "ignite") == Approx(100 * 0.7));   // 1.8 x 0.5 x 4 s >= 1 -> always up

    SkillDpsResult a = withLines({"Adds 100 - 100 Base Ignite Damage", "+50% Ignite chance",
                                  "+30 Affliction inflicted per second", "+50% Affliction Effect"});
    CHECK(ailmentDps(a, "ignite") == Approx(100 * 0.7 * (1 + 1.0 * 1.5)));
}

TEST_CASE("Ailments do not crit, ignore armor, and need base damage") {
    // Trauma is physical: no resistance and no armor; crit and Double Damage do not apply.
    SkillDpsResult r = withLines({"Adds 50 - 50 Base Trauma Damage", "+100% chance to inflict Trauma",
                                  "+100% chance to deal Double Damage", "+5000 Critical Strike Rating"});
    CHECK(ailmentDps(r, "trauma") == Approx(50));
    CHECK(ailmentDps(withLines({"+100% Wilt chance"}), "wilt") == 0.0);
}

TEST_CASE("Reaping deals seconds of final DoT, limited by each source's cooldown") {
    const double wilt = 20 * 2.7 * 0.7;   // final Wilt DPS from the earlier test setup
    const char* wiltLines[] = {"Adds 10 - 30 Base Wilt Damage", "+100% Wilt chance"};

    // DoT-triggered, 2 s cooldown: 0.5 reaps/s x 0.5 s of DoT.
    SkillDpsResult r = withLines({wiltLines[0], wiltLines[1],
                                  "Reaps 0.5 s of Damage Over Time when dealing Damage Over Time. The effect has a 2 s Recovery Time against the same target"});
    CHECK(r.reapingDps == Approx(0.5 * wilt * 0.5));
    CHECK(r.dps == Approx(r.hitDps + wilt + 0.5 * wilt * 0.5));

    // Reaping Duration lengthens the seconds reaped; Reaping Recovery Speed shortens the cooldown.
    SkillDpsResult scaled = withLines({wiltLines[0], wiltLines[1],
                                       "Reaps 0.5 s of Damage Over Time when dealing Damage Over Time. The effect has a 2 s Recovery Time against the same target",
                                       "+100% Reaping Duration", "+100% Reaping Recovery Speed"});
    CHECK(scaled.reapingDps == Approx(1.0 * wilt * 1.0));

    // Hit-triggered: limited by the hit rate (1.8/s) or the cooldown (1 s), whichever is slower.
    SkillDpsResult onHit = withLines({wiltLines[0], wiltLines[1],
                                      "Reaps 0.2 s of Damage Over Time when the skill hits. The effect has a 1 s Recovery Time against the same target"});
    CHECK(onHit.reapingDps == Approx(0.2 * wilt * 1.0));

    // Beam reaps happen once per channel cycle: none unless the rate is configured.
    nlohmann::json build = attackBuild(false, false);
    build["items"].push_back({{"slot", "Gloves"}, {"name", "Test Gloves"},
                              {"lines", {wiltLines[0], wiltLines[1], "Reaps enemies within the Beam Area, dealing 0.5s of Damage Over Time."}}});
    CHECK(calculateMainSkillDps(loadBuild(attackData(), build)).reapingDps == 0.0);
    build["config"]["vars"]["beam reaps per second"] = 0.4;
    CHECK(calculateMainSkillDps(loadBuild(attackData(), build)).reapingDps == Approx(0.5 * wilt * 0.4));

    // No DoT, nothing to reap.
    CHECK(withLines({"Reaps 0.2 s of Damage Over Time when the skill hits"}).reapingDps == 0.0);
}

TEST_CASE("Deterioration: 60% of erosion hit damage per stack, only Deterioration mods scale it") {
    auto rot = [](const std::string& skill, std::initializer_list<const char*> lines) {
        nlohmann::json build = attackBuild(false, false);
        build["skills"]["main"] = skill;
        build["skills"]["active"][0]["name"] = skill;
        nlohmann::json ring = {{"slot", "Ring 1"}, {"name", "Test Ring"}, {"lines", nlohmann::json::array()}};
        for (const char* l : lines) ring["lines"].push_back(l);
        build["items"].push_back(ring);
        return calculateMainSkillDps(loadBuild(attackData(), build));
    };
    // Base chance is 0%.
    CHECK(rot("Test Rot", {}).deteriorationDps == 0.0);

    SkillDpsResult r = rot("Test Rot", {"+50% Deterioration Chance"});
    CHECK(r.deteriorationDps == Approx(r.hitDps * 0.5 * 0.6));
    CHECK(r.dps == Approx(r.hitDps + r.deteriorationDps));

    // Deterioration Damage mods scale it; generic damage only changes it through the hit.
    SkillDpsResult scaled = rot("Test Rot", {"+50% Deterioration Chance", "+100% additional Deterioration Damage"});
    CHECK(scaled.deteriorationDps == Approx(r.deteriorationDps * 2));

    // Extra stacks: 50% chance for 2 more -> 1 expected stack per hit at 50% chance.
    SkillDpsResult extra = rot("Test Rot", {"+50% Deterioration Chance",
                                            "There is a 50% chance for the supported skill to inflict 2 additional stack(s) of Deterioration"});
    CHECK(extra.deteriorationDps == Approx(r.hitDps * 1.0 * 0.6));

    // 99-stack cap: 2 hits/s x 0.5 x 101 s = 101 stacks -> only 99 apply.
    SkillDpsResult capped = rot("Test Rot", {"+50% Deterioration Chance", "+10000% Deterioration Duration"});
    CHECK(capped.deteriorationDps == Approx(r.deteriorationDps * 99.0 / 101.0));

    // Only erosion hits inflict it.
    CHECK(rot("Test Bolt", {"+50% Deterioration Chance"}).deteriorationDps == 0.0);
}

TEST_CASE("Fight duration averages ramp-up effects; unset means steady state") {
    auto timed = [](std::initializer_list<const char*> lines, double fight, const std::string& skill = "Test Strike") {
        nlohmann::json build = attackBuild(false, false);
        build["skills"]["main"] = skill;
        build["skills"]["active"][0]["name"] = skill;
        nlohmann::json gloves = {{"slot", "Gloves"}, {"name", "Test Gloves"}, {"lines", nlohmann::json::array()}};
        for (const char* l : lines) gloves["lines"].push_back(l);
        build["items"].push_back(gloves);
        if (fight > 0) build["config"]["vars"]["fight duration"] = fight;
        return calculateMainSkillDps(loadBuild(attackData(), build));
    };
    // Wilt: 2.7 steady stacks reached after 1.5 s; over 60 s that averages 1 - 1.5 / 120.
    const double wilt = 20 * 2.7 * 0.7;
    CHECK(ailmentDps(timed({"Adds 10 - 30 Base Wilt Damage", "+100% Wilt chance"}, 0), "wilt") == Approx(wilt));
    CHECK(ailmentDps(timed({"Adds 10 - 30 Base Wilt Damage", "+100% Wilt chance"}, 60), "wilt") ==
          Approx(wilt * (1 - 1.5 / 120)));

    // Affliction builds at 30/s: 100 points after 3.33 s, averaging 1 - 3.33 / 120 of max over 60 s.
    const double afflictionAvg = 1 - (100.0 / 30) / 120;
    CHECK(ailmentDps(timed({"Adds 100 - 100 Base Ignite Damage", "+50% Ignite chance",
                            "+30 Affliction inflicted per second", "+50% Affliction Effect"}, 60), "ignite") ==
          Approx(100 * 0.7 * (1 + afflictionAvg * 1.5)));

    // Deterioration pays out only after 1 s, so a 60 s fight keeps 59/60 of it.
    SkillDpsResult steady = timed({"+50% Deterioration Chance"}, 0, "Test Rot");
    SkillDpsResult fight = timed({"+50% Deterioration Chance"}, 60, "Test Rot");
    CHECK(fight.deteriorationDps == Approx(steady.deteriorationDps * 59.0 / 60.0));
    CHECK(fight.hitDps == Approx(steady.hitDps));
}

TEST_CASE("Spell damage mods do not apply to ailments from spells") {
    auto spellBuild = [](std::initializer_list<const char*> lines) {
        nlohmann::json build = attackBuild(false, false);
        build["skills"]["main"] = "Test Bolt";
        build["skills"]["active"][0]["name"] = "Test Bolt";
        nlohmann::json ring = {{"slot", "Ring 1"}, {"name", "Test Ring"}, {"lines", nlohmann::json::array()}};
        for (const char* l : lines) ring["lines"].push_back(l);
        build["items"].push_back(ring);
        return calculateMainSkillDps(loadBuild(attackData(), build));
    };
    SkillDpsResult plain = spellBuild({"Adds 10 - 30 Base Wilt Damage", "+100% Wilt chance"});
    SkillDpsResult spelly = spellBuild({"Adds 10 - 30 Base Wilt Damage", "+100% Wilt chance", "+100% Spell Damage",
                                        "+50% additional Spell Damage"});
    // 2 casts/s x 1.5 s = 3 stacks of 20 erosion, x0.7 resistance; spell mods change the hit only.
    CHECK(ailmentDps(plain, "wilt") == Approx(20 * 3 * 0.7));
    CHECK(ailmentDps(spelly, "wilt") == Approx(ailmentDps(plain, "wilt")));
    CHECK(spelly.hitDps == Approx(plain.hitDps * 2 * 1.5));

    // Skill level and main stats scale ailments like hits (confirmed).
    SkillDpsResult scaled = spellBuild({"Adds 10 - 30 Base Wilt Damage", "+100% Wilt chance", "+100 Intelligence",
                                        "+5 Main Skill Level"});
    CHECK(ailmentDps(scaled, "wilt") == Approx(20 * 3 * 0.7 * 1.5 * std::pow(1.1, 5)));
}

TEST_CASE("Spell Burst: full recharge in 2 s, fires on the next cast, casts Max Spell Burst times") {
    auto burst = [](std::initializer_list<const char*> lines) {
        nlohmann::json build = attackBuild(false, false);
        build["skills"]["main"] = "Test Bolt";
        build["skills"]["active"][0]["name"] = "Test Bolt";
        nlohmann::json ring = {{"slot", "Ring 1"}, {"name", "Test Ring"}, {"lines", nlohmann::json::array()}};
        for (const char* l : lines) ring["lines"].push_back(l);
        build["items"].push_back(ring);
        return calculateMainSkillDps(loadBuild(attackData(), build));
    };
    const SkillDpsResult plain = burst({});
    CHECK(plain.spellBurstsPerSecond == 0.0);

    // 2 casts/s, 2 s recharge: every 4th cast bursts 3 times -> 1.5 normal + 1.5 burst casts/s.
    const SkillDpsResult three = burst({"+3 Max Spell Burst"});
    CHECK(three.spellBurstsPerSecond == Approx(0.5));
    CHECK(three.hitDps == Approx(plain.hitDps * 1.5));

    // Recharge 1 s: every 2nd cast bursts.
    CHECK(burst({"+3 Max Spell Burst", "+100% Spell Burst Charge Speed"}).hitDps == Approx(plain.hitDps * 2.0));
    // Recharge 1.33 s is not a whole number of casts: the burst waits for the 3rd cast.
    CHECK(burst({"+3 Max Spell Burst", "+50% Spell Burst Charge Speed"}).spellBurstsPerSecond == Approx(2.0 / 3.0));
    // Casting slower than the recharge bursts on every cast.
    CHECK(burst({"+3 Max Spell Burst", "-75% Cast Speed"}).hitDps == Approx(plain.hitDps * 0.25 * 3));

    // Burst-only hit damage scales the burst casts alone: 1.5 + 1.5 x 1.5 = 3.75 of 2 casts.
    CHECK(burst({"+3 Max Spell Burst", "+50% additional Hit Damage for skills cast by Spell Burst"}).hitDps ==
          Approx(plain.hitDps * 3.75 / 2));

    // Burst casts roll ailments too: 3 casts/s x 1.5 s Wilt = 4.5 stacks.
    CHECK(ailmentDps(burst({"+3 Max Spell Burst", "Adds 10 - 30 Base Wilt Damage", "+100% Wilt chance"}), "wilt") ==
          Approx(20 * 4.5 * 0.7));
}

TEST_CASE("Multistrike: extra attacks with compounding increment and faster follow-ups") {
    const SkillDpsResult plain = withLines({});
    CHECK(plain.multistrikeAttacks == 1.0);

    // 100%: one guaranteed extra attack at +20% increased attack speed (no global attack speed here).
    const SkillDpsResult one = withLines({"+100% Multistrike Chance for this skill"});
    CHECK(one.multistrikeAttacks == Approx(2.0));
    CHECK(one.hitDps == Approx(plain.hitDps * 2.0 / (1.0 + 1.0 / 1.2)));

    // 150% with 10% increment: attacks deal 1, 1.1 and (half the time) 1.1^2.
    const double time = 1.0 + 1.5 / 1.2;
    CHECK(withLines({"+150% chance to Multistrike", "Multistrikes deal 10% increasing damage"}).hitDps ==
          Approx(plain.hitDps * (1.0 + 1.1 + 0.5 * 1.21) / time));
    // Additional increment multiplies the increment: 10% x 1.5 = 15%.
    CHECK(withLines({"+150% chance to Multistrike", "Multistrikes deal 10% increasing damage",
                     "+50% additional Multistrike damage increment"}).hitDps ==
          Approx(plain.hitDps * (1.0 + 1.15 + 0.5 * 1.15 * 1.15) / time));
    // Extra speed during Multistrike is one flat bonus, not growing per strike (confirmed).
    CHECK(withLines({"+200% Multistrike Chance", "+10% Attack Speed for this skill for every attack made during Multistrike"}).hitDps ==
          Approx(plain.hitDps * 3.0 / (1.0 + 2.0 / 1.3)));

    // The increment does not raise ailment damage (one refreshed Ignite stack either way).
    const double ignite = ailmentDps(withLines({"Adds 100 - 100 Base Ignite Damage", "+50% Ignite chance"}), "ignite");
    CHECK(ailmentDps(withLines({"Adds 100 - 100 Base Ignite Damage", "+50% Ignite chance", "+100% Multistrike Chance",
                                "Multistrikes deal 50% increasing damage"}), "ignite") == Approx(ignite));

    // Spells do not Multistrike in this model's attack branch.
    nlohmann::json build = attackBuild(false, false);
    build["skills"]["main"] = "Test Bolt";
    build["skills"]["active"][0]["name"] = "Test Bolt";
    build["items"].push_back({{"slot", "Gloves"}, {"name", "Test Gloves"}, {"lines", {"+100% Multistrike Chance"}}});
    CHECK(calculateMainSkillDps(loadBuild(attackData(), build)).multistrikeAttacks == 1.0);
}

namespace {

// The given main skill with extra ring lines and config vars.
SkillDpsResult skillWith(const std::string& skill, std::initializer_list<const char*> lines,
                         const nlohmann::json& vars = nlohmann::json::object()) {
    nlohmann::json build = attackBuild(false, false);
    build["skills"]["main"] = skill;
    build["skills"]["active"][0]["name"] = skill;
    nlohmann::json ring = {{"slot", "Ring 1"}, {"name", "Test Ring"}, {"lines", nlohmann::json::array()}};
    for (const char* l : lines) ring["lines"].push_back(l);
    build["items"].push_back(ring);
    if (!vars.empty()) build["config"]["vars"] = vars;
    return calculateMainSkillDps(loadBuild(attackData(), build));
}

} // namespace

TEST_CASE("Fervor: +2% crit rating per point at 100; Fervor Effect scales its base effects only") {
    // Spells start at 500 crit rating (5%).
    CHECK(skillWith("Test Bolt", {}).critChance == Approx(0.05));
    CHECK(skillWith("Test Bolt", {"Have Fervor"}).critChance == Approx(0.15));
    CHECK(skillWith("Test Bolt", {"Have Fervor", "+50% Fervor effect"}).critChance == Approx(0.20));
    CHECK(skillWith("Test Bolt", {"Have Fervor"}, {{"fervor rating", 50}}).critChance == Approx(0.10));

    const double plain = skillWith("Test Bolt", {"Have Fervor"}).hitDps;
    // "Fervor gains an additional base effect" is scaled: 25% x 2. Fervor Effect also lifts crit from
    // 15% to 25% (crit factor 1.075 -> 1.125 at 150% crit damage).
    CHECK(skillWith("Test Bolt", {"Have Fervor", "+100% Fervor effect",
                                  "Fervor gains an additional base effect: +1% additional damage for every 4 Fervor Rating"})
              .hitDps == Approx(plain * 1.5 * 1.125 / 1.075));
    // Lines that only count Fervor Rating are not scaled.
    const SkillDpsResult counted = skillWith("Test Bolt", {"Have Fervor", "+1% additional damage for every 4 Fervor Rating"});
    CHECK(counted.hitDps == Approx(plain * 1.25));
    // Without Fervor the buff line does nothing.
    CHECK(skillWith("Test Bolt", {"Fervor gains an additional base effect: +1% additional damage for every 4 Fervor Rating"})
              .hitDps == Approx(skillWith("Test Bolt", {}).hitDps));
}

TEST_CASE("Agility Blessing: +4% increased speed and +2% additional damage per stack") {
    const SkillDpsResult plain = withLines({});
    nlohmann::json build = attackBuild(false, false);
    build["config"]["vars"]["agility blessing"] = 4;
    const SkillDpsResult blessed = calculateMainSkillDps(loadBuild(attackData(), build));
    CHECK(blessed.castsPerSecond == Approx(plain.castsPerSecond * 1.16));
    CHECK(blessed.hitDps == Approx(plain.hitDps * 1.16 * 1.08));
    // Divine Blessing generates Focus and Agility Blessing at max stacks.
    const SkillDpsResult divine = skillWith("Test Strike", {"Gain a Divine Blessing every 2s in the following order: "
                                                            "Focus Blessing - Agility Blessing - Tenacity Blessing"});
    CHECK(divine.hitDps == Approx(plain.hitDps * 1.16 * 1.08 * 1.20));
}

TEST_CASE("Stack-count conditions follow the Blessing stacks") {
    nlohmann::json build = attackBuild(false, false);
    build["items"].push_back({{"slot", "Ring 1"}, {"name", "Test Ring"},
                              {"lines", {"+30% additional damage when you have at least 4 stack(s) of Agility Blessing"}}});
    build["config"]["vars"]["agility blessing"] = 3;
    const SkillDpsResult three = calculateMainSkillDps(loadBuild(attackData(), build));
    build["config"]["vars"]["agility blessing"] = 4;
    const SkillDpsResult four = calculateMainSkillDps(loadBuild(attackData(), build));
    // The 4th stack adds +4% speed and 2% damage (1.08 / 1.06) and switches on the +30%.
    CHECK(four.hitDps == Approx(three.hitDps * (1.16 / 1.12) * (1.08 / 1.06) * 1.3));
}

TEST_CASE("Blur rating comes from config; Blur Effect scales only Blur's added effects") {
    const double plain = skillWith("Test Bolt", {}).hitDps;
    const char* added = "Blur gains an additional effect: +1% additional damage for each Blur Rating";
    CHECK(skillWith("Test Bolt", {added}).hitDps == Approx(plain));                     // no Blur
    CHECK(skillWith("Test Bolt", {"Has Blur", added}).hitDps == Approx(plain * 2.0));   // 100 rating by default
    CHECK(skillWith("Test Bolt", {"Has Blur", added, "+50% Blur Effect"}, {{"blur rating", 60}}).hitDps ==
          Approx(plain * 1.9));
    // A line that only checks Blur is not scaled.
    CHECK(skillWith("Test Bolt", {"Has Blur", "+50% Blur Effect", "+20% additional damage while Blur is active"}).hitDps ==
          Approx(plain * 1.2));
}

TEST_CASE("Intimidated: +12% additional damage taken, scaled by Intimidating Effect") {
    const double plain = skillWith("Test Bolt", {}).hitDps;
    CHECK(skillWith("Test Bolt", {"Enemies within 12m are Intimidated"}).hitDps == Approx(plain * 1.12));
    CHECK(skillWith("Test Bolt", {"Enemies within 12m are Intimidated", "+50% Intimidating effect"}).hitDps ==
          Approx(plain * 1.18));
}

TEST_CASE("Lucky damage and min/max damage change the average roll") {
    const double plain = skillWith("Test Spark", {}).averageHit;   // 0-300 rolls 150 on average
    CHECK(skillWith("Test Spark", {"Lucky Damage"}).averageHit == Approx(plain * 200.0 / 150.0));
    CHECK(skillWith("Test Spark", {"+50% additional Max Damage"}).averageHit == Approx(plain * 225.0 / 150.0));
    CHECK(skillWith("Test Spark", {"Lucky Damage", "+50% additional Max Damage"}).averageHit == Approx(plain * 2.0));
}

TEST_CASE("Damage gained as another type is added on top and scaled by both types") {
    // Physical attack: the gained fire meets resistance and non-physical armor (0.7 x 0.7) instead of
    // 50% physical armor.
    const double plain = withLines({}).hitDps;
    CHECK(withLines({"Adds 50% of Physical Damage as Fire Damage"}).hitDps == Approx(plain * (1 + 0.5 * 0.49 / 0.5)));
    CHECK(withLines({"Adds 50% of Physical Damage as Fire Damage", "+100% additional Fire Damage"}).hitDps ==
          Approx(plain * (1 + 2 * 0.5 * 0.49 / 0.5)));
    CHECK(withLines({"Adds 50% of Physical Damage as Fire Damage", "+100% additional Physical Damage"}).hitDps ==
          Approx(plain * 2 * (1 + 0.5 * 0.49 / 0.5)));
    // Cold spell: fire meets the same mitigation as cold.
    const double bolt = skillWith("Test Bolt", {}).hitDps;
    CHECK(skillWith("Test Bolt", {"Adds 50% of Cold Damage as Fire Damage"}).hitDps == Approx(bolt * 1.5));
    CHECK(skillWith("Test Bolt", {"Adds 50% of Cold Damage as Fire Damage", "+100% additional Fire Damage"}).hitDps ==
          Approx(bolt * 2.0));
}

TEST_CASE("Youga build without Biting Cold matches the curse-off prediction") {
    nlohmann::json build = youga();
    auto& active = build["skills"]["active"];
    for (auto it = active.begin(); it != active.end(); ++it) {
        if ((*it)["name"] == "Biting Cold") {
            active.erase(it);
            break;
        }
    }
    SkillDpsResult r = calculateMainSkillDps(loadBuild(gameData(), build));
    describe(r);
    CHECK(r.dps == Approx(3.631e12).epsilon(0.005));
}
