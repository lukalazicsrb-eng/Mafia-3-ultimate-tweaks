# AMD FSR 4.1.2 real-frame backend (development-only)

**This is real FSR dispatch code, but it is NOT yet a functional Mafia III mod.**
The compiled `mafia_live_fsr_backend.dll` does not inject itself into the game or
register with the F8 menu. Copying it into the game folder alone does NOTHING.
The established F8 menu still shows FSR as **[unavailable]**.

## What the backend implements

- Uses the **game's own ID3D11Device and adapter** to create a matching
  D3D12 device and command queue.
- Loads AMD SDK 2.4.0 signed DirectX 12 runtime and requires the selected
  FSR provider string to contain `4.1.2`.
- Creates shareable real GPU color/depth/motion/output textures, with
  D3D11/D3D12 shared fences.
- Accepts **live game** pre-TAA color, raw depth and motion shader views
  and an existing pre-HUD output render target. Does NOT create any
  synthetic pixels during `process`.
- Uses the existing tested `GameFixture::convert` shaders to convert the
  game HDR R11G11B10, reversed R24 depth and UV motion into AMD's
  shareable FP16 HDR, R32 depth and pixel-space motion.
- Executes a real official AMD FSR SDK command list and writes its output
  back to the game's pre-HUD target **only after a successful dispatch and
  GPU fence handoff**. Returns failure if inputs are missing or inconsistent;
  the caller MUST keep the game's original TAA draw as fallback.
- A versioned C ABI in `live_fsr_api.h` can be used by a future game-pass
  interceptor. No exceptions cross the ABI.

## Why this is NOT ready to install

The existing Modern Tweaks v2.5.1 DLAA matcher is gated behind NVIDIA NGX.
Its `DrawIndexed` probe in the user's latest V7 log recorded many D3D11
calls, but **zero matcher matches** and no validated color+depth+raw-motion
bundle. The code follows the Indexed draw path; the game's actual TAA pass
is a fullscreen `Draw` pass (see Luma Mafia III game code). Therefore
none of the required temporal frame evidence is available to the new
backend yet.

There is no validated game-pass hook that supplies correct
`LiveFrameTextures`, verified projection/near/far/FOV, jitter,
frameTime, pre-exposure and history reset. A C ABI caller cannot just
mark the evidence booleans as true without validating the actual
live buffers. **Do not enable it based on the earlier synthetic test.**

If these real inputs are identified and integrated, the backend's
`MafiaFsrProcessNativeAaV1` returns 1 only after FSR 4.1.2 processing,
synchronization and output blit. On return 0, the caller must let
the game's original TAA pass execute and not report that FSR is active.

## Scope and limitations

- Native AA only, with render and output equal at 2560 x 1440 by default.
- Quality/Balanced/Performance need independent in-game internal resolution
  control, viewport/target management and full-resolution HUD composition.
- Currently untested with real Mafia III frames. Windows CI only verifies
  compilation and the *existing synthetic* test/format conversion program.
- The prototype uses a blocking GPU completion wait for correctness. It
  requires profiling and multi-frame scheduling before final gameplay use.
- The prototype assumes a D3D11 Device5 / Context4 with shared-fence support.
  Actual game device and driver support must be validated in a game run.
- The AMD signed runtimes are not included in this source/repository.
- The original Modern Tweaks DLL is not redistributed or patched.

See `live_fsr_bridge.h`, `live_fsr_api.h`, and `docs/fsr-feasibility.md`.
