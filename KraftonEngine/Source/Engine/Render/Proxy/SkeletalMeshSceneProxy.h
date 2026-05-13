#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"

class USkinnedMeshComponent;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	FSkeletalMeshSceneProxy(USkinnedMeshComponent* InComponent);

	void UpdateMaterial() override;
	void UpdateMesh() override;
	void UpdatePerViewport(const FFrameContext& Frame) override;

private:
	USkinnedMeshComponent* GetSkinnedMeshComponent() const;
	void RebuildSectionDraws();
};
