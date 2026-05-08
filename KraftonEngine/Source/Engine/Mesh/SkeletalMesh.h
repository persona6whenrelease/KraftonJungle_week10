#pragma once

#include "Mesh/SkeletalMeshAsset.h"
#include "Object/Object.h"

struct ID3D11Device;

class USkeletalMesh : public UObject
{
public:
	DECLARE_CLASS(USkeletalMesh, UObject)

	USkeletalMesh() = default;
	~USkeletalMesh() override;

	void Serialize(FArchive& Ar);

	const FString& GetAssetPathFileName() const;
	void SetSkeletalMeshAsset(FSkeletalMesh* InMesh);
	FSkeletalMesh* GetSkeletalMeshAsset() const { return SkeletalMeshAsset; }

	void SetMaterials(TArray<FMeshMaterial>&& InMaterials);
	const TArray<FMeshMaterial>& GetMaterials() const { return Materials; }

	const TArray<FMeshSection>& GetSections() const;

private:
	void RebuildSectionMaterialIndices();

	FSkeletalMesh* SkeletalMeshAsset = nullptr;
	TArray<FMeshMaterial> Materials;
};
