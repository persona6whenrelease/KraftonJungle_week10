#pragma once

#include "Object/Object.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Serialization/Archive.h"

struct ID3D11Device;

class USkeletalMesh : public UObject
{
public:
	DECLARE_CLASS(USkeletalMesh, UObject)

	USkeletalMesh() = default;
	~USkeletalMesh() override;

	void Serialize(FArchive& Ar) override;

	const FString& GetAssetPathFileName() const;

	void SetSkeletalMeshAsset(FSkeletalMesh* InMesh);
	FSkeletalMesh* GetSkeletalMeshAsset() const;

	void SetStaticMaterials(TArray<FStaticMaterial>&& InMaterials);
	const TArray<FStaticMaterial>& GetStaticMaterials() const;

private:
	FSkeletalMesh* SkeletalMeshAsset = nullptr;
	TArray<FStaticMaterial> StaticMaterials;
};
