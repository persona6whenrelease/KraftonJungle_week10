#include "Component/SkinnedMeshComponent.h"

#include <cmath>
#include <cstring>
#include "Engine/Runtime/Engine.h"
#include "Mesh/FBX/FBXManager.h"
#include "Object/ObjectFactory.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Serialization/Archive.h"

IMPLEMENT_CLASS(USkinnedMeshComponent, UMeshComponent)
HIDE_FROM_COMPONENT_LIST(USkinnedMeshComponent)

FPrimitiveSceneProxy* USkinnedMeshComponent::CreateSceneProxy()
{
	EnsureRuntimeResources();
	return new FSkeletalMeshSceneProxy(this);
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	SkeletalMesh = InMesh;
	if (SkeletalMesh)
	{
		SkeletalMeshPath = SkeletalMesh->GetAssetPathFileName();
		InitializeMaterialSlots(SkeletalMesh->GetMaterials());
	}
	else
	{
		SkeletalMeshPath = "None";
		ClearMaterialSlots();
		SkinnedVertices.clear();
		RuntimeMeshBuffer.Release();
	}

	CacheLocalBounds();
	BuildReferencePoseMatrices();
	BuildBindPoseRenderVertices();
	RuntimeMeshBuffer.Release();
	EnsureRuntimeResources();
	MarkRenderStateDirty();
	MarkWorldBoundsDirty();
}

FMeshBuffer* USkinnedMeshComponent::GetMeshBuffer() const
{
	return RuntimeMeshBuffer.IsValid() ? const_cast<FMeshBuffer*>(&RuntimeMeshBuffer) : nullptr;
}

FMeshDataView USkinnedMeshComponent::GetMeshDataView() const
{
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		return {};
	}

	const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	FMeshDataView View;
	if (!Asset->Vertices.empty())
	{
		View.VertexData = Asset->Vertices.data();
		View.VertexCount = static_cast<uint32>(Asset->Vertices.size());
		View.Stride = sizeof(FSkeletalVertex);
	}
	if (!Asset->Indices.empty())
	{
		View.IndexData = Asset->Indices.data();
		View.IndexCount = static_cast<uint32>(Asset->Indices.size());
	}
	return View;
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

void USkinnedMeshComponent::Serialize(FArchive& Ar)
{
	UMeshComponent::Serialize(Ar);
	Ar << SkeletalMeshPath;
	Ar << MaterialSlots;
}

void USkinnedMeshComponent::PostDuplicate()
{
	UMeshComponent::PostDuplicate();

	if (!SkeletalMeshPath.empty() && SkeletalMeshPath != "None")
	{
		const bool bFbxSceneSkeletalMeshReference = SkeletalMeshPath.find("#Skeleton_") != FString::npos;
		USkeletalMesh* Loaded = bFbxSceneSkeletalMeshReference
			? FFBXManager::LoadSkeletalMeshFromFbxSceneReference(SkeletalMeshPath)
			: nullptr;
		if (!Loaded && !bFbxSceneSkeletalMeshReference)
		{
			Loaded = FFBXManager::LoadSkeletalMesh(SkeletalMeshPath);
		}
		if (Loaded)
		{
			TArray<FMaterialSlot> SavedSlots = MaterialSlots;
			SetSkeletalMesh(Loaded);

			for (int32 i = 0; i < static_cast<int32>(MaterialSlots.size()) && i < static_cast<int32>(SavedSlots.size()); ++i)
			{
				MaterialSlots[i] = SavedSlots[i];
			}
		}
	}

	RestoreOverrideMaterialsFromSlots();
	CacheLocalBounds();
	BuildReferencePoseMatrices();
	BuildBindPoseRenderVertices();
	EnsureRuntimeResources();
	MarkRenderStateDirty();
	MarkWorldBoundsDirty();
}

void USkinnedMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Skeletal Mesh", EPropertyType::SkeletalMeshRef, &SkeletalMeshPath });
	AppendMaterialSlotProperties(OutProps);
}

void USkinnedMeshComponent::PostEditProperty(const char* PropertyName)
{
	UMeshComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "Skeletal Mesh") == 0)
	{
		if (SkeletalMeshPath.empty() || SkeletalMeshPath == "None")
		{
			SetSkeletalMesh(nullptr);
		}
		else
		{
			const bool bFbxSceneSkeletalMeshReference = SkeletalMeshPath.find("#Skeleton_") != FString::npos;
			USkeletalMesh* Loaded = bFbxSceneSkeletalMeshReference
				? FFBXManager::LoadSkeletalMeshFromFbxSceneReference(SkeletalMeshPath)
				: nullptr;
			if (!Loaded && !bFbxSceneSkeletalMeshReference)
			{
				Loaded = FFBXManager::LoadSkeletalMesh(SkeletalMeshPath);
			}
			SetSkeletalMesh(Loaded);
		}
		CacheLocalBounds();
		MarkWorldBoundsDirty();
	}
}

void USkinnedMeshComponent::CacheLocalBounds()
{
	bHasValidBounds = false;
	if (!SkeletalMesh) return;

	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset || Asset->Vertices.empty()) return;

	if (!Asset->bBoundsValid)
	{
		Asset->CacheBounds();
	}

	CachedLocalCenter = Asset->BoundsCenter;
	CachedLocalExtent = Asset->BoundsExtent;
	bHasValidBounds = Asset->bBoundsValid;
}

void USkinnedMeshComponent::EnsureRuntimeResources()
{
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset() || SkinnedVertices.empty())
	{
		return;
	}

	ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
	ID3D11DeviceContext* Context = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext() : nullptr;
	if (!Device || !Context)
	{
		return;
	}

	const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!RuntimeMeshBuffer.IsValid())
	{
		RuntimeMeshBuffer.CreateDynamic<FVertexPNCTT>(
			Device,
			static_cast<uint32>(SkinnedVertices.size()),
			Asset->Indices);
	}

	UploadSkinnedVertices();
}

void USkinnedMeshComponent::BuildBindPoseRenderVertices()
{
	SkinnedVertices.clear();
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	SkinnedVertices.reserve(Asset->Vertices.size());
	for (const FSkeletalVertex& RawVert : Asset->Vertices)
	{
		FVertexPNCTT RenderVert;
		RenderVert.Position = RawVert.pos;
		RenderVert.Normal = RawVert.normal;
		RenderVert.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		RenderVert.UV = RawVert.tex;
		RenderVert.Tangent = RawVert.tangent;
		SkinnedVertices.push_back(RenderVert);
	}
}

void USkinnedMeshComponent::UploadSkinnedVertices()
{
	ID3D11DeviceContext* Context = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext() : nullptr;
	if (Context)
	{
		RuntimeMeshBuffer.UpdateVertices(Context, SkinnedVertices);
	}
}

void USkinnedMeshComponent::BuildReferencePoseMatrices()
{
	ReferenceBoneMatrices.clear();
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	const TArray<FBoneInfo>& Bones = SkeletalMesh->GetSkeletalMeshAsset()->Bones;
	ReferenceBoneMatrices.resize(Bones.size(), FMatrix::Identity);
	for (int32 i = 0; i < static_cast<int32>(Bones.size()); ++i)
	{
		const int32 ParentIndex = Bones[i].ParentIndex;
		ReferenceBoneMatrices[i] = (ParentIndex >= 0 && ParentIndex < i)
			? Bones[i].LocalBindPose * ReferenceBoneMatrices[ParentIndex]
			: Bones[i].LocalBindPose;
	}
}

void USkinnedMeshComponent::SkinVerticesToReferencePose()
{
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (SkinnedVertices.size() != Asset->Vertices.size())
	{
		BuildBindPoseRenderVertices();
	}

	for (int32 VertexIndex = 0; VertexIndex < static_cast<int32>(Asset->Vertices.size()); ++VertexIndex)
	{
		const FSkeletalVertex& Source = Asset->Vertices[VertexIndex];
		FVector SkinnedPos(0.0f, 0.0f, 0.0f);
		FVector SkinnedNormal(0.0f, 0.0f, 0.0f);
		float TotalWeight = 0.0f;

		for (int32 Influence = 0; Influence < 4; ++Influence)
		{
			const float Weight = Source.BoneWeights[Influence];
			const uint32 BoneIndex = Source.BoneIDs[Influence];
			if (Weight <= 0.0f || BoneIndex >= ReferenceBoneMatrices.size())
			{
				continue;
			}

			const FMatrix SkinMatrix =
				Asset->Bones[BoneIndex].InverseBindPose * ReferenceBoneMatrices[BoneIndex];
			SkinnedPos += SkinMatrix.TransformPositionWithW(Source.pos) * Weight;
			SkinnedNormal += SkinMatrix.TransformVector(Source.normal) * Weight;
			TotalWeight += Weight;
		}

		FVertexPNCTT& Dest = SkinnedVertices[VertexIndex];
		if (TotalWeight > 0.0f)
		{
			Dest.Position = SkinnedPos;
			Dest.Normal = SkinnedNormal;
			Dest.Normal.Normalize();
		}
		else
		{
			Dest.Position = Source.pos;
			Dest.Normal = Source.normal;
		}
		Dest.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		Dest.UV = Source.tex;
		Dest.Tangent = Source.tangent;
	}
}
