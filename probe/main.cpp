#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsr_session.h"

extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion = 616;
__declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";
}

namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT renderWidth = 320;
constexpr UINT renderHeight = 180;
constexpr UINT outputWidth = 640;
constexpr UINT outputHeight = 360;
constexpr UINT amdVendorId = 0x1002;

void check(HRESULT result, std::string_view operation)
{
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex
                << static_cast<std::uint32_t>(result) << ')';
        throw std::runtime_error(message.str());
    }
}

struct CloseHandleDeleter {
    void operator()(HANDLE handle) const { CloseHandle(handle); }
};
using UniqueHandle = std::unique_ptr<void, CloseHandleDeleter>;

ComPtr<IDXGIAdapter1> selectAdapter()
{
    ComPtr<IDXGIFactory1> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");

    ComPtr<IDXGIAdapter1> selected;
    DXGI_ADAPTER_DESC1 selectedDesc{};
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT result = factory->EnumAdapters1(index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND)
            break;
        check(result, "EnumAdapters1");
        DXGI_ADAPTER_DESC1 desc{};
        check(adapter->GetDesc1(&desc), "GetDesc1");
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        const bool isAmd = desc.VendorId == amdVendorId;
        const bool selectedIsAmd = selectedDesc.VendorId == amdVendorId;
        if (!selected || (isAmd && !selectedIsAmd) ||
            (isAmd == selectedIsAmd && desc.DedicatedVideoMemory > selectedDesc.DedicatedVideoMemory)) {
            selected = std::move(adapter);
            selectedDesc = desc;
        }
    }

    if (!selected)
        throw std::runtime_error("No hardware DXGI adapter found");
    std::wcout << L"Adapter: " << selectedDesc.Description << L" (vendor 0x" << std::hex
               << selectedDesc.VendorId << L", device 0x" << selectedDesc.DeviceId << std::dec
               << L")\n";
    return selected;
}

struct SharedTexture {
    ComPtr<ID3D11Texture2D> d3d11;
    ComPtr<ID3D12Resource> d3d12;
};

D3D11_TEXTURE2D_DESC textureDesc(UINT width, UINT height, DXGI_FORMAT format)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    return desc;
}

SharedTexture shareTexture(ID3D11Device* device11, ID3D12Device* device12,
                           UINT width, UINT height, DXGI_FORMAT format, bool output,
                           const std::string& description)
{
    D3D11_TEXTURE2D_DESC desc = textureDesc(width, height, format);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (output)
        desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    SharedTexture shared;
    check(device11->CreateTexture2D(&desc, nullptr, &shared.d3d11), "CreateTexture2D (" + description + ')');
    ComPtr<IDXGIResource> dxgiResource;
    check(shared.d3d11.As(&dxgiResource), "QueryInterface (shared texture)");

    // GetSharedHandle returns a legacy handle owned by the texture.
    HANDLE handle = nullptr;
    check(dxgiResource->GetSharedHandle(&handle), "GetSharedHandle (" + description + ')');
    check(device12->OpenSharedHandle(handle, IID_PPV_ARGS(&shared.d3d12)),
          "OpenSharedHandle (" + description + ')');
    return shared;
}

void uploadFixture(ID3D11DeviceContext* context, const SharedTexture& color,
                   const SharedTexture& depth, const SharedTexture& motion)
{
    const std::size_t pixelCount = renderWidth * renderHeight;
    std::vector<std::uint32_t> pixels(pixelCount);
    for (UINT y = 0; y < renderHeight; ++y) {
        for (UINT x = 0; x < renderWidth; ++x)
            pixels[y * renderWidth + x] = x < renderWidth / 2 ? 0xff2020e0 : 0xffe0e020;
    }
    const std::vector<float> depths(pixelCount, 0.5f);
    // Both half-float motion components are zero for the stationary synthetic frame.
    const std::vector<std::uint32_t> stillMotion(pixelCount, 0);
    context->UpdateSubresource(color.d3d11.Get(), 0, nullptr, pixels.data(), renderWidth * 4, 0);
    context->UpdateSubresource(depth.d3d11.Get(), 0, nullptr, depths.data(), renderWidth * 4, 0);
    context->UpdateSubresource(motion.d3d11.Get(), 0, nullptr, stillMotion.data(), renderWidth * 4, 0);
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* texture, D3D12_RESOURCE_STATES destination)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = destination;
    return barrier;
}

void verifyOutput(ID3D11DeviceContext* context, ID3D11Texture2D* readback, UINT frame)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(readback, 0, D3D11_MAP_READ, 0, &mapped), "Map (output readback)");
    const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + outputHeight / 2 * mapped.RowPitch;
    const auto* left = row + outputWidth / 4 * 4;
    const auto* right = row + outputWidth * 3 / 4 * 4;
    const int leftRed = left[0];
    const int leftGreen = left[1];
    const int rightRed = right[0];
    const int rightGreen = right[1];
    context->Unmap(readback, 0);

    std::wcout << L"Frame " << frame << L" output samples (red, green): ("
               << leftRed << L", " << leftGreen << L") and ("
               << rightRed << L", " << rightGreen << L")\n";
    if (leftRed < leftGreen + 24 || rightGreen < rightRed + 24)
        throw std::runtime_error("Upscaled output does not match the two-color input fixture");
}

void run(const std::filesystem::path& runtimeDir)
{
    auto adapter = selectAdapter();
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> immediate;
    check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                            D3D11_SDK_VERSION, &device11, nullptr, &immediate), "D3D11CreateDevice");
    ComPtr<ID3D11Device5> device11WithFences;
    ComPtr<ID3D11DeviceContext4> context11;
    check(device11.As(&device11WithFences), "QueryInterface (ID3D11Device5)");
    check(immediate.As(&context11), "QueryInterface (ID3D11DeviceContext4)");

    ComPtr<ID3D12Device> device12;
    check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12)),
          "D3D12CreateDevice (check D3D12/D3D12Core.dll)");
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_6};
    check(device12->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)),
          "CheckFeatureSupport (shader model)");
    if (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6)
        throw std::runtime_error("The adapter and driver do not expose Shader Model 6.6");

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    check(device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
    check(device12->CreateCommandAllocator(queueDesc.Type, IID_PPV_ARGS(&allocator)), "CreateCommandAllocator");
    check(device12->CreateCommandList(0, queueDesc.Type, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)),
          "CreateCommandList");
    check(commands->Close(), "Close (initial command list)");

    ComPtr<ID3D12Fence> fence12;
    check(device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12)), "CreateFence");
    HANDLE fenceHandle = nullptr;
    check(device12->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &fenceHandle),
          "CreateSharedHandle (fence)");
    const UniqueHandle fenceLifetime(fenceHandle);
    ComPtr<ID3D11Fence> fence11;
    check(device11WithFences->OpenSharedFence(fenceLifetime.get(), IID_PPV_ARGS(&fence11)), "OpenSharedFence");
    const UniqueHandle doneEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!doneEvent)
        check(HRESULT_FROM_WIN32(GetLastError()), "CreateEventW");

    const auto color = shareTexture(device11.Get(), device12.Get(), renderWidth, renderHeight,
                                    DXGI_FORMAT_R8G8B8A8_UNORM, false, "color R8G8B8A8_UNORM");
    const auto depth = shareTexture(device11.Get(), device12.Get(), renderWidth, renderHeight,
                                    DXGI_FORMAT_R32_FLOAT, false, "depth R32_FLOAT");
    const auto motion = shareTexture(device11.Get(), device12.Get(), renderWidth, renderHeight,
                                     DXGI_FORMAT_R16G16_FLOAT, false, "motion R16G16_FLOAT");
    const auto output = shareTexture(device11.Get(), device12.Get(), outputWidth, outputHeight,
                                     DXGI_FORMAT_R8G8B8A8_UNORM, true, "output R8G8B8A8_UNORM");
    D3D11_TEXTURE2D_DESC readbackDesc = textureDesc(outputWidth, outputHeight, DXGI_FORMAT_R8G8B8A8_UNORM);
    readbackDesc.Usage = D3D11_USAGE_STAGING;
    readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    check(device11->CreateTexture2D(&readbackDesc, nullptr, &readback), "CreateTexture2D (CPU readback)");

    FsrSession fsr(device12.Get(), runtimeDir, renderWidth, renderHeight, outputWidth, outputHeight);
    std::wcout << L"Synthetic input " << renderWidth << L'x' << renderHeight << L", output "
               << outputWidth << L'x' << outputHeight << L'\n' << std::flush;

    uploadFixture(context11.Get(), color, depth, motion);
    const std::vector<std::uint32_t> black(outputWidth * outputHeight, 0);
    UINT64 fenceValue = 0;
    for (UINT frame = 0; frame < 2; ++frame) {
        context11->UpdateSubresource(output.d3d11.Get(), 0, nullptr, black.data(), outputWidth * 4, 0);
        check(context11->Signal(fence11.Get(), ++fenceValue), "D3D11 Signal (inputs ready)");
        context11->Flush();

        check(allocator->Reset(), "Reset (command allocator)");
        check(commands->Reset(allocator.Get(), nullptr), "Reset (command list)");
        std::array barriers = {
            transition(color.d3d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transition(depth.d3d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transition(motion.d3d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transition(output.d3d12.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        };
        commands->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        fsr.dispatch(commands.Get(), color.d3d12.Get(), depth.d3d12.Get(), motion.d3d12.Get(),
                     output.d3d12.Get(), frame == 0);
        for (auto& barrier : barriers)
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commands->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

        check(commands->Close(), "Close (dispatch command list)");
        check(queue->Wait(fence12.Get(), fenceValue), "D3D12 Wait (inputs ready)");
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        const UINT64 outputReady = ++fenceValue;
        try {
            check(queue->Signal(fence12.Get(), outputReady), "D3D12 Signal (output ready)");
            check(context11->Wait(fence11.Get(), outputReady), "D3D11 Wait (output ready)");
            context11->CopyResource(readback.Get(), output.d3d11.Get());
            check(context11->Signal(fence11.Get(), ++fenceValue), "D3D11 Signal (readback ready)");
            context11->Flush();

            check(fence12->SetEventOnCompletion(fenceValue, doneEvent.get()), "SetEventOnCompletion (readback)");
            const DWORD waitResult = WaitForSingleObject(doneEvent.get(), 30000);
            if (waitResult == WAIT_TIMEOUT)
                throw std::runtime_error("D3D11/D3D12 interop did not complete within the wait limit");
            if (waitResult != WAIT_OBJECT_0)
                check(HRESULT_FROM_WIN32(GetLastError()), "WaitForSingleObject (readback)");
        } catch (const std::exception& error) {
            if (fence12->GetCompletedValue() < outputReady && SUCCEEDED(device12->GetDeviceRemovedReason())) {
                // The FSR context must not be destroyed while its dispatch may still be in flight.
                std::cerr << "Probe failed with an unretired GPU dispatch: " << error.what() << std::endl;
                std::quick_exit(1);
            }
            throw;
        }
        check(device12->GetDeviceRemovedReason(), "D3D12 device status");
        check(device11->GetDeviceRemovedReason(), "D3D11 device status");
        verifyOutput(immediate.Get(), readback.Get(), frame + 1);
    }
    std::wcout << L"Bridge check passed with provider " << fsr.providerName().c_str() << L".\n";
}

}  // namespace

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 2) {
        std::wcerr << L"Usage: fsr-interop-probe.exe <AMD SDK signedbin directory>\n";
        return 2;
    }
    try {
        run(std::filesystem::absolute(argv[1]));
    } catch (const std::exception& error) {
        std::cerr << "Probe failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
