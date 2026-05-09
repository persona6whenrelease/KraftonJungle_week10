#pragma once
#include "Object/Object.h"

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Render/Resource/Buffer.h"
#include "Serialization/Archive.h"
#include "Engine/Object/FName.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include <memory>
#include <algorithm>

// Cooked Data 내부용 정점
//최종 파싱 데이터 + Tarray로 감싸주기
struct FSkeletalSourceVertex
{
	FVector Position;      // bind pose position
	FVector Normal;        // bind pose normal
	FVector4 Tangent;
	FVector2 UV;
	FVector4 Color;
	int32 BoneIndices[4];
	float BoneWeights[4];
};
struct FBoneInfo
{
	FString Name;
	int32 ParentIndex;
	FMatrix BindPoseGlobal;
	FMatrix InverseBindPose;


};

inline FArchive& operator<<(FArchive& Ar, FBoneInfo& Bone)
{
	Ar << Bone.Name;
	Ar << Bone.ParentIndex;

	Ar.Serialize(Bone.BindPoseGlobal.Data, sizeof(Bone.BindPoseGlobal.Data));
	Ar.Serialize(Bone.InverseBindPose.Data, sizeof(Bone.InverseBindPose.Data));

	return Ar;
}


struct FSkeletalMeshSection
{
	int32 MaterialIndex = -1; // Index into UStaticMesh's FStaticMaterial array. Cached to avoid per-frame string comparison.
	FString MaterialSlotName;
	uint32 FirstIndex;
	uint32 NumTriangles;

	friend FArchive& operator<<(FArchive& Ar, FSkeletalMeshSection& Section)
	{
		Ar << Section.MaterialSlotName << Section.FirstIndex << Section.NumTriangles;
		return Ar;
	}
};

struct FSkeletalMeshAsset
{
	TArray<FSkeletalSourceVertex> SourceVertices;
	TArray<uint32> Indices;
	TArray<FSkeletalMeshSection> Sections;
	TArray<FBoneInfo> Bones;
};
inline FArchive& operator<<(FArchive& Ar, FSkeletalMeshAsset& Asset)
{
	Ar << Asset.SourceVertices;
	Ar << Asset.Indices;
	Ar << Asset.Sections;
	Ar << Asset.Bones;
	return Ar;
}
// Cooked Data — GPU용 정점/인덱스
struct FStkeletalMesh
{
	FString PathFileName;

	FSkeletalMeshAsset MeshAsset;

	//std::unique_ptr<FSkinnedMeshBuffer> RenderBuffer; -> 프록시로 이동

	// 메시 로컬 바운드 캐시 (정점 순회 1회로 계산)
	FVector BoundsCenter = FVector(0, 0, 0);
	FVector BoundsExtent = FVector(0, 0, 0);
	bool    bBoundsValid = false;

	void CacheBounds()
	{
		bBoundsValid = false;
		if (MeshAsset.SourceVertices.empty()) return;

		FVector LocalMin = MeshAsset.SourceVertices[0].Position;
		FVector LocalMax = MeshAsset.SourceVertices[0].Position;
		for (const FSkeletalSourceVertex& V : MeshAsset.SourceVertices)
		{
			LocalMin.X = (std::min)(LocalMin.X, V.Position.X);
			LocalMin.Y = (std::min)(LocalMin.Y, V.Position.Y);
			LocalMin.Z = (std::min)(LocalMin.Z, V.Position.Z);
			LocalMax.X = (std::max)(LocalMax.X, V.Position.X);
			LocalMax.Y = (std::max)(LocalMax.Y, V.Position.Y);
			LocalMax.Z = (std::max)(LocalMax.Z, V.Position.Z);
		}

		BoundsCenter = (LocalMin + LocalMax) * 0.5f;
		BoundsExtent = (LocalMax - LocalMin) * 0.5f;
		bBoundsValid = true;
	}

	void Serialize(FArchive& Ar)
	{
		Ar << PathFileName;
		Ar << MeshAsset;
	}
};
