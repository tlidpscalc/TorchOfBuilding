# TorchOfBuilding — notes for Claude

- Offline Torchlight Infinite build planner. Engine is C++20 (`engine/`, no Qt), UI is Qt 6 Widgets (`app/`), tlidb scraper is Python (`tools/scraper/`, dev-only).
- The project owner is the authority on game mechanics. Never invent a formula: mark guesses `UNVERIFIED`/`ASSUMED` and ask.
- Every confirmed mechanic gets a test in `tests/`. Keep tests passing: `cmake --preset engine-only && cmake --build --preset engine-debug && ctest --preset engine-debug` (from a VS Developer shell).
- Desktop app: `cmake --preset msvc && cmake --build --preset msvc-debug --target TorchOfBuilding`, then `windeployqt --debug` on `build/msvc/app/Debug/TorchOfBuilding.exe` once so it runs outside a Qt shell. It finds `data/` by walking up from the exe or working directory.
- In-game-style editing data: `gear_types.json`, `vorax.json`, `crafting.json` (scraper `gear`, `crafting`), legendary line pairs in `legendaries.json`, Activation Medium tiers in `skills.json` (scraper `mediums`). Builds keep `inventory` + `equipped`; worn items are mirrored to `items` for the engine. Crafter check: `QT_QPA_PLATFORM=offscreen tob_craft_selftest data`.
- One build's left-out lines: `tob_coverage data --build <build.json>`.
- ModParser must never silently drop text. Unsupported lines return `ok=false` with a reason; lines with no DPS effect are recognised as `inert`.

## Engine data flow
- `data/json/*.json` (scraped from tlidb) → `GameData`; a build file (`data/builds/*.json`, all modifiers as in-game text) → `loadBuild` → `BuildState` (ModDB + Config + MainSkill) → `calculateMainSkillDps`.
- Items, talents, slates, Pactspirits etc. are data. New content should need parser patterns, not code. New *mechanics* go in `SkillDps.cpp` (built-in buffs/debuffs) or as skill facts read from the card in `Build.cpp`.
- Mod features: `skillScope` (supports and a skill's own lines), `effectStat`/`effectScope` (curse/aura/buff effect scaling), conditions with flags or var thresholds, `PerStat` (for each / per / stacks, `(multiplies)`). Variable and flag names are canonicalised in `canonicalVar`/`canonicalFlag`.
- `tools/verify/youga_handcalc.py` is the independent reference; `tests/test_build.cpp` holds the engine to it (7.941T, curse-off 3.631T).
