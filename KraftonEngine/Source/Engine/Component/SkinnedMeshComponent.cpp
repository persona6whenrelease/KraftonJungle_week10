#include "Component/SkinnedMeshComponent.h"

#include "Mesh/SkeletalMesh.h"
#include "Object/ObjectFactory.h"
#include "Core/Log.h"
#include "Math/MathUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>

IMPLEMENT_CLASS(USkinnedMeshComponent, UMeshComponent)

HIDE_FROM_COMPONENT_LIST(USkinnedMeshComponent)

namespace
{
	constexpr bool bEnableDebugShoulderPose = true;
	constexpr float DebugShoulderMaxRotationDegrees = 65.0f;
	constexpr float DebugShoulderPhaseStep = 0.05f;

	FString ToLowerCopy(FString Value)
	{
		std::transform(
			Value.begin(),
			Value.end(),
			Value.begin(),
			[](unsigned char Ch)
			{
				return static_cast<char>(std::tolower(Ch));
			}
		);
		return Value;
	}

	bool ContainsText(const FString& Text, const char* Needle)
	{
		return Text.find(Needle) != FString::npos;
	}

	bool IsDebugShoulderBoneName(const FString& BoneName)
	{
		const FString LowerName = ToLowerCopy(BoneName);

		if (ContainsText(LowerName, "leftshoulder") ||
			ContainsText(LowerName, "shoulder_l") ||
			ContainsText(LowerName, "l_shoulder") ||
			ContainsText(LowerName, "leftclavicle") ||
			ContainsText(LowerName, "clavicle_l") ||
			ContainsText(LowerName, "leftupperarm") ||
			ContainsText(LowerName, "upperarm_l") ||
			ContainsText(LowerName, "leftarm"))
		{
			return true;
		}

		const bool bLooksLikeShoulder = ContainsText(LowerName, "shoulder");
		const bool bLooksLikeArm =
			ContainsText(LowerName, "arm") &&
			!ContainsText(LowerName, "forearm") &&
			!ContainsText(LowerName, "hand") &&
			!ContainsText(LowerName, "finger");

		return bLooksLikeShoulder || bLooksLikeArm;
	}
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	if (SkeletalMesh == InMesh)
	{
		return;
	}

	SkeletalMesh = InMesh;

	InitializeSkinningBuffers();
	CacheLocalBounds();

	MarkRenderStateDirty();
	MarkWorldBoundsDirty();
}

FMeshBuffer* USkinnedMeshComponent::GetMeshBuffer() const
{
	return nullptr;
}

FMeshDataView USkinnedMeshComponent::GetMeshDataView() const
{
	if (SkinnedVertices.empty())
	{
		return {};
	}

	FMeshDataView View;
	View.VertexData = SkinnedVertices.data();
	View.VertexCount = static_cast<uint32>(SkinnedVertices.size());
	View.Stride = sizeof(FNormalVertex);
	View.IndexData = SkinnedIndices.data();
	View.IndexCount = static_cast<uint32>(SkinnedIndices.size());

	return View;
}

void USkinnedMeshComponent::UpdateWorldAABB() const
{
	if (!bHasValidBounds)
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	const FMatrix WorldMatrix = GetWorldMatrix();

	const FVector WorldCenter =
		WorldMatrix.TransformPositionWithW(CachedLocalCenter);

	const float Ex =
		std::abs(WorldMatrix.M[0][0]) * CachedLocalExtent.X +
		std::abs(WorldMatrix.M[1][0]) * CachedLocalExtent.Y +
		std::abs(WorldMatrix.M[2][0]) * CachedLocalExtent.Z;

	const float Ey =
		std::abs(WorldMatrix.M[0][1]) * CachedLocalExtent.X +
		std::abs(WorldMatrix.M[1][1]) * CachedLocalExtent.Y +
		std::abs(WorldMatrix.M[2][1]) * CachedLocalExtent.Z;

	const float Ez =
		std::abs(WorldMatrix.M[0][2]) * CachedLocalExtent.X +
		std::abs(WorldMatrix.M[1][2]) * CachedLocalExtent.Y +
		std::abs(WorldMatrix.M[2][2]) * CachedLocalExtent.Z;

	WorldAABBMinLocation = WorldCenter - FVector(Ex, Ey, Ez);
	WorldAABBMaxLocation = WorldCenter + FVector(Ex, Ey, Ez);

	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
}

void USkinnedMeshComponent::InitDynamicResources(ID3D11Device* InDevice)
{
	if (!InDevice)
	{
		return;
	}

	if (!SkinnedVertices.empty())
	{
		DynamicVertexBuffer.Create(
			InDevice,
			static_cast<uint32>(SkinnedVertices.size()),
			sizeof(FNormalVertex)
		);
	}

	if (!SkinnedIndices.empty())
	{
		DynamicIndexBuffer.Create(
			InDevice,
			static_cast<uint32>(SkinnedIndices.size())
		);
	}
}

void USkinnedMeshComponent::UploadSkinnedMeshToGPU(
	ID3D11Device* InDevice,
	ID3D11DeviceContext* InContext
)
{
	if (!InDevice || !InContext)
	{
		return;
	}

	if (SkinnedVertices.empty() || SkinnedIndices.empty())
	{
		return;
	}

	const uint32 VertexCount =
		static_cast<uint32>(SkinnedVertices.size());

	const uint32 IndexCount =
		static_cast<uint32>(SkinnedIndices.size());

	if (!DynamicVertexBuffer.GetBuffer() ||
		DynamicVertexBuffer.GetStride() != sizeof(FNormalVertex))
	{
		DynamicVertexBuffer.Create(
			InDevice,
			VertexCount,
			sizeof(FNormalVertex)
		);
	}

	if (!DynamicIndexBuffer.GetBuffer())
	{
		DynamicIndexBuffer.Create(InDevice, IndexCount);
	}

	DynamicVertexBuffer.EnsureCapacity(InDevice, VertexCount);
	DynamicIndexBuffer.EnsureCapacity(InDevice, IndexCount);

	DynamicVertexBuffer.Update(
		InContext,
		SkinnedVertices.data(),
		VertexCount
	);

	DynamicIndexBuffer.Update(
		InContext,
		SkinnedIndices.data(),
		IndexCount
	);
}


USkeletalMesh* USkinnedMeshComponent::GetSkeletalMesh() const
{
	return SkeletalMesh;
}

void USkinnedMeshComponent::InitializeSkinningBuffers()
{
	CurrentBoneLocalTransforms.clear();
	CurrentBoneWorldTransforms.clear();
	SkinningMatrices.clear();
	SkinnedVertices.clear();
	SkinnedIndices.clear();

	if (!SkeletalMesh)
	{
		return;
	}

	FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();

	if (!MeshAsset)
	{
		return;
	}

	const size_t BoneCount = MeshAsset->Bones.size();
	const size_t VertexCount = MeshAsset->Vertices.size();
	const size_t IndexCount = MeshAsset->Indices.size();

	CurrentBoneLocalTransforms.resize(BoneCount, FMatrix::Identity);
	CurrentBoneWorldTransforms.resize(BoneCount, FMatrix::Identity);
	SkinningMatrices.resize(BoneCount, FMatrix::Identity);

	SkinnedVertices.resize(VertexCount);
	SkinnedIndices = MeshAsset->Indices;

	ResetPoseToBindPose();
	ApplyDebugShoulderPose();
	BuildCurrentBoneWorldTransforms();
	BuildSkinningMatrices();
	SkinVerticesCPU();
}

void USkinnedMeshComponent::ResetPoseToBindPose()
{
	if (!SkeletalMesh)
	{
		return;
	}

	FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();

	if (!MeshAsset)
	{
		return;
	}

	const size_t BoneCount = MeshAsset->Bones.size();

	if (CurrentBoneLocalTransforms.size() != BoneCount)
	{
		CurrentBoneLocalTransforms.resize(BoneCount, FMatrix::Identity);
	}

	for (size_t BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		CurrentBoneLocalTransforms[BoneIndex] =
			MeshAsset->Bones[BoneIndex].LocalBindPose;
	}
}

void USkinnedMeshComponent::ApplyDebugShoulderPose()
{
	if (!bEnableDebugShoulderPose || !SkeletalMesh)
	{
		return;
	}

	FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();

	if (!MeshAsset)
	{
		return;
	}

	const size_t BoneCount = MeshAsset->Bones.size();

	if (CurrentBoneLocalTransforms.size() != BoneCount)
	{
		return;
	}

	for (size_t BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FBoneInfo& Bone = MeshAsset->Bones[BoneIndex];

		if (!IsDebugShoulderBoneName(Bone.Name))
		{
			continue;
		}

		static float DebugShoulderPhase = 0.0f;
		DebugShoulderPhase += DebugShoulderPhaseStep;
		if (DebugShoulderPhase > FMath::Pi * 2.0f)
		{
			DebugShoulderPhase -= FMath::Pi * 2.0f;
		}

		const float DebugRotationDegrees =
			std::sinf(DebugShoulderPhase) * DebugShoulderMaxRotationDegrees;

		const FMatrix DebugRotation =
			FMatrix::MakeRotationZ(DebugRotationDegrees * FMath::DegToRad);

		CurrentBoneLocalTransforms[BoneIndex] =
			DebugRotation * CurrentBoneLocalTransforms[BoneIndex];

		static bool bLoggedDebugShoulderPose = false;
		if (!bLoggedDebugShoulderPose)
		{
			bLoggedDebugShoulderPose = true;
			UE_LOG(
				"[SkinnedMeshComponent] Debug shoulder pose. Bone=%s, Axis=Z, MaxRotationDegrees=%.1f",
				Bone.Name.c_str(),
				DebugShoulderMaxRotationDegrees
			);
		}

		return;
	}

	static bool bLoggedMissingDebugShoulderBone = false;
	if (!bLoggedMissingDebugShoulderBone)
	{
		bLoggedMissingDebugShoulderBone = true;
		UE_LOG(
			"[SkinnedMeshComponent] Debug shoulder pose target not found. BoneCount=%zu",
			BoneCount
		);

		const size_t LogCount = std::min<size_t>(BoneCount, 32);
		for (size_t BoneIndex = 0; BoneIndex < LogCount; ++BoneIndex)
		{
			const FBoneInfo& Bone = MeshAsset->Bones[BoneIndex];
			UE_LOG(
				"[SkinnedMeshComponent] Bone[%zu]=%s, Parent=%d",
				BoneIndex,
				Bone.Name.c_str(),
				Bone.ParentIndex
			);
		}
	}
}


void USkinnedMeshComponent::BuildCurrentBoneWorldTransforms()
{
	if (!SkeletalMesh)
	{
		return;
	}

	FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();

	if (!MeshAsset)
	{
		return;
	}

	const size_t BoneCount = MeshAsset->Bones.size();

	if (CurrentBoneWorldTransforms.size() != BoneCount)
	{
		CurrentBoneWorldTransforms.resize(BoneCount, FMatrix::Identity);
	}

	if (CurrentBoneLocalTransforms.size() != BoneCount)
	{
		CurrentBoneLocalTransforms.resize(BoneCount, FMatrix::Identity);
	}

	for (size_t BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FBoneInfo& Bone = MeshAsset->Bones[BoneIndex];

		if (Bone.ParentIndex >= 0 &&
			Bone.ParentIndex < static_cast<int32>(BoneCount))
		{
			CurrentBoneWorldTransforms[BoneIndex] =
				CurrentBoneLocalTransforms[BoneIndex] *
				CurrentBoneWorldTransforms[Bone.ParentIndex];
		}
		else
		{
			CurrentBoneWorldTransforms[BoneIndex] =
				CurrentBoneLocalTransforms[BoneIndex];
		}
	}
}


void USkinnedMeshComponent::BuildSkinningMatrices()
{
	if (!SkeletalMesh)
	{
		return;
	}

	FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();

	if (!MeshAsset)
	{
		return;
	}

	const size_t BoneCount = MeshAsset->Bones.size();

	if (SkinningMatrices.size() != BoneCount)
	{
		SkinningMatrices.resize(BoneCount, FMatrix::Identity);
	}

	if (CurrentBoneWorldTransforms.size() != BoneCount)
	{
		CurrentBoneWorldTransforms.resize(BoneCount, FMatrix::Identity);
	}

	for (size_t BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FBoneInfo& Bone = MeshAsset->Bones[BoneIndex];

		SkinningMatrices[BoneIndex] =
			Bone.InverseBindPose *
			CurrentBoneWorldTransforms[BoneIndex];
	}
}

static FVector SkinVertexPositionCPU(
	const FSkeletalVertex& SourceVertex,
	const TArray<FMatrix>& SkinningMatrices
)
{
	FVector SkinnedPosition(0.0f, 0.0f, 0.0f);

	for (int32 InfluenceIndex = 0; InfluenceIndex < MaxBoneInfluences; ++InfluenceIndex)
	{
		const float Weight = SourceVertex.BoneWeights[InfluenceIndex];

		if (Weight <= 0.0f)
		{
			continue;
		}

		const uint32 BoneIndex = SourceVertex.BoneIndices[InfluenceIndex];

		if (BoneIndex >= SkinningMatrices.size())
		{
			continue;
		}

		const FMatrix& SkinningMatrix = SkinningMatrices[BoneIndex];

		const FVector TransformedPosition =
			SkinningMatrix.TransformPositionWithW(SourceVertex.pos);

		SkinnedPosition += TransformedPosition * Weight;
	}

	return SkinnedPosition;
}

static FVector SkinVertexNormalCPU(
	const FSkeletalVertex& SourceVertex,
	const TArray<FMatrix>& SkinningMatrices
)
{
	FVector SkinnedNormal(0.0f, 0.0f, 0.0f);

	for (int32 InfluenceIndex = 0; InfluenceIndex < MaxBoneInfluences; ++InfluenceIndex)
	{
		const float Weight = SourceVertex.BoneWeights[InfluenceIndex];

		if (Weight <= 0.0f)
		{
			continue;
		}

		const uint32 BoneIndex = SourceVertex.BoneIndices[InfluenceIndex];

		if (BoneIndex >= SkinningMatrices.size())
		{
			continue;
		}

		const FMatrix& SkinningMatrix = SkinningMatrices[BoneIndex];

		const FVector TransformedNormal =
			SkinningMatrix.TransformVector(SourceVertex.normal);

		SkinnedNormal += TransformedNormal * Weight;
	}

	if (SkinnedNormal.Length() > 0.0001f)
	{
		SkinnedNormal.Normalize();
	}
	else
	{
		SkinnedNormal = FVector(0.0f, 0.0f, 1.0f);
	}

	return SkinnedNormal;
}

void USkinnedMeshComponent::SkinVerticesCPU()
{
	SkinnedVertices.clear();

	if (!SkeletalMesh)
	{
		return;
	}

	FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();

	if (!MeshAsset)
	{
		return;
	}

	const size_t VertexCount = MeshAsset->Vertices.size();

	SkinnedVertices.resize(VertexCount);

	for (size_t VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const FSkeletalVertex& SourceVertex = MeshAsset->Vertices[VertexIndex];

		FNormalVertex& TargetVertex = SkinnedVertices[VertexIndex];

		TargetVertex.pos =
			SkinVertexPositionCPU(SourceVertex, SkinningMatrices);

		TargetVertex.normal =
			SkinVertexNormalCPU(SourceVertex, SkinningMatrices);

		TargetVertex.color = SourceVertex.color;
		TargetVertex.tex = SourceVertex.tex;
		TargetVertex.tangent = SourceVertex.tangent;
	}
}

void USkinnedMeshComponent::CacheLocalBounds()
{
	bHasValidBounds = false;
	CachedLocalCenter = FVector(0.0f, 0.0f, 0.0f);
	CachedLocalExtent = FVector(0.5f, 0.5f, 0.5f);
	LocalExtents = CachedLocalExtent;

	if (!SkeletalMesh)
	{
		return;
	}

	FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();

	const bool bUseSkinnedVertices = !SkinnedVertices.empty();
	if (!MeshAsset || (!bUseSkinnedVertices && MeshAsset->Vertices.empty()))
	{
		return;
	}

	const FVector FirstPosition = bUseSkinnedVertices
		? SkinnedVertices[0].pos
		: MeshAsset->Vertices[0].pos;

	FVector Min = FirstPosition;
	FVector Max = FirstPosition;

	auto IncludePosition = [&Min, &Max](const FVector& Position)
	{
		Min.X = std::min(Min.X, Position.X);
		Min.Y = std::min(Min.Y, Position.Y);
		Min.Z = std::min(Min.Z, Position.Z);

		Max.X = std::max(Max.X, Position.X);
		Max.Y = std::max(Max.Y, Position.Y);
		Max.Z = std::max(Max.Z, Position.Z);
	};

	if (bUseSkinnedVertices)
	{
		for (const FNormalVertex& Vertex : SkinnedVertices)
		{
			IncludePosition(Vertex.pos);
		}
	}
	else
	{
		for (const FSkeletalVertex& Vertex : MeshAsset->Vertices)
		{
			IncludePosition(Vertex.pos);
		}
	}

	CachedLocalCenter = (Min + Max) * 0.5f;
	CachedLocalExtent = (Max - Min) * 0.5f;

	LocalExtents = CachedLocalExtent;
	bHasValidBounds = true;

	UE_LOG(
		"[SkinnedMeshComponent] Bounds. Center=(%.3f, %.3f, %.3f), Extent=(%.3f, %.3f, %.3f)",
		CachedLocalCenter.X,
		CachedLocalCenter.Y,
		CachedLocalCenter.Z,
		CachedLocalExtent.X,
		CachedLocalExtent.Y,
		CachedLocalExtent.Z
	);
}

void USkinnedMeshComponent::RefreshSkinning()
{
	if (!SkeletalMesh)
	{
		return;
	}

	ResetPoseToBindPose();
	ApplyDebugShoulderPose();
	BuildCurrentBoneWorldTransforms();
	BuildSkinningMatrices();
	SkinVerticesCPU();
}
