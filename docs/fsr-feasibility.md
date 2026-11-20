# FSR 4.1.2 feasibility in Modern Tweaks v2.5.1

The v2.5.1 DLL has a game-specific TAA interception point with access to scene color, motion vectors, and depth in its DLAA path. The same binary rejects the RX 6700 XT before loading NGX, and the DLAA path feeds equal input and output dimensions to NGX. An F8 integration needs a way to reach the game's temporal resources on AMD and a verified way to render the 3D scene below the presentation resolution. A Windows capture is needed to verify the resource semantics and the handoff back to the game's post-processing and HUD.

## What was inspected

The supplied archive passed its ZIP integrity check. It contains `dinput8.dll`, `README.md`, and `INSTALLATION.txt`. The 61,071,872-byte x64 PE DLL has SHA-256 `529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900` and preferred image base `0x243500000`. The archive contains no source or extension SDK, and searches of the author's release page and public repositories found no public source or extension API. The project requester confirmed author permission and that source code is unavailable, so the investigation uses the shipped binary. [Modern Tweaks release](https://www.nexusmods.com/mafia3definitiveedition/mods/78)

### Loader, menu, and configuration

- The archive's documented install location is alongside `mafia3definitiveedition.exe`. `DirectInput8Create` at DLL RVA `0x1ac0` is the only named export. The forwarding path obtains the Windows system directory around RVA `0x1c62`, loads that directory's `dinput8.dll`, and resolves the original export. A second game-local DLL under the same filename would replace this entry point.
- The import table lists `D3D11CreateDevice` and `D3D11CreateDeviceAndSwapChain`. Static code calls the latter in the code around RVA `0x51570` and uses swap-chain vtable slot 8 (`Present`) when setting up the hook around `0x515ed`. The mod contains Dear ImGui 1.92.8 with Win32 and D3D11 backends. Its documentation identifies F8 and Back + Start as menu entry points. No public menu registration interface was found.
- The mod's documentation places `ModernTweaks.dat` beside the game. The binary contains the file name, sectioned settings text, and distinct `[Visuals]` settings `TemporalAA` and `DLAA`. Any source-level extension would need to retain those settings and presets and leave FSR opt-in. Configuration migration and behavior in-game remain untested.

The Visuals callback is dispatched inside the existing menu window at RVA `0x61b26`. Its function starts at `0x5ede0`, and its existing combo call at `0x5eee3` receives three localized labels. The callback derives the selected index from the settings bytes at object offsets `0x319` (`TemporalAA`) and `0x324` (`DLAA`). After a change, it writes `TemporalAA = index > 0`, `DLAA = index == 2`, and marks the settings dirty. Index `3` alone would therefore persist as game TAA. A separate opt-in value and an explicit rendering state transition are needed for FSR. A version-specific extension can use the callback inside Visuals and preserve the existing setting behavior for choices 0–2. Menu, preset, hotkey, and persistence behavior would still need Windows testing.

The menu evidence can be reproduced with GNU `objdump` on the DLL with the SHA-256 above, using preferred virtual addresses for disassembly. The first command shows the combo count, call and setting writes. The second shows the category dispatch.

```sh
objdump -d -M intel --start-address=0x24355eec5 --stop-address=0x24355ef06 dinput8.dll
objdump -d -M intel --start-address=0x243561b1a --stop-address=0x243561b29 dinput8.dll
```

### DLAA and temporal inputs

Addresses below are relative virtual addresses in the submitted DLL only. They describe the code inspection and are specific to this file hash.

| Evidence | Static result |
| --- | --- |
| TAA interception around `0x83c80` | The matcher checks the pixel shader, reads pixel shader resources `t0`–`t2`, and inspects the bound render targets. The traced DLAA data flow uses `t0` for scene color, `t2` for source motion data, the bound depth-stencil resource for depth, and `RTV0` for the result. It checks for `R11G11B10_FLOAT` on `t0`, `t1`, and `RTV0`, and `R16G16B16A16_FLOAT` on `t2`, with matching dimensions. Those format checks describe the mod's matching criteria. |
| Adapter test around `0x7cf07` | The code compares the DXGI vendor ID with NVIDIA's `0x10de`. On a mismatch the branch at `0x7cf12` leaves initialization before loading NGX. The NVIDIA path resolves the `NVSDK_NGX_D3D11_*` functions dynamically after locating `_nvngx.dll` via driver information. |
| NGX setup around `0x7fff0` | Feature input and output width are written from the same value, as are the heights. This is the native-resolution DLAA path. Selecting a different NGX output implementation alone would leave the internal-resolution problem unsolved. |
| NGX evaluation around `0x81f21`–`0x8211d` | The mod prepares private color, motion, and depth textures, populates the NGX inputs via the helper at `0x7c340`, and renders a successful result back to `RTV0`. The parameter builder sets jitter and a reset flag, passes null exposure and transparency textures, and sets frame-time delta to zero. Those values describe the existing NGX path. AMD inputs must be validated against AMD's conventions. |

The audit identifies a candidate game pass and the mod's resource roles. It does not establish the resource descriptors or color encoding on the target Windows build, depth inversion and clip planes at runtime, the motion-vector units and sign across camera cuts, or interaction with post-processing and the HUD. The TAA matcher, resource creation path, and HUD composition need capture-based validation together. No independent method of resizing the 3D scene and its dependent targets was established.

The draw hook around RVA `0x796ed` calls the matcher and skips the original draw on success. The settings applier at `0x307b0` treats `TemporalAA || DLAA` as the condition for leaving the game's TAA enabled, so the game can issue the draw. When an FSR replacement is active, the integrated path needs to preserve this fallback until an upscaled result is ready and keep the original DLAA path mutually exclusive.

The DLAA code creates a private texture around RVA `0x84ac0` using dimensions from the game's already bound resources and uses them for the fullscreen blit around `0x80a3b`. The inspected device and immediate-context hooks target shader creation, draw and dispatch calls, and sampler state. The visual settings resolver includes a half-resolution reflections switch. The inspected hooks and settings include no verified scene-wide allocation or viewport control. Changing the DLAA private dimensions would leave the game's earlier geometry and G-buffer work at its original resolution.

### Original DLL diagnostics for a Windows capture

The F8 menu exposes `Write ModernTweaks.log`, effective on the next launch. A normal TAA gameplay capture on Radeon is the baseline. A separate test can use the existing internal `MT_TEST_DLAA` environment variable, with `DLAA=1` effective in `[Visuals]` so the original calibration code runs. The check at `0x82c25` bypasses NVIDIA initialization when the variable is present, and the draw branch at `0x81c20` bypasses NGX evaluation. The calibration path can still write a copy into `RTV0` and skip the original TAA draw. Compare it against the separate baseline and remove the environment variable before ordinary play. The diagnostic reports TAA sizes, formats, depth classification, motion calibration, and jitter. Resource inspection remains necessary for the original projection and exposure values and actual frame ordering.

### Confirmed gameplay inputs

The original Modern Tweaks 2.5.1 diagnostic ran in the game on Windows and exited normally. The game EXE SHA-256 was `1db8d827c1528c3b80861eeed111a3f11a357fc5683f2b5640dbe119f871abaf`. The mod logged a matched TAA pass and texture initialization at **2560×1440**.

| Input | Observed game evidence | Native AA handling |
| --- | --- | --- |
| Scene color | DXGI format `26`, `R11G11B10_FLOAT` | Convert to shareable FP16 RGBA and blit the result back to the existing R11G11B10 target. The separate Luma pass map identifies the scene color as linear HDR. |
| Depth | DXGI format `44`, `R24G8_TYPELESS`. The diagnostic identified reversed depth with far at zero. | Read through an `R24_UNORM_X8_TYPELESS` view and convert to normalized `R32_FLOAT` without changing the depth convention. Set AMD's inverted-depth flag. |
| Motion | The mod's lowest-error interpretation was XY in UV units (`0.67` versus `1.00` with no vectors). | Convert UV displacement to pixels with render width and height, and preserve the current-to-previous sign. Confirm projected jitter in the chosen producer before enabling jitter cancellation. |
| Jitter | First fit used frame count with offset `0`, explaining 93% of the observations. Fitted scales varied later in the run. | Supply the applied jitter for the actual frame. The fitted scales describe the diagnostic and cannot replace the per-frame jitter input. |

The evaluator call at DLL RVA `0x85210` receives the D3D11 context, a record of the matched TAA resources, and a consecutive-frame flag. The scene color, motion, output RTV and depth view are available in that record. Returning success suppresses the original TAA draw, so an integration needs to return failure until official dispatch and output handoff succeed. The current build exercises the color, depth and motion converters with synthetic frames. Before calling FSR on game frames, read and verify the actual projection and exposure in the matched draw's per-view constant data. The log does not establish near and far distances, vertical FOV, pre-exposure, or independent scene-size control. [Luma pass and camera candidates](https://github.com/Filoppi/Luma-Framework/blob/30c4045a47edc1a175646bd8a0e18b89778f91e5/Source/Games/Mafia%20III/main.cpp) · [AMD input contract](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-ml/#input-resources)

## Backend feasibility

**AMD FSR SDK 2.4.0** supplies signed FSR Upscaling 4.1.2 DLLs for D3D12 and Vulkan. AMD lists the RX 6700 XT with the full INT8 model. D3D12 requires Shader Model 6.6, and the Vulkan 1.2 path requires Adrenalin 26.10.1 or later. On other GPUs the API can select the analytical FSR 3.1.6 fallback. ML Frame Generation 4.0.2 has separate requirements, including RX 9000 series or later and Windows 11. [SDK release](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.4.0) · [SDK requirements](https://gpuopen.com/amd-fsr-sdk/#requirements) · [AMD upscaling manual](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-ml/)

**D3D11 to D3D12** is the best-evidenced route for a prototype. Use the same adapter on both devices and allocate explicitly shareable textures that both APIs accept. After D3D11 GPU-side copies or conversions, synchronize ownership in both directions with shared fences, perform the AMD dispatch in D3D12, and return the output to the game at the validated pre-HUD point. Any copies or conversions need to fit the frame budget. D3D11On12 creates a new D3D11 device over an existing D3D12 device, so attaching it to the current game would require control over device creation. Luma demonstrated a shared-resource bridge with FSR 4.1.1 in Scarlet Nexus, which provides a precedent for the bridge itself. [Microsoft shared resources](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createsharedhandle) · [Microsoft D3D11On12](https://learn.microsoft.com/en-us/windows/win32/api/d3d11on12/nf-d3d11on12-d3d11on12createdevice) · [Luma D3D11 prototype](https://github.com/Filoppi/Luma-Framework/pull/238)

**Vulkan** is another possible backend. DXVK exposes Vulkan handles and image layout transitions through its D3D11 interop interface. A game-specific integration would have to validate access to the temporal textures on the same Vulkan device, AMD's required device capabilities, synchronization, image usage and layouts, and interaction with the existing mod. The translated rendering API alone does not determine which game textures are appropriate for upscaling. [DXVK interop implementation](https://github.com/doitsujin/dxvk/blob/master/src/d3d11/d3d11_interop.cpp) · [AMD SDK requirements](https://gpuopen.com/amd-fsr-sdk/#requirements)

**OptiScaler** v0.9.5 includes FSR 4.1.2 and supports intercepting D3D11 NGX calls for a D3D12 FSR backend. Its replacement depends on seeing NGX create and evaluate calls. Modern Tweaks' Radeon path exits before reaching either call and uses explicit NGX loading on the NVIDIA path. No drop-in OptiScaler configuration was verified for this mod. [OptiScaler v0.9.5](https://github.com/optiscaler/OptiScaler/releases/tag/v0.9.5) · [D3D11 NGX interception source](https://github.com/optiscaler/OptiScaler/blob/v0.9.5/OptiScaler/inputs/NVNGX_DLSS_Dx11.cpp#L512-L554)

**Luma's separate Mafia III implementation** provides corroborating game-specific source. It retrieves pre-TAA linear HDR color and interprets `t2` in the TAA pass as encoded motion. It takes raw motion and scene depth from earlier passes and handles jitter, inverted depth, motion-vector scaling, and history reset for its super-resolution path. The pinned Mafia III project enables NGX and does not opt in to Luma's FSR 4 backend. Its source establishes useful candidate passes. Coexistence with Modern Tweaks' TAA replacement, control of independent game render resolution, and integration into the Modern Tweaks F8 menu remain unverified. [Game pass and dispatch code](https://github.com/Filoppi/Luma-Framework/blob/30c4045a47edc1a175646bd8a0e18b89778f91e5/Source/Games/Mafia%20III/main.cpp#L378-L625) · [Mafia III build configuration](https://github.com/Filoppi/Luma-Framework/blob/30c4045a47edc1a175646bd8a0e18b89778f91e5/Source/Games/Mafia%20III/Mafia%20III.vcxproj#L57-L70)

## Implementation and validation dependencies

1. Verify the standalone bridge on the target Radeon RX 6700 XT. This step passed on Windows with FSR 4.1.2. [Probe source and instructions](../probe/README.md)
2. Verify the remaining per-view inputs and frame order in the exact Windows game build. The gameplay diagnostic established the dimensions, formats, depth convention and motion interpretation above. Use real resource inspection to verify projection, jitter, exposure and whether the output is consumed before HUD composition. The mod's `MT_TEST_RECORD` flag records one frame at initialization and may report a startup frame. [RenderDoc capture guide](https://github.com/baldurk/renderdoc/blob/v1.x/docs/how/how_capture_frame.rst)
3. Validate an FSR Native AA dispatch on actual game frames through the candidate TAA stage. Confirm that the dispatch output reaches post-processing and the HUD in order, and validate motion, cuts, depth, color, exposure, reactive inputs where needed, and history reset. Keep the game's TAA path available until dispatch and output handoff succeed. [AMD integration manual](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-ml/)
4. Identify where the game controls 3D resource allocations, viewports and resolution-dependent constants. Verify lower-resolution scene rendering at a fixed output and full-resolution HUD. At a 2560×1440 output, inspect actual sizes near 1707×960 in Quality and 1280×720 in Performance, allowing for alignment. Check dependent passes, resizing, Alt+Tab, and device recreation.
5. Integrate the validated renderer into F8, with TAA, DLAA, Off, and FSR using one anti-aliasing path at a time. Verify fallback, settings persistence, Vanilla and Author's Choice, screenshot and gameplay features, menu inputs, and existing behavior on an RTX system. Measure image quality and frame times at a fixed output size with matched scenes.

## Verification status

Completed archive integrity and SHA-256 checks, PE import and export inspection, code and data tracing in the supplied DLL, and review of official SDK and upstream renderer sources. The audit script was checked with both the release ZIP and extracted DLL. The standalone probe was compiled for Windows x64 against the official SDK 2.4.0 headers.

A user-provided Windows run on the **Radeon RX 6700 XT** selected **AMD FSR Upscaling 4.1.2** with provider ID `17700776140664213506`. Both synthetic 320×180 to 640×360 dispatches passed the expected D3D11 output readback checks, and the probe exited with code `0`. This verifies the standalone bridge for the tested texture formats and stable DirectX Agility SDK 1.616.1. [Probe source and instructions](../probe/README.md)

The game subsequently reported a real 2560×1440 TAA pass with reversed depth and normalized XY motion. That run exercised the original mod's diagnostic path. [Game evidence](#confirmed-gameplay-inputs)

Game launch with an integrated FSR path, menu behavior, FSR dispatch on real game frames, internal-resolution changes, DLAA regressions, persistence, stability, image quality, and performance remain untested.
