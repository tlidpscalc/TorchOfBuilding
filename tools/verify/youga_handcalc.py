"""Hand calculation of the reference Youga (The Ultimate One) build's Homing Frost Bolt DPS.

This is a transparent oracle for the engine: every factor is listed with its source, so a gap to
the in-game dummy reading can be traced to specific assumptions. Values come from
data/builds/youga_ultimate_one.json and tlidb; rules from the project owner unless marked ASSUMED.

Run: python tools/verify/youga_handcalc.py
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

DUMMY_DPS = 7_643_825_141_854

# --- Unknowns, set to the most likely value; the report shows their effect ---------------------
BASE_SPELL_CRIT_RATING = 500        # ASSUMED: Homing Frost Bolt base crit rating (tlidb card lists none)
# Main stats (confirmed): the skill's main stats add up into one bucket worth +0.5% additional damage per
# point; Homing Frost Bolt's only main stat is Intelligence. 295 matches the character sheet (confirmed):
# memory 110, amulet 24, belt 52, wand 101, talent 8.
INTELLIGENCE_TOTAL = 110 + 24 + 52 + 101 + 8
SHOTGUN_FALLOFF = 0.60              # confirmed: every later bolt of one cast hitting the same target deals 1 - 0.60 = 40% (not compounding)
# Confirmed by experiment: removing Ring 1 (-18% attack and -18% spell block) changed the sheet's Spell DPS by
# exactly 1.53/1.44 = x1.0625, so attack and spell block are summed.
WIELD_MY_SWORD_MODE = "sum"
CURSE_EFFECT_SCALES_BITING_COLD = True  # ASSUMED: +12% Curse Effect (Warlock) scales Biting Cold's value
# Blink's Periodic Burst: +30% attack and cast speed for 2 s after a mobility skill. The user blinked "a few
# times, not consistently" during the test; ~3 blinks in 60 s gives about 10% uptime.
PERIODIC_BURST_UPTIME = 0.10
# Paralysis (tlidb): enemy takes +15% damage (ASSUMED additional). Applied by the Warlock legendary node
# "+25% chance to Paralyze a Cursed target on damage", so it needs the curse; uptime may vary per run.
PARALYSIS_UPTIME = 1.0


@dataclass
class Bucket:
    name: str
    items: list[tuple[str, float]] = field(default_factory=list)

    def add(self, source: str, pct: float) -> None:
        self.items.append((source, pct))

    @property
    def total(self) -> float:
        return sum(v for _, v in self.items)


def level_multiplier(level: int) -> float:
    """Damaging skills: x1.10 per level 21..30, x1.08 per level above 30 (confirmed)."""
    above20 = max(0, min(level, 30) - 20)
    above30 = max(0, level - 30)
    return 1.10 ** above20 * 1.08 ** above30


def compute(biting_cold_on: bool = True, ring1_on: bool = True, report: bool = False) -> float:
    """Homing Frost Bolt DPS on the dummy. The switches model one-change dummy experiments:
    biting_cold_on=False removes the curse (Biting Cold, Deepened Curse and every 'vs Cursed' bonus);
    ring1_on=False removes Ring 1 (armor pen 15, Frostbite Rating 34 -> not Frozen, 18/18 block)."""
    multipliers: list[tuple[str, float]] = []
    frozen = ring1_on                                  # rating 115 with the ring, 81 without (needs > 100)

    def mult(source: str, factor: float) -> None:
        multipliers.append((source, factor))

    # ---------------- Base hit -------------------------------------------------------------------
    base_avg = (792 + 1188) / 2       # level-20 table value; levels above 20 add the multiplier below
    mult("Skill level 36 (x1.10^10 x 1.08^6)", level_multiplier(36))
    mult(f"Main stat: {INTELLIGENCE_TOTAL} Intelligence x 0.5%", 1 + 0.005 * INTELLIGENCE_TOTAL)

    # ---------------- Increased damage (one additive bucket) ------------------------------------
    inc = Bucket("Increased damage")
    inc.add("Knowledge: +9% damage x3", 27)
    inc.add("Knowledge: +9% spell damage x3", 27)
    inc.add("Knowledge: +10% damage while Focus Blessing active x3", 30)
    inc.add("Lich: +9% damage x3", 27)
    ailments = 6 if frozen else 5                     # Freeze is one of the six ailment types
    if biting_cold_on:
        inc.add("Warlock: +8% damage vs Cursed x3", 24)
        inc.add("Warlock: +16% damage vs Cursed x3", 48)
    inc.add(f"Warlock legendary: +15% Hit Damage per ailment type x{ailments}", 15 * ailments)
    inc.add("Main hand: +40% spell (implicit) +137% spell", 177)
    inc.add("Off hand: +40% spell (implicit) +131% spell", 171)
    inc.add("Fallen Starlight: +18% projectile damage", 18)
    inc.add("Dreamweaver: +12% elemental (inner) +24% (mid)", 36)
    inc.add("Witch of the Night: +24% spell damage (mid)", 24)
    inc.add("Pactspirits: +6% damage end node on each of 3 Pactspirits", 18)
    inc.add("Empowered Element L25: +26.5% elemental damage", 26.5)
    mult(f"Increased damage bucket (+{inc.total:.1f}%)", 1 + inc.total / 100)

    # ---------------- Additional damage (each its own multiplier) -------------------------------
    unfired = 6
    mult("Unfired bolts: +4% x6", 1 + 0.04 * unfired)
    mult("Multiple Projectiles L25", 1.17)
    mult("Deep Chill: 5-star canvas", 1.20)
    if frozen:
        mult("Deep Chill: +17% vs Frozen", 1.17)
    mult("Frost Barb: 5-star canvas", 1.20)
    mult("Frost Barb: +24% with >= 6 unfired bolts", 1.24)
    mult(f"Ailment Termination L25: 9.1% per ailment (multiplies) x{ailments}", 1.091 ** ailments)
    mult(f"Dirty Tricks: 12% per ailment (multiplies) x{ailments}", 1.12 ** ailments)
    mult("Insight: +30% additional spell damage", 1.30)
    mult("Acquaintance: +25% additional spell damage", 1.25)
    mult("Haze prism: +12% additional spell damage (Spell Aggression)", 1.12)
    if frozen:
        mult("Prophet legendary: +10% vs Frozen", 1.10)
    if biting_cold_on:
        mult("Warlock legendary: +8% vs Cursed", 1.08)
    mult("Lich legendary: +10% with Sealed Mana and Life", 1.10)
    mult("Fallen Starlight: +10% with Sealed Mana and Life", 1.10)
    mult("Sparks of Moth Fire (copy): +10% with Sealed Mana and Life", 1.10)
    mult("Knowledge slate: +8% additional spell damage", 1.08)
    if frozen:
        mult("Knowledge slate: +10% vs Frozen", 1.10)
        mult("Knowledge slate: +10% vs Frozen", 1.10)
    spell_aggression = 0.07 * 1.26
    mult("Spell Aggression: 7% x 1.26 effect", 1 + spell_aggression)

    sealed_mana_comp = 4.5 + 9 + 8                    # Lich nodes
    aura_effect = 19 + 13 + 2 * math.floor(sealed_mana_comp / 3)
    mult(f"Spell Amplification L20: 40% x (1 + {aura_effect}% aura effect)", 1 + 0.40 * (1 + aura_effect / 100))

    mult("Three Birds with One Stone: +10% projectile damage", 1.10)
    proj_speed = Bucket("Projectile speed")
    proj_speed.add("Amulet", 42)
    proj_speed.add("Origin memory 16 + 40", 56)
    proj_speed.add("Discipline memory", 30)
    proj_speed.add("Knowledge: +6% x3", 18)
    proj_speed.add("Knowledge: +12% x3", 36)
    proj_speed.add("Fallen Starlight", 6)
    proj_speed.add("Follower Swift Shadow Raid (100% uptime)", 40)
    mult(f"Gale: 60% of +{proj_speed.total:.0f}% projectile speed", 1 + 0.6 * proj_speed.total / 100)
    mult("Projectile Amplification: 1.4% (multiplies) x40", 1.014 ** 40)

    focus = 8
    # Focus Blessing base effect (tlidb): +5% additional damage per stack. Confirmed by the belt experiment.
    mult("Focus Blessing: +5% additional damage per stack x8", 1 + 0.05 * focus)
    mult("Bunch: +3% per Focus Blessing x8", 1 + 0.03 * focus)
    mult("Israfel's Laws: +13% cold per Focus Blessing x8", 1 + 0.13 * focus)

    ring1_block = 18 if ring1_on else 0
    attack_block = 28 + 20 + ring1_block + 13 + 30
    spell_block = 21 + 26 + 20 + ring1_block + 13 + 8
    block = attack_block + spell_block if WIELD_MY_SWORD_MODE == "sum" else max(attack_block, spell_block)
    mult(f"Wield My Sword: +1% per 4% block ({WIELD_MY_SWORD_MODE}: {block}%)", 1 + math.floor(block / 4) / 100)

    mult("Dreamweaver: +19% additional elemental (spells)", 1.19)
    mult("Dreamweaver: +4% additional damage", 1.04)
    mult("Witch of the Night: +8% additional spell (Night Feather)", 1.08)
    mult("Night Feather: 7% x 1.5 effect x 2 stacks tangle damage", 1 + 0.07 * 1.5 * 2)
    mult("Tangle Damage Enhancement bucket: 46 + 47", 1 + 0.93)

    cdr = 4.5 + 8 + 3 * 1.5 * 2                        # Lich nodes + Night Feather (3% x 1.5 effect x 2 stacks)
    sed = 6 + 8 + 14                                  # Lich nodes + I Meet the Selves L3
    cc_bonus = min(cdr + sed, 36)
    mult(f"I Am the Ultimate One L1: CDR+SED {cdr + sed:.1f}% -> Clockwork Core +{cc_bonus:.0f}%", 1 + cc_bonus / 100)
    mult("I Need Not Lift a Finger L3: +17% Clockwork Core", 1.17)

    # ---------------- Enemy side -----------------------------------------------------------------
    if biting_cold_on:
        biting_cold = 47.5 * (1.12 if CURSE_EFFECT_SCALES_BITING_COLD else 1.0)
        mult(f"Biting Cold L36: +{biting_cold:.1f}% cold damage taken", 1 + biting_cold / 100)
        mult("Deepened Curse L25: +5.13% damage taken", 1.0513)
        mult(f"Paralysis (Warlock legendary vs Cursed): +15% x {PARALYSIS_UPTIME:.0%} uptime", 1 + 0.15 * PARALYSIS_UPTIME)
    mult("Frail: +15% spell damage taken", 1.15)
    mult("Cold Infiltration: 13% x 1.14 effect", 1 + 0.13 * 1.14)
    rating = 10 + 50 + 6 + 15 + (34 if ring1_on else 0)
    # ASSUMED: "I Am the Measure of All Time" L3 (+6.5% Selves effect per +2% CDR, up to +130%) scales
    # the Alternate Spacetime Youga bonus for Homing Frost Bolt (+36% Frostbite Effect per unfired bolt).
    selves_cdr = cdr + 25                              # + Cooldown Reduction L25 support on Selves (confirmed)
    selves_effect = min(math.floor(selves_cdr / 2) * 6.5, 130)
    frostbite_effect = 36 * unfired * (1 + selves_effect / 100)
    mult(f"Frostbite: 1 + {rating}/100 x (1 + {frostbite_effect:.0f}% effect; Selves +{selves_effect:.0f}%)",
         1 + rating / 100 * (1 + frostbite_effect / 100))
    pen = 20 + 4 * 1.2 + 4 * 3 + 2 + 2 + 4
    mult(f"Cold resistance 30% - {pen:.1f}% pen", 1 - (30 - pen) / 100)
    # Confirmed: penetration comes off AFTER the 60% non-physical factor (30% - pen), and can go negative.
    armor_pen = (15 if ring1_on else 0) + 6 + 21 + 6 + 6
    mult(f"Armor mitigation 30% - {armor_pen}% pen", 1 - (30 - armor_pen) / 100)
    mult("Double Damage 26% (average)", 1.26)

    # ---------------- Crit -----------------------------------------------------------------------
    flat_crit = BASE_SPELL_CRIT_RATING + 180 * 4 + 55 + (80 if frozen else 0)   # Deep Chill +80 vs Frozen
    inc_crit = (15 * 3 + 20 * 2) + (15 * 3 + 20 * 3) + (15 * 3 + 20 * 3) + 80 + (22 * 2 + 45)
    crit_chance = min(1.0, flat_crit * (1 + inc_crit / 100) * 0.0001)
    crit_dmg_bonus = (5 * 2) + (5 * 3) + (14 * 3) + 47 + 44 + 65 + 59 + 84 + 82 + 4 * focus
    crit_mult = 1.5 + crit_dmg_bonus / 100
    on_crit = 1 + 0.18 * 4                            # helmet: crit hits only
    crit_factor = (1 - crit_chance) + crit_chance * crit_mult * on_crit
    mult(f"Crit: {crit_chance:.1%} chance, x{crit_mult:.2f} damage, x{on_crit:.2f} helmet on crit", crit_factor)

    # ---------------- Hits per second -------------------------------------------------------------
    cast_inc = 9 + 18 + 9 + 18 + 18 + 7 + 47 + 38 + 3 + 6 + 8 + 8 + 8 + 8   # talents, ring, wands, slates, pacts
    cast_inc += 30 * PERIODIC_BURST_UPTIME                                  # Blink: Periodic Burst, averaged
    cast_more = (1 + spell_aggression) * 1.08                               # Spell Aggression x Hasten
    casts_per_s = 1.54 * (1 + cast_inc / 100) * cast_more
    rt = casts_per_s * (1 / (1 + 0.55 * cast_inc / 100))
    bolts = 3
    bolt_weights = [1.0] + [1.0 - SHOTGUN_FALLOFF] * (bolts - 1)
    tangles = 4

    # ---------------- Report ---------------------------------------------------------------------
    per_hit = base_avg
    for _, f in multipliers:
        per_hit *= f
    hits_weight = sum(bolt_weights)
    dps = per_hit * hits_weight * casts_per_s * tangles
    if not report:
        return dps

    print(f"{'Factor':<72}{'x':>10}")
    print("-" * 82)
    for src, f in multipliers:
        print(f"{src:<72}{f:>10.3f}")
    print("-" * 82)
    print(f"Base average hit (level-20 table)                     {base_avg:>14,.0f}")
    print(f"Average hit after all factors                         {per_hit:>14,.0f}")
    print(f"Cast speed +{cast_inc}% increased, x{cast_more:.3f} additional -> {casts_per_s:.2f} casts/s per Tangle (R*T = {rt:.2f})")
    print(f"Bolts per cast {bolts}, effective hits {hits_weight:.2f} (shotgun falloff {SHOTGUN_FALLOFF:.0%}), Tangles {tangles}")
    print()
    print(f"Calculated DPS: {dps:,.0f}")
    print(f"Dummy DPS:      {DUMMY_DPS:,.0f}")
    print(f"Ratio calc/dummy: {dps / DUMMY_DPS:.3f}")
    stat_now = 1 + 0.005 * INTELLIGENCE_TOTAL
    needed_int = (stat_now * DUMMY_DPS / dps - 1) / 0.005
    print(f"Intelligence needed to close the gap on its own: {needed_int:,.0f}")
    return dps


def main() -> None:
    full = compute(report=True)
    print()
    print("Predicted dummy DPS for one-change experiments (scaled to the measured 7.64T baseline):")
    for label, kwargs in (("Biting Cold removed", {"biting_cold_on": False}),
                          ("Ring 1 removed", {"ring1_on": False})):
        ratio = compute(**kwargs) / full
        print(f"  {label:<22} x{ratio:.3f} -> {DUMMY_DPS * ratio / 1e12:.2f}T")


if __name__ == "__main__":
    main()
