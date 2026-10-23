// Headless check of the item crafter: opens it on saved craft choices for each item kind and prints the
// modifier lines it produces. Run with QT_QPA_PLATFORM=offscreen; exits non-zero on a failed check.
#include "CraftDialog.h"
#include "Editors.h"

#include "tob/Build.h"
#include "tob/GameData.h"
#include "tob/SkillDps.h"

#include <QApplication>
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

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
