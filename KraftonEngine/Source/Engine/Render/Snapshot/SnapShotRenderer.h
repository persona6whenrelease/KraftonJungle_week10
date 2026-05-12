#pragma once

#include "Core/CoreTypes.h"
#include "Render/Resource/RenderResources.h"
#include "Render/RenderPass/RenderPassPipeline.h"
#include "Viewport/Viewport.h"

class AActor;
class FD3DDevice;
struct ID3D11ShaderResourceView;

class FSnapShotRenderer
{
public:
	static FSnapShotRenderer& Get();

	bool Initialize(FD3DDevice& InDevice, uint32 InWidth = 256, uint32 InHeight = 256);
	void Release();

	void DrawActor(AActor* Actor);
	ID3D11ShaderResourceView* GetSnapShot();

	void Clear();
	void Resize(uint32 InWidth, uint32 InHeight);

	bool IsInitialized() const { return Device != nullptr; }
	uint32 GetWidth() const { return Width; }
	uint32 GetHeight() const { return Height; }

private:
	FSnapShotRenderer() = default;
	~FSnapShotRenderer() = default;

	FSnapShotRenderer(const FSnapShotRenderer&) = delete;
	FSnapShotRenderer& operator=(const FSnapShotRenderer&) = delete;

private:
	FD3DDevice* Device = nullptr;
	FViewport Viewport;
	FSystemResources Resources;
	FRenderPassPipeline Pipeline;
	TArray<AActor*> PendingActors;
	uint32 Width = 0;
	uint32 Height = 0;
	bool bResourcesCreated = false;
	bool bPipelineInitialized = false;
};
