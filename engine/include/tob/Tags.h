#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tob {

// ---------------------------------------------------------------------------
// Damage types
// ---------------------------------------------------------------------------

enum class DamageType : std::uint8_t { Physical, Fire, Cold, Lightning, Erosion, Count };

inline constexpr std::size_t kDamageTypeCount = static_cast<std::size_t>(DamageType::Count);

inline constexpr std::array<DamageType, kDamageTypeCount> kAllDamageTypeList = {
    DamageType::Physical, DamageType::Fire, DamageType::Cold, DamageType::Lightning, DamageType::Erosion,
};

using DamageTypeMask = std::uint8_t;

constexpr DamageTypeMask bit(DamageType t) {
    return static_cast<DamageTypeMask>(1u << static_cast<unsigned>(t));
}

inline constexpr DamageTypeMask kElementalMask =
    bit(DamageType::Fire) | bit(DamageType::Cold) | bit(DamageType::Lightning);
inline constexpr DamageTypeMask kAllDamageTypesMask = 0x1F;

constexpr std::size_t index(DamageType t) { return static_cast<std::size_t>(t); }

std::string_view toString(DamageType t);

// Accepts single types ("fire") and groups ("elemental"). Case-insensitive.
std::optional<DamageTypeMask> damageTypeMaskFromString(std::string_view s);

// ---------------------------------------------------------------------------
// Skill tags (as shown on tlidb skill cards)
// ---------------------------------------------------------------------------

enum class SkillTag : std::uint8_t {
    Attack, Spell, Melee, Ranged, Projectile, Area, Horizontal, Mobility,
    Channeled, Summon, Sentry, Persistent, Warcry, Demolisher,
    Physical, Fire, Cold, Lightning, Erosion,
    Strength, Dexterity, Intelligence,
    Curse, Aura, Empower, Support,
    // Not tlidb card tags, set by the engine:
    Tangle,         // the skill is tangled, so "+X% Tangle Damage" applies
    ClockworkCore,  // supported by Clockwork Core, so "Clockwork Core damage" applies
    Active,         // any non-support skill ("+X Active Skill Level")
    Minion,         // minion-only mods; player skills never have it, so those mods stay inert
    Main,           // the build's main skill ("+4 Main Skill Level")
    Count
};

using TagMask = std::uint64_t;
static_assert(static_cast<unsigned>(SkillTag::Count) <= 64, "TagMask is 64 bits");

constexpr TagMask bit(SkillTag t) {
    return TagMask{1} << static_cast<unsigned>(t);
}

std::string_view toString(SkillTag t);
std::optional<SkillTag> skillTagFromString(std::string_view s);

// Builds a mask from a comma/space separated list. Unknown names are written to
// `unknown` (comma separated) so callers can surface them instead of silently dropping.
TagMask parseTagList(std::string_view list, std::string* unknown = nullptr);

std::string tagMaskToString(TagMask mask);

// Lowercases ASCII and collapses runs of whitespace to single spaces; trims ends.
std::string normalizeText(std::string_view s);

} // namespace tob
