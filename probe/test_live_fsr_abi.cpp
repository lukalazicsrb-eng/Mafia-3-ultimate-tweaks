#include "live_fsr_api.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    void* session = reinterpret_cast<void*>(static_cast<uintptr_t>(123));
    assert(MafiaFsrCreateV1(nullptr, nullptr, 2560, 1440, &session) == 0);
    assert(session == nullptr);
    const char* error = MafiaFsrLastErrorV1(nullptr);
    assert(error != nullptr && std::strlen(error) > 0);

    MafiaFsrFrameEvidenceV1 evidence{};
    evidence.size = sizeof(evidence);
    MafiaFsrCameraV1 camera{};
    camera.size = sizeof(camera);

    // No session or caller-proven real textures -> NEVER claim FSR succeeded.
    assert(MafiaFsrProcessNativeAaV1(nullptr, nullptr, nullptr, nullptr, nullptr,
                                     nullptr, &evidence, &camera) == 0);
    assert(MafiaFsrProviderV1(nullptr) != nullptr);
    MafiaFsrDestroyV1(nullptr);
    std::cout << "Real-frame ABI rejects missing session/inputs as expected.\n";
}
