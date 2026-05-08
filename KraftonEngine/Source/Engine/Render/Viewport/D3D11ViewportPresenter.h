#pragma once

#include "Viewport/ViewportPresentationTypes.h"

#include <d3d11.h>

class FViewport;

// ImGui를 거치지 않고 FViewport의 SRV를 현재 바인딩된 D3D11 render target에 직접 출력한다.
// 클라이언트 최종 경로는 이 presenter를 쓰고, ImGui는 디버그/툴 UI만 담당하게 만드는 것이 목표다.
class FD3D11ViewportPresenter
{
public:
	FD3D11ViewportPresenter() = default;
	~FD3D11ViewportPresenter() = default;

	bool Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext);
	void Release();

	void SetRenderTargetSize(float InWidth, float InHeight);
	void DrawViewport(FViewport* Viewport, const FViewportPresentationRect& Rect);

private:
	struct FViewportPresentConstants
	{
		float Rect[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		float TargetSize[2] = { 1.0f, 1.0f };
		float Padding[2] = { 0.0f, 0.0f };
	};

	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* Context = nullptr;
	ID3D11VertexShader* VertexShader = nullptr;
	ID3D11PixelShader* PixelShader = nullptr;
	ID3D11Buffer* ConstantBuffer = nullptr;
	ID3D11SamplerState* SamplerState = nullptr;
	ID3D11BlendState* OpaqueBlendState = nullptr;
	ID3D11RasterizerState* RasterizerState = nullptr;
	ID3D11DepthStencilState* DepthStencilState = nullptr;
	float RenderTargetWidth = 1.0f;
	float RenderTargetHeight = 1.0f;
};
