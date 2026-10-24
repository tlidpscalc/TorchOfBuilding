#include "tob/Build.h"

#include "tob/ModParser.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace tob {

namespace {

constexpr int kBaseSkillLevel = 20;
// Canvas (Noble/Magnificent) supports: separate additional damage by star level 1..5 (confirmed).
constexpr double kCanvasStarDamage[] = {0.0, 0.0, 4.0, 8.0, 14.0, 20.0};

struct LineOptions {
    std::string scope;          // skill the mods are limited to (empty = global)
    double multiplier = 1.0;    // talent points
    std::string effectStat;     // e.g. AuraEffect, CurseEffect
    std::string effectScope;
};

void addLine(BuildState& b, const std::string& text, const std::string& source, const LineOptions& o = {}) {
    ParseResult pr = parseModLine(text, source);
    b.lines.push_back({source, text, pr.ok, pr.inert, pr.error});
    if (!pr.ok) return;
    for (Mod m : pr.mods) {
        m.value *= o.multiplier;
        // Enemy debuffs from a skill or support affect every hit, not only that skill.
        const bool enemyDebuff = m.stat == "DamageTaken";
        if (!o.scope.empty() && !enemyDebuff && !pr.playerBuff) m.skillScope = o.scope;
        if (!o.effectStat.empty()) {
            m.effectStat = o.effectStat;
            m.effectScope = o.effectScope;
        }
        b.mods.add(std::move(m));
    }
}

void addLines(BuildState& b, const nlohmann::json& lines, const std::string& source, const LineOptions& o = {}) {
    if (!lines.is_array()) return;
    for (const auto& l : lines) addLine(b, l.get<std::string>(), source, o);
}

bool isWeaponSlot(const std::string& slot) {
    const std::string k = looseKey(slot);
    return k == "mainhand" || k == "offhand" || k == "mainhandweapon" || k == "offhandweapon" || k == "weapon";
}

// Weapon items: local ("gear") modifiers build the weapon; everything else is global.
void loadWeaponItem(BuildState& b, const nlohmann::json& lines, const std::string& slot, const std::string& source) {
    DamageByType base{}, added{};
    std::array<double, kDamageTypeCount> damageInc{};
    double speedBase = 0.0, speedInc = 0.0, critBase = 0.0, critInc = 0.0;
    // The weapon's implicit crit is written without a sign ("500 Critical Strike Rating"); global
    // crit rating on the same item always has one ("+55 Spell Critical Strike Rating").
    static const std::regex kBaseCrit(R"(^\s*(\d+(?:\.\d+)?) critical strike rating\s*$)", std::regex::icase);

    for (const auto& l : lines) {
        const std::string line = l.get<std::string>();
        std::smatch m;
        if (std::regex_match(line, m, kBaseCrit)) {
            critBase += std::stod(m[1].str());
            b.lines.push_back({source, line, true, false, ""});
            continue;
        }
        ParseResult pr = parseModLine(line, source);
        b.lines.push_back({source, line, pr.ok, pr.inert, pr.error});
        if (!pr.ok) continue;
        for (const Mod& mod : pr.mods) {
            if (mod.stat.rfind("Weapon", 0) != 0) {
                b.mods.add(mod);
                continue;
            }
            if (mod.stat == "WeaponAttackSpeed") (mod.type == ModType::Base ? speedBase : speedInc) += mod.value;
            if (mod.stat == "WeaponCritRating") critInc += mod.value;
            // "Elemental" adds the same roll to each of fire, cold and lightning (confirmed).
            for (DamageType t : kAllDamageTypeList) {
                if (!(mod.damageTypes & bit(t))) continue;
                const std::size_t i = index(t);
                if (mod.stat == "WeaponBaseMin") base[i].min += mod.value;
                if (mod.stat == "WeaponBaseMax") base[i].max += mod.value;
                if (mod.stat == "WeaponAddedMin") added[i].min += mod.value;
                if (mod.stat == "WeaponAddedMax") added[i].max += mod.value;
                if (mod.stat == "WeaponDamage") damageInc[i] += mod.value;   // "% Gear <type> Damage" (confirmed)
            }
        }
    }
    if (speedBase <= 0.0) return;   // not a weapon base (e.g. only modifiers were entered)

    WeaponStats w;
    w.slot = slot;
    for (std::size_t i = 0; i < kDamageTypeCount; ++i) {
        const double f = std::max(0.0, 1.0 + damageInc[i] / 100.0);   // "-100% gear physical damage" removes it
        w.damage[i] = {(base[i].min + added[i].min) * f, (base[i].max + added[i].max) * f};
    }
    w.attackSpeed = speedBase * (1.0 + speedInc / 100.0);
    w.critRating = critBase * (1.0 + critInc / 100.0);
    b.weapons.push_back(w);
}

// Array member or an empty array (const json::operator[] must not be used on missing keys).
nlohmann::json arr(const nlohmann::json& j, const char* key) {
    return j.value(key, nlohmann::json::array());
}

TagMask cardTags(const nlohmann::json& card) {
    TagMask tags = 0;
    for (const auto& t : card.value("tags", nlohmann::json::array())) {
        if (auto tag = skillTagFromString(t.get<std::string>())) tags |= bit(*tag);
    }
    return tags;
}

int skillLevel(const BuildState& b, TagMask tags) {
    EvalContext ctx{tags, 0, &b.config};
    return kBaseSkillLevel + static_cast<int>(b.mods.sum(ModType::Base, {"SkillLevel"}, ctx));
}

// Level-table text for a skill at `level` (tables stop at 40; damaging skills repeat level 20 above 20).
nlohmann::json levelText(const nlohmann::json& card, int level) {
    const auto& levels = card.value("levels", nlohmann::json::array());
    if (levels.empty()) return nlohmann::json::array();
    int idx = std::clamp(level, 1, static_cast<int>(levels.size())) - 1;
    nlohmann::json text = nlohmann::json::array();
    for (const auto& l : levels[idx]["text"]) text.push_back(l);
    return text;
}

void loadTalents(BuildState& b, const GameData& data, const nlohmann::json& talents) {
    for (const auto& panel : talents) {
        const std::string slug = panel.value("board", "");
        if (slug.empty()) continue;   // empty panel row
        const nlohmann::json* board = data.board(slug);
        if (!board) {
            b.lines.push_back({"Talent board", slug, false, false, "unknown talent board"});
            continue;
        }
        std::vector<int> prismSlot;
        if (panel.contains("prism")) prismSlot = panel["prism"].value("slot", std::vector<int>{});

        for (const auto& n : panel.value("nodes", nlohmann::json::array())) {
            int col = n[0], row = n[1], points = n[2];
            if (prismSlot.size() == 2 && prismSlot[0] == col && prismSlot[1] == row) continue;
            auto it = std::find_if((*board)["nodes"].begin(), (*board)["nodes"].end(),
                                   [&](const nlohmann::json& node) { return node["col"] == col && node["row"] == row; });
            if (it == (*board)["nodes"].end()) {
                b.lines.push_back({"Talent: " + slug, "node " + std::to_string(col) + "," + std::to_string(row), false,
                                   false, "no node at this position"});
                continue;
            }
            addLines(b, (*it)["mods"], "Talent: " + slug, {.multiplier = static_cast<double>(points)});
        }
        for (const auto& core : panel.value("core", nlohmann::json::array())) {
            const std::string name = core.get<std::string>();
            if (const nlohmann::json* c = data.coreTalent(name)) addLines(b, (*c)["mods"], "Core talent: " + name);
            else b.lines.push_back({"Core talent", name, false, false, "unknown core talent"});
        }
        if (panel.contains("prism")) addLines(b, panel["prism"]["lines"], "Prism: " + panel["prism"].value("name", ""));
    }
}

void loadTraits(BuildState& b, const GameData& data, const nlohmann::json& traits) {
    const std::string slug = traits.value("tree", "");
    const nlohmann::json* tree = data.traitTree(slug);
    if (!tree) {
        b.lines.push_back({"Hero trait", slug, false, false, "unknown trait tree"});
        return;
    }
    for (const auto& want : traits.value("nodes", nlohmann::json::array())) {
        const std::string name = want.value("name", "");
        const int level = want.value("level", 1);
        auto node = std::find_if((*tree)["nodes"].begin(), (*tree)["nodes"].end(),
                                 [&](const nlohmann::json& n) { return n.value("name", "") == name; });
        if (node == (*tree)["nodes"].end()) {
            b.lines.push_back({"Hero trait", name, false, false, "unknown trait"});
            continue;
        }
        for (const auto& lv : (*node)["levels"]) {
            if (lv.value("level", 0) != level) continue;
            addLines(b, lv["affix"], "Trait: " + name);
            addLines(b, lv["server_affix"], "Trait: " + name);
        }
    }
}

void loadMainSkillCard(BuildState& b, const nlohmann::json& card) {
    MainSkill& s = b.main;
    const auto& stats = card.value("stats", nlohmann::json::object());
    if (stats.contains("Cast Speed")) s.baseCastSpeed = std::stod(stats["Cast Speed"].get<std::string>());
    if (stats.contains("Main Stat")) {
        std::string ms = stats["Main Stat"].get<std::string>();
        static const std::regex kWord(R"([A-Za-z]+)");
        for (auto it = std::sregex_iterator(ms.begin(), ms.end(), kWord); it != std::sregex_iterator(); ++it)
            s.mainStats.push_back(it->str());
    }

    // Facts read from the level-20 row (damaging skills repeat it above 20).
    const auto& levels = card.value("levels", nlohmann::json::array());
    if (levels.size() >= kBaseSkillLevel && levels[kBaseSkillLevel - 1].contains("added_damage_effectiveness") &&
        levels[kBaseSkillLevel - 1]["added_damage_effectiveness"].is_number())
        s.addedDamageEffectiveness = levels[kBaseSkillLevel - 1]["added_damage_effectiveness"].get<double>() / 100.0;
    std::string text;
    for (const auto& l : levelText(card, kBaseSkillLevel)) text += normalizeText(l.get<std::string>()) + "\n";
    std::smatch m;
    static const std::regex kDeals(R"(deals (\d+(?:\.\d+)?)-(\d+(?:\.\d+)?) (?:spell |attack |hit )?(\w+) damage)");
    if (std::regex_search(text, m, kDeals)) {
        s.baseMin = std::stod(m[1].str());
        s.baseMax = std::stod(m[2].str());
        if (auto mask = damageTypeMaskFromString(m[3].str())) {
            for (DamageType t : kAllDamageTypeList)
                if (*mask == bit(t)) s.damageType = t;
        }
    }
    // Attack hit components: a "Name:" line names the component whose "Deals X% Weapon Attack Damage" follows.
    static const std::regex kWeaponPct(R"(deals (\d+(?:\.\d+)?)% weapon attack damage)");
    std::string component;
    for (const auto& l : levelText(card, kBaseSkillLevel)) {
        const std::string line = normalizeText(l.get<std::string>());
        std::smatch pm;
        if (!line.empty() && line.back() == ':') component = line.substr(0, line.size() - 1);
        else if (std::regex_search(line, pm, kWeaponPct))
            s.attackComponents.push_back({component.empty() ? "hit" : component, std::stod(pm[1].str())});
    }
    static const std::regex kConvert(R"(converts (\d+(?:\.\d+)?)% of the skill's (\w+) damage to (\w+) damage)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), kConvert); it != std::sregex_iterator(); ++it) {
        auto from = damageTypeMaskFromString((*it)[2].str()), to = damageTypeMaskFromString((*it)[3].str());
        DamageType f = DamageType::Physical, t = DamageType::Physical;
        for (DamageType d : kAllDamageTypeList) {
            if (from && *from == bit(d)) f = d;
            if (to && *to == bit(d)) t = d;
        }
        if (from && to) s.conversions.push_back({f, t, std::stod((*it)[1].str())});
    }
    static const std::regex kExtra(R"(generates 1 additional .*? for every \+(\d+) projectile quantity, up to \+(\d+))");
    if (std::regex_search(text, m, kExtra)) {
        s.extraProjectilePer = std::stod(m[1].str());
        s.extraProjectileCap = std::stoi(m[2].str());
    }
    static const std::regex kFalloff(R"(shotgun effect falloff coefficient is (\d+(?:\.\d+)?)%)");
    if (std::regex_search(text, m, kFalloff)) s.shotgunFalloffPct = std::stod(m[1].str());
    static const std::regex kCondense(R"(base condensation time: (\d+(?:\.\d+)?)s)");
    if (std::regex_search(text, m, kCondense)) s.condensationTime = std::stod(m[1].str());
    static const std::regex kEfficiency(R"(cast speed affects .*?condensation time at (\d+(?:\.\d+)?)% efficiency)");
    if (std::regex_search(text, m, kEfficiency)) s.condensationCastSpeedEfficiency = std::stod(m[1].str()) / 100.0;
}

// Activation Medium supports have tiers instead of levels. The build entry picks "tier" (default: the
// best one) and, for mediums with "<Cooldown Recovery Speed or Duration Bonus>", one "bonus" ("cooldown"
// or "duration", default cooldown). Rolled ranges "(12–19)" take "values" in order, else the top.
// Mediums without a tier table use their description lines.
std::vector<std::string> activationMediumLines(const nlohmann::json& card, const nlohmann::json& sup) {
    std::vector<std::string> lines;
    const auto& tiers = card["tiers"];
    if (tiers.empty()) {
        const auto& description = card.value("description", nlohmann::json::array());
        for (std::size_t i = 1; i < description.size(); ++i)
            for (const auto& l : description[i]) lines.push_back(l.get<std::string>());
        return lines;
    }
    const std::string wanted = sup.value("tier", tiers.back().value("tier", ""));
    const nlohmann::json* tier = &tiers.back();
    for (const auto& t : tiers)
        if (t.value("tier", "") == wanted) tier = &t;
    const bool choice = card.value("bonus_choice", false);
    const std::string bonus = sup.value("bonus", "cooldown");
    for (const auto& l : (*tier)["lines"]) {
        const std::string line = l.get<std::string>();
        if (choice) {
            const bool cooldown = line.find("Cooldown Recovery Speed") != std::string::npos;
            const bool duration = line.find("Duration") != std::string::npos;
            if ((cooldown && bonus != "cooldown") || (duration && !cooldown && bonus != "duration")) continue;
        }
        // Multi-line effects are joined with " | ".
        std::size_t start = 0;
        for (std::size_t bar; (bar = line.find(" | ", start)) != std::string::npos; start = bar + 3)
            lines.push_back(line.substr(start, bar - start));
        lines.push_back(line.substr(start));
    }
    // Rolled ranges, in order across the kept lines.
    static const std::regex kRange(R"(\(\s*(-?[\d.]+)\s*(?:–|-)\s*(-?[\d.]+)\s*\))");
    const auto values = sup.value("values", nlohmann::json::array());
    std::size_t next = 0;
    for (std::string& line : lines) {
        std::string out;
        auto begin = std::sregex_iterator(line.begin(), line.end(), kRange);
        std::size_t last = 0;
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            const std::smatch& m = *it;
            const double lo = std::stod(m[1].str()), hi = std::stod(m[2].str());
            double v = next < values.size() ? values[next].get<double>() : std::max(lo, hi);
            ++next;
            std::ostringstream num;
            num << v;
            out += line.substr(last, m.position(0) - last) + num.str();
            last = m.position(0) + m.length(0);
        }
        line = out + line.substr(last);
    }
    return lines;
}

void loadSupports(BuildState& b, const GameData& data, const nlohmann::json& supports, const std::string& skill) {
    const int supportLevel = skillLevel(b, bit(SkillTag::Support));
    for (const auto& sup : supports) {
        if (!sup.is_object() || sup.value("name", "").empty()) continue;   // empty support slot
        const std::string name = sup.value("name", "");
        const std::string source = "Support: " + name + " (" + skill + ")";
        if (name == "Clockwork Core") {
            if (skill == b.main.name) {
                b.main.clockworkCore = true;
                b.main.tags |= bit(SkillTag::Tangle) | bit(SkillTag::ClockworkCore);
            }
            continue;
        }
        if (sup.contains("stars")) {
            int stars = std::clamp(sup.value("stars", 1), 1, 5);
            if (kCanvasStarDamage[stars] > 0)
                addLine(b, "+" + std::to_string(static_cast<int>(kCanvasStarDamage[stars])) +
                               "% additional damage for the supported skill",
                        source, {.scope = skill});
            addLines(b, arr(sup, "lines"), source, {.scope = skill});
            continue;
        }
        const nlohmann::json* card = data.skill(name);
        if (card && card->contains("tiers")) {
            for (const std::string& line : activationMediumLines(*card, sup)) addLine(b, line, source, {.scope = skill});
            continue;
        }
        if (!card || card->value("levels", nlohmann::json::array()).empty()) {
            b.lines.push_back({source, name, false, false, "no level table for this support"});
            continue;
        }
        for (const auto& l : levelText(*card, supportLevel)) {
            std::string line = l.get<std::string>();
            // "while the supported skill lasts" buffs everything while that skill is active.
            static const std::regex kLasts(R"(while the supported skill lasts)", std::regex::icase);
            if (std::regex_search(line, kLasts)) {
                addLine(b, std::regex_replace(line, kLasts, "while " + skill + " is active"), source);
                continue;
            }
            addLine(b, line, source, {.scope = skill});
        }
    }
}

void loadAura(BuildState& b, const GameData& data, const std::string& name, int level, const std::string& source) {
    const nlohmann::json* card = data.skill(name);
    if (!card) {
        b.lines.push_back({source, name, false, false, "unknown aura skill"});
        return;
    }
    const std::string auraName = card->value("name", name);
    addLines(b, levelText(*card, level), source, {.effectStat = "AuraEffect", .effectScope = auraName});
}

} // namespace

BuildState loadBuild(const GameData& data, const nlohmann::json& build) {
    BuildState b;

    // Config first: conditions the user sets for the scenario being calculated.
    const nlohmann::json cfg = build.value("config", nlohmann::json::object());
    for (const auto& f : arr(cfg, "flags")) b.config.flags[f.get<std::string>()] = true;
    const nlohmann::json vars = cfg.value("vars", nlohmann::json::object());
    for (auto it = vars.begin(); it != vars.end(); ++it) b.config.vars[it.key()] = it.value().get<double>();
    if (cfg.contains("enemy")) {
        b.enemyResistance = cfg["enemy"].value("resistance", 30.0);
        b.enemyArmorPhysical = cfg["enemy"].value("armor_physical", 50.0);
        b.enemyArmorNonPhysical = cfg["enemy"].value("armor_non_physical", 30.0);
    }

    loadTalents(b, data, build.value("talents", nlohmann::json::array()));
    if (build.contains("traits")) loadTraits(b, data, build["traits"]);

    for (const auto& item : build.value("items", nlohmann::json::array())) {
        const std::string slot = item.value("slot", "");
        const std::string source = "Item: " + slot + " " + item.value("name", "");
        if (isWeaponSlot(slot)) loadWeaponItem(b, arr(item, "lines"), slot, source);
        else addLines(b, arr(item, "lines"), source);
        for (const auto& core : arr(item, "blend_core")) {
            if (const nlohmann::json* c = data.coreTalent(core.get<std::string>())) addLines(b, (*c)["mods"], source);
            else b.lines.push_back({source, core.get<std::string>(), false, false, "unknown core talent"});
        }
    }
    std::stable_sort(b.weapons.begin(), b.weapons.end(), [](const WeaponStats& a, const WeaponStats& c) {
        return looseKey(a.slot).rfind("main", 0) == 0 && looseKey(c.slot).rfind("main", 0) != 0;
    });
    for (const auto& slate : build.value("slates", nlohmann::json::array())) {
        const std::string source = "Slate: " + slate.value("name", "");
        addLines(b, arr(slate, "lines"), source);
        for (const auto& core : arr(slate, "core_talents")) {
            if (const nlohmann::json* c = data.coreTalent(core.get<std::string>())) addLines(b, (*c)["mods"], source);
            else b.lines.push_back({source, core.get<std::string>(), false, false, "unknown core talent"});
        }
    }
    for (const auto& p : arr(build, "pactspirits")) addLines(b, arr(p, "lines"), "Pactspirit: " + p.value("name", ""));
    for (const auto& mem : arr(build, "hero_memories"))
        addLines(b, arr(mem, "lines"), "Hero Memory: " + mem.value("name", ""));

    if (build.contains("follower")) {
        const auto& f = build["follower"];
        addLines(b, f.value("player_lines", nlohmann::json::array()), "Follower: " + f.value("name", ""));
        if (f.contains("projectile_amplification_stacks"))
            b.config.vars["projectile amplification"] = f["projectile_amplification_stacks"].get<double>();
        for (const auto& a : f.value("ailments", nlohmann::json::array())) b.externalAilments.push_back(a.get<std::string>());
    }

    // Skills last: their levels depend on every "+X Skill Level" mod loaded above.
    const auto& skills = build.value("skills", nlohmann::json::object());
    b.main.name = skills.value("main", "");
    for (const auto& s : skills.value("active", nlohmann::json::array())) {
        if (!s.is_object() || s.value("name", "").empty()) continue;   // empty skill slot
        const std::string name = s.value("name", "");
        b.activeSkills.push_back(name);
        const nlohmann::json* card = data.skill(name);
        TagMask tags = (card ? cardTags(*card) : 0) | bit(SkillTag::Active);
        if (name == b.main.name) tags |= bit(SkillTag::Main);
        const int level = skillLevel(b, tags);

        if (name == b.main.name) {
            b.main.tags = tags;
            b.main.level = level;
            if (card) loadMainSkillCard(b, *card);
        }
        loadSupports(b, data, s.value("supports", nlohmann::json::array()), name);

        if (!card) continue;
        const std::string source = "Skill: " + name;
        if (name == b.main.name) {
            addLines(b, levelText(*card, level), source, {.scope = name});
        } else if (tags & bit(SkillTag::Curse)) {
            // Curses: their enemy debuff scales with Curse Effect.
            addLines(b, levelText(*card, level), source,
                     {.scope = name, .effectStat = "CurseEffect", .effectScope = name});
            b.config.flags["cursed"] = true;
        }
    }
    if (b.main.clockworkCore) b.main.tags |= bit(SkillTag::Tangle) | bit(SkillTag::ClockworkCore);

    // Alternate Spacetime Youga: per-skill bonuses while Selves of All Realms is active, scaled by
    // Selves effect (evaluated with Selves' own supports).
    if (build.contains("traits") && build["traits"].value("tree", "") == "The_Ultimate_One" &&
        data.alternateYouga.contains(b.main.name)) {
        addLines(b, data.alternateYouga[b.main.name], "Alternate Spacetime Youga",
                 {.scope = b.main.name, .effectStat = "SelvesEffect", .effectScope = "Selves of All Realms"});
    }

    for (const auto& p : skills.value("passive", nlohmann::json::array())) {
        if (!p.is_object() || p.value("name", "").empty()) continue;   // empty skill slot
        if (!p.value("enabled", true)) continue;
        const std::string name = p.value("name", "");
        const nlohmann::json* card = data.skill(name);
        TagMask tags = (card ? cardTags(*card) : 0) | bit(SkillTag::Active);
        loadAura(b, data, name, skillLevel(b, tags), "Aura: " + name);
        loadSupports(b, data, p.value("supports", nlohmann::json::array()), card ? card->value("name", name) : name);
    }
    // Skills granted by items ("You have a Lv. 20 Precise Spell Amplification") keep their fixed level.
    std::vector<std::pair<std::string, int>> granted;
    for (const Mod& m : b.mods.mods())
        if (m.stat == "GrantSkill") granted.emplace_back(m.group, static_cast<int>(m.value));
    for (const auto& [name, level] : granted) loadAura(b, data, name, level, "Granted: " + name);

    // Flags the user switched off, applied last so they also override flags set while loading (e.g. "cursed").
    for (const auto& f : arr(cfg, "flags_off")) b.config.flags[f.get<std::string>()] = false;
    return b;
}

BuildState loadBuildFile(const GameData& data, const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    return loadBuild(data, nlohmann::json::parse(in));
}

} // namespace tob
