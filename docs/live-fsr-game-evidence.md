# Mafia III real-frame FSR integration: data-path evidence

## Latest user test (V7)

The provided `MafiaTaaTrace(2).log` demonstrates that the direct
`DrawIndexed` hook at Modern Tweaks RVA `0x799D6` executes. It counted
thousands of calls, but all logged `original_matcher_result=0`.
Some sampled color views were 2560x1440 `DXGI_FORMAT 26`
(`R11G11B10_FLOAT`). The observed sample motion slots were
either absent or 960x544 `DXGI_FORMAT 61` (`R8_UNORM`),
NOT validated temporal vectors. Depth view was absent at those
samples, so **do not feed them to FSR**. The log's first samples
included a 1920x1080 texture in t1; that is not automatically
a TAA history or depth input.

The trace therefore confirms **a working diagnostic hook**, not
a valid TAA draw, not a completed FSR frame, and not an image quality gain.
The F8 menu remains intentionally disabled for FSR.

## Better TAA discovery source

The open-source Luma Mafia III game-specific implementation
(see [upstream game source](https://github.com/Filoppi/Luma-Framework/blob/30c4045a47edc1a175646bd8a0e18b89778f91e5/Source/Games/Mafia%20III/main.cpp))
identifies the original game's fullscreen TAA pass and distinguishes it
from indexed geometry draws. Its TAA signature has three pixel-shader
identifiers (Luma's own hashing format): `2200CBD7`,
`E781A41B`, `A8D4D208` (upstream near line 1863).
Do not treat these as standard CRC-32 without reproducing the exact
upstream hashing scheme.

Luma's source (near lines 371-625) demonstrates:
- The game's temporal AA uses a full-screen **Draw** path. Separate
  motion-encoding and depth-linearization passes are captured upstream.
- TAA `t0` is the pre-TAA linear HDR color;
  TAA `t1` is history; TAA `t2` is *encoded* motion (not raw
  FSR-ready motion). Luma captures independent raw motion from
  previous passes.
- Inverted depth, UV-to-pixel motion scaling, and per-view jitter
  must be handled for each frame.
- The game's TAA process can draw multiple passes for different parts
  of the frame, so a replacement must avoid double dispatch and must
  restore the correct output before subsequent post-processing/HUD.

The previously reverse-engineered Modern Tweaks DLL's DLAA matcher
is gated behind an NVIDIA-specific setup path and is not sufficient
on its own on the user's RX 6700 XT. The AMD path needs an
independent shader/pass interceptor or cooperation with a different
game-specific source implementation.

## Implemented backend (this PR)

`probe/live_fsr_bridge.h/.cpp` and `probe/live_fsr_api.h/.cpp`
provide a real AMD FSR 4.1.2 DX11-to-DX12 backend designed to receive
*actual* pre-TAA game textures, convert them to compatible shareable
resources, call the official AMD SDK on the same GPU, and return the
output to a validated pre-HUD target. The AMD runtime/SDK dispatch
code derives from the working synthetic bridge in `probe/`.
`mafia_live_fsr_backend.dll` is not loaded into Mafia III by the current
F8 beta; no verified real-game source buffers exist yet.

The backend refuses to process frames with missing or incompatible
resource descriptors or missing real-game camera/jitter evidence.
The caller must run original TAA whenever processing returns false.
Native AA is the only implemented output-size mode. Support for
Quality/Balanced/Performance requires actual render-target resizing
throughout the scene, not just changing the FSR output image.

## Next technical dependency (not another repeated user log)

Develop a version-specific, guarded **D3D11 full-screen Draw**
interceptor keyed to actual game shader identity, and use earlier
upstream motion/depth passes. Verify real view descriptors and
per-frame camera data before connecting that interceptor to
`MafiaFsrProcessNativeAaV1`. Do not expose FSR as selectable in
F8 until a live frame is processed and confirmed. Existing F8
Off/TAA/DLAA paths must remain preserved. No binaries should be
installed as a functioning FSR mod while these requirements are
outstanding.
