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
#include <sstream>
#include <utility>
#include <unordered_set>
#include <algorithm>

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
std::atomic<bool> shadowGameFrameVerified{false};
std::atomic<bool> validOutputHandoff{false};
std::atomic<bool> backend4Ready{false};
std::atomic<bool> temporalValidated{false};
std::atomic<std::uint64_t> executedCalls{0};
std::atomic<std::uint64_t> draw6Calls{0};
std::atomic<std::uint64_t> candidateCount{0};
std::atomic<std::uint64_t> traceLines{0};
// Always diagnostic only until a verified raw-motion/depth/camera producer exists.
// The log must never make the renderer claim AMD FSR is active.
std::atomic<bool> fullscreenTraceEnabled{false};
std::mutex gate;
std::filesystem::path logPath;
HMODULE backendDll = nullptr;
BackendCreate create = nullptr;
BackendProcess process = nullptr;
BackendProcess preview = nullptr;
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
struct ResourceDescription {
    UINT width = 0, height = 0, resourceFormat = 0, viewFormat = 0;
    bool valid = false;
};
ResourceDescription resourceDescription(ID3D11Resource* resource) {
    if(!resource)return {};
    ComPtr<ID3D11Texture2D> texture;
    if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture))))return {};
    D3D11_TEXTURE2D_DESC d{};
    texture->GetDesc(&d);
    return {d.Width,d.Height,static_cast<UINT>(d.Format),0,true};
}
ResourceDescription describeView(ID3D11ShaderResourceView* view) {
    if(!view)return {};
    D3D11_SHADER_RESOURCE_VIEW_DESC v{};
    view->GetDesc(&v);
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    auto result=resourceDescription(resource.Get());
    result.viewFormat=static_cast<UINT>(v.Format);
    return result;
}
ResourceDescription describeView(ID3D11RenderTargetView* view) {
    if(!view)return {};
    D3D11_RENDER_TARGET_VIEW_DESC v{};
    view->GetDesc(&v);
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    auto result=resourceDescription(resource.Get());
    result.viewFormat=static_cast<UINT>(v.Format);
    return result;
}
ResourceDescription describeView(ID3D11DepthStencilView* view) {
    if(!view)return {};
    D3D11_DEPTH_STENCIL_VIEW_DESC v{};
    view->GetDesc(&v);
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    auto result=resourceDescription(resource.Get());
    result.viewFormat=static_cast<UINT>(v.Format);
    return result;
}
std::string describe(const char* name,const ResourceDescription& r) {
    std::ostringstream s;
    s<<name<<'=';
    if(!r.valid)s<<"missing";
    else s<<r.width<<'x'<<r.height<<":fmt="<<r.resourceFormat<<":view="<<r.viewFormat;
    return s.str();
}
// V10: log multiple Draw topology sizes instead of assuming all TAA is
// Draw(6,0). The upstream Mafia III Luma implementation manually uses
// Draw(6,0) for a *different* pre-TAA copy, so the old heuristic was wrong.
// These samples are diagnostics, not actual validation for FSR.
std::atomic<std::uint64_t> draw3Calls{0},draw4Calls{0},drawOtherCalls{0};
std::mutex shaderTraceMutex;
std::unordered_set<std::uintptr_t> observedShaderPointers;
std::atomic<unsigned> totalLogged{0}, hdrLogged{0}, uniqueShaderCount{0};

std::atomic<std::uint64_t>& categoryCounter(UINT vertices) {
    if(vertices==3)return draw3Calls;
    if(vertices==4)return draw4Calls;
    if(vertices==6)return draw6Calls;
    return drawOtherCalls;
}
void traceFullscreenDraw(ID3D11DeviceContext* ctx,UINT vertices,UINT first) {
    if(!fullscreenTraceEnabled.load(std::memory_order_relaxed))return;
    const auto total=executedCalls.load(std::memory_order_relaxed);
    auto& bucket=categoryCounter(vertices);
    const auto sequence=++bucket;
    if(total==1 || total==1024 || total==8192 || total==32768 || total==131072) {
        log("[Draw summary] total="+std::to_string(total)
            +" Draw3="+std::to_string(draw3Calls.load())
            +" Draw4="+std::to_string(draw4Calls.load())
            +" Draw3="+std::to_string(draw3Calls.load())
        +" Draw4="+std::to_string(draw4Calls.load())
        +" Draw6="+std::to_string(draw6Calls.load())
        +" Other="+std::to_string(drawOtherCalls.load())
            +" DrawOther="+std::to_string(drawOtherCalls.load())
            +" HDRCandidates="+std::to_string(candidateCount.load())
            +" shaders="+std::to_string(uniqueShaderCount.load()));
    }
    if(!ctx || first!=0)return;
    // Keep this sampling bounded and avoid perturbing in-game frame timings.
    // Samples extend beyond the first loading/menu draws, which dominated v9.
    const bool inspect = (sequence<=16)
        || (sequence<=256 && sequence%16==0)
        || (sequence<=8192 && sequence%128==0)
        || (sequence<=131072 && sequence%2048==0);
    if(!inspect)return;
    ID3D11ShaderResourceView* views[3]{};
    ComPtr<ID3D11PixelShader> shader;
    ComPtr<ID3D11RenderTargetView> output;
    ComPtr<ID3D11DepthStencilView> depth;
    ctx->PSGetShader(&shader,nullptr,nullptr);
    ctx->PSGetShaderResources(0,3,views);
    ctx->OMGetRenderTargets(1,&output,&depth);
    ResourceDescription input[3]{};
    for(int i=0;i<3;++i) {
        input[i]=describeView(views[i]);
        if(views[i])views[i]->Release();
    }
    const auto rt=describeView(output.Get());
    const auto ds=describeView(depth.Get());
    const auto shaderPtr=reinterpret_cast<std::uintptr_t>(shader.Get());
    bool newShader=false;
    unsigned uniqueShaders=0;
    {
        std::lock_guard<std::mutex> lock(shaderTraceMutex);
        const auto insert=observedShaderPointers.insert(shaderPtr);
        newShader=insert.second;
        uniqueShaders=static_cast<unsigned>(observedShaderPointers.size());
        uniqueShaderCount.store(uniqueShaders,std::memory_order_release);
    }
    const bool hdrColor = input[0].valid
        && (input[0].viewFormat==DXGI_FORMAT_R11G11B10_FLOAT
            || input[0].resourceFormat==DXGI_FORMAT_R11G11B10_FLOAT);
    const bool hdrOutput = rt.valid
        && (rt.viewFormat==DXGI_FORMAT_R11G11B10_FLOAT
            || rt.resourceFormat==DXGI_FORMAT_R11G11B10_FLOAT);
    const bool matchingDimensions = input[0].valid && rt.valid
        && input[0].width==rt.width && input[0].height==rt.height;
    const bool hdrCandidate=hdrColor && hdrOutput && matchingDimensions;
    if(hdrCandidate) ++candidateCount;
    // Include normal examples, new shaders and every 8th HDR candidate.
    // 96 lines total to avoid giant logs and per-frame disk writes.
    const bool interesting=(sequence<=8)
        || (newShader && uniqueShaders<=40)
        || (hdrCandidate && hdrLogged.load()<32)
        || (sequence==256 || sequence==1024 || sequence==8192);
    if(!interesting)return;
    if(++traceLines>96)return;
    if(hdrCandidate)++hdrLogged;
    ++totalLogged;
    std::ostringstream line;
    line<<"[Draw V10] total="<<total<<" verts="<<vertices
        <<" first="<<first<<" bucket="<<sequence
        <<" thread="<<GetCurrentThreadId()
        <<" context=0x"<<std::hex<<reinterpret_cast<std::uintptr_t>(ctx)
        <<" ps=0x"<<shaderPtr<<std::dec
        <<" newPS="<<(newShader?1:0)
        <<" hdrCandidate="<<(hdrCandidate?1:0)
        <<" shaderCount="<<uniqueShaders
        <<" "<<describe("t0",input[0])
        <<" "<<describe("t1",input[1])
        <<" "<<describe("t2",input[2])
        <<" "<<describe("rtv",rt)
        <<" "<<describe("dsv",ds);
    log(line.str());
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
    // Read-only trace is independent of live FSR readiness.
    traceFullscreenDraw(ctx,vertices,first);

    if(!backend4Ready.load(std::memory_order_acquire) ||
        !temporalValidated.load(std::memory_order_acquire) ||
        !process || !preview || !session) {
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
    bool previewSucceeded = false;
    {
        std::lock_guard<std::mutex> lock(gate);
        if(current.pending && current.verifiedTaaShader.Get()==ps.Get() &&
           current.rawDepth && current.rawMotion &&
           current.camera.size==sizeof(MafiaFsrCameraV1) &&
           current.evidence.size==sizeof(MafiaFsrFrameEvidenceV1)) {
            const bool active = desiredMode.load(std::memory_order_acquire)==3
                && shadowGameFrameVerified.load(std::memory_order_acquire);
            MafiaFsrCameraV1 parameters = current.camera;
            // Reset temporal history on the first visible FSR frame.
            if(active && !inGameFramePresented.load())parameters.resetHistory=1;
            const auto dispatch=active?process:preview;
            const bool ok=dispatch(session,ctx,scene.Get(),current.rawDepth.Get(),
                current.rawMotion.Get(),output.Get(),&current.evidence,&parameters)!=0;
            replaced=active && ok;
            previewSucceeded=!active && ok;
            current.pending=false; // never dispatch twice for the same frame.
        }
    }
    if(previewSucceeded) {
        // The AMD GPU has now processed an ACTUAL game frame without writing
        // to the game. Its matching pre-HUD target was validated.
        shadowGameFrameVerified.store(true,std::memory_order_release);
        validOutputHandoff.store(true,std::memory_order_release);
    }
    if(replaced) {
        inGameFramePresented.store(true,std::memory_order_release);
        return; // Only a successful pre-HUD FSR result may replace Draw.
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
    out->shadowGameFrameDispatchSucceeded=shadowGameFrameVerified.load(std::memory_order_acquire);
    out->outputHandoffValidated=validOutputHandoff.load(std::memory_order_acquire);
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
    preview=reinterpret_cast<BackendProcess>(GetProcAddress(backendDll,"MafiaFsrPreviewNativeAaV1"));
    destroy=reinterpret_cast<BackendDestroy>(GetProcAddress(backendDll,"MafiaFsrDestroyV1"));
    provider=reinterpret_cast<BackendProvider>(GetProcAddress(backendDll,"MafiaFsrProviderV1"));
    if(!create || !process || !preview || !destroy || !provider) {
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

extern "C" __declspec(dllexport) void __cdecl MafiaFsrEnableDrawTraceV1(int enabled) {
    fullscreenTraceEnabled.store(enabled!=0,std::memory_order_release);
}

extern "C" __declspec(dllexport) void __cdecl MafiaFsrReportTraceV1() {
    log("[End summary] draws="+std::to_string(executedCalls.load())
        +" Draw6="+std::to_string(draw6Calls.load())
        +" strictTaaCandidates="+std::to_string(candidateCount.load())
        +" backendReady="+std::to_string(static_cast<int>(backend4Ready.load()))
        +" shadowFSR="+std::to_string(static_cast<int>(shadowGameFrameVerified.load()))
        +" FSRPresented="+std::to_string(static_cast<int>(inGameFramePresented.load())));
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
    void* relayPage=nearRelay(reinterpret_cast<std::uintptr_t>(site));
    if(!relayPage)return FALSE;
    auto diff=reinterpret_cast<std::intptr_t>(relayPage) -
              reinterpret_cast<std::intptr_t>(site+5);
    if(diff<std::numeric_limits<std::int32_t>::min() ||
       diff>std::numeric_limits<std::int32_t>::max()) {
        VirtualFree(relayPage,0,MEM_RELEASE);return FALSE;
    }
    std::array<unsigned char,12> trampoline{{0x48,0xB8}};
    auto target=reinterpret_cast<std::uintptr_t>(&onDraw);
    std::memcpy(trampoline.data()+2,&target,8);
    trampoline[10]=0xFF;trampoline[11]=0xE0;
    std::memcpy(relayPage,trampoline.data(),trampoline.size());
    FlushInstructionCache(GetCurrentProcess(),relayPage,trampoline.size());
    std::array<unsigned char,6> replace{{0xE8,0,0,0,0,0x90}};
    auto rel32=static_cast<std::int32_t>(diff);
    std::memcpy(replace.data()+1,&rel32,4);
    originalDraw=*slot;
    wchar_t modPath[32768]{};
    if(GetModuleFileNameW(originalMod,modPath,32768))
        logPath=std::filesystem::path(modPath).parent_path()/L"MafiaFsrDraw.log";
    {std::ofstream clear(logPath,std::ios::trunc);}
    log("Fullscreen Draw callback V10: verified original mod; captures Draw3/4/6/other.");
    log("No actual FSR activated until trusted producer supplies depth, motion and camera.");
    log("Draw profiler V10: handles all vertex counts, not just Draw(6,0).");
    log("Candidate shader identity/texture descriptors are NOT proof of valid FSR inputs.");
    DWORD before=0;
    if(!VirtualProtect(site,6,PAGE_EXECUTE_READWRITE,&before)) {
        originalDraw=nullptr;
        VirtualFree(relayPage,0,MEM_RELEASE);
        return FALSE;
    }
    std::memcpy(site,replace.data(),replace.size());
    FlushInstructionCache(GetCurrentProcess(),site,replace.size());
    DWORD ignored=0;VirtualProtect(site,6,before,&ignored);
    relay=relayPage;callsite=site;
    installed.store(true,std::memory_order_release);
    return TRUE;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD why,LPVOID) {
    if(why==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(h);
    return TRUE;
}
