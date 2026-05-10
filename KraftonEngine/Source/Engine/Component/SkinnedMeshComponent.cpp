#include "Component/SkinnedMeshComponent.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(USkinnedMeshComponent, UMeshComponent)

void USkinnedMeshComponent::InitializePoseBuffers(int32 BoneCount)
{
	LocalTransforms.assign(BoneCount, FTransform{});
	ComponentSpaceMatrices.assign(BoneCount, FMatrix::Identity);
}

void USkinnedMeshComponent::RecalcComponentSpaceMatrices(const TArray<int32>& ParentIndices)
{
	// Bones는 부모 인덱스가 항상 자신보다 작음(FBX 보장) → 순서대로 처리
	for (int32 i = 0; i < (int32)LocalTransforms.size(); ++i)
	{
		FMatrix Local = LocalTransforms[i].ToMatrix();
		int32 Parent = ParentIndices[i];
		if (Parent < 0)
			ComponentSpaceMatrices[i] = Local;
		else
			ComponentSpaceMatrices[i] = ComponentSpaceMatrices[Parent] * Local;
	}
}
