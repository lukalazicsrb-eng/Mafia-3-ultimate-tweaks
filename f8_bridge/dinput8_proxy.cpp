// EXPERIMENTAL MOD LOADER. Not the AMD FSR renderer.
// Allows testing the F8 extension without altering the author's DLL on disk.
// Only use after backing up the game-local dinput8.dll.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h> // REFIID and LPUNKNOWN, without dinput8.dll import declaration
#include <filesystem>
#include <fstream>
#include <string>
#include "menu_state.h"

namespace {
using DInputFactory = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using F8Installer = BOOL (__cdecl*)(HMODULE);
using TraceInstaller = BOOL (__cdecl*)(HMODULE);
using FsrTraceInstaller = BOOL (__cdecl*)(HMODULE);
using EnableDrawTrace = void (__cdecl*)(int);
using GetEvidence = void (__cdecl*)(mafiafsr::BackendEvidence*);
using SetMode = void (__cdecl*)(int);
using RegisterEvidence = void (__cdecl*)(GetEvidence);
using RegisterMode = void (__cdecl*)(SetMode);

HINSTANCE self = nullptr;
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
HMODULE original = nullptr;
HMODULE systemInput = nullptr;
HMODULE extension = nullptr;
HMODULE taaProbe = nullptr;
HMODULE fsrGamepass = nullptr;
DInputFactory forwardTo = nullptr;

std::filesystem::path ownFolder() {
    wchar_t path[32768]{};
    if (!self || GetModuleFileNameW(self, path, 32768) == 0)
        return {};
    return std::filesystem::path(path).parent_path();
}

void report(const std::filesystem::path& folder, const std::string& message) {
    // Only called during one-time init, outside DllMain.
    if (folder.empty()) return;
    std::ofstream out(folder / "MafiaF8_loader.log", std::ios::app);
    if (out) out << message << "\n";
}

HMODULE loadFrom(const std::filesystem::path& file) {
    return LoadLibraryExW(file.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
}

bool flagEnabled(const std::filesystem::path& folder, const char* flag) {
    std::ifstream file(folder / "MafiaF8_test.ini");
    std::string line;
    while (std::getline(file, line))
        if (line == flag) return true;
    return false;
}

DWORD WINAPI gamepassWorker(LPVOID) {
    const auto folder=ownFolder();
    if(!fsrGamepass || !original)return 0;
    const auto install=reinterpret_cast<FsrTraceInstaller>(
        GetProcAddress(fsrGamepass,"MafiaFsrInstallDrawHookV1"));
    const auto enableTrace=reinterpret_cast<EnableDrawTrace>(
        GetProcAddress(fsrGamepass,"MafiaFsrEnableDrawTraceV1"));
    if(!install || !enableTrace) {
        report(folder,"Gamepass exports missing; keeping original DirectX Draw");
        return 0;
    }
    // Modern Tweaks fills its original Draw function pointer on the game's
    // D3D11 hook initialization, after this proxy starts. Never block the
    // DirectInput8Create or Steam startup thread while waiting for it.
    for (int retry=0; retry<90; ++retry) {
        if(install(original)) {
            enableTrace(1); // bounded read-only fullscreen Draw diagnostics
            report(folder,"DirectDraw probe INSTALLED; check MafiaFsrDraw.log");
            return 0;
        }
        if (retry==0 || retry==20 || retry==60)
            report(folder,"DirectDraw probe waiting for game's D3D11 initialization");
        Sleep(1000);
    }
    report(folder,"DirectDraw probe did not install within 90s. Game can still run with original TAA.");
    return 0;
}

BOOL CALLBACK initialize(PINIT_ONCE, PVOID, PVOID*) {
    const auto folder = ownFolder();
    report(folder, "=== Mafia III v9 F8 + DirectDraw probe (FSR not active) ===");
    if (folder.empty()) return TRUE;

    auto modFile = folder / "ModernTweaks_original.dll";
    original = loadFrom(modFile);
    if (!original) {
        report(folder, "Original ModernTweaks_original.dll could not be loaded. Win32 error "
            + std::to_string(GetLastError()));
    } else {
        forwardTo = reinterpret_cast<DInputFactory>(
            GetProcAddress(original, "DirectInput8Create"));
        if (!forwardTo) {
            report(folder, "Original mod has no DirectInput8Create; falling back to system input");
        } else {
            report(folder, "Original Modern Tweaks loaded and DirectInput8Create available");
        }
    }

    if (!forwardTo) {
        wchar_t systemPath[32768]{};
        const UINT count = GetSystemDirectoryW(systemPath, 32768);
        if (count && count < 32768) {
            systemInput = loadFrom(std::filesystem::path(systemPath) / "dinput8.dll");
            if (systemInput) forwardTo = reinterpret_cast<DInputFactory>(
                GetProcAddress(systemInput, "DirectInput8Create"));
        }
    }

    if (original && flagEnabled(folder, "EnableExperimentalF8=1")) {
        extension = loadFrom(folder / "mafia_f8_experimental.dll");
        if (!extension) {
            report(folder, "F8 adapter DLL load failed. Win32 error "
                + std::to_string(GetLastError()));
        } else {
            auto install = reinterpret_cast<F8Installer>(
                GetProcAddress(extension, "InstallF8Extension"));
            if (install && install(original)) {
                report(folder, "F8 adapter hook installation returned TRUE (gameplay not yet verified)");
            } else {
                report(folder, "F8 adapter hook installation returned FALSE; original menu unchanged");
            }
        }
    } else {
        report(folder, "F8 beta disabled or Modern Tweaks missing; menu unchanged");
    }

    if (original && flagEnabled(folder, "EnableTaaTrace=1")) {
        taaProbe = loadFrom(folder / "mafia_taa_trace.dll");
        if (!taaProbe) {
            report(folder, "TAA probe load failed, Win32 error " + std::to_string(GetLastError()));
        } else {
            auto install = reinterpret_cast<TraceInstaller>(
                GetProcAddress(taaProbe, "InstallTaaTrace"));
            if (install && install(original)) {
                report(folder, "TAA read-only probe installed (check MafiaTaaTrace.log)");
            } else {
                report(folder, "TAA probe refused its hook; original TAA rendering untouched");
            }
        }
    } else {
        report(folder, "TAA probe disabled by configuration (no tracing)");
    }
    if (original && flagEnabled(folder, "EnableFsrDrawTrace=1")) {
        fsrGamepass=loadFrom(folder/"mafia_fsr_gamepass.dll");
        if(!fsrGamepass) {
            report(folder,"FSR Draw probe DLL load failed. Win32 error "+std::to_string(GetLastError()));
        } else {
            // The F8 menu remains unchanged until in-game output is genuinely
            // processed. No fake FSR functionality in this installer.
            auto registerEvidence=reinterpret_cast<RegisterEvidence>(
                extension ? GetProcAddress(extension,"RegisterFsrEvidenceCallback"):nullptr);
            auto registerMode=reinterpret_cast<RegisterMode>(
                extension ? GetProcAddress(extension,"RegisterFsrModeCallback"):nullptr);
            auto evidence=reinterpret_cast<GetEvidence>(
                GetProcAddress(fsrGamepass,"MafiaFsrGetEvidenceV1"));
            auto mode=reinterpret_cast<SetMode>(
                GetProcAddress(fsrGamepass,"MafiaFsrSetModeV1"));
            if(registerEvidence && registerMode && evidence && mode) {
                registerEvidence(evidence);
                registerMode(mode);
                report(folder,"F8 menu connected to FSR runtime telemetry (FSR not yet available)");
            } else {
                report(folder,"Runtime telemetry not connected; F8 defaults to unavailable FSR");
            }
            HANDLE thread=CreateThread(nullptr,0,gamepassWorker,nullptr,0,nullptr);
            if(thread) CloseHandle(thread);
            else report(folder,"Could not start delayed DirectDraw probe worker");
        }
    } else {
        report(folder,"DirectDraw probe disabled by configuration");
    }
    report(folder, forwardTo ? "DirectInput forwarding available" : "DirectInput forwarding MISSING");
    return TRUE;
}

void ensureInitialized() {
    InitOnceExecuteOnce(&once, initialize, nullptr, nullptr);
}

DWORD WINAPI backgroundInitialize(LPVOID) {
    ensureInitialized();
    return 0;
}
}

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD version, REFIID riid, LPVOID* output, LPUNKNOWN outer) {
    ensureInitialized();
    if (!forwardTo) return E_FAIL;
    return forwardTo(hinst, version, riid, output, outer);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        DisableThreadLibraryCalls(module);
        // Never call LoadLibrary under loader lock. The worker does not wait here.
        HANDLE thread = CreateThread(nullptr, 0, backgroundInitialize, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
