#include "Component/SkeletalMeshComponent.h"
#include <algorithm>
#include <cmath>
#include "Object/ObjectFactory.h"
#include "Engine/Runtime/Engine.h"
#include "Render/Proxy/SkeletalSceneProxy.h"

IMPLEMENT_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

// ---------------------------------------------------------------------------
// 에셋 바인딩
// ---------------------------------------------------------------------------

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	SkeletalMesh = InMesh;

	if (!InMesh)
	{
		SkeletalMeshPath = "None";
		OverrideMaterials.clear();
		MaterialSlots.clear();
		return;
	}

	SkeletalMeshPath = InMesh->GetSkeletalMeshAsset()->PathFileName;

	FSkeletalMesh* Asset = InMesh->GetSkeletalMeshAsset();
	if (!Asset) return;

	// 머티리얼 슬롯 초기화
	const TArray<FStaticMaterial>& DefaultMaterials = InMesh->GetStaticMaterials();
	OverrideMaterials.resize(DefaultMaterials.size());
	MaterialSlots.resize(DefaultMaterials.size());
	for (int32 i = 0; i < (int32)DefaultMaterials.size(); ++i)
	{
		OverrideMaterials[i] = DefaultMaterials[i].MaterialInterface;
		MaterialSlots[i].Path = OverrideMaterials[i]
			? OverrideMaterials[i]->GetAssetPathFileName()
			: "None";
	}

	// 본 계층 캐시
	const int32 BoneCount = (int32)Asset->Bones.size();
	ParentIndices.resize(BoneCount);
	for (int32 i = 0; i < BoneCount; ++i)
		ParentIndices[i] = Asset->Bones[i].ParentIndex;

	// 포즈 버퍼 초기화 (부모)
	InitializePoseBuffers(BoneCount);
	SkinningMatrices.assign(BoneCount, FMatrix::Identity);

	// CPU 스키닝 버퍼
	SkinnedVertices = Asset->Vertices;
	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	DynamicVB.Create(Device, (uint32)Asset->Vertices.size(), sizeof(FSkeletalMeshVertex));

	CacheLocalBounds();
	MarkRenderStateDirty();
	MarkWorldBoundsDirty();
}

// ---------------------------------------------------------------------------
// 머티리얼 오버라이드
// ---------------------------------------------------------------------------

void USkeletalMeshComponent::SetMaterial(int32 ElementIndex, UMaterial* InMaterial)
{
	if (ElementIndex < 0 || ElementIndex >= (int32)OverrideMaterials.size()) return;

	OverrideMaterials[ElementIndex] = InMaterial;
	if (ElementIndex < (int32)MaterialSlots.size())
	{
		MaterialSlots[ElementIndex].Path = InMaterial
			? InMaterial->GetAssetPathFileName()
			: "None";
	}
	MarkProxyDirty(EDirtyFlag::Material);
}

UMaterial* USkeletalMeshComponent::GetMaterial(int32 ElementIndex) const
{
	if (ElementIndex >= 0 && ElementIndex < (int32)OverrideMaterials.size())
		return OverrideMaterials[ElementIndex];
	return nullptr;
}

// ---------------------------------------------------------------------------
// 애니메이션 틱
// ---------------------------------------------------------------------------

void USkeletalMeshComponent::UpdateAnimation(float DeltaTime)
{
	if (!SkeletalMesh) return;

	UpdateLocalTransforms();
	RecalcComponentSpaceMatrices(ParentIndices);
	UpdateSkinning();
}

void USkeletalMeshComponent::UpdateLocalTransforms()
{
	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset) return;

	// 현재는 바인드 포즈 고정. 이후 AnimInstance가 여기에 블렌딩 결과를 기록.
	for (int32 i = 0; i < (int32)Asset->Bones.size(); ++i)
	{
		const FBone& B = Asset->Bones[i];
		LocalTransforms[i] = FTransform(B.Translation, B.Rotation, B.Scale);
	}
}

// ---------------------------------------------------------------------------
// 스키닝
// ---------------------------------------------------------------------------

void USkeletalMeshComponent::UpdateSkinning()
{
	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset) return;

	for (int32 i = 0; i < (int32)SkinningMatrices.size(); ++i)
		SkinningMatrices[i] = ComponentSpaceMatrices[i] * Asset->Bones[i].InverseBindMatrix;

	if (SkinningMode == ESkinningMode::CPU)
		UpdateSkinningCPU();
	else
		UpdateSkinningGPU();
}

void USkeletalMeshComponent::UpdateSkinningCPU()
{
	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset) return;

	const TArray<FSkeletalMeshVertex>& Src = Asset->Vertices;
	SkinnedVertices = Src;

	for (int32 vi = 0; vi < (int32)Src.size(); ++vi)
	{
		const FSkeletalMeshVertex& V = Src[vi];
		FVector SkinPos  = { 0.f, 0.f, 0.f };
		FVector SkinNorm = { 0.f, 0.f, 0.f };

		for (int j = 0; j < 4; ++j)
		{
			float W = V.boneWeights[j];
			if (W <= 0.f) continue;
			const FMatrix& M = SkinningMatrices[V.boneIndices[j]];
			SkinPos  += M.TransformPositionWithW(V.Position) * W;
			SkinNorm += M.TransformVector(V.Normal) * W;
		}

		SkinnedVertices[vi].Position = SkinPos;
		SkinnedVertices[vi].Normal   = SkinNorm;
	}

	ID3D11DeviceContext* Ctx = GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
	DynamicVB.Update(Ctx, SkinnedVertices.data(), (uint32)SkinnedVertices.size());
}

void USkeletalMeshComponent::UpdateSkinningGPU()
{
	// GPU 스키닝: 파이프라인에 SRV 슬롯 추가 후 아래 라인 활성화
	// static_cast<FSkeletalSceneProxy*>(SceneProxy)->UpdateBoneMatrices(SkinningMatrices);
}

// ---------------------------------------------------------------------------
// UPrimitiveComponent overrides
// ---------------------------------------------------------------------------

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset()) return nullptr;
	return new FSkeletalSceneProxy(this);
}

FMeshBuffer* USkeletalMeshComponent::GetMeshBuffer() const
{
	if (!SkeletalMesh) return nullptr;
	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset || !Asset->RenderBuffer) return nullptr;
	return Asset->RenderBuffer.get();
}

FMeshDataView USkeletalMeshComponent::GetMeshDataView() const
{
	if (!SkeletalMesh) return {};
	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset || Asset->Vertices.empty()) return {};

	FMeshDataView View;
	View.VertexData  = Asset->Vertices.data();
	View.VertexCount = (uint32)Asset->Vertices.size();
	View.Stride      = sizeof(FSkeletalMeshVertex);
	View.IndexData   = Asset->Indices.data();
	View.IndexCount  = (uint32)Asset->Indices.size();
	return View;
}

void USkeletalMeshComponent::UpdateWorldAABB() const
{
	if (!bHasValidBounds)
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	FVector WorldCenter = CachedWorldMatrix.TransformPositionWithW(CachedLocalCenter);

	float Ex = std::abs(CachedWorldMatrix.M[0][0]) * CachedLocalExtent.X
		+ std::abs(CachedWorldMatrix.M[1][0]) * CachedLocalExtent.Y
		+ std::abs(CachedWorldMatrix.M[2][0]) * CachedLocalExtent.Z;
	float Ey = std::abs(CachedWorldMatrix.M[0][1]) * CachedLocalExtent.X
		+ std::abs(CachedWorldMatrix.M[1][1]) * CachedLocalExtent.Y
		+ std::abs(CachedWorldMatrix.M[2][1]) * CachedLocalExtent.Z;
	float Ez = std::abs(CachedWorldMatrix.M[0][2]) * CachedLocalExtent.X
		+ std::abs(CachedWorldMatrix.M[1][2]) * CachedLocalExtent.Y
		+ std::abs(CachedWorldMatrix.M[2][2]) * CachedLocalExtent.Z;

	WorldAABBMinLocation = WorldCenter - FVector(Ex, Ey, Ez);
	WorldAABBMaxLocation = WorldCenter + FVector(Ex, Ey, Ez);
	bWorldAABBDirty      = false;
	bHasValidWorldAABB   = true;
}

// ---------------------------------------------------------------------------
// 내부 헬퍼
// ---------------------------------------------------------------------------

void USkeletalMeshComponent::CacheLocalBounds()
{
	bHasValidBounds = false;
	if (!SkeletalMesh) return;
	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset || Asset->Vertices.empty()) return;

	if (!Asset->bBoundsValid)
		Asset->CacheBounds();

	CachedLocalCenter = Asset->BoundsCenter;
	CachedLocalExtent = Asset->BoundsExtent;
	bHasValidBounds   = Asset->bBoundsValid;
}
