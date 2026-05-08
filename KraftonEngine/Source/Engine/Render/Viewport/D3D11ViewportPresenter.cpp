#include "Engine/Render/Viewport/D3D11ViewportPresenter.h"

#include "Viewport/Viewport.h"

#include <d3dcompiler.h>

#include <cstring>

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

	const char* ViewportPresenterShader = R"HLSL(
cbuffer ViewportPresentConstants : register(b0)
{
	float4 Rect;
	float2 TargetSize;
	float2 Padding;
};

struct VSOut
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD0;
};

VSOut VSMain(uint VertexId : SV_VertexID)
{
	float2 UV;
	UV.x = (VertexId == 1 || VertexId == 3) ? 1.0f : 0.0f;
	UV.y = (VertexId >= 2) ? 1.0f : 0.0f;

	float2 PixelPosition = Rect.xy + UV * Rect.zw;
	float2 NDC;
	NDC.x = PixelPosition.x / TargetSize.x * 2.0f - 1.0f;
	NDC.y = 1.0f - PixelPosition.y / TargetSize.y * 2.0f;

	VSOut Output;
	Output.Position = float4(NDC, 0.0f, 1.0f);
	Output.UV = UV;
	return Output;
}

Texture2D ViewportTexture : register(t0);
SamplerState ViewportSampler : register(s0);

float4 PSMain(VSOut Input) : SV_Target
{
	return ViewportTexture.Sample(ViewportSampler, Input.UV);
}
)HLSL";
}

bool FD3D11ViewportPresenter::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
{
	Release();

	Device = InDevice;
	Context = InContext;
	if (!Device || !Context)
	{
		return false;
	}

	ID3DBlob* VSBlob = nullptr;
	ID3DBlob* PSBlob = nullptr;
	ID3DBlob* ErrorBlob = nullptr;

	auto FailInitialize = [&]() -> bool
	{
		SafeRelease(ErrorBlob);
		SafeRelease(VSBlob);
		SafeRelease(PSBlob);
		Release();
		return false;
	};

	HRESULT HR = D3DCompile(
		ViewportPresenterShader,
		std::strlen(ViewportPresenterShader),
		"D3D11ViewportPresenterVS",
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
		return FailInitialize();
	}
	SafeRelease(ErrorBlob);

	HR = D3DCompile(
		ViewportPresenterShader,
		std::strlen(ViewportPresenterShader),
		"D3D11ViewportPresenterPS",
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
		return FailInitialize();
	}
	SafeRelease(ErrorBlob);

	HR = Device->CreateVertexShader(VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), nullptr, &VertexShader);
	if (FAILED(HR))
	{
		return FailInitialize();
	}

	HR = Device->CreatePixelShader(PSBlob->GetBufferPointer(), PSBlob->GetBufferSize(), nullptr, &PixelShader);
	SafeRelease(VSBlob);
	SafeRelease(PSBlob);
	if (FAILED(HR))
	{
		return FailInitialize();
	}

	D3D11_BUFFER_DESC ConstantBufferDesc = {};
	ConstantBufferDesc.ByteWidth = sizeof(FViewportPresentConstants);
	ConstantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	ConstantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	ConstantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	HR = Device->CreateBuffer(&ConstantBufferDesc, nullptr, &ConstantBuffer);
	if (FAILED(HR))
	{
		return FailInitialize();
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
		return FailInitialize();
	}

	D3D11_BLEND_DESC BlendDesc = {};
	BlendDesc.RenderTarget[0].BlendEnable = FALSE;
	BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	HR = Device->CreateBlendState(&BlendDesc, &OpaqueBlendState);
	if (FAILED(HR))
	{
		return FailInitialize();
	}

	D3D11_RASTERIZER_DESC RasterizerDesc = {};
	RasterizerDesc.FillMode = D3D11_FILL_SOLID;
	RasterizerDesc.CullMode = D3D11_CULL_NONE;
	RasterizerDesc.DepthClipEnable = TRUE;
	RasterizerDesc.ScissorEnable = TRUE;
	HR = Device->CreateRasterizerState(&RasterizerDesc, &RasterizerState);
	if (FAILED(HR))
	{
		return FailInitialize();
	}

	D3D11_DEPTH_STENCIL_DESC DepthStencilDesc = {};
	DepthStencilDesc.DepthEnable = FALSE;
	DepthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	DepthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	HR = Device->CreateDepthStencilState(&DepthStencilDesc, &DepthStencilState);
	if (FAILED(HR))
	{
		return FailInitialize();
	}

	return true;
}

void FD3D11ViewportPresenter::Release()
{
	SafeRelease(DepthStencilState);
	SafeRelease(RasterizerState);
	SafeRelease(OpaqueBlendState);
	SafeRelease(SamplerState);
	SafeRelease(ConstantBuffer);
	SafeRelease(PixelShader);
	SafeRelease(VertexShader);
	Device = nullptr;
	Context = nullptr;
}

void FD3D11ViewportPresenter::SetRenderTargetSize(float InWidth, float InHeight)
{
	RenderTargetWidth = InWidth > 1.0f ? InWidth : 1.0f;
	RenderTargetHeight = InHeight > 1.0f ? InHeight : 1.0f;
}

void FD3D11ViewportPresenter::DrawViewport(FViewport* Viewport, const FViewportPresentationRect& Rect)
{
	if (!Context || !VertexShader || !PixelShader || !ConstantBuffer || !SamplerState || !Viewport || !Viewport->GetSRV() || !Rect.IsValid())
	{
		return;
	}

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (FAILED(Context->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return;
	}

	FViewportPresentConstants* Constants = reinterpret_cast<FViewportPresentConstants*>(Mapped.pData);
	Constants->Rect[0] = Rect.X;
	Constants->Rect[1] = Rect.Y;
	Constants->Rect[2] = Rect.Width;
	Constants->Rect[3] = Rect.Height;
	Constants->TargetSize[0] = RenderTargetWidth;
	Constants->TargetSize[1] = RenderTargetHeight;
	Context->Unmap(ConstantBuffer, 0);

	D3D11_VIEWPORT D3DViewport = {};
	D3DViewport.TopLeftX = 0.0f;
	D3DViewport.TopLeftY = 0.0f;
	D3DViewport.Width = RenderTargetWidth;
	D3DViewport.Height = RenderTargetHeight;
	D3DViewport.MinDepth = 0.0f;
	D3DViewport.MaxDepth = 1.0f;
	Context->RSSetViewports(1, &D3DViewport);

	D3D11_RECT ScissorRect = {};
	ScissorRect.left = static_cast<LONG>(Rect.X);
	ScissorRect.top = static_cast<LONG>(Rect.Y);
	ScissorRect.right = static_cast<LONG>(Rect.Right());
	ScissorRect.bottom = static_cast<LONG>(Rect.Bottom());
	Context->RSSetScissorRects(1, &ScissorRect);

	const float BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	Context->OMSetBlendState(OpaqueBlendState, BlendFactor, 0xffffffff);
	Context->OMSetDepthStencilState(DepthStencilState, 0);
	Context->RSSetState(RasterizerState);

	Context->IASetInputLayout(nullptr);
	Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	Context->VSSetShader(VertexShader, nullptr, 0);
	Context->VSSetConstantBuffers(0, 1, &ConstantBuffer);
	Context->PSSetShader(PixelShader, nullptr, 0);
	ID3D11ShaderResourceView* ViewportSRV = Viewport->GetSRV();
	Context->PSSetShaderResources(0, 1, &ViewportSRV);
	Context->PSSetSamplers(0, 1, &SamplerState);
	Context->Draw(4, 0);

	ID3D11ShaderResourceView* NullSRV = nullptr;
	Context->PSSetShaderResources(0, 1, &NullSRV);
}
