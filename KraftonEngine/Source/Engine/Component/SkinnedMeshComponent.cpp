#include "SkinnedMeshComponent.h"

DEFINE_CLASS(USkinnedMeshComponent, UPrimitiveComponent)
HIDE_FROM_COMPONENT_LIST(USkinnedMeshComponent)


void USkinnedMeshComponent::UpdateBoneMatrices()
{
    const uint32 BoneCount = static_cast<uint32>(LocalTransforms.size());
    if (BoneCount == 0)
    {
        return;
    }

    if (ComponentSpaceMatrices.size() != BoneCount)
    {
        ComponentSpaceMatrices.resize(BoneCount);
    }

    for (uint32 i = 0; i < BoneCount; ++i)
    {
        FMatrix LocalMatrix = LocalTransforms[i].ToMatrix();
        int32 ParentIndex = GetParentBoneIndex(i);

        if (ParentIndex >= 0 && static_cast<uint32>(ParentIndex) < i)
        {
            // Parent matrices are assumed to be already calculated because ParentIndex < i
            ComponentSpaceMatrices[i] = LocalMatrix * ComponentSpaceMatrices[ParentIndex];
        }
        else
        {
            // Root bone or invalid parent index
            ComponentSpaceMatrices[i] = LocalMatrix;
        }
    }
}
