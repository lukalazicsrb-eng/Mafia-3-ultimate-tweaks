#include "menu_state.h"
#include <windows.h>
#include <cassert>
#include <iostream>

extern "C" __declspec(dllimport) void __cdecl MafiaFsrGetEvidenceV1(mafiafsr::BackendEvidence*);
extern "C" __declspec(dllimport) void __cdecl MafiaFsrSetModeV1(int);
extern "C" __declspec(dllimport) BOOL __cdecl MafiaFsrInstallDrawHookV1(HMODULE);
extern "C" __declspec(dllimport) BOOL __cdecl MafiaFsrConfigureBackendV1(
    ID3D11Device*, const wchar_t*, unsigned int, unsigned int);

int main() {
    mafiafsr::BackendEvidence evidence{};
    MafiaFsrGetEvidenceV1(&evidence);
    assert(!evidence.providerIs412);
    assert(!evidence.dx11Dx12BridgeReady);
    assert(!evidence.temporalInputsValidated);
    assert(!evidence.gameFrameDispatchSucceeded);
    assert(!evidence.outputPresentedInGame);
    assert(!evidence.shadowGameFrameDispatchSucceeded);
    assert(!evidence.outputHandoffValidated);
    assert(!mafiafsr::modeAvailable(mafiafsr::Mode::NativeAa, evidence));
    MafiaFsrSetModeV1(3);
    MafiaFsrGetEvidenceV1(&evidence);
    assert(!mafiafsr::modeAvailable(mafiafsr::Mode::NativeAa, evidence));
    assert(MafiaFsrInstallDrawHookV1(nullptr)==FALSE);
    assert(MafiaFsrConfigureBackendV1(nullptr,nullptr,2560,1440)==FALSE);
    std::cout << "No real frame => no FSR readiness. TAA fallback intact.\n";
    return 0;
}
