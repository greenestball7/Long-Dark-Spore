# Spore: The Long Dark

A Melty mashup of **Spore** and **The Long Dark**. You play Spore from the very start: the meteor
crashes into the ocean and you crawl out as a single cell. This time the planet is The Long Dark's
frozen world. The cell and creature stages run under The Long Dark's survival rules, weather and
sound. Tribal, civilization and space play as normal Spore.

- **Host game (primary):** Spore with Galactic Adventures (Steam, GOG or EA app release)
- **Companion game:** The Long Dark. Its own audio is read from the player's install at launch, and nothing from it is shipped.
- **Players:** single player

## What a player gets

| | Cell stage | Creature stage |
|---|---|---|
| Warmth | ✔ freezing ocean, -4 °C | ✔ frozen land, -10 °C |
| Calories | ✔ eating food refills it and warms you | ✔ fed by Spore's own hunger: eating refills both |
| Thirst | | ✔ stand in water to drink |
| Fatigue | | ✔ stand still or rest at your nest |
| Weather | clear → light snow → blizzard (wind chill, whiteout) | same |
| Shelter | moving fast warms you | your nest (+22 °C), huddling with your herd (+8 °C) |
| Freezing | 20% health per in-game hour at zero warmth, 40% with hypothermia | same, through Spore's own damage |
| The Long Dark's sound | wind beds per weather, ice cracks, freezing breath, music | wind, wolf howls, freezing breath, music |

The HUD shows four Long Dark–style bars, the feels-like temperature and the weather. Frost creeps
in from the screen edges as you freeze.

## How it works

- `FrostSpore.exe` (launcher): Melty starts it with `--spore {game} --tld {game:custom-the-long-dark}`.
  It finds The Long Dark's Wwise audio (`*_Data/StreamingAssets`: `.wem`, `.bnk`, `.pck` and
  `SoundbanksInfo` names), picks the clips listed in `design/sheets/tld_content.json`, converts them
  with ww2ogg + stb_vorbis into a WAV cache in `%LOCALAPPDATA%\FrostSpore`, then starts
  `SporebinEP1\SporeApp.exe` and loads the Spore ModAPI with the mod, the same way the official
  ModAPI Launcher Kit does.
- `FrostSpore.dll` (Spore ModAPI C++ mod): runs the survival simulation each frame, applies condition
  loss to Spore's health, plays the Long Dark clips through its own mixer, and draws the HUD on
  Spore's frames.

## Design sheets: the source of truth

`design/sheets/*.json`: needs, restore, temperature, weather, timing, thresholds, tld_content, hooks,
hud, stages. Each row becomes one generated C++ struct (`design/codegen.py` → `src/common/Sheets.gen.h`).
`design/preflight.py` overlays every sheet, then lists unfilled cells, unresolved cross-sheet
references, and any hook or HUD row without code tagged `HOOK:<id>` / `HUD:<id>`. The build stops
unless it prints CLEAN.

## Building (Linux, cross-compiling for 32-bit Windows)

```
tools/setup_toolchain.sh   # clang-cl + MSVC/Windows SDK headers from Microsoft NuGet packages
tools/prepare_sdk.sh       # Spore ModAPI SDK at a pinned commit, patched for clang
tools/build.sh             # → dist/FrostSpore-<version>.zip
tests/run.sh               # off-game tests: survival rules and The Long Dark audio reader
```

## Status

0.1.0 builds and its off-game tests pass. **It has not been run in Spore yet.** The first in-game test
and gameplay capture need a Windows PC with both games installed.

Credits: see [CREDITS.md](CREDITS.md).
