# Release review, 2026-09-24

Status: release 0.2.1. All technical checks pass, including the in-game check of the FOV slider patch. The player approved the release and uploads it.

Nexus Mods quarantined the 0.2.0 file. That archive contained the C++ source and two `.cmd` build scripts next to the DLLs. Release 0.2.1 has the same tested binaries and contains no source or script files.

## Closed findings

1. Missing startup log line. The line is present. The plugin wrote it to the fallback location: `%LOCALAPPDATA%\NMSCreatureAnimationFix\CreatureAnimationFix.log` contains `2026-09-24 12:41:37 PID=32700 APPLIED: creature visibility animation suppression bypass, RVA 0x12097A4, 75 -> EB.` PID 32700 is the fresh Steam start of the earlier validation. The earlier review looked only next to the plugin.
2. Controller problem. The player confirmed that the cause was on the player side. The controller worked again after the player connected it again. The loader is not the cause.
3. Ultimate ASI Loader 9.7.4 default deferred loading crashed a disposable test process before main. The package uses `DontLoadFromDllMain=0`, `ForceEntryPointHook=0`, and `DisableCrashDumps=1`. With these settings, the test process forwarded both WinMM timer calls and started the plugin. No NMS process was part of this failure or this test.
4. MSVC analysis C6262 found large stack use. The source now uses checked heap allocations for the path and hash buffers.
5. The plugin checks the bounds of the loaded DOS header before it reads the PE header.

## FOV slider patch review

The player asked for the native sliders to work above the vanilla limit, in the same plugin. The patch changes the displacement of one `movss` instruction at RVA `0x6DF320`. The instruction then loads the existing 200.0 constant instead of the 120.0 constant. Only displacement bytes 5 and 6 change. The length, opcode, registers, and control flow of the instruction do not change. The plugin writes the change with one aligned 8-byte compare-and-exchange. Then it restores the page protection, flushes the instruction cache, and reads the result back.

Before the write, the plugin reads the three constants and requires 60.0, 120.0, and 200.0. It also requires the exact 64 original bytes around the instruction. If another mod changed that code, the plugin declines. The plugin applies the slider patch only when the animation fix is active, so a higher FOV never comes without the animation fix.

## Checks completed

- The original executable was disassembled independently. The animation branch reaches the first instruction after the suppression call.
- A static check against the installed NMS.exe confirms the 32-byte and 64-byte signatures. It also confirms the constants 60.0, 120.0, and 200.0.
- The player confirmed normal animation without the visor after the live byte change.
- 11 previously frozen tracked creatures changed poses in every sample after the change. All 40 tracked creatures updated.
- Build and static analysis pass with warnings as errors. Rebuild on 2026-09-24 13:07: `BUILD_EXIT=0`, `ANALYZE_EXIT=0`.
- The patch test harness passes both suites on the rebuilt binary.
- The loader integration test with the final settings passes.
- The official loader archive digest matches the GitHub release metadata. The DLL matches the verified archive.
- Fresh Steam start on 2026-09-24, PID 32700: the loader and the plugin loaded, with status APPLIED and runtime opcode EB. The disk hash of NMS.exe did not change. The player confirmed that creatures animate and that performance was fine.
- The installed plugin is byte-identical to the tested build. SHA256 `8F945906FDE32FD6D9768E1AB611514BA4D52B988C36DF84FF1B74E38F40FE0C`.

- Fresh Steam start with this build on 2026-09-24 13:19, PID 27228. The log next to the plugin shows `APPLIED` and `FOV APPLIED`.
- In the same process, a read-only memory check shows `EB 12` at the animation site. The slider instruction loads its maximum from the 200.0 constant.
- The player confirmed that the FOV sliders and the creature animation work in the game.

## Release steps

The player uploads the release to Nexus Mods. The steps are in `NEXUS-UPLOAD-STEPS.md`. The page text is in `NEXUS-LISTING.bbcode.txt`.

## Known limitations

- The performance cost is not measured. More creatures can animate outside the intended visibility range.
- The animation fix bypasses a visibility-based skip. It does not repair the frustum calculation of the game.
