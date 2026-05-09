#pragma once
#include "Object/Object.h"

#include "Serialization/Archive.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Mesh/StaticMeshAsset.h"
#include <memory>

struct ID3D11Device;

class USkeletalMesh :
    public UObject
{
public:
	DECLARE_CLASS(USkeletalMesh, UObject)


	USkeletalMesh() = default;
	~USkeletalMesh() override;

	void Serialize(FArchive& Ar);

	const FString& GetAssetPathFileName() const;
	void SetSkeletalMeshAsset(FStkeletalMesh* InMesh);
	FStkeletalMesh* GetSkeletalMeshAsset() const;
	void SetStaticMaterials(TArray<FStaticMaterial>&& InMaterials);
	const TArray<FStaticMaterial>& GetStaticMaterials() const;

	//void InitResources(ID3D11Device* InDevice);	//프록시로 이동

private:
	FStkeletalMesh* SkeletalMeshAsset = nullptr;
	TArray<FStaticMaterial> StaticMaterials; // 슬롯 이름과 머티리얼 인터페이스를 묶어서 저장하는 배열

	static const FString EmptyPath;
};

