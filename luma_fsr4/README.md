# Mafia III — actual Luma FSR 4.1.2 build (experimental)

Unlike the earlier Modern Tweaks F8 placeholder builds, this branch
builds a genuine, game-specific temporal upscaler integration.

## Architecture

Filoppi/Luma-Framework Mafia III main.cpp has game shader fingerprints,
three separate TAA stages, pre-TAA scene color, earlier raw depth and
motion producers, correct jitter and camera data, and a pre-HUD output
handoff. The shared Luma Core/fsr/FSR4.cpp contains actual official
AMD FidelityFX API upscaling and DX11-to-DX12 synchronization.

We compile the upstream Mafia III project with the flags
UseLumaFSR=true and UseLumaFSR4=true. The latter enables the FSR4
backend and all supporting interop/hooking dependencies.
The baseline installation comes from upstream Luma release latest-840.
Its addon is replaced by our recompiled FSR4-enabled binary. Official
AMD SDK 2.4 signed DX12 loader + upscaler binaries are supplied.

Pinned upstream refs:
Luma SHA: 30c4045a47edc1a175646bd8a0e18b89778f91e5
AMD SDK 2.4 SHA: 7539ae8b6568ad29ed78a753404351daf3b5cca7

## Scope and limitations

The new Luma source path is fundamentally different from the old
opaque Modern Tweaks dinput8 code. No user-provided Modern Tweaks binary
is redistributed or modified by our build.

The custom Luma addon has not yet been run in Mafia III. Windows CI
compiling the addon does not guarantee successful AMD FSR4 processing.
Potential issues include the signed runtime ABI, FSR4 jitter/depth
interop, conflicts between the DXGI and DInput graphical hooks, and
game HUD or framebuffer restoration. Check the game/ReShade log before
claiming that AMD 4.1.2 has actually been selected and presented.

Original Modern Tweaks F8 and Luma's HOME UI are independent.
FSR4 is selected through Luma HOME/ReShade UI. The old experimental
v9/v10 F8 proxy is restored to the verified original Modern Tweaks
during Luma testing to avoid overlapping experimental hooks.
A future, separately validated bridge could link Luma SR settings
to Modern Tweaks F8; this is not present in the current binaries.

A reversible installer and uninstaller preserve file hashes and backups.
The package includes Luma's custom MIT attribution and AMD SDK license.
Respect the upstream license conditions, including required attribution
and restrictions on commercial use.

Source projects:
https://github.com/Filoppi/Luma-Framework
https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK

This branch represents the first serious integration test of the
actual Mafia III temporal FSR4 renderer, not a finished stable release.
