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
 */
struct FSkeletalMesh
{
	FString PathFileName;

	// CPU Skinning의 소스가 되는 원본 정점 데이터
	TArray<FSkeletalMeshVertex> Vertices;
	TArray<uint32> Indices;

	// 스켈레톤 본 정보
	TArray<FBone> Bones;

	// 머티리얼 슬롯별 섹션 정보
	TArray<FStaticMeshSection> Sections;

	// 정적 리소스 버퍼 (IB는 여기서 공유, VB는 T-Pose 프리뷰용)
	std::unique_ptr<FMeshBuffer> RenderBuffer;

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
		
		// 1. Vertices 수동 직렬화
		uint32 VCount = (uint32)Vertices.size();
		Ar << VCount;
		if (Ar.IsLoading()) Vertices.resize(VCount);
		for (auto& V : Vertices)
		{
			Ar.Serialize(&V.Position, sizeof(FVector));
			Ar.Serialize(&V.Normal, sizeof(FVector));
			Ar.Serialize(&V.Tangent, sizeof(FVector4));
			Ar.Serialize(&V.Color, sizeof(FVector4));
			Ar.Serialize(&V.UV, sizeof(FVector2));
			
			for (int i = 0; i < 4; ++i) Ar << V.boneIndices[i];
			for (int i = 0; i < 4; ++i) Ar << V.boneWeights[i];
		}

		// 2. Indices 직렬화 (uint32는 OK)
		Ar << Indices;

		// 3. Bones 수동 직렬화
		uint32 BCount = (uint32)Bones.size();
		Ar << BCount;
		if (Ar.IsLoading()) Bones.resize(BCount);
		for (auto& B : Bones)
		{
			Ar << B.ParentIndex;
			Ar.Serialize(&B.Scale, sizeof(FVector));
			Ar.Serialize(&B.Rotation, sizeof(FQuat));
			Ar.Serialize(&B.Translation, sizeof(FVector));
			Ar.Serialize(B.InverseBindMatrix.Data, sizeof(float) * 16);
		}

		// 4. Sections 직렬화
		Ar << Sections;
	}
};
