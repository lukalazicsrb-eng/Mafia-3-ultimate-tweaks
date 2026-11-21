// Copyright (c) 2026 contributors. Experimental, version-locked game-pass hook.
// This is not loaded by the retail beta. It never invokes FSR unless a separate
// trusted game-frame producer supplies actual depth, raw motion and camera data.
//
// Original Modern Tweaks 2.5.1: SHA256
// 529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900
// Direct Draw wrapper: RVA 0x79A40; original Draw callsite RVA 0x79A71.
// Original call: FF 15 D1 80 9C 03 -> [module+0x3A41B48] (D3D11 original Draw).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>

#include "../probe/live_fsr_api.h"
#include "menu_state.h"

namespace {
using Microsoft::WRL::ComPtr;
constexpr wchar_t expectedHash[] =
    L"529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900";
constexpr std::uintptr_t callRva = 0x79A71;
constexpr std::uintptr_t originalDrawSlotRva = 0x3A41B48;
constexpr std::array<unsigned char, 6> callBytes{{0xFF,0x15,0xD1,0x80,0x9C,0x03}};
using OriginalDraw = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
using BackendCreate = int (__cdecl*)(ID3D11Device*,const wchar_t*,uint32_t,uint32_t,void**);
using BackendProcess = int (__cdecl*)(void*,ID3D11DeviceContext*,ID3D11ShaderResourceView*,
    ID3D11ShaderResourceView*,ID3D11ShaderResourceView*,ID3D11RenderTargetView*,
    const MafiaFsrFrameEvidenceV1*,const MafiaFsrCameraV1*);
using BackendDestroy = void (__cdecl*)(void*);
using BackendProvider = const char* (__cdecl*)(void*);
OriginalDraw originalDraw = nullptr;
std::uint8_t* callsite = nullptr;
void* relay = nullptr;
std::atomic<int> desiredMode{0}; // 0=original, 3=FSR Native AA; others are not ready.
std::atomic<bool> installed{false};
std::atomic<bool> inGameFramePresented{false};
std::atomic<bool> backend4Ready{false};
std::atomic<bool> temporalValidated{false};
std::atomic<std::uint64_t> executedCalls{0};
std::mutex gate;
std::filesystem::path logPath;
HMODULE backendDll = nullptr;
BackendCreate create = nullptr;
BackendProcess process = nullptr;
BackendDestroy destroy = nullptr;
BackendProvider provider = nullptr;
void* session = nullptr;
std::wstring runtimePath;
UINT width = 0, height = 0;

struct FrameInputs {
    ComPtr<ID3D11PixelShader> verifiedTaaShader;
    ComPtr<ID3D11ShaderResourceView> rawDepth;
    ComPtr<ID3D11ShaderResourceView> rawMotion;
    MafiaFsrCameraV1 camera{};
    MafiaFsrFrameEvidenceV1 evidence{};
    std::uint64_t serial = 0;
    bool pending = false;
};
FrameInputs current;

void log(const std::string& s) {
    if (logPath.empty()) return;
    std::ofstream out(logPath,std::ios::app);
    if (out) out << s << "\n";
}
bool exactOriginal(HMODULE m) {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(m,path,32768)) return false;
    std::ifstream in(path,std::ios::binary);
    if (!in) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        return false;
    if (BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)<0) {
        BCryptCloseAlgorithmProvider(alg,0);return false;
    }
    std::array<char,65536> block{};
    bool ok = true;
    while (in) {
        in.read(block.data(),static_cast<std::streamsize>(block.size()));
        const auto n=in.gcount();
        if (n>0 && BCryptHashData(h,reinterpret_cast<PUCHAR>(block.data()),
                static_cast<ULONG>(n),0)<0) {ok=false;break;}
    }
    if (!in.eof()) ok=false;
    std::array<UCHAR,32> digest{};
    if (ok)ok=BCryptFinishHash(h,digest.data(),32,0)>=0;
    BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(alg,0);
    if (!ok) return false;
    constexpr wchar_t hex[]=L"0123456789abcdef";
    std::wstring actual;
    for (auto b:digest) {actual.push_back(hex[b>>4]);actual.push_back(hex[b&15]);}
    return actual==expectedHash;
}
void* nearRelay(std::uintptr_t address) {
    SYSTEM_INFO si{}; GetSystemInfo(&si);
    const auto gran=static_cast<std::uintptr_t>(si.dwAllocationGranularity);
    const auto base=address-(address%gran);
    for (std::uintptr_t d=gran;d<0x70000000;d+=gran) {
        for (int sign:{1,-1}) {
            if (sign<0 && d>base) continue;
            if (sign>0 && base>std::numeric_limits<std::uintptr_t>::max()-d) continue;
            auto at=sign<0?base-d:base+d;
            if(auto p=VirtualAlloc(reinterpret_cast<void*>(at),4096,
                MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE))return p;
        }
    }
    return nullptr;
}
bool readOnlySignature(ID3D11DeviceContext* ctx, UINT vertices, UINT first,
                       ComPtr<ID3D11ShaderResourceView>& scene,
                       ComPtr<ID3D11RenderTargetView>& output,
                       ComPtr<ID3D11PixelShader>& shader) {
    if(!ctx || vertices!=6 || first!=0)return false;
    // The game's fullscreen TAA uses Draw(6,0), not DrawIndexed.
    // Do not assume that every Draw(6,0) is TAA.
    ctx->PSGetShader(&shader,nullptr,nullptr);
    ctx->PSGetShaderResources(0,1,&scene);
    ctx->OMGetRenderTargets(1,&output,nullptr);
    if(!scene || !output || !shader) return false;
    ComPtr<ID3D11Resource> inResource,outResource;
    scene->GetResource(&inResource);
    output->GetResource(&outResource);
    ComPtr<ID3D11Texture2D> inTexture,outTexture;
    if(!inResource || !outResource || FAILED(inResource.As(&inTexture))
       || FAILED(outResource.As(&outTexture)))return false;
    D3D11_TEXTURE2D_DESC a{},b{};
    inTexture->GetDesc(&a); outTexture->GetDesc(&b);
    return a.Format==DXGI_FORMAT_R11G11B10_FLOAT
        && b.Format==DXGI_FORMAT_R11G11B10_FLOAT
        && a.Width==b.Width && a.Height==b.Height
        && a.Width==width && a.Height==height;
}
void STDMETHODCALLTYPE onDraw(ID3D11DeviceContext* ctx,UINT vertices,UINT first) {
    auto forward=originalDraw;
    if(!forward)return;
    ++executedCalls;

    if(desiredMode.load(std::memory_order_relaxed)!=3 ||
        !backend4Ready.load(std::memory_order_acquire) ||
        !temporalValidated.load(std::memory_order_acquire) ||
        !process || !session) {
        forward(ctx,vertices,first);
        return;
    }

    ComPtr<ID3D11ShaderResourceView> scene;
    ComPtr<ID3D11RenderTargetView> output;
    ComPtr<ID3D11PixelShader> ps;
    if (!readOnlySignature(ctx,vertices,first,scene,output,ps)) {
        forward(ctx,vertices,first);
        return;
    }

    // The verified frame producer must identify the exact TAA pixel shader and
    // supply truly raw motion/depth plus camera/jitter for this SAME frame.
    // This producer DOES NOT EXIST YET in the current game plugin.
    // Never silently substitute t2 (encoded game motion) for raw vectors.
    bool replaced = false;
    {
        std::lock_guard<std::mutex> lock(gate);
        if(current.pending && current.verifiedTaaShader.Get()==ps.Get() &&
           current.rawDepth && current.rawMotion &&
           current.camera.size==sizeof(MafiaFsrCameraV1) &&
           current.evidence.size==sizeof(MafiaFsrFrameEvidenceV1)) {
            replaced=process(session,ctx,scene.Get(),current.rawDepth.Get(),
                current.rawMotion.Get(),output.Get(),&current.evidence,&current.camera)!=0;
            current.pending=false; // never dispatch twice for the same frame.
        }
    }
    if(replaced) {
        inGameFramePresented.store(true,std::memory_order_release);
        return; // Only a successfully written pre-HUD FSR result may replace Draw.
    }
    forward(ctx,vertices,first);
}
} // namespace

extern "C" __declspec(dllexport) void __cdecl MafiaFsrGetEvidenceV1(
    mafiafsr::BackendEvidence* out) {
    if (!out) return;
    const bool ready=backend4Ready.load(std::memory_order_acquire);
    const bool temporal=temporalValidated.load(std::memory_order_acquire);
    out->providerIs412=ready;
    out->dx11Dx12BridgeReady=ready;
    out->temporalInputsValidated=temporal;
    out->gameFrameDispatchSucceeded=inGameFramePresented.load(std::memory_order_acquire);
    out->outputPresentedInGame=inGameFramePresented.load(std::memory_order_acquire);
    out->lowerResolution3DVerified=false; // native AA only
}

extern "C" __declspec(dllexport) void __cdecl MafiaFsrSetModeV1(int mode) {
    // No real consumer is allowed to mark mode 3 available before live dispatch.
    // F8 adapter uses this only after its evidence gate approves mode.
    desiredMode.store(mode==3?3:0,std::memory_order_release);
}

extern "C" __declspec(dllexport) BOOL __cdecl MafiaFsrSetVerifiedFrameV1(
    ID3D11PixelShader* knownTaaShader,
    ID3D11ShaderResourceView* rawDepth,
    ID3D11ShaderResourceView* rawMotion,
    const MafiaFsrCameraV1* camera,
    const MafiaFsrFrameEvidenceV1* evidence,
    std::uint64_t serial) {
    if(!knownTaaShader || !rawDepth || !rawMotion ||
       !camera || !evidence ||
       camera->size!=sizeof(MafiaFsrCameraV1) ||
       evidence->size!=sizeof(MafiaFsrFrameEvidenceV1) ||
       serial==0 || !installed.load(std::memory_order_acquire))return FALSE;
    if(!evidence->originalTaaPass || !evidence->preHudOutput ||
       !evidence->linearHdrSceneColor || !evidence->reversedDepth ||
       !evidence->rawMotionVectors || !evidence->motionInUvCoordinates ||
       !evidence->currentFrameJitter || !evidence->cameraProjection ||
       !evidence->completeFrame)return FALSE;
    std::lock_guard<std::mutex> lock(gate);
    if(serial<=current.serial)return FALSE;
    current.verifiedTaaShader=knownTaaShader;
    current.rawDepth=rawDepth;
    current.rawMotion=rawMotion;
    current.camera=*camera;
    current.evidence=*evidence;
    current.serial=serial;
    current.pending=true;
    temporalValidated.store(true,std::memory_order_release);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL __cdecl MafiaFsrConfigureBackendV1(
    ID3D11Device* gameDevice, const wchar_t* amdRuntimeDirectory,
    UINT outputWidth, UINT outputHeight) {
    if(!gameDevice || !amdRuntimeDirectory || !amdRuntimeDirectory[0] ||
        !installed.load(std::memory_order_acquire) || outputWidth==0 || outputHeight==0)
        return FALSE;
    std::lock_guard<std::mutex> lock(gate);
    if(session) return FALSE;
    // Trusted AMD runtime must be user-provided. No bundled signed DLL binaries.
    wchar_t modulePath[32768]{};
    if(!GetModuleFileNameW(nullptr,modulePath,32768))return FALSE;
    const auto gameFolder=std::filesystem::path(modulePath).parent_path();
    backendDll=LoadLibraryExW((gameFolder/L"mafia_live_fsr_backend.dll").c_str(),
                             nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!backendDll) {log("Real-frame backend DLL not found; original TAA preserved");return FALSE;}
    create=reinterpret_cast<BackendCreate>(GetProcAddress(backendDll,"MafiaFsrCreateV1"));
    process=reinterpret_cast<BackendProcess>(GetProcAddress(backendDll,"MafiaFsrProcessNativeAaV1"));
    destroy=reinterpret_cast<BackendDestroy>(GetProcAddress(backendDll,"MafiaFsrDestroyV1"));
    provider=reinterpret_cast<BackendProvider>(GetProcAddress(backendDll,"MafiaFsrProviderV1"));
    if(!create || !process || !destroy || !provider) {
        log("Incomplete FSR backend exports; original TAA preserved");return FALSE;
    }
    void* newSession=nullptr;
    if(!create(gameDevice,amdRuntimeDirectory,outputWidth,outputHeight,&newSession)
        || !newSession)return FALSE;
    if(std::string(provider(newSession)).find("4.1.2")==std::string::npos) {
        destroy(newSession); return FALSE;
    }
    width=outputWidth;height=outputHeight;
    session=newSession;runtimePath=amdRuntimeDirectory;
    backend4Ready.store(true,std::memory_order_release);
    log("Genuine AMD FSR 4.1.2 backend constructed; no game frame processed yet");
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL __cdecl MafiaFsrInstallDrawHookV1(HMODULE originalMod) {
    if(!originalMod || installed.load() || !exactOriginal(originalMod))return FALSE;
    auto base=reinterpret_cast<unsigned char*>(originalMod);
    auto site=base+callRva;
    if(std::memcmp(site,callBytes.data(),callBytes.size())!=0)return FALSE;
    // A version-locked rip-relative indirect call to Modern Tweaks' original Draw pointer.
    std::int32_t offset=0;std::memcpy(&offset,site+2,sizeof(offset));
    if(site+6+offset!=base+originalDrawSlotRva)return FALSE;
    auto slot=reinterpret_cast<OriginalDraw*>(base+originalDrawSlotRva);
    if(!slot || !*slot)return FALSE;
    auto near=nearRelay(reinterpret_cast<std::uintptr_t>(site));
    if(!near)return FALSE;
    auto diff=reinterpret_cast<std::intptr_t>(near) -
              reinterpret_cast<std::intptr_t>(site+5);
    if(diff<std::numeric_limits<std::int32_t>::min() ||
       diff>std::numeric_limits<std::int32_t>::max()) {
        VirtualFree(near,0,MEM_RELEASE);return FALSE;
    }
    std::array<unsigned char,12> trampoline{{0x48,0xB8}};
    auto target=reinterpret_cast<std::uintptr_t>(&onDraw);
    std::memcpy(trampoline.data()+2,&target,8);
    trampoline[10]=0xFF;trampoline[11]=0xE0;
    std::memcpy(near,trampoline.data(),trampoline.size());
    FlushInstructionCache(GetCurrentProcess(),near,trampoline.size());
    std::array<unsigned char,6> replace{{0xE8,0,0,0,0,0x90}};
    auto rel32=static_cast<std::int32_t>(diff);
    std::memcpy(replace.data()+1,&rel32,4);
    originalDraw=*slot;
    wchar_t modPath[32768]{};
    if(GetModuleFileNameW(originalMod,modPath,32768))
        logPath=std::filesystem::path(modPath).parent_path()/L"MafiaFsrDraw.log";
    {std::ofstream clear(logPath,std::ios::trunc);}
    log("Fullscreen Draw callback v1: exact mod hash and original call verified.");
    log("No actual FSR activated until trusted producer supplies depth, motion and camera.");
    DWORD before=0;
    if(!VirtualProtect(site,6,PAGE_EXECUTE_READWRITE,&before)) {
        originalDraw=nullptr;
        VirtualFree(near,0,MEM_RELEASE);
        return FALSE;
    }
    std::memcpy(site,replace.data(),replace.size());
    FlushInstructionCache(GetCurrentProcess(),site,replace.size());
    DWORD ignored=0;VirtualProtect(site,6,before,&ignored);
    relay=near;callsite=site;
    installed.store(true,std::memory_order_release);
    return TRUE;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD why,LPVOID) {
    if(why==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(h);
    return TRUE;
}
