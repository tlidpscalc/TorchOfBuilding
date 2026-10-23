# Homing Frost Bolt – Build Math Handover

Oct 22, 2026 · @Girish

> **Updates since this handover (TorchOfBuilding, 2026-10-22)**
> - Trait conflicts are resolved: The Ultimate One uses a trait tree, not one pick per tier. The reference build takes the "I Meet the Selves" branch (Meet the Selves, Measure of All Time, Need Not Lift a Finger, all at level 3), so **Stasis Vault and its −50% cast speed debuff are not in play**.
> - Measure of All Time is level 3 in the reference build: +6.5% Selves effect per +2% CDR, up to +130% (the handover used level-5 values).
> - Spell Tangles are independent copies (confirmed): DPS = single-copy DPS × Tangle count (4 in the reference build).
> - Frostbite (confirmed): +1% additional Cold Damage taken per point of Frostbite Rating, as its own multiplier, and Frostbite Effect scales that bonus (not the rating). So the "Frostbite Effect damage" section below is correct: multiplier = 1 + rating/100 × (1 + effect). Inflicting Frostbite applies your max Frostbite Rating instantly (no ramp-up). Max rating = min(10 + "+X to Max Frostbite Rating" mods, cap). The cap is 120 unless a mod says "Frostbite Rating can exceed its upper limit, up to N" (e.g. Gemma's Deepfreeze: 150–200). Above 100 the target is Frozen.

## Summary

Aim for about +105–115% additive cast speed to max the 24% unfired-bolt bonus. Past that, cast speed is a normal DPS stat and never pushes you into the 10-bolt cap. This doc covers the Homing Frost Bolt math for a Youga (The Ultimate One) build in Torchlight Infinite, with 3 bolts per cast.

| Question | Answer |
| --- | --- |
| Cast speed to max the 24% bonus | > +105.3% additive (target +110–115%) |
| Risk of hitting the 10-bolt cap up to 30 casts/s | None: peaks at 9 bolts with 3 per cast |
| Stasis Vault −50% additional cast speed | Caps the bonus at 12% unless offset |
| Other additional cast speed to restore 6 bolts | +100% at +105% additive; +72% at +300%; never below +43% |
| Selves of All Realms 100% uptime (Measure of All Time, 22.4s cooldown, 50–60% crit) | About +80% skill effect duration |
| +216% Frostbite Effect at 100 rating | About 2.08× Cold damage vs 0% effect |

## Skill mechanics and model

Everything below rests on one ratio, R × T: casts per second times condensation time. It counts how many later casts are still condensing when a bolt launches.

Skill facts used:

- Base cast time 0.65s. Each cast makes bolts that condense for 1s, then launch at a random enemy.
- Cast speed affects condensation at 55% efficiency.
- +2 Projectile Quantity adds 1 bolt, up to +2, so 3 bolts per cast at most.
- +4% damage per unfired bolt, up to +24% (6 bolts). Hold cap: 10 unfired bolts.
- Shotgun falloff 60%. Frame cap: 30 casts/s.

With CS = additive increased cast speed (1.0 = +100%) and M = the combined additional (multiplicative) cast speed multiplier:

```latex
R = \frac{M\,(1 + CS)}{0.65}, \qquad T = \frac{1}{1 + 0.55\,CS}
```

Assumptions:

- Additional cast speed changes cast rate only, not condensation. Condensation uses additive cast speed only.
- The unfired-bolt bonus is checked when a bolt launches or hits, and bolts from the same cast don't count each other.
- Additional cast speed modifiers multiply with each other.

## Cast speed breakpoint for the 24% bonus

The bonus maxes out at +105.3% additive cast speed, where R × T passes 2. Crossing it is a jump from 12% to 24%, about +10.7% more damage at once.

| R × T | Later casts still condensing | Unfired bolts at launch | Bonus |
| --- | --- | --- | --- |
| ≤ 1 | 0 | 0 | 0% |
| 1–2 | 1 | 3 | 12% |
| > 2 | 2 | 6 | 24% (cap) |

```latex
\frac{1 + CS}{0.65\,(1 + 0.55\,CS)} > 2 \;\Rightarrow\; CS > 1.0526
```

| Additive cast speed | Casts/s | Condensation | R × T | Bonus |
| --- | --- | --- | --- | --- |
| +0% | 1.54 | 1.00s | 1.54 | 12% |
| +50% | 2.31 | 0.78s | 1.81 | 12% |
| +106% | 3.17 | 0.63s | 2.00 | 24% |
| +150% | 3.85 | 0.55s | 2.11 | 24% |
| +300% | 6.15 | 0.38s | 2.32 | 24% |

Target +110–115% for a buffer against conditional buffs dropping. Past the breakpoint, each +1% cast speed is worth about 1/(1 + CS) more DPS (about 0.49% at +106%), so weigh it against other stats normally. Shotgun falloff doesn't move the breakpoint.

## 10-bolt cap

You can scale cast speed all the way to 30 casts/s without hitting the 10-bolt cap. R × T rises with cast speed but levels off below 1 / (0.65 × 0.55) ≈ 2.80, so at most 3 casts are ever condensing at once: 9 bolts at peak, 6–8 most of the time.

At 30 casts/s (about +1850% additive), condensation is about 0.090s and R × T ≈ 2.69.

What could still push you to 10:

- **A 4th bolt per cast** (higher projectile cap, multicast, a duplicating support): 3 casts in flight would be 12 bolts.
- **Frame rounding at very high speed:** condensation is about 2.7 frames at 30 casts/s. If the engine rounds to 3 frames and processes a new cast before a launch on the same frame, you could briefly hold 4 casts' worth. Unconfirmed, and at most one frame.

## Stasis Vault −50% cast speed debuff

With "I Will Hunt You Down", the −50% additional cast speed halves your cast rate but not condensation. That caps R × T near 1.40, so you hold at most 3 unfired bolts at launch: a 12% bonus instead of 24%. The bolt cap becomes a non-issue (peak 6 bolts).

To get back to 6 unfired bolts, you need other additional cast speed to offset it. Solve for X, the other additional cast speed:

```latex
\frac{0.5\,(1 + X)(1 + CS)}{0.65\,(1 + 0.55\,CS)} > 2
```

| Additive cast speed | Other additional cast speed needed | Net additional after the −50% |
| --- | --- | --- |
| +105% | +100% | 0% |
| +150% | +90% | −5% |
| +200% | +82% | −9% |
| +300% | +72% | −14% |
| +400% | +66% | −17% |
| +500% | +63% | −19% |
| +1000% | +54% | −23% |
| No limit | +43% | −29% |

Below +43% from other sources, 6 unfired bolts is impossible at any additive cast speed. These are exact breakpoints, so add a few percent of buffer.

Kit notes:

- Stasis Vault lasts 8s on a 16s cooldown, so the debuff applies only about half the time unless extended.
- "I Come to Claim My Due" (lv75) can extend the Vault by 1s per Recall once you have 20+ Centralization stacks (0.5s interval), which can keep it up permanently, and the debuff with it.
- Clockwork Core's Spell Tangles might each cast the skill. If tangles share the unfired-bolt count, 2 tangles double the cast rate and cancel the debuff. Unconfirmed.

## Selves of All Realms uptime

With "I Am the Measure of All Time", 40% cooldown recovery speed (22.4s cooldown) and 50–60% crit, target about +80% skill effect duration for 100% uptime. Selves of All Realms has a 7s duration and a 16s base cooldown.

**Without Measure of All Time**, you need (1 + CDR) × (1 + Duration) ≥ 16 / 7 ≈ 2.29. The cheapest balanced split is about +51% of each; with 0% CDR it's +129% duration.

**With Measure of All Time** (lv60), cooldown recovery speed lengthens the cooldown instead (cap 24s), each +2% CDR gives +8% Selves effect (max +160% at +40% CDR), and duration applies at 200%. So 7 × (1 + 2 × Duration) must cover the cooldown: +64% at 16s, +110% at 22.4s with no crit procs.

**Clockwork Core crit procs** cut all cooldowns by 0.1s, at most once per 0.2s, which shortens the effective cooldown:

```latex
\text{Effective cooldown} = \frac{22.4}{1 + 0.1 \times \text{procs/s}}, \qquad \text{procs/s} \approx \frac{1}{0.2 + 1/(\text{hits/s} \times \text{crit})}
```

| Bolt hits/s | Crit | Procs/s | Effective cooldown | Duration needed |
| --- | --- | --- | --- | --- |
| No crits | — | 0 | 22.4s | +110% |
| 9 | 50% | 2.4 | 18.1s | +79% |
| 9 | 60% | 2.6 | 17.8s | +77% |
| 18 | 55% | 3.3 | 16.8s | +70% |
| 30 | 55% | 3.8 | 16.2s | +66% |
| Very high | any | 5 (max) | 14.9s | +57% |

About 9 hits/s is typical single-target (3 bolts at about 3 casts/s), so +80% covers bosses and short gaps in hits. Procs only happen while a Clockwork Core exists; "I Need Not Lift a Finger" re-summons it and adds a 4s grace window after Selves ends.

## Frostbite Effect damage

+216% Frostbite Effect at 100 Frostbite Rating is worth about 2.08× Cold damage compared with 0% effect. Each point of rating gives +1% additional Cold Damage taken; this assumes Frostbite Effect scales that bonus.

| Frostbite Effect | Additional Cold Damage taken at 100 rating | Multiplier |
| --- | --- | --- |
| 0% | +100% | ×2.00 |
| +108% (3 unfired bolts) | +208% | ×3.08 |
| +216% (6 unfired bolts) | +316% | ×4.16 |

The bonus comes from Selves of All Realms: +36% Frostbite Effect per unfired bolt, up to +216%. It only applies while Selves is active and to Cold damage; Homing Frost Bolt is pure Cold. Other Frostbite Effect sources (e.g. Deepfreeze +80%) probably add to the same pool.

## Open questions and in-game tests

Six assumptions above are unconfirmed; each can be settled with a quick target-dummy test.

- [ ] Unfired-bolt bonus timing: compare hits at about +90% vs +120% additive cast speed. If both show 24%, the bonus locks in at bolt creation and cast speed is a plain linear DPS stat.
- [ ] Same-cast bolts: with the Stasis Vault debuff active, check whether hits reach 20% (bolts from one cast count each other) or stop at 12%.
- [ ] Spell Tangles: check whether bolts from each Clockwork Core tangle share one unfired-bolt count.
- [ ] Crit procs vs Measure of All Time: watch the Selves cooldown timer to confirm the 0.1s crit reductions still shorten it.
- [ ] Frostbite Effect: check the enemy debuff tooltip. Above +120% Cold damage taken means Effect scales the bonus, not the rating.
- [ ] Frame rounding near 30 casts/s: only matters if you actually get that fast.

Trait conflicts:

- lv45 options are "I Hold All in My Grasp" (Stasis Vault), "I Arrive Alongside Destiny" (+28% CDR) and "I Meet the Selves" (+28% duration). If it's one pick per tier, you can't run both Stasis Vault and Selves of All Realms.
- "I Will Hunt You Down", "I Am the Measure of All Time" and "I Need Not Lift a Finger" are all lv60 and compete with each other.

## Sources

- [Stasis Vault – TLIDB](https://tlidb.com/en/Stasis_Vault)
- [Clockwork Core – TLIDB](https://tlidb.com/en/Clockwork_Core)
- [The Ultimate One (hero traits) – TLIDB](https://tlidb.com/en/The_Ultimate_One)
- [Frostbite – TLIDB](https://tlidb.com/en/Frostbite)

TLIDB notes that it uses beta data, so live numbers may differ. Skill text for Homing Frost Bolt came from the in-game tooltip.
