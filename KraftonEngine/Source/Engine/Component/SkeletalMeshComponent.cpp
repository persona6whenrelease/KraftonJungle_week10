#include "Component/SkeletalMeshComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Mesh/StaticMesh.h"
#include "GameFramework/AActor.h"
#include <algorithm>
#include <cmath>
#include "Object/ObjectFactory.h"
#include "Engine/Runtime/Engine.h"
#include "Render/Proxy/SkeletalSceneProxy.h"
#include "SkeletalMesh/FBXManager.h"
#include "Core/Log.h"

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
		SyncEmbeddedStaticMesh(); // 기존 child가 있으면 제거
		return;
	}

	FSkeletalMesh* Asset = InMesh->GetSkeletalMeshAsset();
	SkeletalMeshPath = Asset ? Asset->PathFileName : FString("None");
	if (!Asset)
	{
		SyncEmbeddedStaticMesh();
		return;
	}

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

	// CPU 스키닝 버퍼 — vertex 레이아웃은 FNormalVertex (bone 데이터 없음).
	SkinnedVertices = Asset->Vertices;
	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	DynamicVB.Create(Device, (uint32)Asset->Vertices.size(), sizeof(FNormalVertex));

	UpdateAnimation(0.0f);
	CacheLocalBounds();
	SyncEmbeddedStaticMesh();
	MarkRenderStateDirty();
	MarkWorldBoundsDirty();
}

void USkeletalMeshComponent::SyncEmbeddedStaticMesh()
{
	AActor* Owner = GetOwner();
	UStaticMesh* Embedded = SkeletalMesh ? SkeletalMesh->GetEmbeddedStaticMesh() : nullptr;

	if (!Embedded)
	{
		if (EmbeddedStaticMeshComp && Owner)
		{
			Owner->RemoveComponent(EmbeddedStaticMeshComp);
		}
		EmbeddedStaticMeshComp = nullptr;
		return;
	}

	if (!Owner)
	{
		// 액터에 부착 전이면 lazy-create 불가. 다음 SetSkeletalMesh 호출 시 재시도.
		return;
	}

	if (!EmbeddedStaticMeshComp)
	{
		EmbeddedStaticMeshComp = Owner->AddComponent<UStaticMeshComponent>();
	}
	if (EmbeddedStaticMeshComp)
	{
		EmbeddedStaticMeshComp->SetStaticMesh(Embedded);
	}
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

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsActive())
	{
		UpdateAnimation(DeltaTime);
	}
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
	if (!SkeletalMesh) return;
	if (!SkeletalMesh->GetSkeletalMeshAsset()) return;

	if (SkinningMode == ESkinningMode::CPU)
		UpdateSkinningCPU();
	else
		UpdateSkinningGPU();
}

void USkeletalMeshComponent::UpdateSkinningCPU()
{
	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset) return;

	const TArray<FNormalVertex>&  Src      = Asset->Vertices;
	const TArray<FBoneCluster>&   Clusters = Asset->Clusters;
	const TArray<FMatrix>&        Comp     = ComponentSpaceMatrices;
	const size_t                  N        = Src.size();
	if (N == 0) return;

	// 1) bind-pose 그대로 복사 (color/tex/tangent 보존).
	SkinnedVertices = Src;

	// 2) cluster에 잡힌 vertex는 pos/normal을 0으로 초기화한 뒤 누적.
	//    cluster에 한 번도 등장하지 않는 vertex는 bind-pose에 그대로 통과한다
	//    — 이전 모델의 (0,0,0) collapse 버그가 자연 해결.
	TArray<bool> bSkinned;
	bSkinned.assign(N, false);
	for (const FBoneCluster& C : Clusters)
	{
		for (uint32 vi : C.VertexIndices)
		{
			if (vi < N) bSkinned[vi] = true;
		}
	}
	for (size_t vi = 0; vi < N; ++vi)
	{
		if (bSkinned[vi])
		{
			SkinnedVertices[vi].pos    = FVector(0.f, 0.f, 0.f);
			SkinnedVertices[vi].normal = FVector(0.f, 0.f, 0.f);
		}
	}

	// 3) cluster 순회 — per-cluster matrix 한 번 계산 후 영향 vertex 누적.
	const int32 BoneCount = (int32)Comp.size();
	for (const FBoneCluster& C : Clusters)
	{
		if (C.BoneIndex < 0 || C.BoneIndex >= BoneCount) continue;

		// Row-Major: V' = V * IBP * ComponentSpace
		const FMatrix M = C.InverseBindMatrix * Comp[C.BoneIndex];

		const size_t Cnt = C.VertexIndices.size();
		for (size_t i = 0; i < Cnt; ++i)
		{
			const uint32 vi = C.VertexIndices[i];
			const float  w  = C.Weights[i];
			if (vi >= N || w <= 0.f) continue;

			const FNormalVertex& SrcV = Src[vi];
			SkinnedVertices[vi].pos    += M.TransformPositionWithW(SrcV.pos) * w;
			SkinnedVertices[vi].normal += M.TransformVector(SrcV.normal)     * w;
		}
	}

	ID3D11DeviceContext* Ctx = GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
	DynamicVB.Update(Ctx, SkinnedVertices.data(), (uint32)SkinnedVertices.size());
}

void USkeletalMeshComponent::UpdateSkinningGPU()
{
	// GPU 스키닝: 아직 구현되지 않음 (stub).
	// cluster 모델에서는 per-vertex bone index/weight가 없으므로,
	// vertex shader가 cluster SRV(또는 bone matrix palette + cluster index buffer)
	// 를 읽도록 별도 파이프라인 설계가 필요. 후속 작업.
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
	View.Stride      = sizeof(FNormalVertex);
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

void USkeletalMeshComponent::CalcDynamicLocalBounds()
{
	if (SkinnedVertices.empty()) return;

	FBoundingBox LocalBox;

	for (const auto& V : SkinnedVertices)
	{
		LocalBox.Expand(V.pos);
	}

	if (LocalBox.IsValid())
	{
		CachedLocalCenter = LocalBox.GetCenter();
		CachedLocalExtent = LocalBox.GetExtent();
		bHasValidBounds = true;
	}
}

void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	USkinnedMeshComponent::GetEditableProperties(OutProps);

	OutProps.push_back({ "Skeletal Mesh", EPropertyType::SkeletalMeshRef, &SkeletalMeshPath });

	// Skinning Mode Enum (CPU/GPU)
	static const char* SkinningModeNames[] = { "CPU", "GPU" };
	OutProps.push_back({ "Skinning Mode", EPropertyType::Enum, &SkinningMode, 0.0f, 0.0f, 0.0f, SkinningModeNames, 2 });

	// 머티리얼 슬롯
	for (int32 i = 0; i < (int32)MaterialSlots.size(); ++i)
	{
		char Label[32];
		snprintf(Label, sizeof(Label), "Element %d", i);
		OutProps.push_back({ Label, EPropertyType::MaterialSlot, &MaterialSlots[i] });
	}
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
	USkinnedMeshComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Skeletal Mesh") == 0)
	{
		if (SkeletalMeshPath == "None" || SkeletalMeshPath.empty())
		{
			SetSkeletalMesh(nullptr);
		}
		else
		{
			ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
			USkeletalMesh* NewMesh = FFBXManager::LoadSkeletalMesh(SkeletalMeshPath, Device);
			if (NewMesh)
			{
				SetSkeletalMesh(NewMesh);
			}
		}
		MarkRenderStateDirty();
		MarkWorldBoundsDirty();
	}
	else if (strncmp(PropertyName, "Element ", 8) == 0)
	{
		int32 Index = atoi(&PropertyName[8]);
		if (Index >= 0 && Index < (int32)MaterialSlots.size())
		{
			UMaterial* NewMat = FMaterialManager::Get().GetOrCreateMaterial(MaterialSlots[Index].Path);
			SetMaterial(Index, NewMat);
		}
	}
	else if (strcmp(PropertyName, "Skinning Mode") == 0)
	{
		MarkRenderStateDirty();
	}
}
