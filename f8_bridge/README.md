# Mafia III v9 — Installable F8 / DirectDraw pipeline test

**Status: experimental Windows x64 in-game compatibility test, NOT working
AMD FSR gameplay.**

The supplied original Modern Tweaks 2.5.1 archive SHA-256 is
`529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900`.
The original binary is never uploaded to the repository or distributed in our package.

## What's new

We statically confirmed the real DirectX 11 fullscreen Draw wrapper
at RVA `0x79A40` and the original call at RVA `0x79A71`.
Previous v7 captured DrawIndexed geometry instead of the fullscreen TAA path.

The new `mafia_fsr_gamepass.dll` hooks the exact original Draw callsite
only after SHA-256 + opcodes check and only when the game's original draw
function pointer is ready. The `dinput8.dll` proxy now loads it on a
separate delayed worker, without blocking Steam game startup. It records
bounded shader-pointer identities, t0/t1/t2 render inputs, RTV0 and DSV
in `MafiaFsrDraw.log`. Candidate pass signatures are descriptive
diagnostics, NOT a final validation of motion vectors.

The proxy registers the game's FSR telemetry and mode-selection callback
with the already verified Modern Tweaks F8 Visuals menu. All four FSR choices
remain **[unavailable]** because the actual game has not yet supplied
raw motion, true depth, per-frame camera/projection/jitter and a successful
FSR shadow output. This safeguard means a false positive cannot enable
an unproven GPU effect.

This ZIP includes `mafia_live_fsr_backend.dll` built with the official AMD
FidelityFX SDK 2.4.0 headers; the backend is present but NOT auto-configured.
Its AMD signed runtime files are not redistributed.

## Installation

The package includes `Instaliraj ili Nadogradi v9.cmd`, which works for
both an original Modern Tweaks 2.5.1 install and our previously installed
F8 beta (v4–v7). The scripts verify backup/version hashes, preserve the
original 61 MB mod and create a reversible manifest. On failure or after the
test run `Vrati originalni mod.cmd`.

The only requested game test: launch through Steam, play/move camera for
~30 seconds, then send `MafiaFsrDraw.log` and `MafiaF8_loader.log`.

**No guarantee of game stability**: executable patching can race the render
thread, and we cannot run Mafia III on the hosted Windows CI machines.
Restoration is provided and must be done if the game doesn't launch.

## Technical remaining requirement

Before turning the FSR Native AA mode on, implement trustworthy actual-game
pass classification and **raw** motion/depth + camera data capture.
The TAA t2 texture contains encoded motion, and older DrawIndexed logs
did not contain depth. No unsafe in-game dispatch occurs from v9.
Quality/Balanced/Performance also require internal 3D render resolution
redirection, not yet developed. Real AMD FSR gameplay has not been
confirmed, and this branch stays a draft PR.

See also `docs/directdraw-fsr-integration-status.md` and
`docs/live-fsr-game-evidence.md`.
