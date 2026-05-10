#pragma once

#include "Component/MeshComponent.h"
#include "Math/Transform.h"
#include "Math/Matrix.h"
#include "Render/Types/RenderTypes.h"

class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	ESkinningMode GetSkinningMode() const { return SkinningMode; }
	void SetSkinningMode(ESkinningMode InMode) { SkinningMode = InMode; }

	const TArray<FMatrix>& GetComponentSpaceMatrices() const { return ComponentSpaceMatrices; }

	// SetSkeletalMesh() 시점에 USkeletalMeshComponent가 호출
	void InitializePoseBuffers(int32 BoneCount);

	// LocalTransforms → ComponentSpaceMatrices 전파 (FK)
	void RecalcComponentSpaceMatrices(const TArray<int32>& ParentIndices);

protected:
	ESkinningMode SkinningMode = ESkinningMode::CPU;

	// 블렌딩/IK 개입 지점 — FBone SRT로 구성한 로컬 트랜스폼 배열
	TArray<FTransform> LocalTransforms;

	// FK 누적 결과 — USkeletalMeshComponent가 읽어서 IBP와 곱함
	TArray<FMatrix> ComponentSpaceMatrices;
};
