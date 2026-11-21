#pragma once
#include <stdint.h>

// The DLL exports the backend ABI; consumers import it (or resolve it with
// GetProcAddress). Avoid accidentally exporting unresolved symbols from
// future DirectX hook modules.
#ifdef MAFIA_FSR_BACKEND_BUILD
#define MAFIA_FSR_API __declspec(dllexport)
#else
#define MAFIA_FSR_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif

// ABI v1: Windows x64 only. This is a backend library, NOT a game loader.
// The hosting D3D11 pass hook (not implemented in this module) is responsible
// for supplying verified real Mafia III temporal resources and camera data.
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;

typedef struct MafiaFsrFrameEvidenceV1 {
    uint32_t size;                 // sizeof(MafiaFsrFrameEvidenceV1)
    uint32_t originalTaaPass;
    uint32_t preHudOutput;
    uint32_t linearHdrSceneColor;
    uint32_t reversedDepth;
    uint32_t rawMotionVectors;
    uint32_t motionInUvCoordinates;
    uint32_t currentFrameJitter;
    uint32_t cameraProjection;
    uint32_t completeFrame;
} MafiaFsrFrameEvidenceV1;

typedef struct MafiaFsrCameraV1 {
    uint32_t size;                 // sizeof(MafiaFsrCameraV1)
    float jitterX;
    float jitterY;
    float motionScaleX;           // 1 after our UV->pixel conversion
    float motionScaleY;           // 1 after our UV->pixel conversion
    float frameTimeMs;
    float preExposure;
    float cameraNear;
    float cameraFar;
    float verticalFov;
    float viewSpaceToMeters;
    uint32_t resetHistory;
} MafiaFsrCameraV1;

// Return 1 on success, 0 on failure. No exceptions cross this ABI.
MAFIA_FSR_API int __cdecl MafiaFsrCreateV1(
    struct ID3D11Device* gameDevice,
    const wchar_t* fullPathToAmdSignedBinDirectory,
    uint32_t renderWidth,
    uint32_t renderHeight,
    void** outSession);

MAFIA_FSR_API int __cdecl MafiaFsrProcessNativeAaV1(
    void* session,
    struct ID3D11DeviceContext* gameImmediateContext,
    struct ID3D11ShaderResourceView* preTaaColor,
    struct ID3D11ShaderResourceView* realDepth,
    struct ID3D11ShaderResourceView* realRawMotion,
    struct ID3D11RenderTargetView* preHudOutput,
    const MafiaFsrFrameEvidenceV1* evidence,
    const MafiaFsrCameraV1* camera);

// Preflight: real-game AMD dispatch, without changing the game's framebuffer.
// Successful preflight is necessary but not sufficient for showing FSR as enabled.
MAFIA_FSR_API int __cdecl MafiaFsrPreviewNativeAaV1(
    void* session,
    struct ID3D11DeviceContext* gameImmediateContext,
    struct ID3D11ShaderResourceView* preTaaColor,
    struct ID3D11ShaderResourceView* realDepth,
    struct ID3D11ShaderResourceView* realRawMotion,
    struct ID3D11RenderTargetView* preHudOutput,
    const MafiaFsrFrameEvidenceV1* evidence,
    const MafiaFsrCameraV1* camera);

MAFIA_FSR_API const char* __cdecl MafiaFsrProviderV1(void* session);
MAFIA_FSR_API const char* __cdecl MafiaFsrLastErrorV1(void* session);
MAFIA_FSR_API void __cdecl MafiaFsrDestroyV1(void* session);

#ifdef __cplusplus
}
#endif
