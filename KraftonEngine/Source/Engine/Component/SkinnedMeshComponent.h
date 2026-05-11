#pragma once

#include "Component/MeshComponent.h"
#include "Math/Matrix.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Render/Resource/Buffer.h"

class USkeletalMesh;
struct ID3D11Device;
struct ID3D11DeviceContext;

class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	USkinnedMeshComponent() = default;
	~USkinnedMeshComponent() override = default;

	void SetSkeletalMesh(USkeletalMesh* InMesh);
	USkeletalMesh* GetSkeletalMesh() const;

	FMeshBuffer* GetMeshBuffer() const override;
	FMeshDataView GetMeshDataView() const override;
	void UpdateWorldAABB() const override;

	void InitDynamicResources(ID3D11Device* InDevice);
	void UploadSkinnedMeshToGPU(ID3D11Device* InDevice, ID3D11DeviceContext* InContext);

	const TArray<FNormalVertex>& GetSkinnedVertices() const
	{
		return SkinnedVertices;
	}

	const TArray<uint32>& GetSkinnedIndices() const
	{
		return SkinnedIndices;
	}

	FDynamicVertexBuffer& GetDynamicVertexBuffer()
	{
		return DynamicVertexBuffer;
	}

	FDynamicIndexBuffer& GetDynamicIndexBuffer()
	{
		return DynamicIndexBuffer;
	}

	const FDynamicVertexBuffer& GetDynamicVertexBuffer() const
	{
		return DynamicVertexBuffer;
	}

	const FDynamicIndexBuffer& GetDynamicIndexBuffer() const
	{
		return DynamicIndexBuffer;
	}

	void RefreshSkinning();

protected:
	void InitializeSkinningBuffers();
	void ResetPoseToBindPose();
	void ApplyDebugShoulderPose();
	void BuildCurrentBoneWorldTransforms();
	void BuildSkinningMatrices();
	void SkinVerticesCPU();
	void CacheLocalBounds();

	USkeletalMesh* SkeletalMesh = nullptr;

	TArray<FMatrix> CurrentBoneLocalTransforms;
	TArray<FMatrix> CurrentBoneWorldTransforms;
	TArray<FMatrix> SkinningMatrices;

	TArray<FNormalVertex> SkinnedVertices;
	TArray<uint32> SkinnedIndices;

	FDynamicVertexBuffer DynamicVertexBuffer;
	FDynamicIndexBuffer DynamicIndexBuffer;

	FVector CachedLocalCenter = FVector(0.0f, 0.0f, 0.0f);
	FVector CachedLocalExtent = FVector(0.5f, 0.5f, 0.5f);
	bool bHasValidBounds = false;
};
