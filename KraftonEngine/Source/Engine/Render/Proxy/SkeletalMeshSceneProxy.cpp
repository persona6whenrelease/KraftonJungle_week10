#include "Render/Proxy/SkeletalMeshSceneProxy.h"

#include <algorithm>
#include "Component/SkinnedMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Render/Types/FrameContext.h"
#include "Mesh/SkeletalMesh.h"

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
}

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkinnedMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate;
}

USkinnedMeshComponent* FSkeletalMeshSceneProxy::GetSkinnedMeshComponent() const
{
	return static_cast<USkinnedMeshComponent*>(GetOwner());
}

void FSkeletalMeshSceneProxy::UpdateMaterial()
{
	RebuildSectionDraws();
}

void FSkeletalMeshSceneProxy::UpdateMesh()
{
	MeshBuffer = GetOwner()->GetMeshBuffer();
	RebuildSectionDraws();
}

void FSkeletalMeshSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	UpdateVisibility();

	if (!Frame.RenderOptions.ShowFlags.bSkeletalMesh)
	{
		bVisible = false;

		return;
	}
	RebuildSectionDraws();
}

void FSkeletalMeshSceneProxy::RebuildSectionDraws()
{
	USkinnedMeshComponent* SkinnedComp = GetSkinnedMeshComponent();
	USkeletalMesh* Mesh = SkinnedComp ? SkinnedComp->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !bVisible)
	{
		MeshBuffer = nullptr;
		SectionDraws.clear();
		return;
	}

	MeshBuffer = SkinnedComp->GetMeshBuffer();
	const TArray<FMeshMaterial>& Slots = Mesh->GetMaterials();
	const TArray<UMaterial*>& Overrides = SkinnedComp->GetOverrideMaterials();
	const TArray<FMeshSection>& Sections = Mesh->GetSections();
	UMaterial* FallbackMaterial = FMaterialManager::Get().GetOrCreateMaterial("None");

	SectionDraws.clear();
	SectionDraws.reserve(Sections.size());

	for (const FMeshSection& Section : Sections)
	{
		FMeshSectionDraw Draw;
		Draw.FirstIndex = Section.FirstIndex;
		Draw.IndexCount = Section.NumTriangles * 3;

		const int32 MaterialIndex = Section.MaterialIndex;
		if (MaterialIndex >= 0 && MaterialIndex < static_cast<int32>(Slots.size()))
		{
			if (MaterialIndex < static_cast<int32>(Overrides.size()) && Overrides[MaterialIndex])
				Draw.Material = Overrides[MaterialIndex];
			else if (Slots[MaterialIndex].MaterialInterface)
				Draw.Material = Slots[MaterialIndex].MaterialInterface;
		}

		if (!Draw.Material)
		{
			Draw.Material = FallbackMaterial;
		}

		SectionDraws.push_back(Draw);
	}

	if (SectionDraws.size() > 1)
	{
		std::sort(SectionDraws.begin(), SectionDraws.end(), SectionMaterialLess);
	}
}
