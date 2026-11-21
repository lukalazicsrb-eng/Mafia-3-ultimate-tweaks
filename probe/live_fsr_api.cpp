#include "live_fsr_api.h"
#include "live_fsr_bridge.h"

#include <exception>
#include <memory>
#include <string>

using mafiafsr::LiveNativeAaBridge;
using mafiafsr::LiveFrameEvidence;
using mafiafsr::LiveFrameTextures;

namespace {
thread_local std::string createError;

LiveFrameEvidence convert(const MafiaFsrFrameEvidenceV1& e) {
    LiveFrameEvidence result{};
    result.originalTaaPass = e.originalTaaPass != 0;
    result.preHudOutput = e.preHudOutput != 0;
    result.linearHdrSceneColor = e.linearHdrSceneColor != 0;
    result.reversedDepth = e.reversedDepth != 0;
    result.rawMotionVectors = e.rawMotionVectors != 0;
    result.motionInUvCoordinates = e.motionInUvCoordinates != 0;
    result.currentFrameJitter = e.currentFrameJitter != 0;
    result.cameraProjection = e.cameraProjection != 0;
    result.completeFrame = e.completeFrame != 0;
    return result;
}

FsrFrameParameters convert(const MafiaFsrCameraV1& p) {
    return {p.jitterX, p.jitterY, p.motionScaleX, p.motionScaleY,
            p.frameTimeMs, p.preExposure, p.cameraNear, p.cameraFar,
            p.verticalFov, p.viewSpaceToMeters, p.resetHistory != 0};
}
} // namespace

extern "C" __declspec(dllexport) int __cdecl MafiaFsrCreateV1(
    ID3D11Device* gameDevice, const wchar_t* signedBinaryDir,
    uint32_t width, uint32_t height, void** outSession) {
    if (outSession) *outSession = nullptr;
    if (!outSession || !gameDevice || !signedBinaryDir || !signedBinaryDir[0]) {
        createError = "Missing game device, signed AMD runtime directory, or output session";
        return 0;
    }
    try {
        auto session = std::make_unique<LiveNativeAaBridge>(
            gameDevice, signedBinaryDir, width, height);
        *outSession = session.release();
        createError.clear();
        return 1;
    } catch (const std::exception& e) {
        createError = e.what();
        return 0;
    } catch (...) {
        createError = "Unknown failure constructing real-game FSR Native AA backend";
        return 0;
    }
}

extern "C" __declspec(dllexport) int __cdecl MafiaFsrProcessNativeAaV1(
    void* session, ID3D11DeviceContext* gameContext,
    ID3D11ShaderResourceView* color, ID3D11ShaderResourceView* depth,
    ID3D11ShaderResourceView* motion, ID3D11RenderTargetView* output,
    const MafiaFsrFrameEvidenceV1* evidence,
    const MafiaFsrCameraV1* camera) {
    if (!session || !evidence || !camera
        || evidence->size != sizeof(MafiaFsrFrameEvidenceV1)
        || camera->size != sizeof(MafiaFsrCameraV1)) return 0;
    const LiveFrameTextures inputs{color, depth, motion, output};
    // No default camera/jitter or fabricated motion. If validation fails,
    // process() returns false and caller MUST let the original TAA draw run.
    return static_cast<LiveNativeAaBridge*>(session)->process(
        gameContext, inputs, convert(*evidence), convert(*camera)) ? 1 : 0;
}

extern "C" __declspec(dllexport) const char* __cdecl MafiaFsrProviderV1(void* session) {
    return session ? static_cast<LiveNativeAaBridge*>(session)->provider().c_str() : "";
}
extern "C" __declspec(dllexport) const char* __cdecl MafiaFsrLastErrorV1(void* session) {
    return session ? static_cast<LiveNativeAaBridge*>(session)->lastError().c_str()
                   : createError.c_str();
}
extern "C" __declspec(dllexport) void __cdecl MafiaFsrDestroyV1(void* session) {
    delete static_cast<LiveNativeAaBridge*>(session);
}
