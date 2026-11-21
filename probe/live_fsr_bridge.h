#pragma once

#include "fsr_session.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// Deliberately separate from the original Modern Tweaks binary.
// This processor is NOT installed by the F8 beta loader.
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;

namespace mafiafsr {

// All false by default. Must be supported by real game capture, not simulated frames.
struct LiveFrameEvidence {
    bool originalTaaPass = false;
    bool preHudOutput = false;
    bool linearHdrSceneColor = false;
    bool reversedDepth = false;
    bool rawMotionVectors = false;
    bool motionInUvCoordinates = false;
    bool currentFrameJitter = false;
    bool cameraProjection = false;
    bool completeFrame = false;
};

struct LiveFrameTextures {
    ID3D11ShaderResourceView* sceneColor = nullptr; // R11G11B10_FLOAT
    ID3D11ShaderResourceView* sceneDepth = nullptr; // R24_UNORM_X8_TYPELESS view
    ID3D11ShaderResourceView* rawMotion = nullptr;  // R16G16B16A16_FLOAT
    ID3D11RenderTargetView* preHudOutput = nullptr; // R11G11B10_FLOAT
};

// This adapter takes *actual game-provided* pre-TAA textures and performs
// genuine AMD FSR 4.1.2 Native AA through D3D11->D3D12 interop.
// The caller owns a shader/pass hook. This library neither finds that pass,
// patches the executable, nor installs itself inside Mafia III.
class LiveNativeAaBridge final {
public:
    LiveNativeAaBridge(ID3D11Device* existingGameDevice,
                       const std::filesystem::path& amdRuntime,
                       std::uint32_t width, std::uint32_t height);
    ~LiveNativeAaBridge();
    LiveNativeAaBridge(const LiveNativeAaBridge&) = delete;
    LiveNativeAaBridge& operator=(const LiveNativeAaBridge&) = delete;

    // False means the game's ORIGINAL TAA draw must proceed.
    // No game output texture is overwritten unless a real FSR dispatch completed.
    bool process(ID3D11DeviceContext* actualGameContext,
                 const LiveFrameTextures& textures,
                 const LiveFrameEvidence& verified,
                 const FsrFrameParameters& cameraAndTiming) noexcept;

    const std::string& provider() const noexcept;
    const std::string& lastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mafiafsr
