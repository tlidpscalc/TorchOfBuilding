#pragma once

#include "tob/Mod.h"

#include <string>
#include <string_view>
#include <vector>

namespace tob {

struct ParseResult {
    std::string line;           // original text
    std::vector<Mod> mods;
    bool ok = false;
    bool inert = false;         // recognised, but has no effect on damage (defence, utility, skill text)
    bool multiplies = false;    // line carried "(multiplies)"
    bool supportScoped = false; // "... for the supported skill": the loader scopes it to that skill
    bool selfScoped = false;    // "... for this skill": applies to the skill whose card it is on
    bool playerBuff = false;    // a buff the skill grants to the player: applies to every skill
    std::string error;          // why parsing failed, for the UI and coverage report
};

// Turns one line of modifier text (as written in game / on tlidb) into mods.
// Unsupported lines return ok=false; callers should show them, not drop them (like PoB's red mods).
ParseResult parseModLine(std::string_view line, const std::string& source = {});

// One ParseResult per non-empty line.
std::vector<ParseResult> parseModText(std::string_view text, const std::string& source = {});

// Canonical names so that different wordings of the same thing match, e.g.
// "stack of Focus Blessing owned" and "every stack of Focus Blessing" -> "focus blessing".
std::string canonicalVar(std::string text);
std::string canonicalFlag(std::string text);

} // namespace tob
