#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"

class USkeletalMeshComponent;

// ============================================================
// FSkeletalSceneProxy — USkeletalMeshComponent 전용 프록시
// ============================================================
// CPU Skinning 모드: 컴포넌트의 DynamicVB(변형된 정점) + 에셋의 정적 IB를 사용.
// GPU Skinning 모드: 에셋의 정적 VB/IB를 사용 (파이프라인 확장 시 완성 예정).
// SectionDraws는 StaticMeshSceneProxy와 동일한 방식으로 구축 및 재사용.
class FSkeletalSceneProxy : public FPrimitiveSceneProxy
{
public:
	static constexpr uint32 MAX_LOD = 4;

	FSkeletalSceneProxy(USkeletalMeshComponent* InComponent);

	// CPU/GPU 모드에 따라 다른 버퍼를 반환
	FGPUGeometryView GetGeometryView() const override;

	void UpdateMaterial() override;
	void UpdateMesh() override;
	void UpdateLOD(uint32 LODLevel) override;

private:
	USkeletalMeshComponent* GetSkeletalMeshComponent() const;
	void RebuildSectionDraws();

	struct FLODDrawData
	{
		FMeshBuffer* MeshBuffer = nullptr;
		TArray<FMeshSectionDraw> SectionDraws;
	};

	FLODDrawData LODData[MAX_LOD];
	uint32 LODCount = 1;
};
