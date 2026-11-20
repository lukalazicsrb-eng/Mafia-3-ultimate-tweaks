# Windows D3D11–D3D12 FSR bridge probe

This console program is a standalone hardware test for the proposed Mafia III rendering bridge. It creates D3D11 and D3D12 devices on the same hardware adapter, prefers an AMD adapter, and prints AMD's selected upscaler and numeric provider ID. It runs two FSR dispatches on a stationary 320×180 synthetic input and checks that the 640×360 output can be read on D3D11. The two queues share textures and a GPU fence. The test uses a CPU readback to check two sample pixels.

The executable runs outside the game. Windows GPU execution and the bridge's behavior with actual Mafia III frames still need validation. [Technical report](https://github.com/lukalazicsrb-eng/Mafia-3-ultimate-tweaks/blob/main/docs/fsr-feasibility.md)

## Run on Windows x64

1. Download and extract [AMD FSR SDK 2.4.0](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.4.0). Keep `amd_fidelityfx_loader_dx12.dll` and `amd_fidelityfx_upscaler_dx12.dll` together inside the SDK's `Kits\FidelityFX\signedbin` directory.
2. Download the official [DirectX 12 Agility SDK 1.616.1 NuGet package](https://www.nuget.org/packages/Microsoft.Direct3D.D3D12/1.616.1). It is a ZIP archive with a `.nupkg` extension. Extract `build\native\bin\x64\D3D12Core.dll` to `D3D12\D3D12Core.dll` next to `fsr-interop-probe.exe`.
3. On Windows 10 or 11 with an up-to-date AMD driver, run the probe from PowerShell. Replace the path with the SDK directory you extracted.

```powershell
.\fsr-interop-probe.exe 'C:\SDK\FidelityFX-SDK-2.4.0\Kits\FidelityFX\signedbin'
```

Read the `Selected upscaler` line to see what AMD actually created. A `4.1.2` result or newer followed by `Bridge check passed` verifies the ML provider and both synthetic output checks on that machine. A `3.1.6` result identifies the analytical provider. A successful analytical run verifies the bridge only for that provider. The process returns `0` on bridge success, `1` for initialization, dispatch, synchronization, or output-check errors, and `2` for incorrect command-line usage. Report the adapter, provider, sample values, and any failure text when sharing results. [AMD hardware requirements](https://gpuopen.com/amd-fsr-sdk/#requirements)

## Build from source

With Visual Studio 2022's C++ desktop tools and CMake 3.21 or later, run from the repository root.

```powershell
cmake -S probe -B build -A x64 -DFSR_SDK_ROOT='C:\SDK\FidelityFX-SDK-2.4.0'
cmake --build build --config Release
cmake --install build --config Release --prefix package
```

Builds include the probe and this guide. Obtain the AMD and Microsoft runtime binaries from their official packages.
