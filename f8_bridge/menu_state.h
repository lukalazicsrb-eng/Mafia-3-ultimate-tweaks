#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace mafiafsr {
enum class Mode : std::uint8_t {
    OriginalOff = 0, OriginalTaa = 1, OriginalDlaa = 2,
    NativeAa = 3, Quality = 4, Balanced = 5, Performance = 6
};
struct BackendEvidence {
    // MUST derive from actual Mafia III renderer telemetry, never synthetic tests.
    bool providerIs412 = false;
    bool dx11Dx12BridgeReady = false;
    bool temporalInputsValidated = false;
    bool gameFrameDispatchSucceeded = false;
    bool outputPresentedInGame = false;
    bool lowerResolution3DVerified = false;
    // A successful real-game GPU preflight that does NOT overwrite the game.
    bool shadowGameFrameDispatchSucceeded = false;
    bool outputHandoffValidated = false;
};
struct Choice { Mode mode; std::string_view label; bool available; };
using Choices = std::array<Choice, 7>;
Choices makeChoices(const BackendEvidence& evidence);
bool modeAvailable(Mode mode, const BackendEvidence& evidence);
struct SelectionResult {
    int originalIndex; Mode requestedMode; bool accepted; bool shouldNotifyOriginal;
};
class MenuState {
public:
    SelectionResult select(Mode requested, const BackendEvidence& evidence);
    int displayedIndex(int originalIndex, const BackendEvidence& evidence) const;
    Mode mode() const noexcept { return mode_; }
private:
    Mode mode_ = Mode::OriginalTaa;
};
}
