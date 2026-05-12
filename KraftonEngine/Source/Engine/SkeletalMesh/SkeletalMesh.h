#pragma once

#include "Engine/Object/Object.h"
#include "SkeletalMeshAsset.h"
#include "Mesh/StaticMeshAsset.h" // For FStaticMaterial
#include "Serialization/Archive.h"
#include <memory>

struct ID3D11Device;
class UStaticMesh;

/**
 * USkeletalMesh
 * FSkeletalMesh를 소유하는 UObject 에셋 클래스.
 * 머티리얼 매핑 및 본 이름 매핑 등을 관리함.
 */
class USkeletalMesh : public UObject
{
public:
	DECLARE_CLASS(USkeletalMesh, UObject)

	USkeletalMesh() = default;
	~USkeletalMesh() override;

	void Serialize(FArchive& Ar) override;

	// 데이터 설정 및 접근
	void SetSkeletalMeshAsset(FSkeletalMesh* InMesh);
	FSkeletalMesh* GetSkeletalMeshAsset() const { return SkeletalMeshAsset; }

	void SetStaticMaterials(TArray<FStaticMaterial>&& InMaterials);
	const TArray<FStaticMaterial>& GetStaticMaterials() const { return StaticMaterials; }

	// GPU 리소스 초기화
	void InitResources(ID3D11Device* InDevice);

	// 본 관련 유틸리티
	int32 GetBoneIndex(const FName& Name) const;
	const TArray<FName>& GetBoneNames() const { return BoneNames; }
	
	// 본 이름 리스트 설정 (Importer에서 호출)
	void SetBoneNames(TArray<FName>&& InNames);

	// FBX hybrid (skinned + static) 케이스에서 함께 추출된 static 파트.
	// 보유하지 않을 수 있다 (pure skeletal FBX). 라이프사이클은 UObjectManager가 관리한다.
	UStaticMesh* GetEmbeddedStaticMesh() const { return EmbeddedStaticMesh; }
	void         SetEmbeddedStaticMesh(UStaticMesh* InMesh) { EmbeddedStaticMesh = InMesh; }

private:
	// 메시 및 본 데이터 본체
	FSkeletalMesh* SkeletalMeshAsset = nullptr;

	// FBX hybrid에서 추출된 static 파트 (옵셔널)
	UStaticMesh* EmbeddedStaticMesh = nullptr;

	// 머티리얼 슬롯 정보
	TArray<FStaticMaterial> StaticMaterials;

	// 본 인덱스에 대응하는 이름 리스트
	TArray<FName> BoneNames;

	// 이름으로 본 인덱스를 빠르게 찾기 위한 맵 (런타임 생성)
	TMap<FName, int32> BoneNameToIndex;

	void RebuildBoneMap();
};
