#pragma once
#include "PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"

class USkeletalMeshComponent;
struct FFrameContext;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent);
	~FSkeletalMeshSceneProxy() override;

	void UpdateMesh() override;
	void UpdateMaterial() override;
	void UpdatePerViewport(const FFrameContext& Frame) override;

private:
	USkeletalMeshComponent* GetSkeletalMeshComponent() const;

	void UpdateDynamicGeometry();

	FConstantBuffer DefaultMaterialCB;
};
