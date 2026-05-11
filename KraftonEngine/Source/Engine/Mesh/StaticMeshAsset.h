#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Engine/Object/Object.h"
#include "Mesh/MeshCommonTypes.h"
#include "Render/Resource/Buffer.h"
#include "Serialization/Archive.h"
#include <memory>
#include <algorithm>
// Cooked Data 내부용 정점
struct FNormalVertex
{
	FVector pos;
	FVector normal;
	FVector4 color;
	FVector2 tex;
	FVector4 tangent;
};


// Cooked Data — GPU용 정점/인덱스
// FStaticMeshLODResources in UE5
struct FStaticMesh
{
	FString PathFileName;
	TArray<FNormalVertex> Vertices;
	TArray<uint32> Indices;

	TArray<FStaticMeshSection> Sections;

	std::unique_ptr<FMeshBuffer> RenderBuffer;

	// 메시 로컬 바운드 캐시 (정점 순회 1회로 계산)
	FVector BoundsCenter = FVector(0, 0, 0);
	FVector BoundsExtent = FVector(0, 0, 0);
	bool    bBoundsValid = false;

	void CacheBounds()
	{
		bBoundsValid = false;
		if (Vertices.empty()) return;

		FVector LocalMin = Vertices[0].pos;
		FVector LocalMax = Vertices[0].pos;
		for (const FNormalVertex& V : Vertices)
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
	}
};
