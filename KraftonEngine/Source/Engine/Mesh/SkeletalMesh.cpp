#include "SkeletalMesh.h"

const TArray<FMatrix>& USkeletalMesh::GetInverseBindPoses() const
{
    EnsureCachedData();
    return CachedInverseBindPoses;
}

int32 USkeletalMesh::GetParentBoneIndex(int32 BoneIndex) const
{
    if (SkeletalMeshAsset && BoneIndex >= 0 && BoneIndex < (int32)SkeletalMeshAsset->Bones.size())
    {
        return SkeletalMeshAsset->Bones[BoneIndex].ParentIndex;
    }
    return -1;
}

void USkeletalMesh::EnsureCachedData() const
{
    if (bCachedDataValid) return;
    if (!SkeletalMeshAsset) return;

    CachedInverseBindPoses.clear();
    for (const auto& Bone : SkeletalMeshAsset->Bones)
    {
        CachedInverseBindPoses.push_back(Bone.InverseBindPose);
    }
    bCachedDataValid = true;
}
