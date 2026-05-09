#include "SkeletalMeshComponent.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Core/Log.h"


IMPLEMENT_CLASS(USkeletalMeshComponent, UPrimitiveComponent)


USkeletalMeshComponent::USkeletalMeshComponent()
{
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    DynamicVB.Release();
}

void USkeletalMeshComponent::UpdateSkinning()
{
    if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
    {
        return;
    }

    FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
    const TArray<FMatrix>& IBPs = GetInverseBindPoses();
    const uint32 BoneCount = static_cast<uint32>(ComponentSpaceMatrices.size());
    const uint32 IBPCount = static_cast<uint32>(IBPs.size());

    if (BoneCount == 0 || IBPCount == 0 || BoneCount != IBPCount)
    {
        return;
    }

    // 1. Pre-calculate Skinning Matrices (In practice, reuse a buffer)
    static TArray<FMatrix> SkinningMatrices;
    if (SkinningMatrices.size() != BoneCount)
    {
        SkinningMatrices.resize(BoneCount);
    }

    for (uint32 i = 0; i < BoneCount; ++i)
    {
        // skinningMatrix = InverseBindPose * ComponentSpaceMatrix
        SkinningMatrices[i] = IBPs[i] * ComponentSpaceMatrices[i];
    }

    // 2. Perform CPU Skinning
    const uint32 VertexCount = static_cast<uint32>(Asset->Vertices.size());
    if (SkinnedVertices.size() != VertexCount)
    {
        SkinnedVertices.resize(VertexCount);
    }

    for (uint32 i = 0; i < VertexCount; ++i)
    {
        const FSkeletalMeshVertex& SrcV = Asset->Vertices[i];
        FSkeletalMeshVertex& DstV = SkinnedVertices[i];

        FVector FinalPos(0.0f, 0.0f, 0.0f);
        
        // Accumulate weighted transformations
        for (int j = 0; j < 4; ++j)
        {
            float Weight = SrcV.boneWeights[j];
            if (Weight > 0.0f)
            {
                int BoneIdx = SrcV.boneIndices[j];
                if (BoneIdx >= 0 && static_cast<uint32>(BoneIdx) < BoneCount)
                {
                    FinalPos += (SrcV.Position * SkinningMatrices[BoneIdx]) * Weight;
                }
            }
        }

        DstV.Position = FinalPos;
        // Copy bone info if needed, or other attributes if added later
        for(int j=0; j<4; ++j) {
            DstV.boneIndices[j] = SrcV.boneIndices[j];
            DstV.boneWeights[j] = SrcV.boneWeights[j];
        }
    }

    // 3. Update GPU Dynamic Buffer (should ideally be done in Proxy)
    // But as per order.md, we can manage it here or proxy. 
    // We'll update it here so it's ready for the Proxy.
}

FMeshBuffer* USkeletalMeshComponent::GetMeshBuffer() const
{
    // For CPU skinning, we might return a proxy buffer or handle it in SceneProxy.
    // Usually, we don't return a single FMeshBuffer for dynamic content 
    // but the Renderer uses the Proxy.
    return nullptr;
}

FMeshDataView USkeletalMeshComponent::GetMeshDataView() const
{
    FMeshDataView View;
    if (!SkinnedVertices.empty())
    {
        View.VertexData = SkinnedVertices.data();
        View.VertexCount = static_cast<uint32>(SkinnedVertices.size());
        View.Stride = sizeof(FSkeletalMeshVertex);
    }
    if (SkeletalMesh && SkeletalMesh->GetSkeletalMeshAsset())
    {
        View.IndexData = SkeletalMesh->GetSkeletalMeshAsset()->Indices.data();
        View.IndexCount = static_cast<uint32>(SkeletalMesh->GetSkeletalMeshAsset()->Indices.size());
    }
    return View;
}

class FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
    return new FSkeletalMeshSceneProxy(this);
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
    SkeletalMesh = InMesh;
    if (SkeletalMesh && SkeletalMesh->GetSkeletalMeshAsset())
    {
        FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
        size_t BoneCount = Asset->Bones.size();
        LocalTransforms.assign(BoneCount, FTransform());
        ComponentSpaceMatrices.assign(BoneCount, FMatrix::Identity);
        SkinnedVertices.resize(Asset->Vertices.size());
    }
}

const TArray<FMatrix>& USkeletalMeshComponent::GetInverseBindPoses() const
{
    static TArray<FMatrix> Empty;
    return SkeletalMesh ? SkeletalMesh->GetInverseBindPoses() : Empty;
}

int32 USkeletalMeshComponent::GetParentBoneIndex(int32 BoneIndex) const
{
    return SkeletalMesh ? SkeletalMesh->GetParentBoneIndex(BoneIndex) : -1;
}

void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	//Todo
    // Super::GetEditableProperties(OutProps);
}
