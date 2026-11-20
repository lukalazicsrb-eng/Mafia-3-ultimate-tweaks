// EXPERIMENTAL MOD LOADER. Not the AMD FSR renderer.
// Allows testing the F8 extension without altering the author's DLL on disk.
// Only use after backing up the game-local dinput8.dll.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dinput.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
using DInputFactory = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using F8Installer = BOOL (__cdecl*)(HMODULE);

HINSTANCE self = nullptr;
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
HMODULE original = nullptr;
HMODULE systemInput = nullptr;
HMODULE extension = nullptr;
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

bool betaEnabled(const std::filesystem::path& folder) {
    std::ifstream file(folder / "MafiaF8_test.ini");
    std::string line;
    while (std::getline(file, line))
        if (line == "EnableExperimentalF8=1") return true;
    return false;
}

BOOL CALLBACK initialize(PINIT_ONCE, PVOID, PVOID*) {
    const auto folder = ownFolder();
    report(folder, "=== Mafia III experimental F8 loader (NO in-game FSR) ===");
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

    if (original && betaEnabled(folder)) {
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
