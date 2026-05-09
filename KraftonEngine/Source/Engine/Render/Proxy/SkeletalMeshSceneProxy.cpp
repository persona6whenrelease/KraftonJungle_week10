#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Component/SkeletalMeshComponent.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Materials/Material.h"
#include "Runtime/Engine.h"
#include <algorithm>

namespace
{
    bool SectionMaterialLess(const FMeshSectionDraw& A, const FMeshSectionDraw& B)
    {
        const uintptr_t AMat = reinterpret_cast<uintptr_t>(A.Material);
        const uintptr_t BMat = reinterpret_cast<uintptr_t>(B.Material);
        if (AMat != BMat)
            return AMat < BMat;

        return A.FirstIndex < B.FirstIndex;
    }

    void SortSectionDrawsByMaterial(TArray<FMeshSectionDraw>& Draws)
    {
        if (Draws.size() > 1)
        {
            std::sort(Draws.begin(), Draws.end(), SectionMaterialLess);
        }
    }
}

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent)
    : FPrimitiveSceneProxy(InComponent)
{
    USkeletalMesh* Mesh = InComponent->GetSkeletalMesh();
    if (!Mesh || !Mesh->GetSkeletalMeshAsset())
    {
        return;
    }

    FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();

    // 1. Initialize Index Buffer (Static)
    if (!Asset->Indices.empty())
    {
        InternalIB.Create(Device, Asset->Indices.data(), 
            static_cast<uint32>(Asset->Indices.size()), 
            static_cast<uint32>(Asset->Indices.size() * sizeof(uint32)));
    }

    // 2. Initialize Dynamic Vertex Buffer
    if (!Asset->Vertices.empty())
    {
        InternalVB.Create(Device, 
            static_cast<uint32>(Asset->Vertices.size()), 
            sizeof(FSkeletalMeshVertex));
    }

    // 3. Initial Mesh/Material Update
    UpdateMesh();
}

FSkeletalMeshSceneProxy::~FSkeletalMeshSceneProxy()
{
    InternalVB.Release();
    InternalIB.Release();
}

USkeletalMeshComponent* FSkeletalMeshSceneProxy::GetSkeletalMeshComponent() const
{
    return static_cast<USkeletalMeshComponent*>(GetOwner());
}

void FSkeletalMeshSceneProxy::UpdateMesh()
{
    USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
    USkeletalMesh* Mesh = SMC->GetSkeletalMesh();
    if (!Mesh || !Mesh->GetSkeletalMeshAsset())
    {
        SectionDraws.clear();
        return;
    }

    FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    ID3D11DeviceContext* Context = GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
    ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();

    // 1. Upload Skinned Vertices to GPU
    FMeshDataView DataView = SMC->GetMeshDataView();
    if (DataView.VertexData && DataView.VertexCount > 0)
    {
        InternalVB.EnsureCapacity(Device, DataView.VertexCount);
        InternalVB.Update(Context, DataView.VertexData, DataView.VertexCount);
    }

    // 2. Rebuild Section Draws
    // Note: USkeletalMesh currently doesn't have a GetStaticMaterials() method.
    // We'll use the component's overrides and fall back to default if asset materials are missing.
    const auto& Overrides = SMC->GetOverrideMaterials();

    SectionDraws.clear();
    SectionDraws.reserve(Asset->Sections.size());

    for (const FSkeletalMeshSection& Section : Asset->Sections)
    {
        FMeshSectionDraw Draw;
        Draw.FirstIndex = Section.FirstIndex;
        Draw.IndexCount = Section.NumTriangles * 3;

        int32 i = Section.MaterialIndex;
        
        // Use override if available
        if (i >= 0 && i < static_cast<int32>(Overrides.size()) && Overrides[i])
        {
            Draw.Material = Overrides[i];
        }
        else
        {
            // Fallback to default material if no asset-level materials are defined
            Draw.Material = DefaultMaterial;
        }

        SectionDraws.push_back(Draw);
    }

    SortSectionDrawsByMaterial(SectionDraws);
}

void FSkeletalMeshSceneProxy::UpdateMaterial()
{
    // Re-run the material part of UpdateMesh or just rebuild section draws
    UpdateMesh();
}
