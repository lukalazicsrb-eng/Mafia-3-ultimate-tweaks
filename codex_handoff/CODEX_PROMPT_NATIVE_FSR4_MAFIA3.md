# CODEX TASK: Finish native AMD FSR 4.1.2 inside Mafia III: Definitive Edition Modern Tweaks 2.5.1 (NO ReShade / NO Luma)

You are a senior Windows graphics / Direct3D 11–12 interop engineer. Implement, compile, validate, and package an actual working AMD FidelityFX Super Resolution 4.1.2 mod for **Mafia III: Definitive Edition** that integrates into the **existing F8 → Visuals → Anti-aliasing dropdown** of **Modern Tweaks 2.5.1 (Nexus Mods mod #78 by nietzchivelli)**. This is an implementation task, **not** an essay, not another generic diagnostics-only prototype, and not a UI-only mockup.

## 0. Non-negotiable requirements

1. **No Luma, no ReShade, no DXGI overlay, no alternate HOME menu, no DLSS-to-FSR replacement masquerading as native integration.** Use the existing Modern Tweaks F8 interface as the only user-facing FSR control. A small native DX11/12 interop component is acceptable, but it must not depend on ReShade, Luma, or external graphics overlays.
2. Target **Windows x64**, **Mafia III: Definitive Edition**, **Modern Tweaks 2.5.1**, AMD **Radeon RX 6700 XT (RDNA2)**, default test output **2560 × 1440**, Steam launch. Do not break Steam initialization or cause duplicate DirectInput/DX11 hook initialization.
3. Add *functional* F8 choices: **Off**, **Game's TAA**, **NVIDIA DLAA** (original choices unchanged), **FSR 4.1.2 Native AA**, **FSR 4.1.2 Quality**, **FSR 4.1.2 Balanced**, **FSR 4.1.2 Performance**. FSR must never be labeled active when it is not really processing the *current in-game frame*. Do not claim 4.1.2 if the actual SDK chooses FSR3 or fails initialization.
4. Prioritize **real Native AA first** (scene render size = output size). Upscaling modes must use actual **reduced internal 3D render resolution**, not a fake resampling of a 1440p image. Keep HUD/UI at native output resolution.
5. Preserve all existing Modern Tweaks features, original F8 ImGui UI, controls, settings, menu behavior, save/config state, original Off/TAA/DLAA paths, visual quality, and 3D rendering. Restore normal Game's TAA on any failed FSR stage. **Never process an unverified resource signature.**
6. **Do not modify or redistribute the author's original Modern Tweaks DLL on disk.** The original v2.5.1 \`dinput8.dll\` must be supplied by the user and preserved in the game folder as a backup/renamed original; any wrapper/relay must forward \`DirectInput8Create\` correctly. Use strict version/hash checks and an opt-in, reversible installer. Respect the third-party author's rights and license.
7. **Deliver real C++ source changes, a compiled Windows x64 test/release ZIP, unit tests, runtime validation and rollback.** Do not stop after a plan. Do not represent synthetic SDK tests, successful compilation, a menu screenshot or a static hook install as proof of working FSR gameplay.
8. The user previously tested a Luma/ReShade attempt and it **removed FSR entries from the F8 menu** and caused **visibly jittering/shaking clouds**. This is a hard regression to prevent. The user explicitly rejected Luma/ReShade. Do not use that branch as the solution.

## 1. Repository and materials

Work in the supplied source bundle, originally from:
- Repository: \`https://github.com/lukalazicsrb-eng/Mafia-3-ultimate-tweaks\`
- Native development branch: \`feature/f8-fsr-menu-experimental\`
- Luma experiment: \`feature/luma-mafia-fsr4\` — **rejected; do not base solution on it**.
- \`f8_bridge/\`: existing F8 ImGui hook, menu state, \`dinput8.dll\` forwarding proxy, Draw pass interception, tests, version-specific beta installers.
- \`probe/\`: AMD FidelityFX SDK 2.4.0 D3D11→D3D12 bridge, format converter shaders, signed-runtime loader, live-frame backend ABI, synthetic tests.
- \`docs/\`: feasibility findings, TAA hooks and temporal-data limitations.
- \`tools/\`: original DLL fingerprint/static checks.
- \`evidence/\` (when included): user-supplied real-game \`MafiaF8_loader(6).log\`, \`MafiaFsrDraw(2).log\`, screenshots, and/or earlier diagnostic logs. Study these before coding.
- The source bundle does **not** contain the proprietary original Modern Tweaks source or its 61 MB binary. Request an authorized local copy of the original DLL only if it is essential for disassembly/runtime validation.

The exact original 2.5.1 \`dinput8.dll\` SHA-256 observed in earlier analysis:
\`529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900\`.
Check this against the actual local user's original if available.

**Build provenance:** use official AMD FidelityFX SDK v2.4.0, pinned commit
\`7539ae8b6568ad29ed78a753404351daf3b5cca7\`, the supported **FSR Upscaling 4.1.2** signed DirectX12 runtime, and verify the actual selected provider and device support at runtime. Confirm current SDK/API requirements rather than guessing from strings.

## 2. Established evidence: don't repeat failed approaches

A. The original Modern Tweaks F8 menu **was successfully extended** in real gameplay by the experimental ImGui adapter: four extra FSR labels appeared, marked \`[unavailable]\`. Later the separate Luma install replaced our proxy with the original author's \`dinput8.dll\`, causing the four extra entries to disappear. This is an installer/loader regression, **not a failure of the initial F8 menu patch**.

B. Original Modern Tweaks 2.5.1 hooks DirectX11. Static findings from this exact binary are version-locked hints, NOT universal game signatures:
- Direct Draw wrapper RVA \`0x79A40\`, original callsite RVA \`0x79A71\` with bytes \`FF 15 D1 80 9C 03\`, original Draw pointer slot \`0x3A41B48\`.
- A separate \`DrawIndexed\` TAA matcher call at RVA \`0x799D6\`; an earlier *indirect* path at \`0x796ED\`. We once hooked the wrong inactive path.
- The DLL's NVIDIA DLAA path is internally conditioned on NGX/RTX; it **does not automatically initialize for an AMD GPU**. Port/capture the temporal resources without requiring the NVIDIA-only branch to run.

C. User ran v5–v10 trace builds. \`MafiaF8_loader(6).log\` reports:
\`Original Modern Tweaks loaded and DirectInput8Create available\`,
\`F8 adapter hook installation returned TRUE\`,
\`DirectDraw probe INSTALLED\`, and **\`FSR not yet available\`**.
\`MafiaFsrDraw(2).log\` shows **131,072** Draw calls, a small set of shader pointers and **zero validated full HDR TAA candidates** under the *current overly strict/broken heuristic*. Many samples show \`2560x1440\` resource \`fmt=27\` with view \`29\`, color resources with mismatched dimensions, \`t2\` absent, and unrelated UI or light passes. Do **not** infer that any one \`Draw(3)\` or \`Draw(6)\` pass is actual TAA based on vertex count, texture dimensions, or pointer address. Hooking live Draw proves a hook runs, not that FSR inputs are correct. Thread/ASLR pointer values vary by game run.

D. Earlier original Modern Tweaks logging (before our hooks) reported a TAA-related stage at 2560×1440, \`R11G11B10_FLOAT\`-like HDR color, **reversed depth** (far=0), motion vector/jitter checks, and plausible temporal resources, but **these logs do not prove direct AMD access to the same live resource objects**. Game's TAA \`t2\` may contain **encoded motion**, not raw AMD-compatible vectors.

E. The \`probe/\` code has run official AMD FSR 4.1.2 GPU processing on **synthetic** frames, including Native AA-sized and upscaled test frames, on the RX 6700 XT. This proves the independent SDK bridge can execute, **NOT** that Mafia III is feeding its frames to it.

F. An independent open-source Mafia III-specific rendering implementation can be consulted *only as technical reference* for game shader stages and pre-HUD order (original TAA, separate raw motion and depth, jitter). **Do not copy, load, package, compile or rely on Luma/ReShade** for the final native mod. Shader fingerprints from other projects may use private hash algorithms; verify identities with original shader bytecode, not unvalidated values.

## 3. Required engineering work (perform, don't just list)

### Phase A — audit and correct what exists
1. Read every \`f8_bridge/\`, \`probe/\`, test, installer, and related \`docs/\` file. Inspect CI workflows and note stale or contradictory documentation. Make a dependency graph: game D3D11 device/context, original TAA pass, shader identity, input resources, temporal metadata, AMD runtime session, output RTV, F8 state and original fallback.
2. Make an explicit table of currently *real*, *simulated*, *guessed* and *missing* inputs and hooks. Delete or quarantine unsafe "always true" evidence booleans or cached/synthetic camera values in live code. Do not relax validation just to make the UI show Enabled.
3. Audit exact C++ ABI/calling conventions, loader lock safety, initialization order, thread races, recursion, COM reference counting, GPU resource state barriers, fence lifetimes, render-thread safety, \`VirtualProtect\` patching atomicity and method forwarding. Avoid startup crashes, deadlocks, unbounded GPU waits, wild scan/hook loops. Do not bypass Steam authentication or license checks.
4. Preserve/extend static signature tests for original Modern Tweaks 2.5.1 and fail closed on unknown hashes/offsets. Record any new verified RVA/callsite with method and bytes.

### Phase B — identify the real Mafia III TAA producer, not arbitrary Draw
1. Identify original \`CreatePixelShader\`/shader bytecode identities and the actual TAA pixel shader(s) **executed on the user's AMD path**, including render pass order. Distinguish postprocess, UI, particle/cloud, sky and geometry shaders from temporal reconstruction.
2. On a valid game frame, acquire **pre-TAA HDR scene color**, **raw depth**, **raw motion vectors** (not the game's encoded TAA history or unrelated t2), and the correct *pre-HUD* output target. Preserve texture ownership/reference lifetimes.
3. Capture the proper **jitter offsets, camera matrices/projection, near/far planes, FOV, frame delta time, exposure/pre-exposure, render/output size, motion vector scale/sign/convention**, view-space-to-meters and temporal reset history flags for the *same* frame/view. Validate whether depth is inverted, motion is UV vs pixel-space, includes jitter, and which constant buffers carry the data. Do not make up values.
4. Match resource frame IDs or an equivalent render-timeline invariant. Avoid stale previous frame motion, different frame depth, duplicated per-frame dispatches, 3D/cutscene transitions and paused frames. Capture *before* vanilla TAA and restore FSR output *before* bloom/HUD where appropriate.
5. Prefer minimal game-specific, shader-identity-based instrumentation to broad \`Draw\` interception. Keep bounded diagnostics with meaningful metadata (shader hash, pass ID, resource format/size, frame index), not repetitive huge logs. Do not request repeated tests merely confirming the DLL loaded.

### Phase C — functional FSR 4.1.2 Native AA
1. Reuse and repair \`probe/live_fsr_bridge.cpp\`, \`live_fsr_api.*\`, \`fsr_session.*\` and \`game_fixture.*\` as a real-game processor; do not take synthetic texture fixtures as substitutes for actual game inputs.
2. Initialize the D3D12 device on the **same physical AMD adapter** as the actual game D3D11 device. Correctly share/copy GPU textures, convert HDR color/depth/motion into official FSR formats, synchronize GPU work with fences, prevent stale frames and minimize render-thread blocking. Ensure correct state before/after FSR dispatch.
3. Load the **official signed AMD SDK 2.4 FSR Upscaling 4.1.2 runtime** and verify the provider ID/version and capabilities. Fail cleanly on any unsupported runtime/device. No invisible fallback to FSR3 while the UI claims FSR4.
4. First run a **shadow/preview dispatch on live Mafia III resources** without modifying the displayed frame, and validate output dimensions, finite pixel values and completion. Then implement an opt-in actual pre-HUD output handoff; do not render both Game's TAA and FSR for the same scene frame.
5. Handle jitter/history correctly across scene cuts, camera motion, sky, clouds, particles, transparency/alpha masks and disocclusion. User saw **shaking clouds** after the rejected Luma experiment; include video/screenshot regression comparison against original Modern Tweaks. Identify and fix ghosting, sky wobble, shimmering vegetation, flickering distant detail, UI ghosting and duplicated sharpen passes.
6. Log a runtime event **only after confirmed live AMD 4.1.2 dispatch and presented output**: \`FSR4_PROVIDER=4.1.2\`, \`FSR4_GAME_FRAME_DISPATCHED=1\`, \`FSR4_OUTPUT_PRESENTED=1\` and frame/size/checksum or suitable telemetry. Mere successful \`LoadLibrary\`, ImGui F8 label or D3D12 context creation is not sufficient.

### Phase D — real F8 integration, no second UI
1. Edit existing \`f8_bridge/native_f8_adapter.cpp\`, \`menu_state.*\` and the game's loader so that F8 \`Visuals → Anti-aliasing\` changes FSR mode **immediately and persistently** within the original Modern Tweaks interface.
2. Keep original Off/TAA/NVIDIA DLAA behavior and settings. Native AA becomes selectable only after evidence confirms a valid runtime setup and real-frame processing. If startup initialization is pending, show an accurate initialization status. If unsupported, show \`[unavailable]\` with a meaningful reason, not an inert option pretending to work.
3. Persist FSR choice in a game-local configuration separate from original proprietary settings if necessary, with sensible reset/fallback and no malformed F8 selections. Correctly handle hot-switching between TAA, Native AA, Quality, Balanced and Performance, resource recreation and temporal history reset.
4. No ReShade menus, Luma overlays, alternate hotkey-based selector or dependency on injected \`dxgi.dll\`. \`dinput8.dll\` must still forward \`DirectInput8Create\` and leave all standard Modern Tweaks F8 features working.

### Phase E — actual Quality/Balanced/Performance
1. Only after Native AA is confirmed working, implement control of **real 3D internal scene render resolution**. Preserve 2560×1440 presentation/HUD, and correctly downscale 3D render targets/viewport/scissor, depth, motion, TAA history and postprocess inputs.
2. Let the SDK choose/recommend valid scaling ratios and support at least 1440p output. Changing window/display resolution or downsampling a full native frame is NOT FSR upscaling.
3. Reset history on resolution and mode changes; verify native UI composition, depth-based effects, water, vegetation, shadow pass, sky/cloud stability and cutscenes. Compare FPS and frame latency under consistent scenes.
4. Do not claim Quality/Balanced/Performance completion unless the captured **internal render width/height is genuinely smaller than output width/height**, FSR4 dispatch succeeds and output is visibly presented.

## 4. Builds, tests and working deliverable

- Use modern MSVC/CMake + Windows SDK, x64 Release. Repair old CI assumptions (e.g., hardcoded VS2022 generator if runner lacks it). Use existing GitHub Actions to compile ALL targets and run a strict failing test suite; do not ignore build errors or \`CTest\` failures.
- Add automated tests for: original F8 menu behavior; unsupported versions; provider-selection reporting; missing or mismatched resources; inverted depth/motion conversion; resize/cutscene/history reset; initialization/failure fallback; ABI; loader forwarding; safe install and rollback; atomic hook patching/concurrent render threads where testable.
- Actual gameplay verification needs a real Windows machine with Mafia III on RX 6700 XT. If unavailable to Codex, clearly list what is compiled vs what remains unverified, and prepare **one targeted user test** that obtains new information, not another identical hundreds-of-Draw logger.
- Install/upgrade script must accept user's original Modern Tweaks 2.5.1 and older F8 beta, refuse unrecognized hashes, avoid overwriting unrelated DLLs, preserve existing settings and originals, and provide a **one-click restore** with manifest and per-file SHA-256 validation.
- Deliver \`MafiaIII_ModernTweaks_FSR4.1.2_Win64.zip\` with compiled loader/bridge DLLs, actual source diff, README in Serbian, installation+recovery scripts and test results. If genuine game-frame processing is NOT implemented, name output **DEVELOPMENT BUILD**, not a final release. Never package the original third-party Modern Tweaks DLL as your own.
- Update the native GitHub PR/branch with substantive committed code, build hashes and a transparent status matrix of verified in-game vs synthetic tests. Keep rejected Luma work closed/separate.
- Aim for minimal DLL complexity and best compatibility. A clean single user-installable proxy \`dinput8.dll\` plus a verified preserved original mod is preferred over conflicting hooks. No additional displays or external overlays.

## 5. Acceptance criteria and anti-hallucination gates

A release may claim "**FSR4 Native AA works**" ONLY if:
- The exact AMD FSR Upscaling **4.1.2** provider actually selected on RX 6700 XT;
- A *real Mafia III current frame* is processed with verified pre-TAA HDR color, depth, raw motion, frame-matched jitter/camera data;
- The FSR result is physically written to and presented from the correct pre-HUD target, and the original TAA is NOT also applied;
- The F8 choice (not HOME/ReShade) enables/disables it and survives a game restart;
- Comparative screenshots/video show no severe image corruption, no recurrent shaking clouds, no significant UI ghosting, and the game boots reliably through Steam;
- Logs/telemetry and actual screenshots corroborate, not merely a green GitHub CI badge.

Quality/Balanced/Performance require ALL of the above AND proven reduced internal 3D render sizes. Do not claim completion of modes that do not satisfy this.

If a step is genuinely blocked by missing original code or unavailable runtime test hardware, **continue all independent implementation that can be done**, commit it, and precisely name the missing evidence/dependency. Never write "Done", "Fully integrated", or "FSR active" simply because the F8 dropdown changed, a synthetic AMD probe ran, or a Windows DLL compiled.

## 6. Start immediately with this action sequence

1. Explore the supplied repository's source, docs, tests and evidence. Summarize exact confirmed blockers without asking the user to reupload existing files.
2. Investigate the game's real TAA and earlier raw-motion/depth producer shaders from the original Modern Tweaks hook path. Correct the tracing/registration architecture rather than repeating v5–v10 sampling.
3. Implement the native source changes, run a Windows build and tests, fix all failures, and produce the compiled ZIP.
4. Show concrete files changed, Git commits, build/test evidence, status of each FSR mode, and the download link. Request a single purposeful in-game test ONLY if all other possible work is done.

**The user wants a native Modern Tweaks F8 FSR 4.1.2 mod. Do not change the assignment to Luma, ReShade, a separate graphics mod, a list of theoretical suggestions, or repeated log collection.**