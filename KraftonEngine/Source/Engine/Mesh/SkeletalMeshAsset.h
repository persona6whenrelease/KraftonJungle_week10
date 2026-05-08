#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Render/Types/VertexTypes.h"
#include "Render/Resource/Buffer.h"
#include <memory>


struct FSkeletalMeshSection
{
    int32 MaterialIndex = -1;
    FString MaterialSlotName;
    uint32 FirstIndex;
    uint32 NumTriangles;
};

struct FSkeletonBone
{
    FString Name;
    int32 ParentIndex = -1;
    FMatrix InverseBindPose;
};

struct FSkeletalMesh
{
    FString PathFileName;
    TArray<FSkeletalMeshVertex> Vertices;
    TArray<uint32> Indices;
    TArray<FSkeletalMeshSection> Sections;
    
    TArray<FSkeletonBone> Bones;
	
	std::unique_ptr<FMeshBuffer> RenderBuffer;
    
    FVector BoundsCenter = FVector(0, 0, 0);
    FVector BoundsExtent = FVector(0, 0, 0);
};
