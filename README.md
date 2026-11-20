# Mafia III: Definitive Edition — FSR 4 feasibility audit

This repository documents a static audit of Modern Tweaks v2.5.1 for adding AMD FSR Upscaling 4.1.2 to its F8 menu. It contains the findings and a read-only tool for inspecting a local copy of the mod. The in-game integration remains unimplemented.

AMD officially supports FSR Upscaling 4.1.2 on the Radeon RX 6700 XT through Direct3D 12 or Vulkan. The supplied D3D11 mod locates the game's TAA pass and prepares temporal inputs for NVIDIA DLAA. Its DLAA path exits on non-NVIDIA adapters before loading NGX and creates the feature at equal input and output sizes. An FSR implementation needs access to that render stage on AMD, control of the internal 3D resolution, and an output handoff before HUD composition. [AMD SDK requirements](https://gpuopen.com/amd-fsr-sdk/#requirements)

[Read the technical report](docs/fsr-feasibility.md)

## Reproduce the static audit

Run the tool with Python 3 and GNU `objdump` on your `PATH`. Supply your own copy of the release ZIP or its `dinput8.dll`. The tool reads PE metadata and lists byte offsets of selected strings. It leaves the game installation untouched.

```sh
python3 tools/audit_modern_tweaks.py /path/to/modern-tweaks-2.5.1.zip
```

Modern Tweaks is by `nietzchivelli`. Obtain the original release from its author. [Modern Tweaks on Nexus Mods](https://www.nexusmods.com/mafia3definitiveedition/mods/78)
