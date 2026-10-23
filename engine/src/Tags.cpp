#include "tob/Tags.h"

#include <cctype>

namespace tob {

namespace {

constexpr std::array<std::string_view, kDamageTypeCount> kDamageTypeNames = {
    "Physical", "Fire", "Cold", "Lightning", "Erosion",
};

constexpr std::array<std::string_view, static_cast<std::size_t>(SkillTag::Count)> kSkillTagNames = {
    "Attack", "Spell", "Melee", "Ranged", "Projectile", "Area", "Horizontal", "Mobility",
    "Channeled", "Summon", "Sentry", "Persistent", "Warcry", "Demolisher",
    "Physical", "Fire", "Cold", "Lightning", "Erosion",
    "Strength", "Dexterity", "Intelligence",
    "Curse", "Aura", "Empower", "Support",
    "Tangle", "ClockworkCore", "Active", "Minion", "Main",
};

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

} // namespace

std::string_view toString(DamageType t) {
    return kDamageTypeNames[index(t)];
}

std::optional<DamageTypeMask> damageTypeMaskFromString(std::string_view s) {
    if (iequals(s, "elemental")) return kElementalMask;
    for (DamageType t : kAllDamageTypeList) {
        if (iequals(s, kDamageTypeNames[index(t)])) return bit(t);
    }
    return std::nullopt;
}

std::string_view toString(SkillTag t) {
    return kSkillTagNames[static_cast<std::size_t>(t)];
}

std::optional<SkillTag> skillTagFromString(std::string_view s) {
    if (iequals(s, "channel")) return SkillTag::Channeled;
    for (std::size_t i = 0; i < kSkillTagNames.size(); ++i) {
        if (iequals(s, kSkillTagNames[i])) return static_cast<SkillTag>(i);
    }
    return std::nullopt;
}

TagMask parseTagList(std::string_view list, std::string* unknown) {
    TagMask mask = 0;
    std::size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && (list[i] == ',' || std::isspace(static_cast<unsigned char>(list[i])))) ++i;
        std::size_t start = i;
        while (i < list.size() && list[i] != ',' && !std::isspace(static_cast<unsigned char>(list[i]))) ++i;
        if (start == i) continue;
        std::string_view word = list.substr(start, i - start);
        if (auto tag = skillTagFromString(word)) {
            mask |= bit(*tag);
        } else if (unknown) {
            if (!unknown->empty()) *unknown += ", ";
            *unknown += word;
        }
    }
    return mask;
}

std::string tagMaskToString(TagMask mask) {
    std::string out;
    for (std::size_t i = 0; i < kSkillTagNames.size(); ++i) {
        if (mask & (TagMask{1} << i)) {
            if (!out.empty()) out += ", ";
            out += kSkillTagNames[i];
        }
    }
    return out;
}

std::string normalizeText(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool pendingSpace = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out += ' ';
            pendingSpace = false;
        }
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

} // namespace tob
