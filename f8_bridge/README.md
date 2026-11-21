# Mafia III F8 / TAA capture v5 — experimental Windows x64 beta

**Status:** the original F8 Anti-aliasing dropdown has been confirmed working by a real-game screenshot. The new read-only TAA tracer has NOT yet been run in game. No real FSR rendering or resolution scaling is implemented.

## Evidence from the first in-game test

MafiaF8_loader.log: Original Modern Tweaks loaded, DirectInput8Create forwarding succeeded, and F8 hook installed. The screenshot shows four FSR 4.1.2 entries inside the original F8 > Visuals > Anti-aliasing selector, all marked [unavailable]. This is intentional.

The uploaded ModernTweaks(1).log was byte-for-byte identical to the earlier log (timestamps from 2026-11-20 22:49–22:52), so it did NOT describe the new F8 test. The new tracer has its own filename, MafiaTaaTrace.log.

## New code in v5

- New `mafia_taa_trace.dll` compiled as Windows x64 C++ code.
- Hooks the original draw-matcher call at Modern Tweaks RVA 0x796ED (target RVA 0x83C80) only after checking the exact DLL SHA-256 and original call bytes. Its wrapper calls the original matcher and returns the unchanged result.
- Only samples draw candidates matching `Draw(6,0)`. Reads D3D11 texture descriptors for scene color, motion, t1, output render target, and depth stencil. Logs up to eight matching candidates plus limited summary counts.
- Does not copy or modify textures, run the AMD FSR SDK, disable TAA, or alter frame output.
- Upgraded DirectInput proxy reads `EnableTaaTrace=1` and loads the tracer only when explicitly enabled.
- Existing v4.1 users can run `Nadogradi TAA test.cmd`; new installations can use `Instaliraj beta test.cmd`. Uninstall script restores original Modern Tweaks.
- The source and Windows build are in this development branch; the original copyrighted Modern Tweaks DLL is not redistributed.

## Test procedure for an existing v4.1 install

1. Quit Mafia III entirely.
2. Extract the v5 ZIP to its own folder.
3. Run `Nadogradi TAA test.cmd` and enter the game directory; type TRACE to confirm. Do not use the clean installer if the earlier F8 beta is still installed.
4. Launch Mafia III through Steam. Walk or rotate the camera for ~30 seconds, then exit through the game menu.
5. Send `MafiaTaaTrace.log` and `MafiaF8_loader.log` from the game directory. A screenshot of the F8 dropdown is optional this time.
6. Run `Vrati originalni mod.cmd` to restore the original after the test.

If the game crashes or fails to start, immediately restore the original DLL via the uninstall script or the backed-up `dinput8.ModernTweaks.2.5.1.bak` file. Do not use this beta for everyday gameplay.

## Missing milestones

A real 4.1.2 in-game FSR renderer still needs validated camera projection/exposure, per-frame jitter, full render-pass synchronization, D3D11-to-D3D12 resource copies/sharing with AMD SDK dispatch, output handoff before HUD, and genuine internal resolution control for Quality/Balanced/Performance. The synthetic probe elsewhere in the repository does not satisfy these requirements.
