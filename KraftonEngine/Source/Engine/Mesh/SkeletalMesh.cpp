#include "Mesh/SkeletalMesh.h"

IMPLEMENT_CLASS(USkeletalMesh, UObject)

USkeletalMesh::~USkeletalMesh()
{
	delete SkeletalMeshAsset;
	SkeletalMeshAsset = nullptr;
}

void USkeletalMesh::Serialize(FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		FString Path = GetAssetPathFileName();
		Ar << Path;
	}

	Ar << StaticMaterials;
}

const FString& USkeletalMesh::GetAssetPathFileName() const
{
	static const FString EmptyPath = "None";

	if (!SkeletalMeshAsset)
	{
		return EmptyPath;
	}

	return SkeletalMeshAsset->PathFileName;
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh* InMesh)
{
	if (SkeletalMeshAsset == InMesh)
	{
		return;
	}

	delete SkeletalMeshAsset;
	SkeletalMeshAsset = InMesh;
}

FSkeletalMesh* USkeletalMesh::GetSkeletalMeshAsset() const
{
	return SkeletalMeshAsset;
}

void USkeletalMesh::SetStaticMaterials(TArray<FStaticMaterial>&& InMaterials)
{
	StaticMaterials = std::move(InMaterials);
}

const TArray<FStaticMaterial>& USkeletalMesh::GetStaticMaterials() const
{
	return StaticMaterials;
}
