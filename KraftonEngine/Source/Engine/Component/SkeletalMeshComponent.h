#pragma once

#include "Component/SkinnedMeshComponent.h"
#include "SkeletalMesh/SkeletalMesh.h"
#include "Render/Types/VertexTypes.h"
#include "Render/Resource/Buffer.h"
#include "Core/PropertyTypes.h"

class UMaterial;
class FPrimitiveSceneProxy;

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

	// UPrimitiveComponent overrides
	FPrimitiveSceneProxy* CreateSceneProxy() override;
	FMeshBuffer* GetMeshBuffer() const override;
	FMeshDataView GetMeshDataView() const override;
	void UpdateWorldAABB() const override;

	// CPU 모드 시 Proxy가 접근
	const FDynamicVertexBuffer& GetDynamicVB() const { return DynamicVB; }

private:
	// FBone SRT → LocalTransforms[]
	void UpdateLocalTransforms();

	// ComponentSpaceMatrices * IBP → SkinningMatrices → 모드별 분기
	void UpdateSkinning();
	void UpdateSkinningCPU();
	void UpdateSkinningGPU();   // stub — Proxy 단계에서 완성

	void CacheLocalBounds();

	// 에셋
	USkeletalMesh*        SkeletalMesh    = nullptr;
	FString               SkeletalMeshPath = "None";
	TArray<UMaterial*>    OverrideMaterials;
	TArray<FMaterialSlot> MaterialSlots;

	// CPU 스키닝
	TArray<FSkeletalMeshVertex> SkinnedVertices;
	FDynamicVertexBuffer        DynamicVB;

	// 공통
	TArray<FMatrix> SkinningMatrices;
	TArray<int32>   ParentIndices;      // RecalcComponentSpaceMatrices 전달용 캐시

	// 바운드 캐시
	FVector CachedLocalCenter = { 0.f, 0.f, 0.f };
	FVector CachedLocalExtent = { 0.5f, 0.5f, 0.5f };
	bool    bHasValidBounds   = false;
};
