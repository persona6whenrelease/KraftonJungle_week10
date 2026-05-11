#pragma once
#include "Core/PropertyTypes.h"
#include "MeshComponent.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/ObjManager.h"

#include "../Engine/Runtime/DelegateSubscriptionBox.h"

class UMaterial;
class USkeletalMesh;

class USkinnedMeshComponent :
    public UMeshComponent
{
public:	//proxy를 제외하고 모든 함수
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	USkinnedMeshComponent() = default;
	~USkinnedMeshComponent() override;

	//FSkinnedMeshBuffer* GetDynamicMeshBuffer() const;	//프록시가 buffer 소유
	//FMeshDataView GetMeshDataView() const override;	//안쓰는 코드라고 함

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;


	void UpdateWorldAABB() const override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;

	void SetSkeletalMesh(USkeletalMesh* InMesh);
	USkeletalMesh* GetSkeletalMesh() const;


	void SetMaterial(int32 ElementIndex, UMaterial* InMaterial);
	UMaterial* GetMaterial(int32 ElementIndex) const;
	const TArray<UMaterial*>& GetOverrideMaterials() const { return OverrideMaterials; }

	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	const FString& GetStaticMeshPath() const { return SkeletalMeshPath; }

	const TArray<FVertexPNCTT>& GetSkinnedVertices() const { return SkinnedVertices; }

protected:
	USkeletalMesh* SkeletalMesh = nullptr;

	FString SkeletalMeshPath = "None";


private:
	void CacheLocalBounds();
	void InitializeSkinningData();
	void UpdateSkinning(float DeltaTime);

	TArray<UMaterial*> OverrideMaterials;
	TArray<FMaterialSlot> MaterialSlots; // 경로 

	FVector CachedLocalCenter = { 0, 0, 0 };
	FVector CachedLocalExtent = { 0.5f, 0.5f, 0.5f };
	bool bHasValidBounds = false;

	//매프레임마다 스키닝할때 필요한 정보(변경X)
	TArray<FMatrix> BoneSkinMatrices;

	//매프레임마다 계산될 버텍스 정보(변경o)
	TArray<FVertexPNCTT> SkinnedVertices;	//프록시에 넣을때 사용
	TArray<FMatrix> CurrentBoneGlobals;

	mutable FMeshTriangleBVH SkinnedMeshPickingBVH;
	mutable bool bSkinnedMeshPickingBVHDirty = true;
};

