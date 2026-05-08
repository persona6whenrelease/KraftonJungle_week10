#pragma once

#include "Component/MeshComponent.h"
#include "Math/Transform.h"
#include "Math/Matrix.h"

/**
 * USkinnedMeshComponent - Base class for components that use skinning (SkeletalMesh, etc.)
 * Manages bone transforms and component-space matrices.
 */
class USkinnedMeshComponent : public UMeshComponent
{
public:
    DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

    USkinnedMeshComponent() = default;
    ~USkinnedMeshComponent() override = default;

    /** Updates BoneMatrices based on current pose. Virtual to allow specialized logic in subclasses. */
    virtual void UpdateBoneMatrices();

    const TArray<FMatrix>& GetComponentSpaceMatrices() const { return ComponentSpaceMatrices; }

    /** Asset-provided inverse bind poses. Subclasses must implement this to return asset data. */
    virtual const TArray<FMatrix>& GetInverseBindPoses() const = 0;

    /** Returns the parent index of a bone. Subclasses must implement this. */
    virtual int32 GetParentBoneIndex(int32 BoneIndex) const = 0;

protected:
    /** Relative pose of each bone compared to its parent. Used for blending and IK. */
    TArray<FTransform> LocalTransforms;

    /** Final world (component) space matrices for each bone, used directly for skinning. */
    TArray<FMatrix> ComponentSpaceMatrices;
};
