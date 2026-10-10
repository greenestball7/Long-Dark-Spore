# MODLOG: Spore: The Long Dark

## Route
- Host **Spore** (Maxis' own engine, native 32-bit C++). Melty installs no loader for it. The
  community's route is **Spore ModAPI** (C++ DLL mods injected at startup by the ModAPI Launcher
  Kit). We bundle our own minimal launcher plus the Launcher Kit's injector DLL (MIT) and a core
  `SporeModAPI.dll` built from the SDK source, so players don't install the Launcher Kit by hand.
- Companion **The Long Dark** (Unity 6000.0.x IL2CPP, Wwise audio; TLD 2.55 per CabinBoy mod README).
  We read its audio from `*_Data/StreamingAssets` at launch and ship none of it.
- Executable detection uses the Launcher Kit's size table (Disc, Origin 2017, EA 2024, Steam/GOG 2017,
  GOG 2024, Steam 2024). Supported: the "march2017" family (EXECUTABLE_TYPE=2). Disc isn't supported.
  Origin/EA builds need SporeApp_ModAPIFix.exe, which only the official kit installs.
- Steam: write `SporebinEP1/steam_appid.txt` = 24720 (GA app id) as the Launcher Kit does, plus `SteamAppId` env.

## Toolchain (no Windows box available)
- clang 18 `--driver-mode=cl` + lld-link, MSVC 14.11 headers and libs from NuGet
  `VisualCppTools.Community.VS2017Layout`, Windows SDK from `Microsoft.Windows.SDK.CPP(.x86)` 10.0.22000.196.
- Headers need case aliases (`tools/fixcase.py`).
- SDK patches (`tools/prepare_sdk.sh`):
  1. backslash includes;
  2. `Args(...)` relied on MSVC's traditional preprocessor, so it now wraps args in parens and terminal macros unwrap them with DEPAREN;
  3. Windows' `IN` macro vs EASTL's `IN...`;
  4. out-of-line `X::DETOUR` definitions need `template<>`;
  5. `T& p = T()` default arguments use a thread-local temporary;
  6. a `constexpr char16_t*` const fix.
- Mods link **SporeModAPIBase.lib** (static, same sources without MODAPI_DLL_EXPORT) plus the core DLL's import lib.
- Everything is built /MT, so no VC++ runtime is needed. Our update listener is our own class, so no
  heap object crosses DLL boundaries.
- Detours: we use MIT Detours 4 (from the Launcher Kit repo), not the SDK's Detours 3.0 Express (non-commercial license).

## Spore facts used (SDK headers)
- Cell: `Simulator::Cell::GetPlayerCell()->mHealthPoints` (int, max 6), `mTransform`. Food:
  `CellGame.mpSerializableData->mFoodProgression + mPlantFoodProgression`.
- Creature: `GameNounManager.GetAvatar()`, `cCreatureBase::mHunger` (0-100), `cCombatant::TakeDamage`,
  `cCreatureAnimal::mHerd->mpNest / mHerd`, `PlanetModel.IsInWater(pos)`.
- `GameTimeManager.IsPaused()`, `Graphics::RenderUtils::GetDevice()` (D3D9). The HUD hooks vtable slot 17 (Present).
- `GetActivePlanetRecord()->mTemperatureScore` = 0 in creature stage (Spore's own frozen-planet look); effect unverified.

## Verified so far
- Preflight clean; off-game tests pass (sim rules; audio reader on a mock install with loose .wem,
  AKPK .pck with an embedded bank, SoundbanksInfo.xml).
- All binaries link. Imports: kernel32/user32/winmm/shell32/ole32 + SporeModAPI.dll only.
- Melty: validate_recipe valid; one_click_check **yes**.

## Not yet verified (needs the Windows PC with both games)
- Injection and startup in each Spore release; HUD drawing; that cell HP set to 0 kills the cell;
  TakeDamage on the avatar; IsInWater / nest / herd signals; whether TLD ships SoundbanksInfo names
  or the length fallback is used; Wwise codec (Vorbis is supported; Opus is not yet).
- Next: launch through Melty, read `%LOCALAPPDATA%\FrostSpore\logs\launcher.log` and `mod.log`, capture gameplay.

## Melty
- Draft listing: modId e97a9ff4-c2c4-40d0-b4e2-277d853bc73d, slug spore-the-long-dark (Studio: https://melty.gg/studio/e97a9ff4-c2c4-40d0-b4e2-277d853bc73d)
- Recipe: packaging/recipe.json (validate_recipe valid, one_click_check yes). Not yet saved as melty.json (waiting for approval).
- Upload of FrostSpore-0.1.0.zip started (uploadId 2f057ffa-12f2-4c34-b2c8-5afb626364e1), but the PUT to Melty's
  storage host (ekvzctwehmwidsengayc.storage.supabase.co) was blocked by this session's network policy.
