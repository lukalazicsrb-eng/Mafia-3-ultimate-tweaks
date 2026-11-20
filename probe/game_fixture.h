#pragma once

#include <array>
#include <cstdint>
#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

// The dimensions describe a full-resolution Native AA pass. All calls use
// the immediate context of the device supplied to the constructor.
class GameFixture final {
public:
    GameFixture(ID3D11Device* device, std::uint32_t width, std::uint32_t height);
    ~GameFixture();

    GameFixture(const GameFixture&) = delete;
    GameFixture& operator=(const GameFixture&) = delete;

    void renderAndConvert(ID3D11DeviceContext* context, ID3D11Texture2D* sharedColor,
        ID3D11Texture2D* sharedDepth, ID3D11Texture2D* sharedMotion, std::uint32_t frameIndex = 0);

    // These passes restore the caller's D3D11 pipeline state. Source and
    // destination views must refer to distinct, full-size, mip-0 2D textures.
    // The caller orders cross-API access to the shared destinations.
    void convert(ID3D11DeviceContext* context, ID3D11ShaderResourceView* gameColor,
        ID3D11ShaderResourceView* gameDepth, ID3D11ShaderResourceView* gameMotion,
        ID3D11RenderTargetView* sharedColor, ID3D11RenderTargetView* sharedDepth,
        ID3D11RenderTargetView* sharedMotion);

    void blit(ID3D11DeviceContext* context, ID3D11Texture2D* sharedFsrOutput);
    void blit(ID3D11DeviceContext* context, ID3D11ShaderResourceView* sharedFsrOutput,
        ID3D11RenderTargetView* gameOutput);

    ID3D11Texture2D* gameOutput() const noexcept;
    static std::array<float, 3> decodeGameColor(std::uint32_t packed) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
