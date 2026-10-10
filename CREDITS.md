# Credits

**Spore: The Long Dark (FrostSpore)** is a mashup of *Spore* (Maxis / Electronic Arts) and *The Long Dark*
(Hinterland Studio). It ships no game content from either game: The Long Dark's sounds are read from the
player's own installation when the mashup starts, and Spore runs from the player's own installation.

Bundled third-party software, with thanks:

| Component | Author | License | Used for |
|---|---|---|---|
| Spore ModAPI SDK core (`SporeModAPI.dll`) | emd4600 and the Spore Community ([Spore-ModAPI](https://github.com/Spore-Community/Spore-ModAPI)) | free community SDK, no license file published | Spore's game structures and function addresses; built from source at commit 2d71289 |
| ModAPI DLL injector (`ModAPI.DLLInjector.dll`) | Splitwirez and the Spore Community ([ModAPI Launcher Kit](https://github.com/Spore-Community/ModAPI-Launcher-Kit)) | MIT | loads the mod into Spore at startup |
| Microsoft Detours | Microsoft | MIT | function hooking inside the injector and the SDK core |
| EASTL | Electronic Arts | BSD-3-Clause | containers used by the Spore ModAPI SDK |
| ww2ogg + packed codebooks | Adam Gashlin (hcs), Xiph.org | BSD-3-Clause | reading The Long Dark's Wwise audio |
| stb_vorbis | Sean Barrett | public domain / MIT | decoding that audio |

Survival numbers follow The Long Dark's own rules as documented by its community wiki (freezing costs
20% condition per hour, 40% with hypothermia; thirst 2%; hunger and fatigue 1%).

Not associated with or endorsed by Electronic Arts, Maxis or Hinterland Studio.
