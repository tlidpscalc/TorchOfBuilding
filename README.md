# TorchOfBuilding

An offline build planner and DPS calculator for **Torchlight Infinite**, in the spirit of
[Path of Building](https://github.com/PathOfBuildingCommunity/PathOfBuilding).
Game data comes from [tlidb.com](https://tlidb.com) (fan-made, not affiliated with XD).

## Layout

| Path | What |
|---|---|
| `engine/` | C++20 calculation engine. No Qt dependency. |
| `engine/include/tob/Mod.h`, `ModDB.h` | Modifier model and database (Base / Increased / Additional / Bucket / Override / Flag) |
| `engine/include/tob/ModParser.h` | Turns game modifier text into mods. Unsupported lines are reported, never silently dropped. |
| `engine/include/tob/Calc.h` | Hit damage pipeline: base → conversion → scaling → enemy mitigation → crit → speed |
| `tests/` | Catch2 tests; they encode the confirmed TLI rules |
| `app/` | Qt 6 desktop app (currently a single-skill custom-modifier sandbox) |
| `tools/scraper/` | Python tlidb scraper (dev-only, not shipped) → `data/json/` |

## Damage rules implemented

- "Increased" (`+X% Foo Damage`) values add into one bucket.
- Each `+X% additional ...` is its own multiplier. Mods with the same `group` add first (same-item stacking without "(multiplies)").
- `... Damage Enhancement` mods form named buckets: summed within the bucket, multiplied with everything else.
- 100 Critical Strike Rating = 1% crit chance.
- Penetration subtracts from enemy resistance and can go below zero. Armor mitigation penetration subtracts from the armor mitigation %, also unbounded.
- Default enemy = the target dummy: level 85, 30% elemental/erosion res, armor 50% physical / 30% non-physical mitigation.

Items marked `UNVERIFIED` in `engine/include/tob/Calc.h` still need in-game confirmation.

## Toolchain setup (Windows)

```powershell
winget install Kitware.CMake Ninja-build.Ninja Git.Git Python.Python.3.12
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
# Qt without an account: official binaries via aqtinstall
py -m pip install aqtinstall
py -m aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 -O C:\Qt
```

## Build and test

From a **Developer PowerShell for VS 2022**:

```powershell
# Engine + tests only (no Qt needed)
cmake --preset engine-only
cmake --build --preset engine-debug
ctest --preset engine-debug

# Full app
$env:QT_DIR = "C:\Qt\6.8.0\msvc2022_64"
cmake --preset msvc
cmake --build --preset msvc-release
```

## Scraper

```powershell
cd tools/scraper
py -m venv .venv; .venv\Scripts\Activate.ps1
pip install -r requirements.txt
python tlidb_scraper.py page Ice_Shot          # one page, printed as JSON
python tlidb_scraper.py traits                 # all hero trait trees
python tlidb_scraper.py skills                 # all active skills
```

Requests are spaced at least 1 s apart (default 2 s) and every page is cached in `data/raw/`,
so re-running only re-parses. Use `--refresh` at the start of a new season.
