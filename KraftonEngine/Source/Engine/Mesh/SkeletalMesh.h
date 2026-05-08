#pragma once

#include "Object/Object.h"
#include "Mesh/SkeletalMeshAsset.h"

class USkeletalMesh : public UObject
{
public:
    DECLARE_CLASS(USkeletalMesh, UObject)

    USkeletalMesh() = default;
    ~USkeletalMesh() override = default;

    void SetSkeletalMeshAsset(FSkeletalMesh* InMesh) { SkeletalMeshAsset = InMesh; }
    FSkeletalMesh* GetSkeletalMeshAsset() const { return SkeletalMeshAsset; }

    const TArray<FMatrix>& GetInverseBindPoses() const;
    int32 GetParentBoneIndex(int32 BoneIndex) const;

private:
    FSkeletalMesh* SkeletalMeshAsset = nullptr;
    
    // Cached for quick access
    mutable TArray<FMatrix> CachedInverseBindPoses;
    mutable bool bCachedDataValid = false;
    
    void EnsureCachedData() const;
};
