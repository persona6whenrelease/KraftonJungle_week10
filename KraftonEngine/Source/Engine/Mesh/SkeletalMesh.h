#pragma once
#include "Object/Object.h"

#include "Serialization/Archive.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Mesh/StaticMeshAsset.h"
#include "Collision/MeshTriangleBVH.h"

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

	// picking / 최적화를 위한 BVH 트리 빌드 및 판정 호출 함수
	void EnsureMeshTrianglePickingBVHBuilt() const;
	bool RaycastMeshTrianglesWithBVHLocal(const FVector& LocalOrigin, const FVector& LocalDirection, FHitResult& OutHitResult) const;

private:
	FStkeletalMesh* SkeletalMeshAsset = nullptr;
	TArray<FStaticMaterial> StaticMaterials; // 슬롯 이름과 머티리얼 인터페이스를 묶어서 저장하는 배열
	mutable FMeshTriangleBVH MeshTrianglePickingBVH; // 빠른 picking을 위해 메시 내부에 트리 형태로 만들어지는 자료구조

	static const FString EmptyPath;
};

