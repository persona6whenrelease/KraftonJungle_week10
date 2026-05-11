#pragma once

#include "Viewport/ViewportClient.h"
#include "Render/Types/ViewTypes.h"

class UCameraComponent;
struct FSkeletalMesh;

class FSkeletalMeshViewerViewportClient : public FViewportClient
{
public:
	FSkeletalMeshViewerViewportClient() = default;
	~FSkeletalMeshViewerViewportClient() override = default;

	void Initialize();
	void Shutdown();

	void Resize(uint32 Width, uint32 Height);
	void FrameMesh(const FSkeletalMesh* MeshAsset);

	UCameraComponent* GetCamera() const { return Camera; }

	FViewportRenderOptions& GetRenderOptions() { return RenderOptions; }
	const FViewportRenderOptions& GetRenderOptions() const { return RenderOptions; }

private:
	UCameraComponent* Camera = nullptr;
	FViewportRenderOptions RenderOptions;
};
