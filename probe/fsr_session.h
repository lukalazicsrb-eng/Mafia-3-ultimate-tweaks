#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

struct FsrContextFlags {
    bool linearHdr = false;
    bool invertedDepth = false;
    bool infiniteDepth = false;
    bool motionIncludesJitter = false;
};

struct FsrFrameParameters {
    float jitterX;
    float jitterY;
    float motionScaleX;
    float motionScaleY;
    float frameTimeMs;
    float preExposure;
    float cameraNear;
    float cameraFar;
    float verticalFov;
    float viewSpaceToMeters;
    bool reset;
};

class FsrSession final {
public:
    FsrSession(ID3D12Device* device, const std::filesystem::path& runtimeDirectory,
        std::uint32_t renderWidth, std::uint32_t renderHeight,
        std::uint32_t outputWidth, std::uint32_t outputHeight, FsrContextFlags flags = {});
    // The caller must retire submitted dispatches before destroying the session.
    ~FsrSession();

    FsrSession(const FsrSession&) = delete;
    FsrSession& operator=(const FsrSession&) = delete;

    const std::string& providerName() const noexcept;

    // The caller supplies compute-readable inputs and a UAV-ready output.
    void dispatch(ID3D12GraphicsCommandList* commands, ID3D12Resource* color,
        ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output,
        const FsrFrameParameters& parameters);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
