#include "Engine/UI/Rml/RmlD3D11RenderInterface.h"

#include "Core/CoreTypes.h"
#include "WICTextureLoader.h"

#include "Engine/UI/Rml/RmlUiConfig.h"
#if WITH_RMLUI
#include "Engine/UI/Rml/RmlUiWindowsMacroGuard.h"
#include <RmlUi/Core/Vertex.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace
{
	template <typename T>
	void SafeRelease(T*& Object)
	{
		if (Object)
		{
			Object->Release();
			Object = nullptr;
		}
	}

	struct FRmlGpuVertex
	{
		float Position[2] = { 0.0f, 0.0f };
		uint8 Color[4] = { 255, 255, 255, 255 };
		float TexCoord[2] = { 0.0f, 0.0f };
	};

	struct FRmlVertexConstants
	{
		float Translation[2] = { 0.0f, 0.0f };
		float PresentationOffset[2] = { 0.0f, 0.0f };
		float TargetSize[2] = { 1.0f, 1.0f };
		float Padding[2] = { 0.0f, 0.0f };
	};

	const char* RmlD3D11Shader = R"HLSL(
cbuffer RmlVertexConstants : register(b0)
{
	float2 Translation;
	float2 PresentationOffset;
	float2 TargetSize;
	float2 Padding;
};

struct VSIn
{
	float2 Position : POSITION;
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
};

struct VSOut
{
	float4 Position : SV_Position;
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
};

VSOut VSMain(VSIn Input)
{
	float2 PixelPosition = PresentationOffset + Translation + Input.Position;
	float2 NDC;
	NDC.x = PixelPosition.x / TargetSize.x * 2.0f - 1.0f;
	NDC.y = 1.0f - PixelPosition.y / TargetSize.y * 2.0f;

	VSOut Output;
	Output.Position = float4(NDC, 0.0f, 1.0f);
	Output.Color = Input.Color;
	Output.TexCoord = Input.TexCoord;
	return Output;
}

Texture2D UiTexture : register(t0);
SamplerState UiSampler : register(s0);

float4 PSMain(VSOut Input) : SV_Target
{
	return UiTexture.Sample(UiSampler, Input.TexCoord) * Input.Color;
}
)HLSL";
}

struct FRmlD3D11RenderInterface::FCompiledGeometry
{
	ID3D11Buffer* VertexBuffer = nullptr;
	ID3D11Buffer* IndexBuffer = nullptr;
	UINT IndexCount = 0;
};

struct FRmlD3D11RenderInterface::FTextureResource
{
	ID3D11ShaderResourceView* SRV = nullptr;
	int Width = 0;
	int Height = 0;
};

FRmlD3D11RenderInterface::~FRmlD3D11RenderInterface()
{
	Shutdown();
}

bool FRmlD3D11RenderInterface::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
{
	Shutdown();

	Device = InDevice;
	Context = InContext;
	if (!Device || !Context)
	{
		return false;
	}

	if (!CreateDeviceResources())
	{
		Shutdown();
		return false;
	}

	return true;
}

void FRmlD3D11RenderInterface::Shutdown()
{
	ReleaseDeviceResources();
	Device = nullptr;
	Context = nullptr;
}

void FRmlD3D11RenderInterface::SetPresentationRect(const FViewportPresentationRect& InRect)
{
	PresentationRect = InRect;
}

void FRmlD3D11RenderInterface::SetRenderTargetSize(float InWidth, float InHeight)
{
	RenderTargetWidth = InWidth > 1.0f ? InWidth : 1.0f;
	RenderTargetHeight = InHeight > 1.0f ? InHeight : 1.0f;
}

bool FRmlD3D11RenderInterface::CreateDeviceResources()
{
	ID3DBlob* VSBlob = nullptr;
	ID3DBlob* PSBlob = nullptr;
	ID3DBlob* ErrorBlob = nullptr;

	auto Fail = [&]() -> bool
	{
		SafeRelease(ErrorBlob);
		SafeRelease(VSBlob);
		SafeRelease(PSBlob);
		ReleaseDeviceResources();
		return false;
	};

	HRESULT HR = D3DCompile(
		RmlD3D11Shader,
		std::strlen(RmlD3D11Shader),
		"RmlD3D11VS",
		nullptr,
		nullptr,
		"VSMain",
		"vs_5_0",
		0,
		0,
		&VSBlob,
		&ErrorBlob);
	if (FAILED(HR))
	{
		return Fail();
	}
	SafeRelease(ErrorBlob);

	HR = D3DCompile(
		RmlD3D11Shader,
		std::strlen(RmlD3D11Shader),
		"RmlD3D11PS",
		nullptr,
		nullptr,
		"PSMain",
		"ps_5_0",
		0,
		0,
		&PSBlob,
		&ErrorBlob);
	if (FAILED(HR))
	{
		return Fail();
	}
	SafeRelease(ErrorBlob);

	HR = Device->CreateVertexShader(VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), nullptr, &VertexShader);
	if (FAILED(HR))
	{
		return Fail();
	}

	HR = Device->CreatePixelShader(PSBlob->GetBufferPointer(), PSBlob->GetBufferSize(), nullptr, &PixelShader);
	if (FAILED(HR))
	{
		return Fail();
	}

	D3D11_INPUT_ELEMENT_DESC Layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(FRmlGpuVertex, Position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(FRmlGpuVertex, Color), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(FRmlGpuVertex, TexCoord), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	HR = Device->CreateInputLayout(Layout, 3, VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), &InputLayout);
	SafeRelease(VSBlob);
	SafeRelease(PSBlob);
	if (FAILED(HR))
	{
		return Fail();
	}

	D3D11_BUFFER_DESC ConstantBufferDesc = {};
	ConstantBufferDesc.ByteWidth = sizeof(FRmlVertexConstants);
	ConstantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	ConstantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	ConstantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	HR = Device->CreateBuffer(&ConstantBufferDesc, nullptr, &ConstantBuffer);
	if (FAILED(HR))
	{
		return Fail();
	}

	D3D11_SAMPLER_DESC SamplerDesc = {};
	SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	SamplerDesc.MinLOD = 0.0f;
	SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	HR = Device->CreateSamplerState(&SamplerDesc, &SamplerState);
	if (FAILED(HR))
	{
		return Fail();
	}

	D3D11_BLEND_DESC BlendDesc = {};
	BlendDesc.RenderTarget[0].BlendEnable = TRUE;
	BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	HR = Device->CreateBlendState(&BlendDesc, &BlendState);
	if (FAILED(HR))
	{
		return Fail();
	}

	D3D11_RASTERIZER_DESC RasterizerDesc = {};
	RasterizerDesc.FillMode = D3D11_FILL_SOLID;
	RasterizerDesc.CullMode = D3D11_CULL_NONE;
	RasterizerDesc.ScissorEnable = TRUE;
	RasterizerDesc.DepthClipEnable = TRUE;
	HR = Device->CreateRasterizerState(&RasterizerDesc, &RasterizerState);
	if (FAILED(HR))
	{
		return Fail();
	}

	D3D11_DEPTH_STENCIL_DESC DepthStencilDesc = {};
	DepthStencilDesc.DepthEnable = FALSE;
	DepthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	DepthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	HR = Device->CreateDepthStencilState(&DepthStencilDesc, &DepthStencilState);
	if (FAILED(HR))
	{
		return Fail();
	}

	const uint8 WhitePixel[4] = { 255, 255, 255, 255 };
	Rml::TextureHandle WhiteHandle = CreateTextureFromRGBA(WhitePixel, 1, 1, false);
	FTextureResource* WhiteResource = reinterpret_cast<FTextureResource*>(WhiteHandle);
	if (!WhiteResource || !WhiteResource->SRV)
	{
		return Fail();
	}
	WhiteTextureSRV = WhiteResource->SRV;
	WhiteTextureSRV->AddRef();
	ReleaseTexture(WhiteHandle);

	return true;
}

void FRmlD3D11RenderInterface::ReleaseDeviceResources()
{
	SafeRelease(WhiteTextureSRV);
	SafeRelease(DepthStencilState);
	SafeRelease(RasterizerState);
	SafeRelease(BlendState);
	SafeRelease(SamplerState);
	SafeRelease(ConstantBuffer);
	SafeRelease(InputLayout);
	SafeRelease(PixelShader);
	SafeRelease(VertexShader);
}

Rml::CompiledGeometryHandle FRmlD3D11RenderInterface::CompileGeometry(
	Rml::Span<const Rml::Vertex> Vertices,
	Rml::Span<const int> Indices)
{
	if (!Device || Vertices.size() == 0 || Indices.size() == 0)
	{
		return 0;
	}

	std::vector<FRmlGpuVertex> GpuVertices;
	GpuVertices.reserve(Vertices.size());
	for (const Rml::Vertex& Vertex : Vertices)
	{
		FRmlGpuVertex GpuVertex;
		GpuVertex.Position[0] = Vertex.position.x;
		GpuVertex.Position[1] = Vertex.position.y;
		GpuVertex.Color[0] = Vertex.colour.red;
		GpuVertex.Color[1] = Vertex.colour.green;
		GpuVertex.Color[2] = Vertex.colour.blue;
		GpuVertex.Color[3] = Vertex.colour.alpha;
		GpuVertex.TexCoord[0] = Vertex.tex_coord.x;
		GpuVertex.TexCoord[1] = Vertex.tex_coord.y;
		GpuVertices.push_back(GpuVertex);
	}

	auto* Geometry = new FCompiledGeometry();
	Geometry->IndexCount = static_cast<UINT>(Indices.size());

	D3D11_BUFFER_DESC VertexBufferDesc = {};
	VertexBufferDesc.ByteWidth = static_cast<UINT>(GpuVertices.size() * sizeof(FRmlGpuVertex));
	VertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	VertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA VertexData = {};
	VertexData.pSysMem = GpuVertices.data();
	HRESULT HR = Device->CreateBuffer(&VertexBufferDesc, &VertexData, &Geometry->VertexBuffer);
	if (FAILED(HR))
	{
		ReleaseGeometry(reinterpret_cast<Rml::CompiledGeometryHandle>(Geometry));
		return 0;
	}

	D3D11_BUFFER_DESC IndexBufferDesc = {};
	IndexBufferDesc.ByteWidth = static_cast<UINT>(Indices.size() * sizeof(int));
	IndexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	IndexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA IndexData = {};
	IndexData.pSysMem = Indices.data();
	HR = Device->CreateBuffer(&IndexBufferDesc, &IndexData, &Geometry->IndexBuffer);
	if (FAILED(HR))
	{
		ReleaseGeometry(reinterpret_cast<Rml::CompiledGeometryHandle>(Geometry));
		return 0;
	}

	return reinterpret_cast<Rml::CompiledGeometryHandle>(Geometry);
}

void FRmlD3D11RenderInterface::RenderGeometry(
	Rml::CompiledGeometryHandle GeometryHandle,
	Rml::Vector2f Translation,
	Rml::TextureHandle TextureHandle)
{
	FCompiledGeometry* Geometry = reinterpret_cast<FCompiledGeometry*>(GeometryHandle);
	if (!Context || !Geometry || !Geometry->VertexBuffer || !Geometry->IndexBuffer || !ConstantBuffer)
	{
		return;
	}

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (FAILED(Context->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return;
	}

	FRmlVertexConstants* Constants = reinterpret_cast<FRmlVertexConstants*>(Mapped.pData);
	Constants->Translation[0] = Translation.x;
	Constants->Translation[1] = Translation.y;
	Constants->PresentationOffset[0] = PresentationRect.X;
	Constants->PresentationOffset[1] = PresentationRect.Y;
	Constants->TargetSize[0] = RenderTargetWidth;
	Constants->TargetSize[1] = RenderTargetHeight;
	Context->Unmap(ConstantBuffer, 0);

	ApplyRenderState();
	ApplyScissor();

	UINT Stride = sizeof(FRmlGpuVertex);
	UINT Offset = 0;
	Context->IASetVertexBuffers(0, 1, &Geometry->VertexBuffer, &Stride, &Offset);
	Context->IASetIndexBuffer(Geometry->IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Context->IASetInputLayout(InputLayout);

	ID3D11ShaderResourceView* TextureSRV = WhiteTextureSRV;
	if (TextureHandle)
	{
		FTextureResource* Texture = reinterpret_cast<FTextureResource*>(TextureHandle);
		if (Texture && Texture->SRV)
		{
			TextureSRV = Texture->SRV;
		}
	}
	Context->PSSetShaderResources(0, 1, &TextureSRV);
	Context->PSSetSamplers(0, 1, &SamplerState);
	Context->DrawIndexed(Geometry->IndexCount, 0, 0);

	ID3D11ShaderResourceView* NullSRV = nullptr;
	Context->PSSetShaderResources(0, 1, &NullSRV);
}

void FRmlD3D11RenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle GeometryHandle)
{
	FCompiledGeometry* Geometry = reinterpret_cast<FCompiledGeometry*>(GeometryHandle);
	if (!Geometry)
	{
		return;
	}

	SafeRelease(Geometry->IndexBuffer);
	SafeRelease(Geometry->VertexBuffer);
	delete Geometry;
}

Rml::TextureHandle FRmlD3D11RenderInterface::LoadTexture(Rml::Vector2i& TextureDimensions, const Rml::String& Source)
{
	if (!Device || Source.empty())
	{
		return 0;
	}

	std::wstring WidePath(Source.begin(), Source.end());

	ID3D11Resource* Resource = nullptr;
	ID3D11ShaderResourceView* SRV = nullptr;
	HRESULT HR = DirectX::CreateWICTextureFromFileEx(
		Device,
		WidePath.c_str(),
		0,
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_SHADER_RESOURCE,
		0,
		0,
		DirectX::WIC_LOADER_IGNORE_SRGB,
		&Resource,
		&SRV);

	if (FAILED(HR) || !SRV)
	{
		SafeRelease(Resource);
		return 0;
	}

	ID3D11Texture2D* Texture2D = nullptr;
	if (SUCCEEDED(Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture2D))))
	{
		D3D11_TEXTURE2D_DESC Desc = {};
		Texture2D->GetDesc(&Desc);
		TextureDimensions.x = static_cast<int>(Desc.Width);
		TextureDimensions.y = static_cast<int>(Desc.Height);
		SafeRelease(Texture2D);
	}
	SafeRelease(Resource);

	auto* Texture = new FTextureResource();
	Texture->SRV = SRV;
	Texture->Width = TextureDimensions.x;
	Texture->Height = TextureDimensions.y;
	return reinterpret_cast<Rml::TextureHandle>(Texture);
}

Rml::TextureHandle FRmlD3D11RenderInterface::GenerateTexture(
	Rml::Span<const Rml::byte> Source,
	Rml::Vector2i SourceDimensions)
{
	if (!Source.data() || SourceDimensions.x <= 0 || SourceDimensions.y <= 0)
	{
		return 0;
	}

	return CreateTextureFromRGBA(Source.data(), SourceDimensions.x, SourceDimensions.y, false);
}

Rml::TextureHandle FRmlD3D11RenderInterface::CreateTextureFromRGBA(
	const uint8* Pixels,
	int Width,
	int Height,
	bool bSourceOriginBottomLeft)
{
	if (!Device || !Pixels || Width <= 0 || Height <= 0)
	{
		return 0;
	}

	const size_t RowBytes = static_cast<size_t>(Width) * 4;
	std::vector<uint8> UploadPixels(static_cast<size_t>(Height) * RowBytes);
	if (bSourceOriginBottomLeft)
	{
		for (int Y = 0; Y < Height; ++Y)
		{
			const uint8* SrcRow = Pixels + static_cast<size_t>(Height - 1 - Y) * RowBytes;
			uint8* DstRow = UploadPixels.data() + static_cast<size_t>(Y) * RowBytes;
			std::memcpy(DstRow, SrcRow, RowBytes);
		}
	}
	else
	{
		std::memcpy(UploadPixels.data(), Pixels, UploadPixels.size());
	}

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = static_cast<UINT>(Width);
	TextureDesc.Height = static_cast<UINT>(Height);
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_IMMUTABLE;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA InitData = {};
	InitData.pSysMem = UploadPixels.data();
	InitData.SysMemPitch = static_cast<UINT>(RowBytes);

	ID3D11Texture2D* Texture2D = nullptr;
	HRESULT HR = Device->CreateTexture2D(&TextureDesc, &InitData, &Texture2D);
	if (FAILED(HR) || !Texture2D)
	{
		return 0;
	}

	ID3D11ShaderResourceView* SRV = nullptr;
	HR = Device->CreateShaderResourceView(Texture2D, nullptr, &SRV);
	SafeRelease(Texture2D);
	if (FAILED(HR) || !SRV)
	{
		return 0;
	}

	auto* Texture = new FTextureResource();
	Texture->SRV = SRV;
	Texture->Width = Width;
	Texture->Height = Height;
	return reinterpret_cast<Rml::TextureHandle>(Texture);
}

void FRmlD3D11RenderInterface::ReleaseTexture(Rml::TextureHandle TextureHandle)
{
	FTextureResource* Texture = reinterpret_cast<FTextureResource*>(TextureHandle);
	if (!Texture)
	{
		return;
	}

	SafeRelease(Texture->SRV);
	delete Texture;
}

void FRmlD3D11RenderInterface::EnableScissorRegion(bool bEnable)
{
	bScissorEnabled = bEnable;
	ApplyScissor();
}

void FRmlD3D11RenderInterface::SetScissorRegion(Rml::Rectanglei Region)
{
	ScissorRegion = Region;
	ApplyScissor();
}

void FRmlD3D11RenderInterface::ApplyRenderState()
{
	D3D11_VIEWPORT D3DViewport = {};
	D3DViewport.TopLeftX = 0.0f;
	D3DViewport.TopLeftY = 0.0f;
	D3DViewport.Width = RenderTargetWidth;
	D3DViewport.Height = RenderTargetHeight;
	D3DViewport.MinDepth = 0.0f;
	D3DViewport.MaxDepth = 1.0f;
	Context->RSSetViewports(1, &D3DViewport);

	const float BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	Context->OMSetBlendState(BlendState, BlendFactor, 0xffffffff);
	Context->OMSetDepthStencilState(DepthStencilState, 0);
	Context->RSSetState(RasterizerState);
	Context->VSSetShader(VertexShader, nullptr, 0);
	Context->VSSetConstantBuffers(0, 1, &ConstantBuffer);
	Context->PSSetShader(PixelShader, nullptr, 0);
}

void FRmlD3D11RenderInterface::ApplyScissor()
{
	if (!Context || !PresentationRect.IsValid())
	{
		return;
	}

	D3D11_RECT Rect = {};
	if (bScissorEnabled)
	{
		Rect.left = static_cast<LONG>(PresentationRect.X + ScissorRegion.Left());
		Rect.top = static_cast<LONG>(PresentationRect.Y + ScissorRegion.Top());
		Rect.right = static_cast<LONG>(PresentationRect.X + ScissorRegion.Right());
		Rect.bottom = static_cast<LONG>(PresentationRect.Y + ScissorRegion.Bottom());
	}
	else
	{
		Rect.left = static_cast<LONG>(PresentationRect.X);
		Rect.top = static_cast<LONG>(PresentationRect.Y);
		Rect.right = static_cast<LONG>(PresentationRect.Right());
		Rect.bottom = static_cast<LONG>(PresentationRect.Bottom());
	}

	Rect.left = std::max<LONG>(0, Rect.left);
	Rect.top = std::max<LONG>(0, Rect.top);
	Rect.right = std::max<LONG>(Rect.left, Rect.right);
	Rect.bottom = std::max<LONG>(Rect.top, Rect.bottom);
	Context->RSSetScissorRects(1, &Rect);
}

#endif
