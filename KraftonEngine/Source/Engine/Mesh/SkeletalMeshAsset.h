#pragma once

#include "Core/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Mesh/MeshCommonTypes.h"
#include "Serialization/Archive.h"

#include <algorithm>

struct FSkeletalVertex
{
	FVector pos;
	FVector normal;
	FVector2 tex;
	FVector4 tangent;
	uint32 BoneIDs[4] = { 0, 0, 0, 0 };
	float BoneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct FBoneInfo
{
	FString Name;
	int32 ParentIndex = -1;
	FMatrix LocalBindPose = FMatrix::Identity;
	FMatrix InverseBindPose = FMatrix::Identity;

	friend FArchive& operator<<(FArchive& Ar, FBoneInfo& Bone)
	{
		Ar << Bone.Name;
		Ar << Bone.ParentIndex;
		Ar.Serialize(&Bone.LocalBindPose, sizeof(FMatrix));
		Ar.Serialize(&Bone.InverseBindPose, sizeof(FMatrix));
		return Ar;
	}
};

struct FSkeletalMesh
{
	FString PathFileName;
	TArray<FSkeletalVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FMeshSection> Sections;
	TArray<FBoneInfo> Bones;

	FVector BoundsCenter = FVector(0, 0, 0);
	FVector BoundsExtent = FVector(0, 0, 0);
	bool bBoundsValid = false;

	void CacheBounds()
	{
		bBoundsValid = false;
		if (Vertices.empty()) return;

		FVector LocalMin = Vertices[0].pos;
		FVector LocalMax = Vertices[0].pos;
		for (const FSkeletalVertex& V : Vertices)
		{
			LocalMin.X = (std::min)(LocalMin.X, V.pos.X);
			LocalMin.Y = (std::min)(LocalMin.Y, V.pos.Y);
			LocalMin.Z = (std::min)(LocalMin.Z, V.pos.Z);
			LocalMax.X = (std::max)(LocalMax.X, V.pos.X);
			LocalMax.Y = (std::max)(LocalMax.Y, V.pos.Y);
			LocalMax.Z = (std::max)(LocalMax.Z, V.pos.Z);
		}

		BoundsCenter = (LocalMin + LocalMax) * 0.5f;
		BoundsExtent = (LocalMax - LocalMin) * 0.5f;
		bBoundsValid = true;
	}

	void Serialize(FArchive& Ar)
	{
		Ar << PathFileName;
		Ar << Vertices;
		Ar << Indices;
		Ar << Sections;
		Ar << Bones;
	}
};
