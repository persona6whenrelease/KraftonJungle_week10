#pragma once

#include "StaticMeshAsset.h"
#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"

// 한 정점이 최대 몇 개의 본 영향을 받을지 정한다.
constexpr int32 MaxBoneInfluences = 4;

// Skeletal Mesh용 정점 데이터.
struct FSkeletalVertex
{
	FVector pos;
	FVector normal;
	FVector4 color; // Color 필요한가..?
	FVector2 tex;
	FVector4 tangent;

	uint32 BoneIndices[MaxBoneInfluences] = {};
	float BoneWeights[MaxBoneInfluences] = {};
};

// 본 하나가 가지는 정보.
// 본은 혼자 존재하지 않고, 부모-자식 계층으로 움직인다.
struct FBoneInfo
{
	FString Name;

	// -1이면 Root 본
	int32 ParentIndex = -1;

	FMatrix LocalBindPose = FMatrix::Identity;
	FMatrix GlobalBindPose = FMatrix::Identity;
	FMatrix InverseBindPose = FMatrix::Identity;
};

struct FFbxBoneInfluence
{
	int32 BoneIndex = -1;
	float Weight = 0.0f;
};

// FBX에서 읽어온 Skeletal Mesh 원본 데이터.
struct FSkeletalMesh
{
	FString PathFileName;

	TArray<FSkeletalVertex> Vertices;
	TArray<uint32> Indices;

	// Static Mesh와 똑같이 섹션/머티리얼 단위로 나눠 그리기 위해 재사용.
	TArray<FStaticMeshSection> Sections; // 음...?

	TArray<FBoneInfo> Bones;
};
