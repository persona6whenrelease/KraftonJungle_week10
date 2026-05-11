#pragma once

#include "Core/CoreTypes.h"
#include "Mesh/SkeletalMeshAsset.h"

struct FFbxMeshPartSection
{
	int32 SourceMeshId = -1;
	int32 MaterialSlotIndex = 0;
	int32 SourceMaterialId = -1;
	FString MaterialSlotName = "None";
	int32 FirstIndex = 0;
	int32 IndexCount = 0;
};

struct FFbxSkinnedMeshPart
{
	int32 MeshId = -1;
	int32 SkinId = -1;
	int32 SkeletonId = -1;
	int32 AttachedBoneId = -1;
	int32 AttachedSkeletonBoneIndex = -1;
	bool bRigidAttached = false;
	bool bSkinned = false;
	FString SourceNodePath;
	TArray<FSkeletalVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FFbxMeshPartSection> Sections;
};
