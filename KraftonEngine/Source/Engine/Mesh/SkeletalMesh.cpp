#include "Mesh/SkeletalMesh.h"

#include "Object/ObjectFactory.h"

#include <utility>

IMPLEMENT_CLASS(USkeletalMesh, UObject)

static const FString EmptySkeletalPath;
static const TArray<FMeshSection> EmptySkeletalSections;

USkeletalMesh::~USkeletalMesh()
{
	delete SkeletalMeshAsset;
	SkeletalMeshAsset = nullptr;
}

void USkeletalMesh::Serialize(FArchive& Ar)
{
	if (Ar.IsLoading() && !SkeletalMeshAsset)
	{
		SkeletalMeshAsset = new FSkeletalMesh();
	}

	if (SkeletalMeshAsset)
	{
		SkeletalMeshAsset->Serialize(Ar);
	}
	Ar << Materials;

	if (Ar.IsLoading())
	{
		RebuildSectionMaterialIndices();
	}
}

const FString& USkeletalMesh::GetAssetPathFileName() const
{
	return SkeletalMeshAsset ? SkeletalMeshAsset->PathFileName : EmptySkeletalPath;
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh* InMesh)
{
	if (SkeletalMeshAsset != InMesh)
	{
		delete SkeletalMeshAsset;
		SkeletalMeshAsset = InMesh;
	}

	if (SkeletalMeshAsset && !SkeletalMeshAsset->bBoundsValid)
	{
		SkeletalMeshAsset->CacheBounds();
	}
	RebuildSectionMaterialIndices();
}

void USkeletalMesh::SetMaterials(TArray<FMeshMaterial>&& InMaterials)
{
	Materials = std::move(InMaterials);
	RebuildSectionMaterialIndices();
}

const TArray<FMeshSection>& USkeletalMesh::GetSections() const
{
	return SkeletalMeshAsset ? SkeletalMeshAsset->Sections : EmptySkeletalSections;
}

void USkeletalMesh::RebuildSectionMaterialIndices()
{
	if (!SkeletalMeshAsset)
	{
		return;
	}

	for (FMeshSection& Section : SkeletalMeshAsset->Sections)
	{
		Section.MaterialIndex = -1;
		for (int32 i = 0; i < static_cast<int32>(Materials.size()); ++i)
		{
			if (Materials[i].MaterialSlotName == Section.MaterialSlotName)
			{
				Section.MaterialIndex = i;
				break;
			}
		}
	}
}
