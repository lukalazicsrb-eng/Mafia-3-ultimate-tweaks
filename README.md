# Mafia III: Definitive Edition — FSR 4 feasibility audit

This repository follows development of AMD FSR Upscaling 4.1.2 for Modern Tweaks v2.5.1. It contains a binary analysis of the existing F8 and TAA paths, a read-only tool for inspecting a local copy of the mod, and a standalone Windows probe for testing the D3D11–D3D12 bridge. The in-game integration remains unimplemented.

AMD officially supports FSR Upscaling 4.1.2 on the Radeon RX 6700 XT through Direct3D 12 or Vulkan. The supplied D3D11 mod locates the game's TAA pass and prepares temporal inputs for NVIDIA DLAA. Its DLAA path exits on non-NVIDIA adapters before loading NGX and creates the feature at equal input and output sizes. An FSR implementation needs access to that render stage on AMD, control of the internal 3D resolution, and an output handoff before HUD composition. [AMD SDK requirements](https://gpuopen.com/amd-fsr-sdk/#requirements)

The standalone Windows probe selected FSR Upscaling 4.1.2 on a Radeon RX 6700 XT and passed both synthetic frame checks through the D3D11–D3D12 bridge. [Verification status](docs/fsr-feasibility.md#verification-status)

[Technical report and disassembly findings](docs/fsr-feasibility.md) · [Windows bridge probe](probe/README.md)

## Reproduce the static audit

Run the tool with Python 3 and GNU `objdump` on your `PATH`. Supply your own copy of the release ZIP or its `dinput8.dll`. The tool reads PE metadata and lists byte offsets of selected strings. It leaves the game installation untouched.

```sh
python3 tools/audit_modern_tweaks.py /path/to/modern-tweaks-2.5.1.zip
```

Modern Tweaks is by `nietzchivelli`. Obtain the original release from its author. [Modern Tweaks on Nexus Mods](https://www.nexusmods.com/mafia3definitiveedition/mods/78)
