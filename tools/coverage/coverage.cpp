// Parser coverage report: runs every modifier line in the scraped tlidb data through the engine's
// parser and writes a Markdown summary plus a ranked list of unsupported wordings.
//
// Usage: tob_coverage <data dir> <output.md>

#include "tob/Build.h"
#include "tob/GameData.h"
#include "tob/ModParser.h"
#include "tob/SkillDps.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

using namespace tob;

namespace {

struct Line {
    std::string category;
    std::string source;
    std::string text;
};

nlohmann::json readJson(const std::string& path) {
    std::ifstream in(path);
    if (!in) return nlohmann::json();
    return nlohmann::json::parse(in);
}

// Legendary affixes are ranges: "+(340–408) gear Energy Shield". Use the top of the range.
std::string resolveRanges(const std::string& s) {
    static const std::regex kRange(R"(\((-?\d+(?:\.\d+)?)(?:\xE2\x80\x93|-)(-?\d+(?:\.\d+)?)\))");
    return std::regex_replace(s, kRange, "$2");
}

// Wording shape: numbers replaced by '#', so lines that differ only in values group together.
std::string shapeOf(const std::string& s) {
    static const std::regex kNumber(R"([+-]?\d+(?:\.\d+)?)");
    return std::regex_replace(normalizeText(s), kNumber, "#");
}

void collectSkills(const nlohmann::json& skills, std::vector<Line>& out) {
    for (auto it = skills.begin(); it != skills.end(); ++it) {
        const auto& s = it.value();
        bool support = false;
        for (const auto& t : s.value("tags", nlohmann::json::array())) support |= t.get<std::string>() == "Support";
        const std::string category = support ? "Support skills" : "Skills";
        const auto& levels = s.value("levels", nlohmann::json::array());
        if (!levels.empty()) {
            const auto& row = levels[std::min<std::size_t>(19, levels.size() - 1)];
            for (const auto& l : row["text"]) out.push_back({category, it.key(), l.get<std::string>()});
        } else {
            // Canvas supports and skills without a level table: use the card description.
            std::set<std::string> seen;
            for (const auto& block : s.value("description", nlohmann::json::array()))
                for (const auto& l : block)
                    if (seen.insert(l.get<std::string>()).second) out.push_back({category, it.key(), l.get<std::string>()});
        }
    }
}

void collectLegendaries(const nlohmann::json& legendaries, std::vector<Line>& out) {
    for (auto it = legendaries.begin(); it != legendaries.end(); ++it) {
        for (auto v = it.value()["variants"].begin(); v != it.value()["variants"].end(); ++v)
            for (const auto& l : v.value()) out.push_back({"Legendary gear", it.key(), l.get<std::string>()});
    }
}

void collectBoards(const nlohmann::json& boards, std::vector<Line>& out) {
    for (const auto& b : boards) {
        const std::string name = b.value("name", "");
        for (const auto& n : b["nodes"])
            for (const auto& l : n["mods"]) out.push_back({"Talent nodes", name, l.get<std::string>()});
        for (const auto& c : b["core_talents"])
            for (const auto& l : c["mods"]) out.push_back({"Core talents", name + ": " + c.value("name", ""), l.get<std::string>()});
    }
}

void collectTraits(const nlohmann::json& traits, std::vector<Line>& out) {
    for (const auto& t : traits) {
        for (const auto& n : t["nodes"]) {
            const auto& levels = n["levels"];
            if (levels.empty()) continue;
            const auto& top = levels.back();   // highest level shows every line the trait has
            for (const char* key : {"affix", "server_affix"})
                for (const auto& l : top[key]) out.push_back({"Hero traits", n.value("name", ""), l.get<std::string>()});
        }
    }
}

} // namespace

// One build's lines the calculator leaves out: unsupported, or parsed into stats it does not use.
int reportBuild(const std::string& dataDir, const std::string& buildFile) {
    const tob::GameData data = tob::GameData::load(dataDir);
    const tob::BuildState b = tob::loadBuildFile(data, buildFile);
    int unsupported = 0, unused = 0;
    for (const tob::LineReport& l : b.lines) {
        if (!l.ok) {
            std::cout << "UNSUPPORTED  " << l.source << " | " << l.line << " | " << l.error << "\n";
            ++unsupported;
            continue;
        }
        if (l.inert) continue;
        std::set<std::string> stats;
        for (const tob::Mod& m : tob::parseModLine(l.line).mods)
            if (!tob::isModelledStat(m.stat)) stats.insert(m.stat);
        if (stats.empty()) continue;
        std::string list;
        for (const std::string& s : stats) list += (list.empty() ? "" : ", ") + s;
        std::cout << "NOT MODELLED " << l.source << " | " << l.line << " | " << list << "\n";
        ++unused;
    }
    std::cout << unsupported << " unsupported, " << unused << " parsed but not used\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[2]) == "--build") return reportBuild(argv[1], argv[3]);
    if (argc < 3) {
        std::cerr << "usage: tob_coverage <data dir> <output.md>\n"
                     "       tob_coverage <data dir> --build <build.json>\n";
        return 2;
    }
    const std::string dir = std::string(argv[1]) + "/json/";
    std::vector<Line> lines;
    collectSkills(readJson(dir + "skills.json"), lines);
    collectLegendaries(readJson(dir + "legendaries.json"), lines);
    collectBoards(readJson(dir + "talent_boards.json"), lines);
    collectTraits(readJson(dir + "hero_traits.json"), lines);

    struct Tally {
        int total = 0, effect = 0, unused = 0, inert = 0, failed = 0;
    };
    std::map<std::string, Tally> byCategory;
    struct Gap {
        int count = 0;
        std::string example, source, error;
        std::set<std::string> categories;
    };
    std::map<std::string, Gap> gaps;
    std::map<std::string, int> unusedStats;

    for (const Line& line : lines) {
        if (normalizeText(line.text).empty()) continue;
        // tlidb shows rolled values as ranges everywhere ("+(16–19)%"); parse the top of the range.
        ParseResult r = parseModLine(resolveRanges(line.text));
        Tally& t = byCategory[line.category];
        ++t.total;
        if (!r.ok) {
            ++t.failed;
            Gap& g = gaps[shapeOf(line.text)];
            if (g.count++ == 0) {
                g.example = line.text;
                g.source = line.source;
                g.error = r.error;
            }
            g.categories.insert(line.category);
        } else if (r.inert) {
            ++t.inert;
        } else if (std::any_of(r.mods.begin(), r.mods.end(), [](const Mod& m) { return isModelledStat(m.stat); })) {
            ++t.effect;
        } else {
            ++t.unused;
            for (const Mod& m : r.mods) ++unusedStats[m.stat];
        }
    }

    std::vector<std::pair<std::string, Gap>> ranked(gaps.begin(), gaps.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second.count > b.second.count; });

    std::ofstream md(argv[2]);
    Tally all;
    md << "# Parser coverage report\n\n"
       << "Generated by `tools/coverage` from the scraped tlidb data in `data/json`. "
          "\"Modelled\" lines change the DPS calculation; \"parsed, not modelled\" lines are understood but feed a "
          "mechanic the calculator does not use yet; \"no DPS effect\" lines are recognised as defensive or descriptive; "
          "\"unsupported\" lines need a new parser pattern or mechanic.\n\n"
       << "| Source | Lines | Modelled | Parsed, not modelled | No DPS effect | Unsupported | Recognised |\n|---|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& [cat, t] : byCategory) {
        md << "| " << cat << " | " << t.total << " | " << t.effect << " | " << t.unused << " | " << t.inert << " | " << t.failed << " | "
           << (t.total ? 100 * (t.total - t.failed) / t.total : 0) << "% |\n";
        all.total += t.total;
        all.effect += t.effect;
        all.unused += t.unused;
        all.inert += t.inert;
        all.failed += t.failed;
    }
    md << "| **Total** | " << all.total << " | " << all.effect << " | " << all.unused << " | " << all.inert << " | " << all.failed << " | **"
       << (all.total ? 100 * (all.total - all.failed) / all.total : 0) << "%** |\n\n";

    md << "## Parsed but not modelled yet (by stat)\n\n| Stat | Lines |\n|---|---:|\n";
    std::vector<std::pair<std::string, int>> unusedRanked(unusedStats.begin(), unusedStats.end());
    std::sort(unusedRanked.begin(), unusedRanked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [stat, n] : unusedRanked) md << "| " << stat << " | " << n << " |\n";
    md << "\n";

    md << "## Unsupported wordings, most common first\n\n"
       << ranked.size() << " distinct wordings (numbers replaced by `#`).\n\n"
       << "| # | Lines | Wording | Example (source) | Where |\n|---:|---:|---|---|---|\n";
    int rank = 0;
    for (const auto& [shape, g] : ranked) {
        std::string where;
        for (const auto& c : g.categories) where += (where.empty() ? "" : ", ") + c;
        auto esc = [](std::string s) {
            for (std::size_t p = 0; (p = s.find('|', p)) != std::string::npos; p += 2) s.replace(p, 1, "\\|");
            return s;
        };
        md << "| " << ++rank << " | " << g.count << " | `" << esc(shape) << "` | " << esc(g.example) << " (" << esc(g.source)
           << ") | " << where << " |\n";
    }
    std::cout << "lines " << all.total << ", modelled " << all.effect << ", parsed not modelled " << all.unused << ", no DPS effect " << all.inert << ", unsupported "
              << all.failed << " (" << ranked.size() << " wordings) -> " << argv[2] << "\n";
    return 0;
}
