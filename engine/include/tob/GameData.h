#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace tob {

// Game data scraped from tlidb (tools/scraper) and stored as JSON under data/json.
struct GameData {
    nlohmann::json boards;          // talent_boards.json: array of panels
    nlohmann::json skills;          // skills.json: name -> skill card + level table
    nlohmann::json traits;          // hero_traits.json: array of trait trees
    nlohmann::json alternateYouga;  // alternate_spacetime_youga.json: skill name -> lines

    // Throws std::runtime_error if a required file is missing or malformed.
    static GameData load(const std::string& dataDir);

    const nlohmann::json* board(const std::string& slug) const;
    // Core talents can appear outside their board (slates, blends), so search every board.
    const nlohmann::json* coreTalent(const std::string& name) const;
    // Name match ignoring case and punctuation ("Precise Spell Amplification" == "Precise: Spell Amplification").
    const nlohmann::json* skill(const std::string& name) const;
    const nlohmann::json* traitTree(const std::string& slug) const;
};

// Lowercase letters and digits only; used for forgiving name matching.
std::string looseKey(const std::string& name);

} // namespace tob
