#include "menu_state.h"
namespace mafiafsr {
bool modeAvailable(Mode mode, const BackendEvidence& evidence) {
    if (static_cast<unsigned>(mode) <= 2u) return true;
    const bool nativeReady = evidence.providerIs412 && evidence.dx11Dx12BridgeReady
        && evidence.temporalInputsValidated && evidence.gameFrameDispatchSucceeded
        && evidence.outputPresentedInGame;
    return nativeReady && (mode == Mode::NativeAa || evidence.lowerResolution3DVerified);
}
Choices makeChoices(const BackendEvidence& evidence) {
    return {{{Mode::OriginalOff, "Off", true},
             {Mode::OriginalTaa, "Original TAA", true},
             {Mode::OriginalDlaa, "Original DLAA", true},
             {Mode::NativeAa, "FSR 4.1.2 Native AA", modeAvailable(Mode::NativeAa,evidence)},
             {Mode::Quality, "FSR 4.1.2 Quality", modeAvailable(Mode::Quality,evidence)},
             {Mode::Balanced, "FSR 4.1.2 Balanced", modeAvailable(Mode::Balanced,evidence)},
             {Mode::Performance, "FSR 4.1.2 Performance", modeAvailable(Mode::Performance,evidence)}}};
}
SelectionResult MenuState::select(Mode requested, const BackendEvidence& evidence) {
    if (!modeAvailable(requested, evidence))
        return {mode_ == Mode::OriginalOff ? 0 : 1, mode_, false, false};
    mode_ = requested;
    const auto value = static_cast<int>(requested);
    // Original Modern Tweaks writes TemporalAA=index>0, DLAA=index==2.
    // Genuine FSR must preserve the TAA draw while avoiding competing DLAA.
    return {value <= 2 ? value : 1, requested, true, true};
}
int MenuState::displayedIndex(int originalIndex, const BackendEvidence& evidence) const {
    if (static_cast<int>(mode_) > 2 && modeAvailable(mode_, evidence))
        return static_cast<int>(mode_);
    return originalIndex >= 0 && originalIndex <= 2 ? originalIndex : 1;
}
}
