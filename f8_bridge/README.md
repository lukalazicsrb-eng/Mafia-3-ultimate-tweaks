# Experimental Modern Tweaks v2.5.1 F8 AA extension

**Status: source-code research prototype; no functional in-game FSR yet.**

Static disassembly of the exact original dinput8.dll (SHA-256 529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900) identified the ImGui anti-aliasing Combo call in the original Visuals menu: Visuals handler RVA 0x5EDE0, count of three at RVA 0x5EEC5, Combo call RVA 0x5EEE3 targeting RVA 0x10E3B0. The original handler stores TemporalAA = index > 0, DLAA = index == 2.

The C++ files in this directory implement the FSR selection policy and tests. They preserve the original three AA modes. Proposed modes are FSR 4.1.2 Native AA, Quality, Balanced and Performance. A mode cannot be accepted unless actual in-game runtime telemetry confirms provider 4.1.2, bridge readiness, temporal inputs, a successful game-frame dispatch, and presentation. Real lower-resolution scene rendering is additionally required for Quality/Balanced/Performance. The synthetic bridge test in the existing probe folder is insufficient.

Build/test: cmake -S f8_bridge -B build-f8 ; cmake --build build-f8 ; ctest --test-dir build-f8 --output-on-failure

**Not yet implemented:** finished Windows call-site patch/loader, actual integrated game FSR renderer, native in-game F8 testing, automatic install, persistence of FSR selection, lower-resolution 3D rendering, ready-to-use dinput8.dll. The original Modern Tweaks binary is not distributed or modified.

The standalone Windows FSR bridge probe in ../probe has already succeeded on synthetic frames. This branch focuses on connecting verified real-game rendering with the original F8 interface safely.