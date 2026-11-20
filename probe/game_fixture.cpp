#include "game_fixture.h"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using Microsoft::WRL::ComPtr;

constexpr auto gameColorFormat = DXGI_FORMAT_R11G11B10_FLOAT;
constexpr auto gameDepthFormat = DXGI_FORMAT_R24G8_TYPELESS;
constexpr auto gameDepthViewFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
constexpr auto gameMotionFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr auto sharedColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr auto sharedDepthFormat = DXGI_FORMAT_R32_FLOAT;
constexpr auto sharedMotionFormat = DXGI_FORMAT_R16G16_FLOAT;

constexpr char shaders[] = R"hlsl(
Texture2D<float4> inputColor : register(t0);
Texture2D<float> inputDepth : register(t1);
Texture2D<float4> inputMotion : register(t2);

cbuffer FixtureParameters : register(b0) {
    float2 resolution;
    float2 offsetInPixels;
    float2 motionInPixels;
    float2 padding;
};

float4 FullscreenVertex(uint vertex : SV_VertexID) : SV_Position {
    float2 coordinate = float2((vertex << 1) & 2, vertex & 2);
    return float4(coordinate * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

struct SceneTargets {
    float4 color : SV_Target0;
    float4 motion : SV_Target1;
    float depth : SV_Depth;
};

SceneTargets DrawFixture(float4 position : SV_Position) {
    float2 scenePosition = position.xy - offsetInPixels;
    bool left = scenePosition.x < resolution.x * 0.5;
    bool top = scenePosition.y < resolution.y * 0.5;

    SceneTargets targets;
    targets.color = left ? float4(4.0, 0.25, 0.125, 1.0) : float4(0.25, 4.0, 0.125, 1.0);
    targets.motion = float4(motionInPixels / resolution, 0.0, 0.0);
    targets.depth = top ? 0.75 : 0.375;
    return targets;
}

struct FsrTargets {
    float4 color : SV_Target0;
    float depth : SV_Target1;
    float2 motion : SV_Target2;
};

FsrTargets ConvertInputs(float4 position : SV_Position) {
    int3 sampleLocation = int3(position.xy, 0);
    uint width, height;
    inputMotion.GetDimensions(width, height);
    float3 color = inputColor.Load(sampleLocation).rgb;
    float2 motion = inputMotion.Load(sampleLocation).xy * float2(width, height);

    FsrTargets targets;
    targets.color = float4(all(isfinite(color)) ? min(color, 64000.0) : float3(0.0, 0.0, 0.0), 1.0);
    targets.depth = inputDepth.Load(sampleLocation);
    targets.motion = all(isfinite(motion)) ? motion : float2(0.0, 0.0);
    return targets;
}

float4 ReturnOutput(float4 position : SV_Position) : SV_Target {
    return float4(inputColor.Load(int3(position.xy, 0)).rgb, 1.0);
}
)hlsl";

struct FixtureParameters {
    float width;
    float height;
    float xOffset;
    float yOffset;
    float xMotion;
    float yMotion;
    float padding[2];
};
static_assert(sizeof(FixtureParameters) % 16 == 0);

void check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex
                << static_cast<std::uint32_t>(result) << ')';
        throw std::runtime_error(message.str());
    }
}

ComPtr<ID3DBlob> compileShader(const char* entryPoint, const char* target) {
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(shaders, sizeof(shaders) - 1, "game_fixture.hlsl", nullptr,
        nullptr, entryPoint, target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &bytecode, &errors);
    if (FAILED(result) && errors) {
        throw std::runtime_error(std::string("Compiling ") + entryPoint + ": "
            + static_cast<const char*>(errors->GetBufferPointer()));
    }
    check(result, "D3DCompile (game-format probe)");
    return bytecode;
}

ComPtr<ID3D11PixelShader> makePixelShader(ID3D11Device* device, const char* entryPoint) {
    auto bytecode = compileShader(entryPoint, "ps_5_0");
    ComPtr<ID3D11PixelShader> shader;
    check(device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
        nullptr, &shader), "CreatePixelShader (game-format probe)");
    return shader;
}

ComPtr<ID3D11Texture2D> makeTexture(ID3D11Device* device, UINT width, UINT height,
    DXGI_FORMAT format, UINT bindFlags) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bindFlags;

    ComPtr<ID3D11Texture2D> texture;
    check(device->CreateTexture2D(&desc, nullptr, &texture), "CreateTexture2D (game-format probe)");
    return texture;
}

ComPtr<ID3D11ShaderResourceView> makeShaderView(ID3D11Device* device, ID3D11Texture2D* texture,
    DXGI_FORMAT format) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels = 1;

    ComPtr<ID3D11ShaderResourceView> view;
    check(device->CreateShaderResourceView(texture, &desc, &view), "CreateShaderResourceView (game-format probe)");
    return view;
}

ComPtr<ID3D11RenderTargetView> makeRenderView(ID3D11Device* device, ID3D11Texture2D* texture,
    DXGI_FORMAT format) {
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    ComPtr<ID3D11RenderTargetView> view;
    check(device->CreateRenderTargetView(texture, &desc, &view), "CreateRenderTargetView (game-format probe)");
    return view;
}

void checkViewSize(ID3D11View* view, UINT width, UINT height, const char* label) {
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    check(resource.As(&texture), label);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.Width != width || desc.Height != height || desc.SampleDesc.Count != 1) {
        throw std::runtime_error(std::string("Unexpected texture dimensions or sample count: ") + label);
    }
}

void checkShaderView(ID3D11ShaderResourceView* view, UINT width, UINT height, DXGI_FORMAT format,
    const char* label) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    if (view != nullptr) {
        view->GetDesc(&desc);
    }
    if (view == nullptr || desc.Format != format || desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D
        || desc.Texture2D.MostDetailedMip != 0) {
        throw std::runtime_error(std::string("Unexpected shader resource view: ") + label);
    }
    checkViewSize(view, width, height, label);
}

void checkRenderView(ID3D11RenderTargetView* view, UINT width, UINT height, DXGI_FORMAT format,
    const char* label) {
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    if (view != nullptr) {
        view->GetDesc(&desc);
    }
    if (view == nullptr || desc.Format != format || desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D
        || desc.Texture2D.MipSlice != 0) {
        throw std::runtime_error(std::string("Unexpected render target view: ") + label);
    }
    checkViewSize(view, width, height, label);
}

class ScopedPipelineState final {
public:
    ScopedPipelineState(ID3D11DeviceContext* context, ID3DDeviceContextState* state) {
        if (context == nullptr || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
            throw std::runtime_error("The game-format probe requires an immediate D3D11 context.");
        }
        check(context->QueryInterface(IID_PPV_ARGS(&context_)), "QueryInterface (ID3D11DeviceContext1)");
        context_->SwapDeviceContextState(state, &previous_);
    }

    ~ScopedPipelineState() {
        context_->ClearState();
        context_->SwapDeviceContextState(previous_.Get(), nullptr);
    }

    ScopedPipelineState(const ScopedPipelineState&) = delete;
    ScopedPipelineState& operator=(const ScopedPipelineState&) = delete;

private:
    ComPtr<ID3D11DeviceContext1> context_;
    ComPtr<ID3DDeviceContextState> previous_;
};

float decodeUnsignedFloat(std::uint32_t bits, int mantissaBits) noexcept {
    const auto exponent = static_cast<int>(bits >> mantissaBits);
    const auto mantissa = bits & ((std::uint32_t{1} << mantissaBits) - 1);
    if (exponent == 31) {
        return mantissa == 0 ? std::numeric_limits<float>::infinity()
                             : std::numeric_limits<float>::quiet_NaN();
    }
    if (exponent == 0) {
        return std::ldexp(static_cast<float>(mantissa), 1 - 15 - mantissaBits);
    }
    return std::ldexp(1.0f + std::ldexp(static_cast<float>(mantissa), -mantissaBits), exponent - 15);
}

} // namespace

struct GameFixture::Impl {
    ComPtr<ID3D11Device> device;
    UINT width;
    UINT height;
    ComPtr<ID3DDeviceContextState> pipelineState;
    ComPtr<ID3D11VertexShader> fullscreenVertex;
    ComPtr<ID3D11PixelShader> fixturePixel;
    ComPtr<ID3D11PixelShader> conversionPixel;
    ComPtr<ID3D11PixelShader> returnPixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11RenderTargetView> sourceColorRtv;
    ComPtr<ID3D11RenderTargetView> sourceMotionRtv;
    ComPtr<ID3D11DepthStencilView> sourceDepthDsv;
    ComPtr<ID3D11ShaderResourceView> sourceColorSrv;
    ComPtr<ID3D11ShaderResourceView> sourceDepthSrv;
    ComPtr<ID3D11ShaderResourceView> sourceMotionSrv;
    ComPtr<ID3D11Texture2D> finalTexture;
    ComPtr<ID3D11RenderTargetView> finalRtv;

    Impl(ID3D11Device* device11, UINT frameWidth, UINT frameHeight)
        : device(device11), width(frameWidth), height(frameHeight) {
        if (device == nullptr || width == 0 || height == 0) {
            throw std::runtime_error("The game-format probe requires a D3D11 device and valid dimensions.");
        }

        ComPtr<ID3D11Device1> device1;
        check(device.As(&device1), "QueryInterface (ID3D11Device1)");
        const auto featureLevel = device->GetFeatureLevel();
        check(device1->CreateDeviceContextState(0, &featureLevel, 1, D3D11_SDK_VERSION,
            __uuidof(ID3D11Device), nullptr, &pipelineState), "CreateDeviceContextState (game-format probe)");

        const auto vertexCode = compileShader("FullscreenVertex", "vs_5_0");
        check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
            nullptr, &fullscreenVertex), "CreateVertexShader (game-format probe)");
        fixturePixel = makePixelShader(device.Get(), "DrawFixture");
        conversionPixel = makePixelShader(device.Get(), "ConvertInputs");
        returnPixel = makePixelShader(device.Get(), "ReturnOutput");

        D3D11_BUFFER_DESC constantsDesc{};
        constantsDesc.ByteWidth = static_cast<UINT>(sizeof(FixtureParameters));
        constantsDesc.Usage = D3D11_USAGE_DEFAULT;
        constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&constantsDesc, nullptr, &constants), "CreateBuffer (fixture constants)");

        D3D11_DEPTH_STENCIL_DESC depthStateDesc{};
        depthStateDesc.DepthEnable = TRUE;
        depthStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthStateDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        depthStateDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
        depthStateDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
        depthStateDesc.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
            D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
        depthStateDesc.BackFace = depthStateDesc.FrontFace;
        check(device->CreateDepthStencilState(&depthStateDesc, &depthState), "CreateDepthStencilState (fixture)");

        const UINT renderAndSample = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        const auto sourceColor = makeTexture(device.Get(), width, height, gameColorFormat, renderAndSample);
        const auto sourceMotion = makeTexture(device.Get(), width, height, gameMotionFormat, renderAndSample);
        const auto sourceDepth = makeTexture(device.Get(), width, height, gameDepthFormat,
            D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE);

        sourceColorRtv = makeRenderView(device.Get(), sourceColor.Get(), gameColorFormat);
        sourceMotionRtv = makeRenderView(device.Get(), sourceMotion.Get(), gameMotionFormat);
        sourceColorSrv = makeShaderView(device.Get(), sourceColor.Get(), gameColorFormat);
        sourceMotionSrv = makeShaderView(device.Get(), sourceMotion.Get(), gameMotionFormat);
        sourceDepthSrv = makeShaderView(device.Get(), sourceDepth.Get(), gameDepthViewFormat);

        D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc{};
        depthViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        check(device->CreateDepthStencilView(sourceDepth.Get(), &depthViewDesc, &sourceDepthDsv),
            "CreateDepthStencilView (fixture)");

        finalTexture = makeTexture(device.Get(), width, height, gameColorFormat, D3D11_BIND_RENDER_TARGET);
        finalRtv = makeRenderView(device.Get(), finalTexture.Get(), gameColorFormat);
    }

    void setFullscreenState(ID3D11DeviceContext* context) const {
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(fullscreenVertex.Get(), nullptr, 0);
    }

    void drawScene(ID3D11DeviceContext* context, std::uint32_t frameIndex) const {
        constexpr float horizontalShift = 3.0f;
        constexpr float verticalShift = -2.0f;
        const bool shifted = (frameIndex & 1) != 0;
        const float offsetSign = shifted ? 1.0f : 0.0f;
        const float motionSign = frameIndex == 0 ? 0.0f : (shifted ? -1.0f : 1.0f);
        FixtureParameters parameters{static_cast<float>(width), static_cast<float>(height),
            horizontalShift * offsetSign, verticalShift * offsetSign,
            horizontalShift * motionSign, verticalShift * motionSign, {}};

        ScopedPipelineState state(context, pipelineState.Get());
        context->UpdateSubresource(constants.Get(), 0, nullptr, &parameters, 0, 0);
        setFullscreenState(context);
        ID3D11Buffer* const parameterBuffer = constants.Get();
        context->PSSetConstantBuffers(0, 1, &parameterBuffer);
        context->PSSetShader(fixturePixel.Get(), nullptr, 0);
        ID3D11RenderTargetView* targets[] = {sourceColorRtv.Get(), sourceMotionRtv.Get()};
        context->OMSetRenderTargets(2, targets, sourceDepthDsv.Get());
        context->OMSetDepthStencilState(depthState.Get(), 0);
        context->Draw(3, 0);
    }
};

GameFixture::GameFixture(ID3D11Device* device, std::uint32_t width, std::uint32_t height)
    : impl_(std::make_unique<Impl>(device, width, height)) {}

GameFixture::~GameFixture() = default;

void GameFixture::renderAndConvert(ID3D11DeviceContext* context, ID3D11Texture2D* sharedColor,
    ID3D11Texture2D* sharedDepth, ID3D11Texture2D* sharedMotion, std::uint32_t frameIndex) {
    if (sharedColor == nullptr || sharedDepth == nullptr || sharedMotion == nullptr) {
        throw std::runtime_error("A game-format conversion target is missing.");
    }

    auto* device = impl_->device.Get();
    const auto color = makeRenderView(device, sharedColor, sharedColorFormat);
    const auto depth = makeRenderView(device, sharedDepth, sharedDepthFormat);
    const auto motion = makeRenderView(device, sharedMotion, sharedMotionFormat);
    impl_->drawScene(context, frameIndex);
    convert(context, impl_->sourceColorSrv.Get(), impl_->sourceDepthSrv.Get(), impl_->sourceMotionSrv.Get(),
        color.Get(), depth.Get(), motion.Get());
}

void GameFixture::convert(ID3D11DeviceContext* context, ID3D11ShaderResourceView* gameColor,
    ID3D11ShaderResourceView* gameDepth, ID3D11ShaderResourceView* gameMotion,
    ID3D11RenderTargetView* sharedColor, ID3D11RenderTargetView* sharedDepth,
    ID3D11RenderTargetView* sharedMotion) {
    const auto width = impl_->width;
    const auto height = impl_->height;
    checkShaderView(gameColor, width, height, gameColorFormat, "game R11G11B10 color");
    checkShaderView(gameDepth, width, height, gameDepthViewFormat, "game D24 depth view");
    checkShaderView(gameMotion, width, height, gameMotionFormat, "game RGBA16F motion");
    checkRenderView(sharedColor, width, height, sharedColorFormat, "shared RGBA16F color");
    checkRenderView(sharedDepth, width, height, sharedDepthFormat, "shared R32F depth");
    checkRenderView(sharedMotion, width, height, sharedMotionFormat, "shared RG16F motion");

    ScopedPipelineState state(context, impl_->pipelineState.Get());
    impl_->setFullscreenState(context);
    ID3D11ShaderResourceView* inputs[] = {gameColor, gameDepth, gameMotion};
    context->PSSetShaderResources(0, 3, inputs);
    context->PSSetShader(impl_->conversionPixel.Get(), nullptr, 0);
    ID3D11RenderTargetView* targets[] = {sharedColor, sharedDepth, sharedMotion};
    context->OMSetRenderTargets(3, targets, nullptr);
    context->Draw(3, 0);
}

void GameFixture::blit(ID3D11DeviceContext* context, ID3D11Texture2D* sharedFsrOutput) {
    if (sharedFsrOutput == nullptr) {
        throw std::runtime_error("The AMD FSR output texture is missing.");
    }
    const auto source = makeShaderView(impl_->device.Get(), sharedFsrOutput, sharedColorFormat);
    blit(context, source.Get(), impl_->finalRtv.Get());
}

void GameFixture::blit(ID3D11DeviceContext* context, ID3D11ShaderResourceView* sharedFsrOutput,
    ID3D11RenderTargetView* gameOutput) {
    checkShaderView(sharedFsrOutput, impl_->width, impl_->height, sharedColorFormat, "FSR RGBA16F output");
    checkRenderView(gameOutput, impl_->width, impl_->height, gameColorFormat, "game R11G11B10 output");

    ScopedPipelineState state(context, impl_->pipelineState.Get());
    impl_->setFullscreenState(context);
    context->PSSetShaderResources(0, 1, &sharedFsrOutput);
    context->PSSetShader(impl_->returnPixel.Get(), nullptr, 0);
    context->OMSetRenderTargets(1, &gameOutput, nullptr);
    context->Draw(3, 0);
}

ID3D11Texture2D* GameFixture::gameOutput() const noexcept {
    return impl_->finalTexture.Get();
}

std::array<float, 3> GameFixture::decodeGameColor(std::uint32_t packed) noexcept {
    return {decodeUnsignedFloat(packed & 0x7ff, 6), decodeUnsignedFloat((packed >> 11) & 0x7ff, 6),
        decodeUnsignedFloat(packed >> 22, 5)};
}
