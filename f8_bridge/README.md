# Experimental F8 compatibility beta for Modern Tweaks 2.5.1

**Status: Windows x64 beta build; NOT working AMD FSR in Mafia III.**

The original Visuals/AA ImGui Combo call is located at RVA 0x5EEE3 and has 3 options. The experiment verifies the original DLL SHA-256 (529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900) and redirects ONLY this call to an adapter that appends four **disabled** FSR choices. The original settings for Off/TAA/DLAA remain intact. FSR entries cannot be enabled from synthetic SDK tests alone.

This branch now also builds:
- dinput8.dll, an experimental DirectInput8Create proxy with a worker-thread chain loader;
- mafia_f8_experimental.dll, the version-locked AA Combo extension;
- PowerShell installer and rollback scripts in f8_bridge/beta;
- C++ menu-selection tests.

When installed via the script, the proxy loads a user's local preserved ModernTweaks_original.dll (no binary from the third-party author is distributed), optionally installs the F8 hook and writes MafiaF8_loader.log. The installer verifies the original DLL SHA-256, makes a backup and requires the literal TEST confirmation. Launch the game through Steam. Use the rollback script after any compatibility test.

**Important:** Actual launch, Steam compatibility, F8 hook interaction, resource synchronization and stability with the game have NOT been verified. The beta is reversible but may crash the game. It MUST NOT be presented as a ready-to-use FSR mod.

The genuine FSR 4.1.2 AMD GPU dispatch has been confirmed only in a standalone synthetic test on the user's RX 6700 XT. A real game-frame renderer and pre-HUD compositing stage, per-frame temporal inputs, and lower scene resolution are still required. Until these are independently verified FSR remains unavailable.

Portable tests:
cmake -S f8_bridge -B build-f8
cmake --build build-f8 --config Release
ctest --test-dir build-f8 -C Release --output-on-failure

Windows GitHub Actions builds are also available. See beta/PROCITAJ.txt for test instructions.