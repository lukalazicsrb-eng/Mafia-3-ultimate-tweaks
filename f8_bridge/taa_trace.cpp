// Experimental read-only tracing of the original game's TAA draw inputs.
// This is NOT a working FSR renderer. Original matcher always executes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <d3d11.h>
#include <dxgiformat.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>
#include <string>
#include <sstream>
#include <initializer_list>
namespace {
constexpr wchar_t EXPECTED_HASH[] = L"529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900";
constexpr std::uintptr_t CALL_RVA = 0x796ed, MATCHER_RVA = 0x83c80;
const std::array<unsigned char,5> ORIGINAL_CALL{{0xe8,0x8e,0xa5,0x00,0x00}};
using Matcher = bool(__fastcall*)(ID3D11DeviceContext*,UINT,UINT);
Matcher originalMatcher=nullptr;
std::filesystem::path logPath;
std::atomic<unsigned> draws{0}, candidates{0}, matcherSuccess{0};
void log(const std::string& message) {
    if(logPath.empty()) return;
    std::ofstream f(logPath,std::ios::app);
    if(f) f << message << '\n';
}
bool exactVersion(HMODULE module) {
    wchar_t name[32768]{};
    if(!GetModuleFileNameW(module,name,32768)) return false;
    std::ifstream f(name,std::ios::binary);
    if(!f) return false;
    BCRYPT_ALG_HANDLE alg=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0) {
        BCryptCloseAlgorithmProvider(alg,0); return false;
    }
    std::array<char,65536> block{};
    bool ok=true;
    while(f) {
        f.read(block.data(),static_cast<std::streamsize>(block.size()));
        auto count=f.gcount();
        if(count>0 && BCryptHashData(hash,reinterpret_cast<PUCHAR>(block.data()),
              static_cast<ULONG>(count),0)<0) {ok=false;break;}
    }
    if(!f.eof()) ok=false;
    std::array<UCHAR,32> digest{};
    if(ok) ok=BCryptFinishHash(hash,digest.data(),32,0)>=0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg,0);
    if(!ok) return false;
    std::wstring actual;
    constexpr wchar_t hex[]=L"0123456789abcdef";
    for(auto b:digest) {actual+=hex[b>>4];actual+=hex[b&15];}
    return actual==EXPECTED_HASH;
}
struct Tex {UINT w=0,h=0,format=0,view=0;bool valid=false;};
Tex resource(ID3D11Resource* r) {
    if(!r) return {};
    ID3D11Texture2D* tex=nullptr;
    Tex d{};
    if(SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&tex)))&&tex){
        D3D11_TEXTURE2D_DESC desc{};
        tex->GetDesc(&desc);
        d={desc.Width,desc.Height,static_cast<UINT>(desc.Format),0,true};
        tex->Release();
    }
    return d;
}
Tex srvInfo(ID3D11ShaderResourceView* v) {
    if(!v) return {};
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};v->GetDesc(&vd);
    ID3D11Resource* r=nullptr;v->GetResource(&r);
    Tex d=resource(r);if(r)r->Release();d.view=static_cast<UINT>(vd.Format);return d;
}
Tex rtvInfo(ID3D11RenderTargetView* v) {
    if(!v) return {};
    D3D11_RENDER_TARGET_VIEW_DESC vd{};v->GetDesc(&vd);
    ID3D11Resource* r=nullptr;v->GetResource(&r);
    Tex d=resource(r);if(r)r->Release();d.view=static_cast<UINT>(vd.Format);return d;
}
Tex dsvInfo(ID3D11DepthStencilView* v) {
    if(!v) return {};
    D3D11_DEPTH_STENCIL_VIEW_DESC vd{};v->GetDesc(&vd);
    ID3D11Resource* r=nullptr;v->GetResource(&r);
    Tex d=resource(r);if(r)r->Release();d.view=static_cast<UINT>(vd.Format);return d;
}
std::string label(const char* name,const Tex& d){
    std::ostringstream o;o<<name<<'=';
    if(!d.valid)o<<"none";
    else o<<d.w<<'x'<<d.h<<" resource_fmt="<<d.format<<" view_fmt="<<d.view;
    return o.str();
}
struct Bound {Tex t0,t1,t2,rtv,depth;};
Bound getBound(ID3D11DeviceContext* context){
    ID3D11ShaderResourceView* srvs[3]{};
    ID3D11RenderTargetView* rtv=nullptr;
    ID3D11DepthStencilView* depth=nullptr;
    context->PSGetShaderResources(0,3,srvs);
    context->OMGetRenderTargets(1,&rtv,&depth);
    Bound b{};
    b.t0=srvInfo(srvs[0]);b.t1=srvInfo(srvs[1]);b.t2=srvInfo(srvs[2]);
    b.rtv=rtvInfo(rtv);b.depth=dsvInfo(depth);
    for(auto& v:srvs)if(v)v->Release();
    if(rtv)rtv->Release();if(depth)depth->Release();
    return b;
}
bool isCandidate(const Bound& b){
    return b.t0.valid&&b.t0.format==DXGI_FORMAT_R11G11B10_FLOAT
      &&b.t2.valid&&b.t2.format==DXGI_FORMAT_R16G16B16A16_FLOAT
      &&b.rtv.valid&&b.rtv.format==DXGI_FORMAT_R11G11B10_FLOAT
      &&b.t0.w==b.t2.w&&b.t0.h==b.t2.h
      &&b.t0.w==b.rtv.w&&b.t0.h==b.rtv.h;
}
bool __fastcall traceDraw(ID3D11DeviceContext* ctx,UINT vertexCount,UINT firstVertex){
    auto orig=originalMatcher;
    if(!orig)return false;
    if(!ctx||vertexCount!=6||firstVertex!=0)return orig(ctx,vertexCount,firstVertex);
    unsigned n=++draws;
    const bool shouldCapture=n<=2048&&candidates.load()<8;
    Bound before{};
    if(shouldCapture) before=getBound(ctx);
    const bool handled=orig(ctx,vertexCount,firstVertex);
    if(handled)++matcherSuccess;
    if(shouldCapture&&isCandidate(before)){
        const auto hit=++candidates;
        if(hit<=8){
            std::ostringstream out;
            out<<"[Candidate #"<<hit<<"] match_result="<<handled<<' '
               <<label("color",before.t0)<<' '<<label("t1",before.t1)<<' '
               <<label("motion",before.t2)<<' '<<label("output",before.rtv)
               <<' '<<label("depth",before.depth);
            log(out.str());
        }
    }
    if(n==256||n==2048){
        log("[Summary] Draw(6,0)="+std::to_string(n)
            +" format_candidates="+std::to_string(candidates.load())
            +" original_matcher_handled="+std::to_string(matcherSuccess.load()));
    }
    return handled;
}
void* allocNear(std::uintptr_t address){
    SYSTEM_INFO si{};GetSystemInfo(&si);
    std::uintptr_t step=si.dwAllocationGranularity;
    auto aligned=address-(address%step);
    for(std::uintptr_t d=step;d<0x70000000;d+=step){
        for(int sign:{1,-1}){
            if(sign<0&&d>aligned)continue;
            if(sign>0&&aligned>std::numeric_limits<std::uintptr_t>::max()-d)continue;
            auto at=sign<0?aligned-d:aligned+d;
            if(void* p=VirtualAlloc(reinterpret_cast<void*>(at),4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE))return p;
        }
    }
    return nullptr;
}
} // namespace
extern "C" __declspec(dllexport) BOOL __cdecl InstallTaaTrace(HMODULE module){
    if(!module||originalMatcher||!exactVersion(module))return FALSE;
    auto base=reinterpret_cast<unsigned char*>(module);
    auto site=base+CALL_RVA;
    if(std::memcmp(site,ORIGINAL_CALL.data(),5)!=0)return FALSE;
    std::int32_t disp=0;std::memcpy(&disp,site+1,4);
    if(site+5+disp!=base+MATCHER_RVA)return FALSE;
    auto relay=allocNear(reinterpret_cast<std::uintptr_t>(site));
    if(!relay)return FALSE;
    const auto dist=reinterpret_cast<std::intptr_t>(relay)-reinterpret_cast<std::intptr_t>(site+5);
    if(dist<std::numeric_limits<std::int32_t>::min()||dist>std::numeric_limits<std::int32_t>::max()){
        VirtualFree(relay,0,MEM_RELEASE);return FALSE;
    }
    std::array<unsigned char,12> jump{{0x48,0xb8}};
    auto target=reinterpret_cast<std::uintptr_t>(&traceDraw);
    std::memcpy(jump.data()+2,&target,sizeof(target));jump[10]=0xff;jump[11]=0xe0;
    std::memcpy(relay,jump.data(),jump.size());
    FlushInstructionCache(GetCurrentProcess(),relay,jump.size());
    std::array<unsigned char,5> patched{{0xe8,0,0,0,0}};
    const auto d=static_cast<std::int32_t>(dist);
    std::memcpy(patched.data()+1,&d,4);
    wchar_t filename[32768]{};
    if(GetModuleFileNameW(module,filename,32768))logPath=std::filesystem::path(filename).parent_path()/"MafiaTaaTrace.log";
    log("=== Mafia III Modern Tweaks read-only TAA resource probe ===");
    log("Mod DLL hash and original Draw matcher CALL RVA 0x796ed verified.");
    log("Only resource descriptors are read; no textures are modified and no FSR is used.");
    originalMatcher=reinterpret_cast<Matcher>(base+MATCHER_RVA);
    DWORD old=0;
    if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old)){
        originalMatcher=nullptr;VirtualFree(relay,0,MEM_RELEASE);log("ERROR: VirtualProtect denied");return FALSE;
    }
    std::memcpy(site,patched.data(),5);
    FlushInstructionCache(GetCurrentProcess(),site,5);
    DWORD unused=0;VirtualProtect(site,5,old,&unused);
    log("Interceptor installed; enter gameplay and rotate the camera.");
    return TRUE;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(instance);
    return TRUE;
}
