#pragma once

#include "Core/CoreTypes.h"
#include "Viewport/ViewportPresentationTypes.h"

#include <d3d11.h>

#include "Engine/UI/Rml/RmlUiConfig.h"
#if WITH_RMLUI
#include "Engine/UI/Rml/RmlUiWindowsMacroGuard.h"
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>
#endif

#if WITH_RMLUI

class FRmlD3D11RenderInterface final : public Rml::RenderInterface
{
public:
	FRmlD3D11RenderInterface() = default;
	~FRmlD3D11RenderInterface() override;

	bool Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext);
	void Shutdown();

	void SetPresentationRect(const FViewportPresentationRect& InRect);
	void SetRenderTargetSize(float InWidth, float InHeight);

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> Vertices, Rml::Span<const int> Indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle Geometry, Rml::Vector2f Translation, Rml::TextureHandle Texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle Geometry) override;

	Rml::TextureHandle LoadTexture(Rml::Vector2i& TextureDimensions, const Rml::String& Source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> Source, Rml::Vector2i SourceDimensions) override;
	void ReleaseTexture(Rml::TextureHandle Texture) override;

	void EnableScissorRegion(bool bEnable) override;
	void SetScissorRegion(Rml::Rectanglei Region) override;

private:
	struct FCompiledGeometry;
	struct FTextureResource;
	struct FVertexConstants;

	bool CreateDeviceResources();
	void ReleaseDeviceResources();
	void ApplyRenderState();
	void ApplyScissor();
	Rml::TextureHandle CreateTextureFromRGBA(const uint8* Pixels, int Width, int Height, bool bSourceOriginBottomLeft);

private:
	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* Context = nullptr;

	ID3D11VertexShader* VertexShader = nullptr;
	ID3D11PixelShader* PixelShader = nullptr;
	ID3D11InputLayout* InputLayout = nullptr;
	ID3D11Buffer* ConstantBuffer = nullptr;
	ID3D11SamplerState* SamplerState = nullptr;
	ID3D11BlendState* BlendState = nullptr;
	ID3D11RasterizerState* RasterizerState = nullptr;
	ID3D11DepthStencilState* DepthStencilState = nullptr;
	ID3D11ShaderResourceView* WhiteTextureSRV = nullptr;

	FViewportPresentationRect PresentationRect;
	float RenderTargetWidth = 1.0f;
	float RenderTargetHeight = 1.0f;
	bool bScissorEnabled = false;
	Rml::Rectanglei ScissorRegion;
};

#else

class FRmlD3D11RenderInterface
{
public:
	bool Initialize(ID3D11Device*, ID3D11DeviceContext*) { return false; }
	void Shutdown() {}
	void SetPresentationRect(const FViewportPresentationRect&) {}
	void SetRenderTargetSize(float, float) {}
};

#endif
