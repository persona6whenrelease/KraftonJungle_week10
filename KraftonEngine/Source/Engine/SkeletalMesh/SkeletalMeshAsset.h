#pragma once

#include "Core/CoreTypes.h"
#include "Render/Types/VertexTypes.h"
#include "Render/Resource/Buffer.h"
#include "Serialization/Archive.h"
#include "Mesh/StaticMeshAsset.h" // For FStaticMeshSection
#include <memory>

/**
 * FSkeletalMesh
 * 실제 기하 데이터와 본 정보를 담고 있는 데이터 컨테이너.
 * 기존 FStaticMesh와 유사한 구조를 가지며, 스키닝을 위한 본 데이터가 추가됨.
 */
struct FSkeletalMesh
{
	FString PathFileName;

	// CPU Skinning의 소스가 되는 원본 정점 데이터
	TArray<FSkeletalMeshVertex> Vertices;
	TArray<uint32> Indices;

	// 스켈레톤 본 정보 (Hierarchy 및 InverseBindMatrix 포함)
	TArray<FBone> Bones;

	// 머티리얼 슬롯별 섹션 정보
	TArray<FStaticMeshSection> Sections;

	// 정적 리소스 버퍼 (IB는 여기서 공유, VB는 T-Pose 프리뷰용)
	std::unique_ptr<FMeshBuffer> RenderBuffer;

	// 메시 로컬 바운드
	FVector BoundsCenter = FVector(0, 0, 0);
	FVector BoundsExtent = FVector(0, 0, 0);
	bool    bBoundsValid = false;

	void CacheBounds()
	{
		bBoundsValid = false;
		if (Vertices.empty()) return;

		FVector LocalMin = Vertices[0].Position;
		FVector LocalMax = Vertices[0].Position;
		for (const FSkeletalMeshVertex& V : Vertices)
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
		Ar << Vertices;
		Ar << Indices;
		Ar << Bones;
		Ar << Sections;
		
		// RenderBuffer는 런타임에 InitResources를 통해 생성되므로 직렬화하지 않음.
	}
};
