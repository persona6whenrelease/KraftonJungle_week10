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

// Baked per-frame local transform sample.
struct FBoneAnimSample
{
	FMatrix LocalMatrix = FMatrix::Identity;
};

// One bone's track across all frames of a clip. Samples.size() == FAnimationClip::FrameCount.
struct FBoneAnimTrack
{
	int32 BoneIndex = -1;
	TArray<FBoneAnimSample> Samples;

	friend FArchive& operator<<(FArchive& Ar, FBoneAnimTrack& Track)
	{
		Ar << Track.BoneIndex;
		// FBoneAnimSample is bitwise-copyable (FMatrix is a POD of floats even though it has user
		// constructors), so serialize the whole block as raw bytes instead of going through the
		// generic TArray<T> path, which would static_assert on is_trivially_copyable.
		uint32 SampleCount = static_cast<uint32>(Track.Samples.size());
		Ar << SampleCount;
		if (Ar.IsLoading()) Track.Samples.resize(SampleCount);
		if (SampleCount > 0)
		{
			Ar.Serialize(Track.Samples.data(), SampleCount * sizeof(FBoneAnimSample));
		}
		return Ar;
	}
};

struct FAnimationClip
{
	FString Name;
	float Duration = 0.0f;
	float FrameRate = 30.0f;
	int32 FrameCount = 0;
	TArray<FBoneAnimTrack> Tracks;

	friend FArchive& operator<<(FArchive& Ar, FAnimationClip& Clip)
	{
		Ar << Clip.Name;
		Ar << Clip.Duration;
		Ar << Clip.FrameRate;
		Ar << Clip.FrameCount;
		Ar << Clip.Tracks;
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
	TArray<FAnimationClip> AnimationClips;

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
		Ar << AnimationClips;
	}
};
