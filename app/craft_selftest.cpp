// Headless check of the item crafter: opens it on saved craft choices for each item kind and prints the
// modifier lines it produces. Run with QT_QPA_PLATFORM=offscreen; exits non-zero on a failed check.
#include "CraftDialog.h"
#include "Editors.h"
#include "HeroEditor.h"

#include "tob/Build.h"
#include "tob/GameData.h"
#include "tob/SkillDps.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

nlohmann::json readJson(const std::filesystem::path& p) {
    std::ifstream in(p);
    return nlohmann::json::parse(in);
}

int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    failures += ok ? 0 : 1;
}

nlohmann::json craft(const CraftData& data, const char* slot, const nlohmann::json& craftBlock) {
    CraftDialog dialog(data, slot, {{"slot", slot}, {"name", "x"}, {"lines", nlohmann::json::array()}, {"craft", craftBlock}});
    nlohmann::json item = dialog.item();
    std::cout << slot << ": " << item.value("name", "") << "\n";
    for (const auto& l : item["lines"]) std::cout << "    " << l.get<std::string>() << "\n";
    return item;
}

bool hasLine(const nlohmann::json& item, const std::string& text) {
    for (const auto& l : item["lines"])
        if (l.get<std::string>() == text) return true;
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const std::filesystem::path json = std::filesystem::path(argc > 1 ? argv[1] : "data") / "json";
    CraftData data{readJson(json / "gear_types.json"), readJson(json / "vorax.json"), readJson(json / "crafting.json"),
                   readJson(json / "legendaries.json")};

    // Crafted helmet: Max Life prefix at T1 with a chosen roll, upgraded once by Arrogance (T1 -> T0).
    const auto& helmet = data.gearTypes["STR Helmet"];
    long long lifeFamily = 0;
    for (const auto& r : helmet["prefixes"])
        if (r["text"].get<std::string>().find("Max Life") != std::string::npos && r["library"] == "Basic") {
            lifeFamily = r["family"].get<long long>();
            break;
        }
    nlohmann::json crafted = {{"kind", "crafted"}, {"type", "STR Helmet"}, {"base", "Ranger's Broken Mask"}, {"ilvl", 100},
                              {"corrosion", "arrogance"},
                              {"base_affixes", nlohmann::json::array()},
                              {"prefixes", {{{"family", lifeFamily}, {"tier", "1"}, {"upgrade", 1}, {"values", {250}}}}},
                              {"suffixes", nlohmann::json::array()}};
    nlohmann::json item = craft(data, "Helmet", crafted);
    check(hasLine(item, "+1920 Gear Armor"), "base implicit line kept");
    check(hasLine(item, "+250 Max Life"), "Arrogance-upgraded Max Life (T0 range) with the chosen roll");
    check(item["craft"]["prefixes"][0]["upgrade"] == 1, "upgrade saved");

    // Vorax chest with a Ghost Slaughter organ line corroded by Hyperplastic.
    nlohmann::json vorax = {{"kind", "vorax"}, {"type", "Vorax Limb: Chest"}, {"ilvl", 100}, {"corrosion", "hyperplastic"},
                            {"organ", {{"legendary", "Ghost Slaughter"},
                                       {"lines", {{{"text", "Loses Fervor at Low Life"}, {"corroded", true}, {"values", nlohmann::json::array()}}}}}}};
    item = craft(data, "Chest", vorax);
    check(hasLine(item, "Consumes all Fervor Rating when at Low Life. Restores 0.5% Life for every Fervor Rating consumed. Interval: 4s."),
          "corroded organ line uses its counterpart");

    // Ghost Slaughter with Desecration: two lines corroded, implicit kept.
    nlohmann::json legendary = {{"kind", "legendary"}, {"corrosion", "desecration"},
                                {"legendary", {{"name", "Ghost Slaughter"},
                                               {"lines", {{{"corroded", true}, {"values", {400}}},
                                                          {{"corroded", true}, {"values", {40}}},
                                                          {{"corroded", false}, {"values", nlohmann::json::array()}},
                                                          {{"corroded", false}, {"values", {11}}},
                                                          {{"corroded", false}, {"values", nlohmann::json::array()}}}}}}};
    item = craft(data, "Gloves", legendary);
    check(hasLine(item, "+1777 Gear Armor"), "legendary implicit kept");
    check(hasLine(item, "+400 Max Life"), "corroded Max Life with roll");
    check(hasLine(item, "+40% Skill Area"), "corroded line's added stat is its own line");
    check(hasLine(item, "Consumes 11% of current Life and Energy Shield per second while Fervor is active"), "uncorroded roll");

    // An old build file moves its items into the inventory, still worn, with the same DPS.
    const std::filesystem::path dataDir = std::filesystem::path(argc > 1 ? argv[1] : "data");
    const tob::GameData game = tob::GameData::load(dataDir.string());
    nlohmann::json build = readJson(dataDir / "builds" / "youga_ultimate_one.json");
    const double before = tob::calculateMainSkillDps(tob::loadBuild(game, build)).dps;
    GearEditor gear(data);
    gear.setBuild(&build);
    const double after = tob::calculateMainSkillDps(tob::loadBuild(game, build)).dps;
    std::cout << "Youga DPS before " << before << ", after inventory migration " << after << "\n";
    check(build["inventory"].size() == build["items"].size(), "every item is in the inventory and worn");
    check(std::abs(after - before) <= before * 1e-9, "same DPS after migration");

    // Skills move into the game's slots without changing DPS; canvas supports go to their slots.
    SkillsEditor::normalize(build, game.skills);
    // Youga's "I Am the Ultimate One" puts Clockwork Core in the main skill's second support slot.
    build["skills"]["active"][0]["supports"][1] = {{"name", "Multiple Projectiles"}};
    check(SkillsEditor::applyTraitSupports(build, game.skills, game.traits), "trait restores Clockwork Core");
    check(build["skills"]["active"][0]["supports"][1].value("name", "") == "Clockwork Core", "Clockwork Core in slot 2");
    build["skills"]["active"][0]["supports"][1] = {{"name", "Clockwork Core"}};
    build["skills"]["active"][0]["supports"][3] = {{"name", "Multiple Projectiles"}};
    const double slotted = tob::calculateMainSkillDps(tob::loadBuild(game, build)).dps;
    const auto& hfb = build["skills"]["active"][0];
    std::cout << "Homing Frost Bolt support slots:";
    for (const auto& s : hfb["supports"]) std::cout << " [" << (s.is_object() ? s.value("name", "") : "-") << "]";
    std::cout << "\n";
    check(hfb.value("name", "") == "Homing Frost Bolt", "main skill in slot 1");
    check(hfb["supports"].size() == 5, "5 support slots");
    check(hfb["supports"][2].value("name", "") == "Homing Frost Bolt: Frost Barb (Magnificent)", "Magnificent in slot 3");
    check(hfb["supports"][4].value("name", "") == "Homing Frost Bolt: Deep Chill (Noble)", "Noble in slot 5");
    check(build["skills"]["active"][1]["supports"][0].value("name", "") == "Activation Medium: Preparation",
          "Activation Medium in slot 1");
    check(std::abs(slotted - before) <= before * 1e-9, "same DPS after slotting skills");

    // Hero Memory base stats against the owner's in-game readings.
    struct Reading { const char* stat; int rarity; int level; double value; };
    for (const Reading& r : {Reading{"+90 Intelligence", 4, 50, 110}, Reading{"+90 Intelligence", 4, 21, 67},
                             Reading{"+90 Intelligence", 4, 10, 50}, Reading{"+90 Intelligence", 4, 1, 37},
                             Reading{"+90 Intelligence", 3, 20, 59}, Reading{"+90 Intelligence", 3, 40, 90},
                             Reading{"+90 Intelligence", 2, 12, 41}, Reading{"+90 Intelligence", 2, 30, 70},
                             Reading{"+90 Intelligence", 1, 1, 17}, Reading{"+90 Intelligence", 1, 20, 50},
                             Reading{"+90 Intelligence", 0, 5, 19}, Reading{"+90 Intelligence", 0, 10, 30},
                             Reading{"+330 Max Life", 4, 50, 403}, Reading{"+525 Max Energy Shield", 4, 50, 642},
                             Reading{"+180 Max Mana", 4, 50, 220}, Reading{"+4320 Armor", 4, 50, 5280},
                             Reading{"+36% Cast Speed", 4, 50, 44}, Reading{"+30% Movement Speed", 4, 50, 30},
                             Reading{"+30% Movement Speed", 3, 40, 25}, Reading{"+90% damage", 4, 50, 132},
                             Reading{"+90% damage", 3, 40, 108}}) {
        const double v = HeroEditor::baseStatValue(r.stat, r.rarity, r.level);
        check(v == r.value, std::string(r.stat) + " rarity " + std::to_string(r.rarity) + " level " +
                                std::to_string(r.level) + " = " + std::to_string(static_cast<int>(v)));
    }

    // The Hero tab keeps an older build's traits and memories (custom lines, trait level 3).
    nlohmann::json heroBuild = readJson(dataDir / "builds" / "youga_ultimate_one.json");
    const nlohmann::json nodesBefore = heroBuild["traits"]["nodes"];
    HeroEditor hero(game.traits, data.crafting);
    hero.setBuild(&heroBuild);
    check(heroBuild["traits"]["nodes"] == nodesBefore, "trait nodes and levels unchanged");
    // Base stats are listed by name only; their value comes from rarity and level.
    QStringList baseNames;
    for (QComboBox* box : hero.findChildren<QComboBox*>())
        for (int i = 0; i < box->count(); ++i)
            if (box->itemData(i).toString() == "+330 Max Life") baseNames << box->itemText(i);
    check(!baseNames.isEmpty() && baseNames.front() == "Max Life", "base stat listed as \"Max Life\"");
    check(std::abs(tob::calculateMainSkillDps(tob::loadBuild(game, heroBuild)).dps - before) <= before * 1e-9,
          "same DPS after the Hero tab loads the build");

    // Creative Genius: one node from each of the two groups at 45, 60 and 75, all six kept.
    nlohmann::json genius = {{"traits", {{"tree", "Creative_Genius"},
                                         {"picks", {"Inspiration Overflow", "Mind Domain", "Brainstorm"}},
                                         {"picks_b", {"Super Sonic Protocol", "Law of Ingenuity", "Hyper-Resonance Hypothesis"}}}}};
    HeroEditor geniusHero(game.traits, data.crafting);
    geniusHero.setBuild(&genius);
    std::cout << "Creative Genius nodes: " << genius["traits"]["nodes"].dump() << "\n";
    check(genius["traits"]["nodes"].size() == 7, "Creative Genius keeps base + 3 group A + 3 group B picks");

    // An Ultimate memory offers every tier down to T0 for its random affixes.
    heroBuild["hero_memories"][0]["memory"] = {{"rarity", 4}, {"level", 50}, {"base", 2},
                                               {"fixed", nlohmann::json::array()},
                                               {"random", {{{"family", "+#% Skill Area"}, {"tier", "0"}, {"values", {45}}}}}};
    hero.setBuild(&heroBuild);
    QStringList tierLists;
    for (QComboBox* box : hero.findChildren<QComboBox*>()) {
        QStringList items;
        for (int i = 0; i < box->count(); ++i) items << box->itemText(i);
        if (items.contains("T1") || items.contains("T0")) tierLists << items.join(",");
    }
    std::cout << "tier lists: " << tierLists.join(" | ").toStdString() << "\n";
    check(std::any_of(tierLists.begin(), tierLists.end(), [](const QString& s) { return s.startsWith("T0"); }),
          "Ultimate random affix offers T0");

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
