#include "live_fsr_bridge.h"
#include "game_fixture.h"

#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace mafiafsr {
namespace {
using Microsoft::WRL::ComPtr;

void check(HRESULT hr, const char* where) {
    if (FAILED(hr)) {
        std::ostringstream s;
        s << where << " failed with HRESULT 0x" << std::hex
          << static_cast<std::uint32_t>(hr);
        throw std::runtime_error(s.str());
    }
}

struct ScopedHandle {
    HANDLE h = nullptr;
    ~ScopedHandle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE handle) : h(handle) {}
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
};

struct SharedTexture {
    ComPtr<ID3D11Texture2D> d11;
    ComPtr<ID3D12Resource> d12;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
};

SharedTexture share(ID3D11Device* d11, ID3D12Device* d12,
                    UINT width, UINT height, DXGI_FORMAT format, bool output) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = format;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (output)
        desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    SharedTexture t{};
    check(d11->CreateTexture2D(&desc, nullptr, &t.d11), "CreateTexture2D (shareable)");
    ComPtr<IDXGIResource> dxgiResource;
    check(t.d11.As(&dxgiResource), "QueryInterface IDXGIResource");
    HANDLE legacyHandle = nullptr;
    check(dxgiResource->GetSharedHandle(&legacyHandle), "GetSharedHandle");
    // Legacy handle belongs to the D3D11 resource. Do NOT CloseHandle it.
    check(d12->OpenSharedHandle(legacyHandle, IID_PPV_ARGS(&t.d12)),
          "D3D12 OpenSharedHandle");
    if (!output)
        check(d11->CreateRenderTargetView(t.d11.Get(), nullptr, &t.rtv),
              "CreateRenderTargetView (FSR input)");
    else
        check(d11->CreateShaderResourceView(t.d11.Get(), nullptr, &t.srv),
              "CreateShaderResourceView (FSR output)");
    return t;
}

struct TextureDesc {
    D3D11_TEXTURE2D_DESC resource{};
    DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
    bool valid = false;
};

TextureDesc describe(ID3D11ShaderResourceView* view) {
    if (!view) return {};
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
    view->GetDesc(&vd);
    if (vd.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) return {};
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> tex;
    if (!resource || FAILED(resource.As(&tex))) return {};
    TextureDesc result{};
    tex->GetDesc(&result.resource);
    result.view = vd.Format;
    result.valid = true;
    return result;
}

TextureDesc describe(ID3D11RenderTargetView* view) {
    if (!view) return {};
    D3D11_RENDER_TARGET_VIEW_DESC vd{};
    view->GetDesc(&vd);
    if (vd.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D) return {};
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> tex;
    if (!resource || FAILED(resource.As(&tex))) return {};
    TextureDesc result{};
    tex->GetDesc(&result.resource);
    result.view = vd.Format;
    result.valid = true;
    return result;
}

bool validTexture(const TextureDesc& d, UINT width, UINT height, DXGI_FORMAT viewFormat) {
    return d.valid && d.resource.Width == width && d.resource.Height == height
        && d.resource.MipLevels == 1 && d.resource.ArraySize == 1
        && d.resource.SampleDesc.Count == 1 && d.view == viewFormat;
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource,
                                  D3D12_RESOURCE_STATES toState) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = toState;
    return barrier;
}

bool hasEvidence(const LiveFrameEvidence& e) {
    return e.originalTaaPass && e.preHudOutput
        && e.linearHdrSceneColor && e.reversedDepth
        && e.rawMotionVectors && e.motionInUvCoordinates
        && e.currentFrameJitter && e.cameraProjection
        && e.completeFrame;
}
}  // namespace

struct LiveNativeAaBridge::Impl {
    ComPtr<ID3D11Device> gameDevice;
    ComPtr<ID3D11Device5> gameFenceDevice;
    ComPtr<ID3D12Device> d12;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence12;
    ComPtr<ID3D11Fence> fence11;
    ScopedHandle fenceHandle;
    ScopedHandle completionEvent;
    SharedTexture color;
    SharedTexture depth;
    SharedTexture motion;
    SharedTexture output;
    std::unique_ptr<GameFixture> converters;
    std::unique_ptr<FsrSession> fsr;
    std::uint64_t nextFence = 0;
    UINT width = 0;
    UINT height = 0;
    bool disabled = false;
    // Unknown input MUST NOT be silently replaced with a synthetic value.
    std::string error;
    std::string providerName;

    Impl(ID3D11Device* source, const std::filesystem::path& runtime, UINT w, UINT h)
        : gameDevice(source), width(w), height(h) {
        if (!source || w == 0 || h == 0)
            throw std::runtime_error("Missing game D3D11 device or invalid size");
        check(gameDevice.As(&gameFenceDevice), "QueryInterface ID3D11Device5");
        ComPtr<IDXGIDevice> dxgiDevice;
        check(gameDevice.As(&dxgiDevice), "QueryInterface IDXGIDevice");
        ComPtr<IDXGIAdapter> sameAdapter;
        check(dxgiDevice->GetAdapter(&sameAdapter), "Game device GetAdapter");
        DXGI_ADAPTER_DESC description{};
        check(sameAdapter->GetDesc(&description), "Game adapter GetDesc");
        if (description.VendorId != 0x1002)
            throw std::runtime_error("This experiment requires the actual AMD game adapter");
        check(D3D12CreateDevice(sameAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                IID_PPV_ARGS(&d12)), "D3D12CreateDevice on game's adapter");
        D3D12_FEATURE_DATA_SHADER_MODEL shader{D3D_SHADER_MODEL_6_6};
        check(d12->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader, sizeof(shader)),
              "Shader model feature query");
        if (shader.HighestShaderModel < D3D_SHADER_MODEL_6_6)
            throw std::runtime_error("FSR 4.1.2 needs Shader Model 6.6");

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(d12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
        check(d12->CreateCommandAllocator(queueDesc.Type, IID_PPV_ARGS(&allocator)),
              "CreateCommandAllocator");
        check(d12->CreateCommandList(0, queueDesc.Type, allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&commands)), "CreateCommandList");
        check(commands->Close(), "Close initial command list");
        check(d12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12)),
              "Create shared D3D12 fence");
        HANDLE handle = nullptr;
        check(d12->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &handle),
              "CreateSharedHandle for fence");
        fenceHandle.h = handle;
        check(gameFenceDevice->OpenSharedFence(fenceHandle.h, IID_PPV_ARGS(&fence11)),
              "OpenSharedFence on game's D3D11 device");
        completionEvent.h = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!completionEvent.h)
            throw std::runtime_error("Could not create GPU completion event");

        color = share(source, d12.Get(), w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
        depth = share(source, d12.Get(), w, h, DXGI_FORMAT_R32_FLOAT, false);
        motion = share(source, d12.Get(), w, h, DXGI_FORMAT_R16G16_FLOAT, false);
        output = share(source, d12.Get(), w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
        converters = std::make_unique<GameFixture>(source, w, h);
        FsrContextFlags flags{};
        flags.linearHdr = true;
        flags.invertedDepth = true;
        // Validated raw motion with jitter needs proper per-view compensation.
        // Do not assume motionIncludesJitter without a game capture.
        fsr = std::make_unique<FsrSession>(d12.Get(), runtime, w, h, w, h, flags);
        providerName = fsr->providerName();
        if (providerName.find("4.1.2") == std::string::npos)
            throw std::runtime_error("AMD runtime did not select FSR 4.1.2");
    }

    bool process(ID3D11DeviceContext* gameContext,
                 const LiveFrameTextures& textures,
                 const LiveFrameEvidence& evidence,
                 const FsrFrameParameters& params) {
        if (disabled) return false;
        if (!hasEvidence(evidence)) {
            error = "Real-game temporal evidence missing; vanilla TAA must run";
            return false;
        }
        if (!gameContext || !textures.sceneColor || !textures.sceneDepth
            || !textures.rawMotion || !textures.preHudOutput) {
            error = "Missing game context or an FSR input";
            return false;
        }
        const auto c = describe(textures.sceneColor);
        const auto d = describe(textures.sceneDepth);
        const auto m = describe(textures.rawMotion);
        const auto o = describe(textures.preHudOutput);
        if (!validTexture(c, width, height, DXGI_FORMAT_R11G11B10_FLOAT)
            || !validTexture(d, width, height, DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
            || !validTexture(m, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT)
            || !validTexture(o, width, height, DXGI_FORMAT_R11G11B10_FLOAT)) {
            error = "Game textures do not match the validated Mafia III TAA profile";
            return false;
        }
        ComPtr<ID3D11Device> contextDevice;
        gameContext->GetDevice(&contextDevice);
        if (contextDevice.Get() != gameDevice.Get()) {
            error = "Game context belongs to another D3D11 device";
            return false;
        }
        ComPtr<ID3D11DeviceContext4> context4;
        check(gameContext->QueryInterface(IID_PPV_ARGS(&context4)),
              "Game context QueryInterface ID3D11DeviceContext4");

        // No copy from a fake fixture: convert THE CURRENT LIVE GAME TEXTURES.
        converters->convert(gameContext, textures.sceneColor, textures.sceneDepth,
                            textures.rawMotion, color.rtv.Get(), depth.rtv.Get(),
                            motion.rtv.Get());
        check(context4->Signal(fence11.Get(), ++nextFence), "D3D11 Signal inputs");
        context4->Flush();

        check(allocator->Reset(), "Reset allocator");
        check(commands->Reset(allocator.Get(), nullptr), "Reset command list");
        std::array<D3D12_RESOURCE_BARRIER, 4> barriers{{
            transition(color.d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transition(depth.d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transition(motion.d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transition(output.d12.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        }};
        commands->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

        // The SDK rejects invalid camera/timing values. No synthetic fallback.
        fsr->dispatch(commands.Get(), color.d12.Get(), depth.d12.Get(),
                      motion.d12.Get(), output.d12.Get(), params);

        for (auto& b : barriers)
            std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        commands->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        check(commands->Close(), "Close FSR dispatch");
        check(queue->Wait(fence12.Get(), nextFence), "D3D12 Wait for game's inputs");
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        const std::uint64_t outputReady = ++nextFence;
        check(queue->Signal(fence12.Get(), outputReady), "D3D12 output fence signal");
        check(context4->Wait(fence11.Get(), outputReady), "D3D11 output wait");

        // Only HERE may the pre-HUD game output be changed.
        converters->blit(gameContext, output.srv.Get(), textures.preHudOutput);
        check(context4->Signal(fence11.Get(), ++nextFence), "D3D11 final fence");
        context4->Flush();
        check(fence12->SetEventOnCompletion(nextFence, completionEvent.h),
              "SetEventOnCompletion");
        const DWORD result = WaitForSingleObject(completionEvent.h, 2000);
        if (result != WAIT_OBJECT_0) {
            disabled = true;
            error = "GPU wait failed; bridge disabled, do not destroy inflight resources";
            return false;
        }
        check(d12->GetDeviceRemovedReason(), "D3D12 device removed");
        check(gameDevice->GetDeviceRemovedReason(), "D3D11 device removed");
        error.clear();
        return true;
    }
};

LiveNativeAaBridge::LiveNativeAaBridge(ID3D11Device* gameDevice,
                                     const std::filesystem::path& runtime, UINT w, UINT h)
    : impl_(std::make_unique<Impl>(gameDevice, runtime, w, h)) {}

LiveNativeAaBridge::~LiveNativeAaBridge() = default;

bool LiveNativeAaBridge::process(ID3D11DeviceContext* context,
                                const LiveFrameTextures& textures,
                                const LiveFrameEvidence& verified,
                                const FsrFrameParameters& parameters) noexcept {
    if (!impl_) return false;
    try {
        return impl_->process(context, textures, verified, parameters);
    } catch (const std::exception& e) {
        impl_->disabled = true;
        impl_->error = e.what();
        return false;
    } catch (...) {
        impl_->disabled = true;
        impl_->error = "Unknown DX11/DX12 dispatch failure";
        return false;
    }
}

const std::string& LiveNativeAaBridge::provider() const noexcept {
    return impl_->providerName;
}
const std::string& LiveNativeAaBridge::lastError() const noexcept {
    return impl_->error;
}
}  // namespace mafiafsr
