#pragma once
#include "PrimitiveSceneProxy.h"
#include <memory>
class USkinnedMeshComponent;
class FSkinnedMeshBuffer;

class FSkinnedMesScenehProxy :
    public FPrimitiveSceneProxy
{
public:

	FSkinnedMesScenehProxy(USkinnedMeshComponent* InComponent);

	void UpdateMaterial() override;
	void UpdateMesh() override;	//리소스 구조 변경
	void UpdateDynamicData() override;

	FRenderBufferView GetRenderBufferView() const override;
private:
	USkinnedMeshComponent* GetSkinnedMeshComponent() const;
	std::unique_ptr<FSkinnedMeshBuffer> RenderBuffer;	//DynamicVertexBuffer 소유
	TArray<FVertexPNCTT> SkinnedVertices;
	uint32 CachedVertexCount = 0;
	uint32 CachedIndexCount = 0;

};

