#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

class FsrSession final {
public:
    FsrSession(ID3D12Device* device, const std::filesystem::path& runtimeDirectory,
        std::uint32_t renderWidth, std::uint32_t renderHeight,
        std::uint32_t outputWidth, std::uint32_t outputHeight);
    // The caller must retire submitted dispatches before destroying the session.
    ~FsrSession();

    FsrSession(const FsrSession&) = delete;
    FsrSession& operator=(const FsrSession&) = delete;

    const std::string& providerName() const noexcept;

    // The caller supplies compute-readable inputs and a UAV-ready output.
    // The fixture has a stationary scene and uses zero jitter and zero motion.
    void dispatch(ID3D12GraphicsCommandList* commands, ID3D12Resource* color,
        ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output,
        bool reset);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
