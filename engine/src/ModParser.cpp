#include "tob/ModParser.h"

#include <cctype>
#include <functional>
#include <regex>
#include <unordered_map>

namespace tob {

namespace {

constexpr const char* kNum = R"(([+-]?\d+(?:\.\d+)?))";
constexpr const char* kQual = R"(((?:[a-z]+ )*))";   // zero or more qualifier words, each followed by a space

std::regex re(const std::string& pattern) {
    return std::regex(pattern, std::regex::ECMAScript | std::regex::optimize);
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '.' || s.back() == ',')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

bool contains(const std::string& s, std::string_view part) {
    return s.find(part) != std::string::npos;
}

// Removes every occurrence of `part`; returns true if anything was removed.
bool strip(std::string& s, std::string_view part) {
    bool any = false;
    for (std::size_t pos; (pos = s.find(part)) != std::string::npos;) {
        s.erase(pos, part.size());
        any = true;
    }
    return any;
}

bool stripPrefix(std::string& s, std::string_view prefix) {
    if (s.rfind(prefix, 0) != 0) return false;
    s.erase(0, prefix.size());
    return true;
}

// Applies qualifier words such as "attack", "spell", "fire", "elemental", "hit" to a mod.
// With "skill(s)" present ("Cold Skill Critical Strike Damage"), damage type words mean the
// skill's tag rather than the damage type of the portion being scaled.
bool applyQualifiers(const std::string& words, Mod& mod, std::string& error) {
    const bool skillQualified = (' ' + words).find(" skill") != std::string::npos;
    std::size_t i = 0;
    while (i < words.size()) {
        std::size_t end = words.find(' ', i);
        if (end == std::string::npos) end = words.size();
        std::string w = words.substr(i, end - i);
        i = end + 1;
        if (w.empty()) continue;

        if (w == "hit") {
            if (mod.stat == "Damage") mod.stat = "HitDamage";
            continue;
        }
        if (w == "skill" || w == "skills" || w == "all" || w == "and" || w == "the") continue;
        if (auto mask = damageTypeMaskFromString(w)) {
            if (!skillQualified) {
                mod.damageTypes |= *mask;
                continue;
            }
            // "Elemental Skill" means any of the Fire/Cold/Lightning tags (confirmed), which needs
            // any-of tag matching. No such mod exists in the current game, so it stays unsupported.
            if (auto tag = skillTagFromString(w)) {
                mod.requiredTags |= bit(*tag);
                continue;
            }
            error = "'" + w + " skill' qualifier not supported yet";
            return false;
        }
        // Plural forms used in game text: "attacks", "spells", "projectiles", "minions".
        std::string singular = (w.size() > 1 && w.back() == 's') ? w.substr(0, w.size() - 1) : w;
        if (auto tag = skillTagFromString(w)) {
            mod.requiredTags |= bit(*tag);
            continue;
        }
        if (auto tag = skillTagFromString(singular)) {
            mod.requiredTags |= bit(*tag);
            continue;
        }
        error = "unknown qualifier '" + w + "'";
        return false;
    }
    return true;
}

struct Rule {
    std::regex pattern;
    // Fills `mods` from the match; returns false with `error` set if the match is unusable.
    std::function<bool(const std::smatch&, std::vector<Mod>&, std::string&)> build;
};

Mod make(std::string stat, ModType type, double value) {
    Mod m;
    m.stat = std::move(stat);
    m.type = type;
    m.value = value;
    return m;
}

// Rule for "<num>% [additional] <qualifiers> <stat words>" style lines.
Rule qualifiedRule(const std::string& suffix, std::string stat, ModType type, bool percent = true) {
    std::string pattern = std::string("^") + kNum + (percent ? "%" : "") +
                          (type == ModType::Additional ? " additional " : " ") + kQual + suffix + "$";
    return Rule{re(pattern), [stat, type](const std::smatch& m, std::vector<Mod>& out, std::string& err) {
                    Mod mod = make(stat, type, std::stod(m[1].str()));
                    if (!applyQualifiers(m[2].str(), mod, err)) return false;
                    out.push_back(std::move(mod));
                    return true;
                }};
}

// "+X% <words>" / "+X <words>" lines whose words name a single stat.
struct StatWords {
    const char* words;
    const char* stat;
    ModType type;
};

constexpr StatWords kPercentStats[] = {
    {"projectile speed", "ProjectileSpeed", ModType::Increased},
    {"skill effect duration", "SkillEffectDuration", ModType::Increased},
    {"duration", "SkillEffectDuration", ModType::Increased},
    {"cooldown recovery speed", "CooldownRecoverySpeed", ModType::Increased},
    {"sealed mana compensation", "SealedManaCompensation", ModType::Increased},
    {"aura effect", "AuraEffect", ModType::Increased},
    {"curse effect", "CurseEffect", ModType::Increased},
    {"frostbite effect", "FrostbiteEffect", ModType::Increased},
    {"cold infiltration effect", "ColdInfiltrationEffect", ModType::Increased},
    {"spell aggression effect", "SpellAggressionEffect", ModType::Increased},
    {"night feather entanglement effect", "NightFeatherEffect", ModType::Increased},
    {"effect for selves of all realms", "SelvesEffect", ModType::Increased},
    {"attack block chance", "AttackBlock", ModType::Base},
    {"spell block chance", "SpellBlock", ModType::Base},
    {"chance to deal double damage", "DoubleDamageChance", ModType::Base},
    {"intelligence", "Intelligence", ModType::Increased},
    {"strength", "Strength", ModType::Increased},
    {"dexterity", "Dexterity", ModType::Increased},
};

constexpr StatWords kFlatStats[] = {
    {"max tangle quantity", "MaxTangle", ModType::Base},
    {"to max frostbite rating", "FrostbiteRating", ModType::Base},
    {"to max focus blessing stacks", "MaxFocusBlessing", ModType::Base},
    {"to max tenacity blessing stacks", "MaxTenacityBlessing", ModType::Base},
    {"to max agility blessing stacks", "MaxAgilityBlessing", ModType::Base},
    {"intelligence", "Intelligence", ModType::Base},
    {"strength", "Strength", ModType::Base},
    {"dexterity", "Dexterity", ModType::Base},
    {"projectile quantity", "ProjectileQuantity", ModType::Base},
};

// Exact (normalized) lines that set a flag stat.
const std::unordered_map<std::string, std::string>& flagLines() {
    static const std::unordered_map<std::string, std::string> k = {
        {"inflicts frostbite when dealing hit cold damage", "InflictFrostbite"},
        {"inflicts cold infiltration when dealing damage", "InflictColdInfiltration"},
        {"inflicts frail when dealing spell damage", "InflictFrail"},
        {"gains spell aggression when casting a spell skill", "GainSpellAggression"},
        {"has hasten", "Hasten"},
        {"gains 1 stack of night feather entanglement for every 1 active tangle", "NightFeatherSource"},
        {"gains 1 stack of focus blessing every 1 s when night feather entanglement reaches max stacks",
         "FocusBlessingSource"},
        {"lucky critical strike", "LuckyCrit"},
        // Off-hand does not attack; its damage is added to the main-hand weapon's base damage.
        {"conspire", "Conspire"},
        // Same, with 60% of the off-hand's damage.
        {"joined forces", "JoinedForces"},
    };
    return k;
}

// Lines recognised as having no effect on damage: defence, utility and descriptive skill text.
const std::vector<std::regex>& inertPatterns() {
    static const std::vector<std::regex> k = {
        re(R"(\b(life|energy shield|mana|evasion|armor|resistance|movement speed|block ratio|skill area|)"
           R"(skill cost|minion|minions|barrier|defense|xp|avoid|immune|regenerates|restores|blessing duration|)"
           R"(parabolic|knockback|blind|slow|max charges|damage over time taken|damage taken|crowd control|ailment duration)\b)"),
        re(R"(^(homing frost bolt:|deals \d+-\d+ |generates \d+ additional|base condensation time|cast speed affects|)"
           R"(this skill's shotgun effect|casts the skill|casts this skill|lasts \d|activates the aura|supports |)"
           R"(non-main skills|tanglable main|bonuses and additional bonuses to cooldown|duration increases apply|)"
           R"(spacetime overlord|you can only deal|nearby enemies are guaranteed|consumes \d+% of current mana|)"
           R"(has dormant entanglement|automatically summons|prepares the supported skill|)"
           R"(the supported curse skill becomes|replaces sealed mana|this skill can only be installed|)"
           R"(there is a \d+% chance for the supported skill to release|secondary icy blades|)"
           R"(cannot be supported|energy shield charge|\+?\d+ penetrations?$|\+?\d+ to parabolic|)"
           R"(supports? |the buff lasts|<.*>$|deals .*weapon attack damage|deals .* damage equal to |lasts for |)"
           R"(skill's max base |the skill fires |the skill's shotgun effect|cannot support |)"
           R"(defeating enemies can increase))"),
        // Component labels on skill cards ("Icy Lances:", "Artificial Moon:"); the loader reads them.
        re(R"(^[a-z' -]+:$)"),
    };
    return k;
}

const std::vector<Rule>& rules() {
    static const std::vector<Rule> kRules = [] {
        std::vector<Rule> r;

        // Special multiplicative buckets, e.g. "+20% Tangle Damage Enhancement".
        r.push_back(Rule{re(std::string("^") + kNum + R"(% ((?:[a-z]+ )+)damage enhancement$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             Mod mod = make("Damage", ModType::Bucket, std::stod(m[1].str()));
                             mod.group = trim(m[2].str()) + " damage enhancement";
                             out.push_back(std::move(mod));
                             return true;
                         }});

        // Enemy debuffs: "Cursed enemies +47.5% additional Cold Damage taken", "Enemies take +5% additional damage".
        r.push_back(Rule{re(std::string("^enemies (?:take )?") + kNum + R"(% additional )" + kQual +
                            "damage(?: taken)?$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string& err) {
                             Mod mod = make("DamageTaken", ModType::Additional, std::stod(m[1].str()));
                             if (!applyQualifiers(m[2].str(), mod, err)) return false;
                             out.push_back(std::move(mod));
                             return true;
                         }});

        // "+18% additional damage on Critical Strike": multiplies crit hits only.
        r.push_back(Rule{re(std::string("^") + kNum + R"(% additional damage on critical strike$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("CritHitDamage", ModType::Additional, std::stod(m[1].str())));
                             return true;
                         }});

        // Gale: "60% of the Projectile Speed bonus is also applied to the additional bonus for Projectile Damage".
        r.push_back(Rule{re(R"(^(\d+(?:\.\d+)?)% of the (.+?) bonus is also applied to the additional bonus for )" +
                            std::string(kQual) + "damage$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string& err) {
                             Mod mod = make("Damage", ModType::Additional, std::stod(m[1].str()) / 100.0);
                             if (!applyQualifiers(m[3].str(), mod, err)) return false;
                             mod.per = PerStat{canonicalVar(m[2].str()), 1.0, std::nullopt, false, std::nullopt};
                             out.push_back(std::move(mod));
                             return true;
                         }});

        // "100% of bonuses to Cooldown Recovery Speed or Skill Effect Duration also apply to
        //  Clockwork Core's additional damage" (cap handled by the generic "up to" clause).
        r.push_back(Rule{re(R"(^(\d+(?:\.\d+)?)% of bonuses to (.+?) or (.+?) also apply to (\w+)'s additional damage$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string& err) {
                             Mod mod = make("Damage", ModType::Additional, std::stod(m[1].str()) / 100.0);
                             if (!applyQualifiers(m[4].str() + " ", mod, err)) return false;
                             mod.per = PerStat{canonicalVar(m[2].str()) + " + " + canonicalVar(m[3].str()), 1.0,
                                               std::nullopt, false, std::nullopt};
                             out.push_back(std::move(mod));
                             return true;
                         }});

        // "Adds 10 - 20 Fire Damage to Attacks"; "... to the gear" is local to the weapon it is on.
        r.push_back(Rule{re(std::string("^adds ") + kNum + " ?- ?" + kNum +
                            R"( ([a-z]+) damage(?: to (attacks and spells|attacks|spells|the gear))?$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string& err) {
                             auto mask = damageTypeMaskFromString(m[3].str());
                             if (!mask) {
                                 err = "unknown damage type '" + m[3].str() + "'";
                                 return false;
                             }
                             const std::string target = m[4].matched ? m[4].str() : "";
                             TagMask tags = 0;
                             if (target == "attacks") tags = bit(SkillTag::Attack);
                             if (target == "spells") tags = bit(SkillTag::Spell);
                             const bool local = target == "the gear";
                             Mod lo = make(local ? "WeaponAddedMin" : "AddedDamageMin", ModType::Base, std::stod(m[1].str()));
                             Mod hi = make(local ? "WeaponAddedMax" : "AddedDamageMax", ModType::Base, std::stod(m[2].str()));
                             lo.damageTypes = hi.damageTypes = *mask;
                             lo.requiredTags = hi.requiredTags = tags;
                             out.push_back(std::move(lo));
                             out.push_back(std::move(hi));
                             return true;
                         }});

        // Weapon base stats and local ("gear") modifiers. They only affect the weapon they are on.
        r.push_back(Rule{re(std::string("^") + kNum + " ?- ?" + kNum + R"( ([a-z]+) damage$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string& err) {
                             auto mask = damageTypeMaskFromString(m[3].str());
                             if (!mask) {
                                 err = "unknown damage type '" + m[3].str() + "'";
                                 return false;
                             }
                             Mod lo = make("WeaponBaseMin", ModType::Base, std::stod(m[1].str()));
                             Mod hi = make("WeaponBaseMax", ModType::Base, std::stod(m[2].str()));
                             lo.damageTypes = hi.damageTypes = *mask;
                             out.push_back(std::move(lo));
                             out.push_back(std::move(hi));
                             return true;
                         }});
        r.push_back(Rule{re(std::string("^") + kNum + " attack speed$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("WeaponAttackSpeed", ModType::Base, std::stod(m[1].str())));
                             return true;
                         }});
        r.push_back(Rule{re(std::string("^") + kNum + R"(% gear ([a-z]+) damage$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string& err) {
                             Mod mod = make("WeaponDamage", ModType::Increased, std::stod(m[1].str()));
                             return applyQualifiers(m[2].str() + " ", mod, err) && (out.push_back(std::move(mod)), true);
                         }});
        r.push_back(Rule{re(std::string("^") + kNum + "% gear attack speed$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("WeaponAttackSpeed", ModType::Increased, std::stod(m[1].str())));
                             return true;
                         }});
        r.push_back(Rule{re(std::string("^") + kNum + "% (?:attack )?critical strike rating for this gear$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("WeaponCritRating", ModType::Increased, std::stod(m[1].str())));
                             return true;
                         }});

        // Attributes: "+12% All Stats", "+30 Strength and Intelligence".
        r.push_back(Rule{re(std::string("^") + kNum + "% all stats$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             for (const char* s : {"Strength", "Dexterity", "Intelligence"})
                                 out.push_back(make(s, ModType::Increased, std::stod(m[1].str())));
                             return true;
                         }});
        r.push_back(Rule{re(std::string("^") + kNum + "(%?) (strength|dexterity|intelligence) and (strength|dexterity|intelligence)$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             const ModType t = m[2].length() ? ModType::Increased : ModType::Base;
                             for (int g : {3, 4}) {
                                 std::string name = m[g].str();
                                 name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
                                 out.push_back(make(name, t, std::stod(m[1].str())));
                             }
                             return true;
                         }});

        // Counts the calculator does not use yet, recorded so the coverage report can show them.
        r.push_back(Rule{re(std::string("^") + kNum + " max spell burst$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("MaxSpellBurst", ModType::Base, std::stod(m[1].str())));
                             return true;
                         }});
        r.push_back(Rule{re(std::string("^you can cast ") + kNum + " additional curses?$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("MaxCurses", ModType::Base, std::stod(m[1].str())));
                             return true;
                         }});

        r.push_back(qualifiedRule("critical strike rating", "CritRating", ModType::Additional));
        r.push_back(qualifiedRule("critical strike rating", "CritRating", ModType::Increased));
        r.push_back(qualifiedRule("critical strike rating", "CritRating", ModType::Base, /*percent=*/false));
        // Points added onto the base 150% crit damage. There is no "additional" crit damage stat
        // in game; if one appears it falls through as unsupported so it gets looked at.
        r.push_back(qualifiedRule("critical strike damage", "CritDamage", ModType::Base));

        // "+3% Attack and Cast Speed" raises both.
        for (ModType t : {ModType::Additional, ModType::Increased}) {
            r.push_back(Rule{re(std::string("^") + kNum + (t == ModType::Additional ? "% additional " : "% ") +
                                "attack and cast speed$"),
                             [t](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                                 out.push_back(make("AttackSpeed", t, std::stod(m[1].str())));
                                 out.push_back(make("CastSpeed", t, std::stod(m[1].str())));
                                 return true;
                             }});
        }
        r.push_back(qualifiedRule("attack speed", "AttackSpeed", ModType::Additional));
        r.push_back(qualifiedRule("attack speed", "AttackSpeed", ModType::Increased));
        r.push_back(qualifiedRule("cast speed", "CastSpeed", ModType::Additional));
        r.push_back(qualifiedRule("cast speed", "CastSpeed", ModType::Increased));

        r.push_back(Rule{re(std::string("^") + kNum + "% attack and spell block chance$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("AttackBlock", ModType::Base, std::stod(m[1].str())));
                             out.push_back(make("SpellBlock", ModType::Base, std::stod(m[1].str())));
                             return true;
                         }});

        r.push_back(Rule{re(std::string("^") + kNum + R"(% armou?r (?:damage |dmg )?mitigation penetration$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("ArmorPenetration", ModType::Base, std::stod(m[1].str())));
                             return true;
                         }});
        auto penetration = [](const std::smatch& m, std::vector<Mod>& out, std::string& err) {
            Mod mod = make("Penetration", ModType::Base, std::stod(m[1].str()));
            if (!applyQualifiers(m[2].str(), mod, err)) return false;
            if (mod.damageTypes == 0) {
                err = "penetration needs a damage type";
                return false;
            }
            out.push_back(std::move(mod));
            return true;
        };
        r.push_back(Rule{re(std::string("^") + kNum + R"(% ((?:[a-z]+ )+?)(?:resistance )?penetration$)"), penetration});
        r.push_back(Rule{re(std::string("^damage penetrates ") + kNum + R"(% ((?:[a-z]+ )+?)resistance$)"), penetration});

        // Skill levels: "+2 Active Skill Level", "+3 Cold Skill Level", "+1 to All Skills' Levels".
        r.push_back(Rule{re(std::string("^") + kNum + R"( (?:to )?((?:[a-z]+ )*)skills?'? levels?$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string& err) {
                             Mod mod = make("SkillLevel", ModType::Base, std::stod(m[1].str()));
                             std::string q = m[2].str();
                             // Damage type words here mean the skill's tag ("Cold Skill Level").
                             if (!applyQualifiers(q + "skill ", mod, err)) return false;
                             out.push_back(std::move(mod));
                             return true;
                         }});
        r.push_back(Rule{re(std::string(R"(^projectile quantity )") + kNum + "$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("ProjectileQuantity", ModType::Base, std::stod(m[1].str())));
                             return true;
                         }});
        r.push_back(Rule{re(std::string("^") + kNum + " to all stats$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             for (const char* s : {"Strength", "Dexterity", "Intelligence"})
                                 out.push_back(make(s, ModType::Base, std::stod(m[1].str())));
                             return true;
                         }});
        r.push_back(Rule{re(std::string("^there is a ") + kNum + "% chance to paralyze it$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("InflictParalysis", ModType::Flag, std::stod(m[1].str())));
                             return true;
                         }});
        // Granted skills: "You have a Lv. 20 Precise Spell Amplification."
        r.push_back(Rule{re(R"(^you have a lv\. (\d+) (.+)$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             Mod mod = make("GrantSkill", ModType::Flag, std::stod(m[1].str()));
                             mod.group = m[2].str();
                             out.push_back(std::move(mod));
                             return true;
                         }});

        // Single-stat lines from the tables above.
        for (const StatWords& w : kPercentStats) {
            std::string stat = w.stat;
            ModType type = w.type;
            r.push_back(Rule{re(std::string("^") + kNum + "% " + w.words + "$"),
                             [stat, type](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                                 out.push_back(make(stat, type, std::stod(m[1].str())));
                                 return true;
                             }});
        }
        for (const StatWords& w : kFlatStats) {
            std::string stat = w.stat;
            ModType type = w.type;
            r.push_back(Rule{re(std::string("^") + kNum + " " + w.words + "$"),
                             [stat, type](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                                 out.push_back(make(stat, type, std::stod(m[1].str())));
                                 return true;
                             }});
        }

        // --- Ailments and damage over time --------------------------------------------------------
        // "Adds 10 - 20 Base Wilt Damage": the ailment's own base damage (group = ailment).
        r.push_back(Rule{re(std::string("^adds ") + kNum + " ?- ?" + kNum + R"( base (ignite|wilt|trauma) damage$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             Mod lo = make("AilmentBaseMin", ModType::Base, std::stod(m[1].str()));
                             Mod hi = make("AilmentBaseMax", ModType::Base, std::stod(m[2].str()));
                             lo.group = hi.group = m[3].str();
                             out.push_back(std::move(lo));
                             out.push_back(std::move(hi));
                             return true;
                         }});
        // "Gains 10% - 20% of the added Flat Damage's value as Base Ignite Damage".
        r.push_back(Rule{re(std::string("^gains ") + kNum + "% ?- ?" + kNum +
                            R"(% of the added flat damage's (?:base )?(?:value as )?base (ignite|wilt|trauma) damage$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             Mod lo = make("AilmentFromFlatMin", ModType::Base, std::stod(m[1].str()));
                             Mod hi = make("AilmentFromFlatMax", ModType::Base, std::stod(m[2].str()));
                             lo.group = hi.group = m[3].str();
                             out.push_back(std::move(lo));
                             out.push_back(std::move(hi));
                             return true;
                         }});
        // Chances: "+3% Wilt chance", "+3% chance to inflict Trauma", "+3% chance to Ignite targets",
        // "+10% chance to inflict Damaging Ailments" (all three).
        auto chance = [](std::string ailment) {
            return [ailment](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                for (const char* a : {"ignite", "wilt", "trauma"}) {
                    if (ailment != "all" && ailment != a) continue;
                    Mod mod = make("AilmentChance", ModType::Base, std::stod(m[1].str()));
                    mod.group = a;
                    out.push_back(std::move(mod));
                }
                return true;
            };
        };
        for (const char* a : {"ignite", "wilt", "trauma"}) {
            r.push_back(Rule{re(std::string("^") + kNum + "% " + a + " chance$"), chance(a)});
            r.push_back(Rule{re(std::string("^") + kNum + "% chance to inflict " + a + "$"), chance(a)});
        }
        r.push_back(Rule{re(std::string("^") + kNum + "% chance to ignite targets$"), chance("ignite")});
        r.push_back(Rule{re(std::string("^") + kNum + "% chance to inflict damaging ailments$"), chance("all")});
        r.push_back(qualifiedRule("ailment duration", "AilmentDuration", ModType::Increased));
        r.push_back(Rule{re(std::string("^") + kNum + R"( affliction (?:inflicted )?per second$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("AfflictionPerSecond", ModType::Base, std::stod(m[1].str())));
                             return true;
                         }});
        r.push_back(qualifiedRule("affliction effect", "AfflictionEffect", ModType::Increased));
        // Deterioration: base chance 0% (confirmed), scaled only by Deterioration-specific mods.
        r.push_back(Rule{re(std::string("^") + kNum + "% deterioration chance$"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("DeteriorationChance", ModType::Base, std::stod(m[1].str())));
                             return true;
                         }});
        // "There is a 30% chance for the supported skill to inflict 2 additional stack(s) of Deterioration":
        // stored as the expected extra stacks per inflicted stack (0.3 x 2).
        r.push_back(Rule{re(std::string("^(?:there is an? )?") + kNum +
                            R"(% chance (?:for the supported skill )?to (?:additionally )?inflict (\d+) additional stacks? of deterioration$)"),
                         [](const std::smatch& m, std::vector<Mod>& out, std::string&) {
                             out.push_back(make("DeteriorationExtraStacks", ModType::Base,
                                                std::stod(m[1].str()) / 100.0 * std::stod(m[2].str())));
                             return true;
                         }});
        for (ModType t : {ModType::Additional, ModType::Increased}) {
            r.push_back(qualifiedRule("deterioration damage", "DeteriorationDamage", t));
            r.push_back(qualifiedRule("deterioration duration", "DeteriorationDuration", t));
        }

        // Reaping scales only through its duration (seconds reaped) and recovery speed (confirmed).
        for (ModType t : {ModType::Additional, ModType::Increased}) {
            r.push_back(qualifiedRule("reaping duration", "ReapingDuration", t));
            r.push_back(qualifiedRule("reaping (?:cooldown )?recovery speed", "ReapingRecoverySpeed", t));
        }
        // Damage scaling that only applies to damage over time / ailments / one ailment.
        for (ModType t : {ModType::Additional, ModType::Increased}) {
            r.push_back(qualifiedRule("damage over time", "DotDamage", t));
            r.push_back(qualifiedRule("ailment damage", "AilmentDamage", t));
            r.push_back(qualifiedRule("ignite damage", "IgniteDamage", t));
            r.push_back(qualifiedRule("wilt damage", "WiltDamage", t));
            r.push_back(qualifiedRule("trauma damage", "TraumaDamage", t));
        }

        // Damage lines last: they are the broadest patterns.
        r.push_back(qualifiedRule("damage", "Damage", ModType::Additional));
        r.push_back(qualifiedRule("damage", "Damage", ModType::Increased));
        return r;
    }();
    return kRules;
}

// Rewrites that make wording uniform before any matching.
std::string preprocess(std::string_view line, ParseResult& result) {
    std::string s = normalizeText(line);

    // Keyword labels some items put in front of a line: "[Beacon] +2 Max Spell Burst".
    static const std::regex kLabel = re(R"(^\[[^\]]+\] )");
    s = std::regex_replace(s, kLabel, "");
    static const std::regex kDivinity = re(R"(\(max active divinity: \d+\))");
    static const std::regex kPlusSpace = re(R"(\+ (\d))");
    static const std::regex kSpacePercent = re(R"((\d) %)");
    s = std::regex_replace(s, kSpacePercent, "$1%");
    static const std::regex kInterval = re(R"(\.? ?interval(?: for each enemy)?: [\d.]+ ?s.*$)");
    s = std::regex_replace(s, kDivinity, "");
    s = std::regex_replace(s, kPlusSpace, "+$1");
    s = std::regex_replace(s, kInterval, "");
    strip(s, "(s)");
    // Multi-word names that act as a single qualifier.
    for (std::size_t pos; (pos = s.find("clockwork core")) != std::string::npos;) s.replace(pos, 14, "clockworkcore");

    if (strip(s, " (multiplies)")) result.multiplies = true;
    if (strip(s, " for the supported skill")) result.supportScoped = true;
    if (strip(s, " of the supported skill")) result.supportScoped = true;
    if (stripPrefix(s, "the supported skill deals ")) result.supportScoped = true;
    if (stripPrefix(s, "the supported skill ")) result.supportScoped = true;
    if (strip(s, " for this skill")) result.selfScoped = true;
    if (strip(s, " inflicted by this skill")) result.selfScoped = true;

    // Phrases that are always true while hitting the target and add nothing to the condition.
    stripPrefix(s, "upon inflicting damage, ");
    stripPrefix(s, "upon dealing damage, ");
    strip(s, " for you and your minions");
    strip(s, " when hitting an enemy with elemental damage");
    strip(s, " every time you hit an enemy with elemental damage recently");
    strip(s, " to enemies on the path");

    // "+19% additional Elemental Damage dealt by Spell Skills" -> "+19% additional spell elemental damage";
    // "+62% additional Ailment Damage dealt by Projectiles" -> "+62% additional projectile ailment damage".
    static const std::regex kDealtBy = re(R"(^(.*?% (?:additional )?)(.*?)damage dealt by (\w+?)s?(?: skills)?$)");
    std::smatch m;
    if (std::regex_match(s, m, kDealtBy)) s = m[1].str() + m[3].str() + " " + m[2].str() + "damage";
    return trim(s);
}

bool tryFixed(const std::string& s, ParseResult& result, const std::string& source) {
    auto it = flagLines().find(s);
    if (it == flagLines().end()) return false;
    Mod mod = make(it->second, ModType::Flag, 1.0);
    mod.source = source;
    result.mods.push_back(std::move(mod));
    result.ok = true;
    return true;
}

} // namespace

std::string canonicalVar(std::string v) {
    v = trim(normalizeText(v));
    if (v.size() > 6 && v.compare(v.size() - 6, 6, " owned") == 0) v.resize(v.size() - 6);
    if (contains(v, "focus blessing")) return "focus blessing";
    if (contains(v, "ailment") && contains(v, "type")) return "ailment types";
    if (contains(v, "unfired frost bolt")) return "unfired bolts";
    if (contains(v, "tangle") && (contains(v, "activ"))) return "activated tangles";
    if (contains(v, "aura")) return "auras";
    return v;
}

std::string canonicalFlag(std::string f) {
    f = trim(normalizeText(f));
    if (contains(f, "frozen")) return "frozen";
    if (contains(f, "cursed") || contains(f, "curse")) return "cursed";
    if (contains(f, "affected by ailment")) return "affected by ailments";
    if (contains(f, "focus blessing")) return "focus blessing active";
    if (contains(f, "spell aggression")) return "spell aggression";
    if (contains(f, "night feather")) return "night feather";
    if (contains(f, "sealed mana") && contains(f, "life")) return "sealed mana and life";
    if (contains(f, "selves of all realms")) return "selves of all realms active";
    if (contains(f, "blocked recently")) return "blocked recently";
    return f;
}

ParseResult parseModLine(std::string_view line, const std::string& source) {
    ParseResult result;
    result.line = std::string(line);
    std::string s = preprocess(line, result);
    if (s.empty()) {
        result.error = "empty line";
        return result;
    }
    if (tryFixed(s, result, source)) return result;

    std::vector<Condition> conditions;
    std::smatch m;

    // Reap sources: "Reaps 0.3 s of Damage Over Time when the skill hits. The effect has a 6 s Recovery
    // Time against the same target". Matched before the generic condition clauses, which would read
    // "against the same target" as a condition. Mod value = seconds reaped; group = "<trigger>|<recovery s>".
    static const std::regex kReap = re(
        R"(^reaps (\d+(?:\.\d+)?) ?s of (?:all )?damage over time (when (?:the|this) skill hits|when dealing damage over time|)"
        R"(from nearby enemies within \d+ ?m that have full affliction)(?:\.? the effect has an? (\d+(?:\.\d+)?) ?s )"
        R"(recovery time against the same target)?(?:\..*)?$)");
    static const std::regex kReapBeam = re(R"(^reaps enemies within the \w+ area, dealing (\d+(?:\.\d+)?) ?s of damage over time$)");
    if (std::regex_match(s, m, kReap) || std::regex_match(s, m, kReapBeam)) {
        const bool beam = m.size() == 2;
        std::string trigger = beam ? "channel" : (m[2].str().find("hits") != std::string::npos ? "hit"
                                              : m[2].str().find("affliction") != std::string::npos ? "affliction" : "dot");
        Mod mod = make("ReapSource", ModType::Base, std::stod(m[1].str()));
        mod.group = trigger + "|" + (!beam && m[3].matched ? m[3].str() : "");
        mod.source = source;
        result.mods.push_back(std::move(mod));
        result.ok = true;
        return result;
    }

    // Buffs a support grants the player, e.g. Periodic Burst: "Gains a stack of buff when using the
    // supported skill every 6s: +34.5% Attack and Cast Speed after using a Mobility Skill".
    static const std::regex kBuffOnUse = re(R"(^gains a stack of buff when using the supported skill every [\d.]+ ?s: (.+?) after using an? (\w+) skill$)");
    if (std::regex_match(s, m, kBuffOnUse)) {
        conditions.push_back({"used " + m[2].str() + " skill recently"});
        s = m[1].str();
        result.playerBuff = true;
        result.supportScoped = false;
    }

    // Leading conditions.
    static const std::regex kWhileFor = re(R"(^while (.+?) is active and for \d+s after its effect ends, (.*)$)");
    static const std::regex kWhilePrefix = re(R"(^while (.+?), (.*)$)");
    static const std::regex kTargetPrefix = re(R"(^upon dealing damage to an? (\w+) target, (.*)$)");
    static const std::regex kEnemyPrefix = re(R"(^(cursed|frozen) enemies (.*)$)");
    if (std::regex_match(s, m, kEnemyPrefix)) {
        // Keep "enemies" so the line still reads as an enemy debuff.
        conditions.push_back({canonicalFlag(m[1].str())});
        s = "enemies " + m[2].str();
    } else if (std::regex_match(s, m, kWhileFor) || std::regex_match(s, m, kWhilePrefix) ||
               std::regex_match(s, m, kTargetPrefix)) {
        conditions.push_back({canonicalFlag(m[1].str())});
        s = m[2].str();
    }
    if (tryFixed(s, result, source)) {
        for (Mod& mod : result.mods) mod.conditions = conditions;
        return result;
    }

    // "Stacks up to N times": the value counts once per stack.
    std::optional<double> maxStacks;
    static const std::regex kStacks = re(R"(^(.*?)(?:,|\.)? (?:stacking|stacks) up to (\d+) times?$)");
    if (std::regex_match(s, m, kStacks)) {
        maxStacks = std::stod(m[2].str());
        s = trim(m[1].str());
    }

    std::optional<double> limit;
    static const std::regex kLimit = re(R"(^(.*?),? \(?up to \+?(\d+(?:\.\d+)?)%?\)?$)");
    if (std::regex_match(s, m, kLimit)) {
        limit = std::stod(m[2].str());
        s = m[1].str();
    }

    // Trailing conditions, repeatedly ("... against Frozen enemies for ..." etc.).
    static const std::regex kAtLeast = re(R"(^(.*) when having at least (\d+) (.+)$)");
    static const std::regex kOrMore = re(R"(^(.*) when affected by (\d+) or more (.+)$)");
    static const std::regex kWhenHaving = re(R"(^(.*) when having (.+)$)");
    static const std::regex kWhile = re(R"(^(.*) while (not )?(.+)$)");
    static const std::regex kAgainstAffected = re(R"(^(.*) against enemies affected by (.+)$)");
    static const std::regex kAgainst = re(R"(^(.*) (?:against|to) (\w+) (?:enemies|targets?)$)");
    static const std::regex kIfCursed = re(R"(^(.*) if affected by curses.*$)");
    static const std::regex kIfYouHave = re(R"(^(.*) if you have (.+)$)");
    for (bool changed = true; changed;) {
        changed = false;
        if (std::regex_match(s, m, kAtLeast) || std::regex_match(s, m, kOrMore)) {
            conditions.push_back({{}, false, canonicalVar(m[3].str()), std::stod(m[2].str())});
            s = m[1].str();
            changed = true;
        } else if (std::regex_match(s, m, kWhenHaving)) {
            conditions.push_back({canonicalFlag(m[2].str())});
            s = m[1].str();
            changed = true;
        } else if (std::regex_match(s, m, kWhile)) {
            conditions.push_back({canonicalFlag(m[3].str()), m[2].matched});
            s = m[1].str();
            changed = true;
        } else if (std::regex_match(s, m, kAgainstAffected)) {
            conditions.push_back({canonicalFlag("affected by " + m[2].str())});
            s = m[1].str();
            changed = true;
        } else if (std::regex_match(s, m, kAgainst) && !m[1].str().ends_with(" chance")) {  // not "chance to Ignite targets"
            conditions.push_back({canonicalFlag(m[2].str())});
            s = m[1].str();
            changed = true;
        } else if (std::regex_match(s, m, kIfCursed)) {
            conditions.push_back({"cursed"});
            s = m[1].str();
            changed = true;
        } else if (std::regex_match(s, m, kIfYouHave)) {
            conditions.push_back({canonicalFlag(m[2].str())});
            s = m[1].str();
            changed = true;
        }
    }

    std::optional<PerStat> per;
    static const std::regex kPer = re(R"(^(.*) (?:for every|for each|per) \+?(\d+(?:\.\d+)?)?%? ?(.+)$)");
    if (std::regex_match(s, m, kPer) && m[3].str() != "second") {   // "per second" is a rate, not a scaling clause
        PerStat p;
        p.per = m[2].matched ? std::stod(m[2].str()) : 1.0;
        p.var = canonicalVar(m[3].str());
        p.limit = limit;
        p.multiplies = result.multiplies;
        per = p;
        s = m[1].str();
    } else if (maxStacks) {
        PerStat p;
        p.var = "stacks";
        p.limit = limit;
        p.multiplies = result.multiplies;
        p.maxStacks = maxStacks;
        per = p;
    } else if (limit) {
        // A cap without a scaling clause belongs to rules like the Clockwork Core one, which build
        // their own PerStat; it is applied after parsing.
    }
    s = trim(s);

    // Compound lines: "+180 Spell Critical Strike Rating and +18% additional damage on Critical Strike".
    std::vector<std::string> cores;
    static const std::regex kAnd = re(R"(^(.+?) and ([+-]?\d.*)$)");
    if (std::regex_match(s, m, kAnd)) {
        cores = {trim(m[1].str()), trim(m[2].str())};
    } else {
        cores = {s};
    }

    std::vector<Mod> all;
    for (const std::string& core : cores) {
        bool matched = false;
        std::string firstError;
        for (const Rule& rule : rules()) {
            if (!std::regex_match(core, m, rule.pattern)) continue;
            std::vector<Mod> mods;
            std::string err;
            if (!rule.build(m, mods, err)) {
                if (firstError.empty()) firstError = err;
                continue;
            }
            all.insert(all.end(), mods.begin(), mods.end());
            matched = true;
            break;
        }
        if (!matched) {
            for (const std::regex& inert : inertPatterns()) {
                if (std::regex_search(core, inert)) {
                    result.ok = true;
                    result.inert = true;
                    return result;
                }
            }
            result.error = firstError.empty() ? "no matching pattern for '" + core + "'" : firstError;
            return result;
        }
    }

    for (Mod& mod : all) {
        mod.conditions.insert(mod.conditions.end(), conditions.begin(), conditions.end());
        if (per) {
            if (mod.per) {
                result.error = "two scaling clauses";
                return result;
            }
            mod.per = per;
        } else if (limit && mod.per) {
            mod.per->limit = limit;
        }
        mod.source = source;
    }
    result.mods = std::move(all);
    result.ok = true;
    return result;
}

std::vector<ParseResult> parseModText(std::string_view text, const std::string& source) {
    std::vector<ParseResult> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!normalizeText(line).empty()) out.push_back(parseModLine(line, source));
        start = end + 1;
    }
    return out;
}

} // namespace tob
