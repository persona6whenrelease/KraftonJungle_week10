#pragma once

#include "Component/MeshComponent.h"
#include "Mesh/SkeletalMesh.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/VertexTypes.h"

class FPrimitiveSceneProxy;

class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	USkinnedMeshComponent() = default;
	~USkinnedMeshComponent() override = default;

	FMeshBuffer* GetMeshBuffer() const override;
	FMeshDataView GetMeshDataView() const override;
	void UpdateWorldAABB() const override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void SetSkeletalMesh(USkeletalMesh* InMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

protected:
	void CacheLocalBounds();
	void EnsureRuntimeResources();
	void BuildBindPoseRenderVertices();
	void UploadSkinnedVertices();
	void BuildReferencePoseMatrices();
	virtual void SkinVerticesToReferencePose();

	USkeletalMesh* SkeletalMesh = nullptr;
	FString SkeletalMeshPath = "None";

	TArray<FVertexPNCTT> SkinnedVertices;
	TArray<FMatrix> ReferenceBoneMatrices;
	FMeshBuffer RuntimeMeshBuffer;

	FVector CachedLocalCenter = { 0, 0, 0 };
	FVector CachedLocalExtent = { 0.5f, 0.5f, 0.5f };
	bool bHasValidBounds = false;
};
