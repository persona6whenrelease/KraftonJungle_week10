#pragma once

#include "Component/SkinnedMeshComponent.h"
#include "Mesh/SkeletalMesh.h"
#include "Render/Resource/Buffer.h"

class FSkeletalMeshSceneProxy;

/**
 * USkeletalMeshComponent - Component that renders a SkeletalMesh and performs CPU Skinning.
 */
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

    USkeletalMeshComponent();
    ~USkeletalMeshComponent() override;

    /** Performs CPU Skinning calculation. */
    void UpdateSkinning();

    // UMeshComponent interface
    FMeshBuffer* GetMeshBuffer() const override;
    FMeshDataView GetMeshDataView() const override;
    
    // UPrimitiveComponent interface
    class FPrimitiveSceneProxy* CreateSceneProxy() override;

    void SetSkeletalMesh(USkeletalMesh* InMesh);
    USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

    // USkinnedMeshComponent interface
    const TArray<FMatrix>& GetInverseBindPoses() const override;
    int32 GetParentBoneIndex(int32 BoneIndex) const override;

    // Property Editor 지원
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

protected:
    USkeletalMesh* SkeletalMesh = nullptr;

    /** CPU skinned vertices result. */
    TArray<FSkeletalMeshVertex> SkinnedVertices;

    /** Dynamic buffer to upload skinned vertices to GPU. */
    mutable FDynamicVertexBuffer DynamicVB;
    
    /** Static index buffer from the asset. */
    FIndexBuffer* IndexBuffer = nullptr;
};
