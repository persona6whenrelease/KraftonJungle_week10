#pragma once

#include "Component/SkinnedMeshComponent.h"
#include "SkeletalMesh/SkeletalMesh.h"
#include "Render/Types/VertexTypes.h"
#include "Render/Resource/Buffer.h"
#include "Mesh/StaticMeshAsset.h"  // FNormalVertex
#include "Core/PropertyTypes.h"

class UMaterial;
class FPrimitiveSceneProxy;
class UStaticMeshComponent;

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	// 에셋 바인딩
	void SetSkeletalMesh(USkeletalMesh* InMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	// 머티리얼 오버라이드
	void SetMaterial(int32 ElementIndex, UMaterial* InMaterial);
	UMaterial* GetMaterial(int32 ElementIndex) const;
	const TArray<UMaterial*>& GetOverrideMaterials() const { return OverrideMaterials; }

	// 매 틱 — 본 변환 갱신 + 스키닝 실행
	void UpdateAnimation(float DeltaTime);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	// UPrimitiveComponent overrides
	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FMeshBuffer* GetMeshBuffer() const override;
	FMeshDataView GetMeshDataView() const override;
	void UpdateWorldAABB() const override;

	// Editor UI Integration
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	// CPU 모드 시 Proxy가 접근
	const FDynamicVertexBuffer& GetDynamicVB() const { return DynamicVB; }

private:
	// FBone SRT → LocalTransforms[]
	void UpdateLocalTransforms();

	// 모드별 스키닝 디스패치 — cluster 모델: per-cluster matrix를 inline에서 계산.
	void UpdateSkinning();
	void UpdateSkinningCPU();
	void UpdateSkinningGPU();   // stub — Proxy 단계에서 완성
	void CalcDynamicLocalBounds();
	void CacheLocalBounds();

	// hybrid FBX: USkeletalMesh가 EmbeddedStaticMesh를 보유하면 sibling을 lazy-create.
	void SyncEmbeddedStaticMesh();

	// 에셋
	USkeletalMesh*        SkeletalMesh    = nullptr;
	FString               SkeletalMeshPath = "None";
	TArray<UMaterial*>    OverrideMaterials;
	TArray<FMaterialSlot> MaterialSlots;

	// CPU 스키닝 결과 (bind-pose vertex 레이아웃과 동일: FNormalVertex 48B)
	TArray<FNormalVertex> SkinnedVertices;
	FDynamicVertexBuffer  DynamicVB;

	// 본 계층 — RecalcComponentSpaceMatrices 전달용 캐시
	TArray<int32>   ParentIndices;

	// 같은 액터에 lazy-create한 static sub-mesh 컴포넌트 (hybrid FBX에만 존재)
	UStaticMeshComponent* EmbeddedStaticMeshComp = nullptr;

	// 바운드 캐시
	FVector CachedLocalCenter = { 0.f, 0.f, 0.f };
	FVector CachedLocalExtent = { 0.5f, 0.5f, 0.5f };
	bool    bHasValidBounds   = false;
};
