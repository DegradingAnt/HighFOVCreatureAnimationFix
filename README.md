# High FOV Creature Animation Fix

Version 0.2.2 for No Man's Sky 7.06, PC Steam build 180836.

This repository contains the source code. To install the mod, download the release from Nexus Mods: https://www.nexusmods.com/nomanssky/mods/4559

The plugin makes two changes in game memory each time the game starts:

1. Animation fix. At a wide FOV, some creatures move with a frozen pose. The plugin bypasses the creature-specific call that stops bone updates after a visibility test. The game keeps the visibility result for other creature logic.
2. FOV slider range. The native on-foot and flight FOV sliders go from 60 to 200 instead of 60 to 120. The plugin applies this change only when the animation fix is active.

The plugin does not change NMS.exe on disk, saves, movement, or network state.

## Compatibility

The plugin supports only this NMS.exe:

`SHA256 13D5060D4EFB9D2A6A6B1B349BC4257231056CC2A055DF4BB15D816262CC3499`

Before it writes to memory, the plugin checks the full file hash, the loaded PE fingerprint, and the machine code around each patch site. If a check fails, the plugin makes no change and writes a DECLINED line to the log. After a game update, this plugin needs a reviewed update. Other stores and versions are not tested.

Version 0.2.1 supports only build 179666. On build 180836 it writes `DECLINED` and makes no change. Use 0.2.2 for build 180836.

## Installation

1. Close No Man's Sky.
2. Extract the package into the No Man's Sky game folder. Merge its `Binaries` folder with the existing `Binaries` folder.
3. Start the game through Steam.

The final layout:

```
Binaries/NMS.exe                           (already present, never replaced)
Binaries/winmm.dll                         (Ultimate ASI Loader 9.7.4, x64)
Binaries/winmm.ini                         (loader settings)
Binaries/scripts/CreatureAnimationFix.asi
```

If another mod already supplies `winmm.dll` or `winmm.ini`, do not overwrite them. If you already use Ultimate ASI Loader, copy only `CreatureAnimationFix.asi` into its `scripts` folder.

The loader settings use direct plugin loading, with the entry-point hook off. The plugin starts its own worker thread. The file check and the memory changes run after DLL initialization. The default deferred loading mode of the loader failed an isolated startup test, so this package does not use it.

## Build from source

You need Visual Studio 2022 with the C++ desktop workload.

1. Open a command prompt in this folder.
2. Run `.\build.cmd`. The script builds `CreatureAnimationFix.asi` and the test program `patch_test.exe`.
3. Run `.\patch_test.exe "<game folder>\Binaries\NMS.exe"`. The test needs the supported NMS.exe. All three test lines must print PASS.
4. Run `.\analyze.cmd` for the Microsoft static analysis. Warnings are errors.

If Visual Studio is not in the default folder, change the `vcvars64.bat` path in both scripts.

The plugin needs an ASI loader. The release uses Ultimate ASI Loader 9.7.4 x64 as `winmm.dll`, with the settings in `winmm.ini`. This repository does not contain the loader. Loader source and releases: https://github.com/ThirteenAG/Ultimate-ASI-Loader

## Status log

The plugin writes a line for each patch at each start. It writes the log next to the plugin, in `Binaries\scripts\CreatureAnimationFix.log`. If it cannot write there, it uses `%LOCALAPPDATA%\NMSCreatureAnimationFix\CreatureAnimationFix.log`.

- `APPLIED`: the plugin applied the verified animation fix.
- `ACTIVE`: the same animation fix was already in memory.
- `FOV APPLIED` or `FOV ACTIVE`: the sliders go from 60 to 200.
- `DECLINED` or `FOV DECLINED`: the plugin does not support this executable or its live code. The plugin made no change.
- `ERROR` or `FOV ERROR`: examine the game before you use the plugin again.

## Removal

1. Close the game.
2. Rename `CreatureAnimationFix.asi` to `CreatureAnimationFix.asi.disabled`, or delete it.
3. Remove `winmm.dll` and `winmm.ini` only if no other mod uses them.

The memory changes stop when the game closes. No save needs a change. The game keeps a FOV value above 120 in its settings file. Without the animation fix, creatures can slide again at that FOV.

## Validation

Version 0.2.2 on build 180836:

- The 0.2.2 file is not yet tested in the game.
- The patch sites, the original bytes, and the changed bytes are the same as in an earlier private build for 180836. On 2026-10-11 and 2026-10-12, that build ran in the game on 180836. Its log shows `APPLIED: creature visibility animation suppression bypass, RVA 0x12137F4, 75 -> EB.` and `FOV APPLIED`.
- A static check against the NMS.exe of build 180836 confirms the PE fingerprint, both patch sites, and the constants 60.0, 120.0, and 200.0. `patch_test.exe` does this check each time it runs.
- A separate search of the same NMS.exe found each of the two byte windows exactly once.
- Compiler warning level 4 and Microsoft static analysis pass, with warnings treated as errors.

Version 0.2.1 on build 179666:

- Live test in co-op at the existing high FOV of the player: the player confirmed normal creature animation.
- Read-only memory capture before and after the animation fix: 11 creatures with frozen bone matrices started to update again. All 40 tracked creatures had changing matrices in all 69 samples after the first second.
- Fresh Steam start on 2026-09-24: the loader and the plugin loaded, and the internal status was APPLIED. The log line `APPLIED: creature visibility animation suppression bypass` is present in the LOCALAPPDATA log. The player confirmed that creatures animate and that performance was fine.
- Compiler warning level 4 and Microsoft static analysis pass, with warnings treated as errors.
- Isolated tests check the hash guard and the refusal in an unsupported process. They also check the instruction signatures, the exact changed bytes, idempotence, page protection restore, and the refusal of changed code.
- The same tests check that the FOV slider patch reads the existing 200.0 constant and changes no shared constant.
- A static check against the real NMS.exe confirms both patch sites and the constants 60.0, 120.0, and 200.0.
- Fresh Steam start with this release on 2026-09-24: the log shows `APPLIED` and `FOV APPLIED`. A read-only memory check confirms both changes. The player confirmed that the FOV sliders and the creature animation work.

## Limitations

- More creatures can animate outside the intended visibility range. This can add CPU work. Nobody has measured the performance cost.
- The animation fix bypasses a visibility-based skip. It does not repair the frustum calculation of the game.

## Technical details

Build 180836 (version 0.2.2):

Animation fix: static VA `0x1412137F4` (RVA `0x12137F4`), opcode `75` (JNE) to `EB` (JMP). Both forms jump to `0x141213808`. That address is immediately after the call to `0x141846FA0`, which requests animation suppression for the model.

FOV slider range: at RVA `0x6E4FC0`, the instruction `movss xmm8, [rip+disp32]` loads the slider maximum. The plugin changes displacement bytes 5 and 6 from `fb 4d` to `5f 4e`. The instruction then loads the existing 200.0 constant at RVA `0x4B29E28` instead of the 120.0 constant at RVA `0x4B29DC4`. The minimum, 60.0 at RVA `0x4B29D64`, does not change. The plugin changes no constant.

Build 179666 (version 0.2.1): the animation fix was at RVA `0x12097A4` (jump target `0x1412097B8`, call `0x14183A1D0`). The slider instruction was at RVA `0x6DF320`, with bytes 5 and 6 changed from `bb ac` to `1f ad`, the 200.0 constant at `0x4B1A048`, 120.0 at `0x4B19FE4`, and 60.0 at `0x4B19F84`.

## Changelog

- 0.2.2: Support for No Man's Sky 7.06, PC Steam build 180836. New executable hash, PE fingerprint, patch site addresses, and byte signatures. The changes in game memory are the same as in 0.2.1. The `APPLIED` log line takes its address from the pinned values. `patch_test.exe` also checks the pins against the executable file.
- 0.2.1: Release for build 179666, 2026-09-24. Same tested binaries as 0.2.0. The Nexus package contains no source or script files.

## Credits

Developed with AI assistance (OpenAI Codex and Anthropic Claude) and tested by the player. The repository contains no game assets and no game executable.

The idea to raise the limit of the native sliders comes from the NoFOVLimit mod by Mrsuss60. This plugin uses a different, smaller change for this build. NoFOVLimit source: https://github.com/Mrsuss60/NoMansSky_MODs

The Nexus release includes Ultimate ASI Loader by ThirteenAG under the MIT license. Loader source: https://github.com/ThirteenAG/Ultimate-ASI-Loader

## License

MIT. See `LICENSE`. This license covers the code in this repository. It does not cover No Man's Sky or Ultimate ASI Loader.
