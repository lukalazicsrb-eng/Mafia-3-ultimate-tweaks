# Experimental Mafia III FSR 4.1.2 DirectDraw integration

This development branch includes the compiled AMD FSR SDK backend and a
**version-locked integration layer for the original Modern Tweaks F8 menu**.

## What changed in this iteration

- The original Modern Tweaks v2.5.1 binary is still NEVER distributed or
  modified on disk. Its SHA-256 is
  `529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900`.
- Reverse-engineering identified the actual **direct fullscreen Draw** wrapper
  at RVA `0x79A40`, and its original DirectX11 Draw call at RVA `0x79A71`.
  Original 6-byte call is `FF 15 D1 80 9C 03`, through pointer
  `modernTweaks+0x3A41B48`. The new gamepass library validates the
  complete original binary hash and exact call bytes before patching memory.
- Source `f8_bridge/draw_pass_bridge.cpp` supplies a real
  `Draw(6,0)` callback, with original D3D11 Draw forwarding by default.
- Dynamic integration with `mafia_live_fsr_backend.dll` can call the official
  AMD FSR 4.1.2 SDK using full-resolution actual D3D11 HDR/depth/raw-motion
  buffers via the existing D3D11/D3D12 shared GPU bridge.
- New `MafiaFsrPreviewNativeAaV1` dispatches an **actual** game frame to AMD
  without presenting its output. F8 Native AA becomes available only if the
  real-frame preview and output-handoff validation succeed.
- The original three F8 AA options are preserved. Native AA can replace only
  a verified fullscreen TAA output. Original TAA runs on every rejected or
  unrecognized frame. Quality/Balanced/Performance remain unavailable until
  genuine in-game low-resolution rendering is established.
- Added test `gamepass_guard`: uninitialized frame inputs, no backend and no
  actual game instance must never enable FSR.

## What is NOT complete

**FSR 4.1.2 is still NOT functional in Mafia III gameplay. The new libraries
are NOT installable gameplay mods.**

A *trusted real-frame producer* is still missing. The producer must
identify the game's real TAA pixel shader on the fullscreen Draw path,
then supply actual raw (not encoded t2) motion vectors, true depth, current
jitter and projection/near/far/FOV for the same frame via
`MafiaFsrSetVerifiedFrameV1`. The backend must also be initialized with the
real game D3D11 device and the signed AMD runtime via
`MafiaFsrConfigureBackendV1`.

The user's latest v7 trace showed direct DrawIndexed calls working but zero
matches. Its `t2` was missing or an R8_UNORM 960x544 buffer; feeding this to
FSR would be wrong. The open-source Luma Framework Mafia III module (source
revision `30c4045`) demonstrates that the game's true TAA pass is fullscreen
Draw and the TAA `t2` motion is encoded; an earlier motion-production pass
supplies the usable raw vectors. It also handles a separate depth pass and
camera jitter.

The shader fingerprints reported by the Luma game module (Luma's OWN hashing
algorithm, not automatically CRC32) are: TAA PS `2200CBD7`,
`E781A41B`, `A8D4D208`, raw motion producer `B00E89BC`,
depth linearizer `E4A815CF`. A correct game-frame producer needs to
capture bytecode identities and per-frame resources at those passes,
not infer them from image size alone.

The exported ABI is defined in `probe/live_fsr_api.h`, and the gamepass
ABI in `f8_bridge/draw_pass_bridge.cpp`. This code is designed to be
connected only after inputs are independently validated; neither the
existing F8 beta installer nor the game itself automatically installs it.
The initial compiled DLLs and CMake/CI checks cannot replace real-world
testing of a game-frame integration, which has not happened.

## Current release safety

Do not copy these two new DLLs over the original `dinput8.dll`. They are
developer components, not a final FSR mod. The existing reversible F8 beta
is separate and preserved. A broken shader/pass capture should never be
treated as successful FSR processing.

## Further implementation needed

1. Complete trustworthy per-pass shader identity tracking for actual Mafia
   III DX11 CreatePixelShader+Draw, using source bytecode hash or equivalent.
2. Capture the game's raw motion/depth producer passes and per-view constant
   buffer for near/far/FOV and camera jitter; validate frame matching.
3. Initialize/drive real-game backend from a verified lifecycle point, and
   register F8 evidence/mode callbacks with the existing loader.
4. Demonstrate one valid in-game shadow dispatch, then allow Native AA
   presentation with correct HUD/postprocessing handoff.
5. Add scene render resolution redirection and validation for actual FSR
   Quality/Balanced/Performance; verify screenshot, stability and performance.

No additional repetitive v5/v6-style logs are requested now.
