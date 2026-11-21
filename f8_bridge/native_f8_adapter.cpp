#include "menu_state.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <vector>
#include <initializer_list>

// EXPERIMENTAL, UNTESTED, version-locked hook; not an FSR renderer or a loader.
namespace {
using namespace mafiafsr;
constexpr wchar_t exactHash[] = L"529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900";
constexpr std::uintptr_t siteRva = 0x5EEE3;
constexpr std::uintptr_t comboRva = 0x10E3B0;
constexpr std::array<unsigned char,5> originalCall{{0xE8,0xC8,0xF4,0x0A,0x00}};
using Combo = bool (__cdecl*)(const char*,int*,const char* const*,int,int);
using EvidenceCallback = void (__cdecl*)(BackendEvidence*);
using ModeCallback = void (__cdecl*)(int);
MenuState state;
Combo originalCombo = nullptr;
EvidenceCallback evidenceCallback = nullptr;
ModeCallback modeCallback = nullptr;
std::uint8_t* patchedSite = nullptr;
void* allocatedRelay = nullptr;
std::array<unsigned char,5> patchedBytes{};
std::mutex guard;

bool sha256(const wchar_t* path,std::wstring& hex) {
    BCRYPT_ALG_HANDLE alg=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    auto close=[&]() {
        if(hash) BCryptDestroyHash(hash);
        if(alg) BCryptCloseAlgorithmProvider(alg,0);
    };
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0) {close();return false;}
    std::ifstream in(path,std::ios::binary);
    if(!in) {close();return false;}
    std::array<char,65536> buf{};
    while(in) {
        in.read(buf.data(),static_cast<std::streamsize>(buf.size()));
        auto n=in.gcount();
        if(n>0 && BCryptHashData(hash,reinterpret_cast<PUCHAR>(buf.data()),static_cast<ULONG>(n),0)<0) {close();return false;}
    }
    if(!in.eof()) {close();return false;}
    std::array<UCHAR,32> digest{};
    bool success=BCryptFinishHash(hash,digest.data(),32,0)>=0;
    close();
    if(!success) return false;
    constexpr wchar_t chars[]=L"0123456789abcdef";
    hex.clear();
    for(auto b:digest) {hex+=chars[b>>4];hex+=chars[b&15];}
    return true;
}
void* allocateNearby(std::uintptr_t address) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    auto gran=static_cast<std::uintptr_t>(info.dwAllocationGranularity);
    auto base=address-(address%gran);
    for(std::uintptr_t delta=gran;delta<0x70000000;delta+=gran) {
        for(int sign:{1,-1}) {
            if(sign<0 && delta>base) continue;
            if(sign>0 && base>std::numeric_limits<std::uintptr_t>::max()-delta) continue;
            auto at=sign<0?base-delta:base+delta;
            if(auto p=VirtualAlloc(reinterpret_cast<void*>(at),4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE)) return p;
        }
    }
    return nullptr;
}
bool modifyFive(std::uint8_t* address,const unsigned char* bytes) {
    DWORD old=0;
    if(!VirtualProtect(address,5,PAGE_EXECUTE_READWRITE,&old)) return false;
    std::memcpy(address,bytes,5);
    FlushInstructionCache(GetCurrentProcess(),address,5);
    DWORD ignored=0;
    VirtualProtect(address,5,old,&ignored);
    return true;
}
bool __cdecl extensionCombo(const char* label,int* current,const char* const* originalLabels,int count,int height) {
    auto fn=originalCombo;
    if(!fn) return false;
    if(!current || !originalLabels || count!=3) return fn(label,current,originalLabels,count,height);
    std::lock_guard<std::mutex> lock(guard);
    BackendEvidence evidence{};
    if(evidenceCallback) evidenceCallback(&evidence); // genuine live game telemetry only
    const auto choices=makeChoices(evidence);
    std::array<std::string,4> names{};
    std::array<const char*,7> labels{};
    for(std::size_t i=0;i<3;++i) labels[i]=originalLabels[i];
    for(std::size_t i=3;i<7;++i) {
        names[i-3]=std::string(choices[i].label);
        if(!choices[i].available) names[i-3]+=" [unavailable]";
        labels[i]=names[i-3].c_str();
    }
    int shown=state.displayedIndex(*current,evidence);
    bool changed=fn(label,&shown,labels.data(),7,height);
    if(!changed || shown<0 || shown>6) return false;
    auto result=state.select(static_cast<Mode>(shown),evidence);
    if(!result.accepted) return false;
    *current=result.originalIndex;
    if(modeCallback) modeCallback(static_cast<int>(result.requestedMode));
    return result.shouldNotifyOriginal;
}
}
extern "C" __declspec(dllexport) BOOL __cdecl InstallF8Extension(HMODULE modernTweaks) {
    if(!modernTweaks || patchedSite) return FALSE;
    wchar_t path[32768]{};
    if(!GetModuleFileNameW(modernTweaks,path,32768)) return FALSE;
    std::wstring hash;
    if(!sha256(path,hash) || hash!=exactHash) return FALSE;
    auto base=reinterpret_cast<std::uint8_t*>(modernTweaks);
    auto site=base+siteRva;
    if(std::memcmp(site,originalCall.data(),5)!=0) return FALSE;
    auto relay=allocateNearby(reinterpret_cast<std::uintptr_t>(site));
    if(!relay) return FALSE;
    auto rel=reinterpret_cast<std::intptr_t>(relay)-reinterpret_cast<std::intptr_t>(site+5);
    if(rel<std::numeric_limits<std::int32_t>::min() || rel>std::numeric_limits<std::int32_t>::max()) {
        VirtualFree(relay,0,MEM_RELEASE);return FALSE;
    }
    std::array<std::uint8_t,12> trampoline{{0x48,0xB8}};
    auto target=reinterpret_cast<std::uintptr_t>(&extensionCombo);
    std::memcpy(trampoline.data()+2,&target,8);
    trampoline[10]=0xFF;trampoline[11]=0xE0;
    std::memcpy(relay,trampoline.data(),trampoline.size());
    FlushInstructionCache(GetCurrentProcess(),relay,trampoline.size());
    std::array<unsigned char,5> bytes{{0xE8,0,0,0,0}};
    auto rel32=static_cast<std::int32_t>(rel);
    std::memcpy(bytes.data()+1,&rel32,4);
    originalCombo=reinterpret_cast<Combo>(base+comboRva);
    if(!modifyFive(site,bytes.data())) {
        originalCombo=nullptr;
        VirtualFree(relay,0,MEM_RELEASE);
        return FALSE;
    }
    patchedBytes=bytes;
    patchedSite=site;
    allocatedRelay=relay;
    return TRUE;
}
extern "C" __declspec(dllexport) void __cdecl RegisterFsrEvidenceCallback(void (__cdecl* callback)(mafiafsr::BackendEvidence*)) {
    std::lock_guard<std::mutex> lock(guard);
    evidenceCallback=callback;
}
extern "C" __declspec(dllexport) void __cdecl RegisterFsrModeCallback(
    void (__cdecl* callback)(int)) {
    std::lock_guard<std::mutex> lock(guard);
    modeCallback=callback;
}
extern "C" __declspec(dllexport) BOOL __cdecl RemoveF8Extension() {
    // Must be called only after the game/menu rendering thread has stopped.
    if(!patchedSite || std::memcmp(patchedSite,patchedBytes.data(),5)!=0) return FALSE;
    if(!modifyFive(patchedSite,originalCall.data())) return FALSE;
    patchedSite=nullptr;
    originalCombo=nullptr;
    evidenceCallback=nullptr;
    modeCallback=nullptr;
    if(allocatedRelay) VirtualFree(allocatedRelay,0,MEM_RELEASE);
    allocatedRelay=nullptr;
    return TRUE;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
