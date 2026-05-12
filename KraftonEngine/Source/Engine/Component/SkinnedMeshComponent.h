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
	const TArray<FMatrix>& GetLocalBonePoseMatrices() const { return LocalBonePoseMatrices; }

	int32 FindBoneIndexByName(const FString& BoneName) const;
	void ResetBonePoseToBindPose();
	bool SetBoneLocalPose(int32 BoneIndex, const FMatrix& LocalPose);
	bool SetBoneLocalPoseByName(const FString& BoneName, const FMatrix& LocalPose);
	const TArray<FMatrix>& GetMeshSpaceBoneMatrices() const { return MeshSpaceBoneMatrices; }

	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

protected:
	void CacheLocalBounds();
	void EnsureRuntimeResources();
	void BuildBindPoseRenderVertices();
	void UploadSkinnedVertices();
	void RebuildMeshSpaceBoneMatrices();
	virtual void SkinVerticesToReferencePose();

	USkeletalMesh* SkeletalMesh = nullptr;
	FString SkeletalMeshPath = "None";

	TArray<FVertexPNCTT> SkinnedVertices;
	TArray<FMatrix> LocalBonePoseMatrices;
	TArray<FMatrix> MeshSpaceBoneMatrices;
	FMeshBuffer RuntimeMeshBuffer;

	FVector CachedLocalCenter = { 0, 0, 0 };
	FVector CachedLocalExtent = { 0.5f, 0.5f, 0.5f };
	bool bHasValidBounds = false;
};
