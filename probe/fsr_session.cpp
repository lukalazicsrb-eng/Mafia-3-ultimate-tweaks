#include "fsr_session.h"

#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <dx12/ffx_api_dx12.h>
#include <ffx_upscale.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

constexpr wchar_t effectName[] = L"amd_fidelityfx_upscaler_dx12.dll";
constexpr wchar_t loaderName[] = L"amd_fidelityfx_loader_dx12.dll";

class Module final {
public:
    Module(const std::filesystem::path& directory, const wchar_t* filename) {
        if (!directory.is_absolute()) {
            throw std::runtime_error("The AMD runtime directory must be an absolute path.");
        }

        const auto path = directory / filename;
        handle_ = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (handle_ == nullptr) {
            const auto error = GetLastError();
            throw std::runtime_error("Could not load " + path.filename().string()
                + " (Win32 error " + std::to_string(error) + ").");
        }
    }

    ~Module() {
        FreeLibrary(handle_);
    }

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    template <typename Function>
    Function resolve(const char* name) const {
        const auto address = GetProcAddress(handle_, name);
        if (address == nullptr) {
            throw std::runtime_error(std::string("AMD runtime export is missing: ") + name);
        }

        Function function{};
        static_assert(sizeof(function) == sizeof(address));
        std::memcpy(&function, &address, sizeof(function));
        return function;
    }

private:
    HMODULE handle_ = nullptr;
};

void checkFfx(ffxReturnCode_t result, const char* operation) {
    if (result != FFX_API_RETURN_OK) {
        throw std::runtime_error(std::string(operation) + " failed (FidelityFX error "
            + std::to_string(result) + ").");
    }
}

void logMessage(std::uint32_t type, const wchar_t* message) noexcept {
    if (message != nullptr) {
        OutputDebugStringW(type == FFX_API_MESSAGE_TYPE_ERROR ? L"FSR error: " : L"FSR warning: ");
        OutputDebugStringW(message);
        OutputDebugStringW(L"\n");
    }
}

} // namespace

struct FsrSession::Impl {
    // AMD's loader looks for an already loaded effect before searching folders.
    Module effect;
    Module loader;
    PfnFfxCreateContext createContext;
    PfnFfxDestroyContext destroyContext;
    PfnFfxQuery query;
    PfnFfxDispatch dispatch;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    ffxCreateContextDescUpscale upscaleDesc{};
    ffxCreateBackendDX12Desc backendDesc{};
    ffxCreateContextDescUpscaleVersion apiVersionDesc{};
    ffxContext context = nullptr;
    std::string providerName;

    explicit Impl(const std::filesystem::path& runtimeDirectory)
        : effect(runtimeDirectory, effectName), loader(runtimeDirectory, loaderName),
          createContext(loader.resolve<PfnFfxCreateContext>("ffxCreateContext")),
          destroyContext(loader.resolve<PfnFfxDestroyContext>("ffxDestroyContext")),
          query(loader.resolve<PfnFfxQuery>("ffxQuery")),
          dispatch(loader.resolve<PfnFfxDispatch>("ffxDispatch")) {}

    ~Impl() {
        if (context != nullptr) {
            static_cast<void>(destroyContext(&context, nullptr));
        }
    }

    void listProviders() const {
        std::uint64_t count = 0;
        ffxQueryDescGetVersions versions{};
        versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versions.device = device.Get();
        versions.outputCount = &count;
        checkFfx(query(nullptr, &versions.header), "Querying the FSR provider count");

        if (count == 0) {
            throw std::runtime_error("The AMD runtime reported no upscaling provider for this adapter.");
        }

        const auto capacity = static_cast<std::size_t>(count);
        std::vector<std::uint64_t> ids(capacity);
        std::vector<const char*> names(capacity);
        versions.versionIds = ids.data();
        versions.versionNames = names.data();
        checkFfx(query(nullptr, &versions.header), "Enumerating the FSR providers");
        if (count > capacity) {
            throw std::runtime_error("The AMD runtime returned more providers than it advertised.");
        }

        for (std::size_t index = 0; index < count; ++index) {
            if (names[index] != nullptr) {
                std::wcout << L"Available upscaler " << names[index] << L" (provider " << ids[index] << L")\n";
            }
        }
    }
};

FsrSession::FsrSession(ID3D12Device* device, const std::filesystem::path& runtimeDirectory,
    std::uint32_t renderWidth, std::uint32_t renderHeight,
    std::uint32_t outputWidth, std::uint32_t outputHeight)
    : impl_(std::make_unique<Impl>(runtimeDirectory)) {
    if (device == nullptr || renderWidth == 0 || renderHeight == 0
        || outputWidth < renderWidth || outputHeight < renderHeight) {
        throw std::runtime_error("FSR needs a D3D12 device and valid render and output dimensions.");
    }

    impl_->device = device;
    impl_->listProviders();

    auto& apiVersion = impl_->apiVersionDesc;
    apiVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
    apiVersion.version = FFX_UPSCALER_VERSION;

    auto& backend = impl_->backendDesc;
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.header.pNext = &apiVersion.header;
    backend.device = device;

    auto& upscale = impl_->upscaleDesc;
    upscale.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    upscale.header.pNext = &backend.header;
    upscale.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
    upscale.maxRenderSize = {renderWidth, renderHeight};
    upscale.maxUpscaleSize = {outputWidth, outputHeight};
    upscale.fpMessage = logMessage;
    checkFfx(impl_->createContext(&impl_->context, &upscale.header, nullptr), "Creating the FSR context");
    if (impl_->context == nullptr) {
        throw std::runtime_error("The AMD runtime did not return an FSR context.");
    }

    ffxQueryGetProviderVersion active{};
    active.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    checkFfx(impl_->query(&impl_->context, &active.header), "Querying the selected FSR provider");
    if (active.versionName == nullptr || active.versionName[0] == '\0' || active.versionId == 0) {
        throw std::runtime_error("The AMD runtime did not identify the selected FSR provider.");
    }

    impl_->providerName = active.versionName;
    std::wcout << L"Selected upscaler " << impl_->providerName.c_str()
               << L" (provider " << active.versionId << L")\n";
}

FsrSession::~FsrSession() = default;

const std::string& FsrSession::providerName() const noexcept {
    return impl_->providerName;
}

void FsrSession::dispatch(ID3D12GraphicsCommandList* commands, ID3D12Resource* color,
    ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output, bool reset) {
    if (commands == nullptr || color == nullptr || depth == nullptr || motion == nullptr || output == nullptr) {
        throw std::runtime_error("An FSR dispatch is missing a command list or a required texture.");
    }

    ffxDispatchDescUpscale frame{};
    frame.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    frame.commandList = commands;
    frame.color = ffxApiGetResourceDX12(color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    frame.depth = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    frame.motionVectors = ffxApiGetResourceDX12(motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    frame.output = ffxApiGetResourceDX12(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    frame.motionVectorScale = {1.0f, 1.0f};
    frame.renderSize = impl_->upscaleDesc.maxRenderSize;
    frame.upscaleSize = impl_->upscaleDesc.maxUpscaleSize;
    frame.frameTimeDelta = 1000.0f / 60.0f;
    frame.preExposure = 1.0f;
    frame.reset = reset;
    frame.cameraNear = 0.1f;
    frame.cameraFar = 100.0f;
    frame.cameraFovAngleVertical = 1.0f;
    frame.viewSpaceToMetersFactor = 1.0f;
    checkFfx(impl_->dispatch(&impl_->context, &frame.header), "Recording an AMD FSR dispatch");
}
