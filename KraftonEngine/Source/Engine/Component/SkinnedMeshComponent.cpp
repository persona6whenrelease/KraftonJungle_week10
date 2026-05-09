#include "SkinnedMeshComponent.h"
#include "Engine/Runtime/Engine.h"
#include "Mesh/FBXManager.h"

IMPLEMENT_CLASS(USkinnedMeshComponent, UMeshComponent)


USkinnedMeshComponent::~USkinnedMeshComponent()
{
}

//FSkinnedMeshBuffer* USkinnedMeshComponent::GetDynamicMeshBuffer() const
//{
//	if (!SkeletalMesh) return nullptr;
//	FStkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
//	if (!Asset || !Asset->RenderBuffer) return nullptr;
//	return Asset->RenderBuffer.get();
//}

void USkinnedMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UpdateSkinning(DeltaTime);
	MarkProxyDirty(EDirtyFlag::DynamicData);
}

void USkinnedMeshComponent::UpdateWorldAABB() const
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
	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	SkeletalMesh = InMesh;

	BoneSkinMatrices.clear();
	CurrentBoneGlobals.clear();
	SkinnedVertices.clear();

	if (InMesh)
	{
		SkeletalMeshPath = InMesh->GetAssetPathFileName();
		const TArray<FStaticMaterial>& DefaultMaterials = SkeletalMesh->GetStaticMaterials();

		OverrideMaterials.resize(DefaultMaterials.size());
		MaterialSlots.resize(DefaultMaterials.size());

		for (int32 i = 0; i < (int32)DefaultMaterials.size(); ++i)
		{
			OverrideMaterials[i] = DefaultMaterials[i].MaterialInterface;

			if (OverrideMaterials[i])
				MaterialSlots[i].Path = OverrideMaterials[i]->GetAssetPathFileName();
			else
				MaterialSlots[i].Path = "None";
		}

		//버텍스 정보 미리 빼놓기
		FStkeletalMesh* Mesh = SkeletalMesh->GetSkeletalMeshAsset();
		if (Mesh)
		{
			const FSkeletalMeshAsset& Asset = Mesh->MeshAsset;

			const int32 BoneCount = static_cast<int32>(Asset.Bones.size());
			const int32 VertexCount = static_cast<int32>(Asset.SourceVertices.size());

			BoneSkinMatrices.resize(BoneCount);
			CurrentBoneGlobals.resize(BoneCount);
			SkinnedVertices.resize(VertexCount);

			for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
			{
				const FBoneInfo& Bone = Asset.Bones[BoneIndex];

				CurrentBoneGlobals[BoneIndex] = Bone.BindPoseGlobal;
				BoneSkinMatrices[BoneIndex] =
					CurrentBoneGlobals[BoneIndex] * Bone.InverseBindPose;
			}

			for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
			{
				const FSkeletalSourceVertex& Src = Asset.SourceVertices[VertexIndex];

				FVertexPNCTT& Dst = SkinnedVertices[VertexIndex];
				Dst.Position = Src.Position;
				Dst.Normal = Src.Normal;
				Dst.UV = Src.UV;
				Dst.Color = Src.Color;
				Dst.Tangent = Src.Tangent;
			}
		}
	}
	else
	{
		SkeletalMeshPath = "None";
		OverrideMaterials.clear();
		MaterialSlots.clear();
	}
	CacheLocalBounds();
	MarkRenderStateDirty();
	MarkWorldBoundsDirty();
}

USkeletalMesh* USkinnedMeshComponent::GetSkeletalMesh() const
{
	return SkeletalMesh;
}

void USkinnedMeshComponent::SetMaterial(int32 ElementIndex, UMaterial* InMaterial)
{
	if (ElementIndex >= 0 && ElementIndex < static_cast<int32>(OverrideMaterials.size()))
	{
		OverrideMaterials[ElementIndex] = InMaterial;

		// MaterialSlots 동기화 — 씬 저장 시 경로가 올바르게 직렬화되도록
		if (ElementIndex < static_cast<int32>(MaterialSlots.size()))
		{
			MaterialSlots[ElementIndex].Path = InMaterial
				? InMaterial->GetAssetPathFileName()
				: "None";
		}

		// 프록시에 Material dirty 전파
		MarkProxyDirty(EDirtyFlag::Material);
	}
}

UMaterial* USkinnedMeshComponent::GetMaterial(int32 ElementIndex) const
{
	if (ElementIndex >= 0 && ElementIndex < OverrideMaterials.size())
	{
		return OverrideMaterials[ElementIndex];
	}
	return nullptr;
}

void USkinnedMeshComponent::Serialize(FArchive& Ar)
{
	UMeshComponent::Serialize(Ar);

}

void USkinnedMeshComponent::PostDuplicate()
{
	UMeshComponent::PostDuplicate();
	// 메시 에셋 재로딩
	if (!SkeletalMeshPath.empty() && SkeletalMeshPath != "None")
	{
		// StaticMesh의 ObjManager 대신 우리가 구축한 FBXManager를 사용합니다.
		USkeletalMesh* Loaded = FFBXManager::LoadSkeletalMesh(SkeletalMeshPath);

		if (Loaded)
		{
			// SetSkeletalMesh는 MaterialSlots를 기본값으로 덮어쓰므로, 
			// 직렬화되어 복제된 기존 슬롯 정보를 백업해둡니다.
			TArray<FMaterialSlot> SavedSlots = MaterialSlots;

			SetSkeletalMesh(Loaded);

			// Override material 재로딩 및 슬롯 복원
			for (int32 i = 0; i < (int32)MaterialSlots.size() && i < (int32)SavedSlots.size(); ++i)
			{
				MaterialSlots[i] = SavedSlots[i];
				const FString& MatPath = MaterialSlots[i].Path;

				if (MatPath.empty() || MatPath == "None")
				{
					OverrideMaterials[i] = nullptr;
				}
				else
				{
					UMaterial* LoadedMat = FMaterialManager::Get().GetOrCreateMaterial(MatPath);
					OverrideMaterials[i] = LoadedMat;
				}
			}
		}
	}
	CacheLocalBounds();
	MarkRenderStateDirty();
	MarkWorldBoundsDirty();

}

void USkinnedMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Skeletal Mesh", EPropertyType::StaticMeshRef, &SkeletalMeshPath });

	for (int32 i = 0; i < (int32)MaterialSlots.size(); ++i)
	{
		FPropertyDescriptor Desc;
		Desc.Name = "Element " + std::to_string(i);
		Desc.Type = EPropertyType::MaterialSlot;
		Desc.ValuePtr = &MaterialSlots[i];
		OutProps.push_back(Desc);
	}

}

void USkinnedMeshComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Skeletal Mesh") == 0)
	{
		if (SkeletalMeshPath.empty() || SkeletalMeshPath == "None")
		{
			SkeletalMesh = nullptr;
		}
		else
		{
			USkeletalMesh* Loaded =
				FFBXManager::LoadSkeletalMesh(SkeletalMeshPath);

			SetSkeletalMesh(Loaded);
		}
		CacheLocalBounds();
		MarkWorldBoundsDirty();
	}

	if (strncmp(PropertyName, "Element ", 8) == 0)
	{
		// "Element 0"에서 8번째 인덱스부터 시작하는 숫자를 정수로 변환
		int32 Index = atoi(&PropertyName[8]);

		// 인덱스 범위 유효성 검사
		if (Index >= 0 && Index < (int32)MaterialSlots.size())
		{
			FString NewMatPath = MaterialSlots[Index].Path;

			if (NewMatPath == "None" || NewMatPath.empty())
			{
				SetMaterial(Index, nullptr);
			}
			else
			{
				UMaterial* LoadedMat = FMaterialManager::Get().GetOrCreateMaterial(NewMatPath);
				if (LoadedMat)
				{
					SetMaterial(Index, LoadedMat);
				}
			}
		}
	}

}

void USkinnedMeshComponent::CacheLocalBounds()
{
	bHasValidBounds = false;
	if (!SkeletalMesh) return;
	FStkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset || Asset->MeshAsset.SourceVertices.empty()) return;

	// FStaticMesh에 이미 계산된 바운드가 있으면 그대로 사용
	if (!Asset->bBoundsValid)
	{
		Asset->CacheBounds();
	}

	CachedLocalCenter = Asset->BoundsCenter;
	CachedLocalExtent = Asset->BoundsExtent;
	bHasValidBounds = Asset->bBoundsValid;
}

void USkinnedMeshComponent::UpdateSkinning(float DeltaTime)
{
	if (!SkeletalMesh) return;

	FStkeletalMesh* Mesh = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Mesh) return;

	const FSkeletalMeshAsset& Asset = Mesh->MeshAsset;
	const uint32 VertexCount =
		static_cast<uint32>(Asset.SourceVertices.size());

	SkinnedVertices.resize(VertexCount);

	TArray<FBoneInfo> Bones = SkeletalMesh->GetSkeletalMeshAsset()->MeshAsset.Bones;

	for (uint32 i = 0; i < VertexCount; ++i)
	{
		const FSkeletalSourceVertex& Src = Asset.SourceVertices[i];

		FVector SkinnedPos(0, 0, 0);
		FVector SkinnedNormal(0, 0, 0);

		for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
		{
			const int32 BoneIndex = Src.BoneIndices[InfluenceIndex];
			const float Weight = Src.BoneWeights[InfluenceIndex];

			if (Weight <= 0.0f) continue;
			if (BoneIndex < 0 || BoneIndex >= BoneSkinMatrices.size()) continue;

			const FMatrix& SkinMatrix = BoneSkinMatrices[BoneIndex];

			SkinnedPos += SkinMatrix.TransformPositionWithW(Src.Position) * Weight;
			SkinnedNormal += SkinMatrix.TransformVector(Src.Normal) * Weight;
		}

		FVertexPNCTT& Dst = SkinnedVertices[i];
		Dst.Position = SkinnedPos;
		Dst.Normal = SkinnedNormal.Normalized();
		Dst.UV = Src.UV;
		Dst.Color = Src.Color;
		Dst.Tangent = Src.Tangent;
	}
}
