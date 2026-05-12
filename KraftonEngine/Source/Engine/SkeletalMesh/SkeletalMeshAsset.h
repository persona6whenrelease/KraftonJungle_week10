#pragma once

#include "Core/CoreTypes.h"
#include "Render/Types/VertexTypes.h"
#include "Render/Resource/Buffer.h"
#include "Serialization/Archive.h"
#include "Mesh/StaticMeshAsset.h" // For FStaticMeshSection, FNormalVertex
#include <memory>
#include <algorithm>

/**
 * FBoneCluster
 * 한 bone이 어떤 vertex들에 어떤 weight로 영향을 미치는지 보존하는 단위.
 * FBX의 FbxCluster와 1:1 대응한다.
 *
 * VertexIndices.size() == Weights.size() 가 항상 유지된다.
 */
struct FBoneCluster
{
	int32 BoneIndex = -1;
	TArray<uint32> VertexIndices;
	TArray<float>  Weights;
	FMatrix        InverseBindMatrix = FMatrix::Identity;

	friend FArchive& operator<<(FArchive& Ar, FBoneCluster& C)
	{
		Ar << C.BoneIndex;
		Ar << C.VertexIndices;
		Ar << C.Weights;
		Ar.Serialize(C.InverseBindMatrix.Data, sizeof(float) * 16);
		return Ar;
	}
};

/**
 * FSkeletalMesh
 * Skeletal mesh asset 본체.
 *
 * Cluster-기반 모델: vertex 자체에는 bone 정보가 없고,
 * Clusters 배열을 통해 bone-to-vertex 영향이 표현된다.
 * 어떤 cluster의 VertexIndices에도 등장하지 않는 vertex는 bind-pose에 그대로 통과한다.
 */
struct FSkeletalMesh
{
	// 직렬화 포맷 버전.
	//  v1 → v2: cluster 도입 (FSkeletalMeshVertex flatten 폐기).
	//  v2 → v3: FBX importer 의 머티리얼 슬롯 / texture 자동 매핑 로직 변경 — 기존 .bin 도 재import.
	//  v3 → v4: non-ASCII (한자/한글) FBX 텍스처 경로 매칭 encoding fix — 기존 캐시의
	//           .mat 가 텍스처 정보 없이 저장된 상태일 수 있으므로 강제 재import.
	//  v4 → v5: ConvertFbxMaterialToMat 의 Origin 검사 (SimpleJSON 파싱) 적용 후 .mat 자동
	//           갱신을 트리거하기 위한 추가 무효화.
	static constexpr uint32 SerializeVersion = 5;

	FString PathFileName;

	// CPU 스키닝의 소스가 되는 bind-pose 정점 (bone 정보 없음, StaticMesh와 동일 레이아웃)
	TArray<FNormalVertex> Vertices;
	TArray<uint32> Indices;

	// 스켈레톤 본 계층 (parent + SRT)
	TArray<FBone> Bones;

	// Bone-to-vertex binding (FbxCluster 1:1)
	TArray<FBoneCluster> Clusters;

	// 머티리얼 슬롯별 섹션 정보
	TArray<FStaticMeshSection> Sections;

	// 정적 리소스 버퍼 (IB는 여기서 공유, VB는 bind-pose 프리뷰용)
	std::unique_ptr<FMeshBuffer> RenderBuffer;

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

	// 직렬화 성공 여부 — 로드 시 버전 mismatch면 false.
	bool Serialize(FArchive& Ar)
	{
		uint32 Version = SerializeVersion;
		Ar << Version;
		if (Ar.IsLoading() && Version != SerializeVersion)
		{
			// 이전 캐시 포맷 — 호출자가 재import 하도록 신호.
			return false;
		}

		Ar << PathFileName;

		// 1. Vertices (FNormalVertex)
		uint32 VCount = (uint32)Vertices.size();
		Ar << VCount;
		if (Ar.IsLoading()) Vertices.resize(VCount);
		for (auto& V : Vertices)
		{
			Ar.Serialize(&V.pos,     sizeof(FVector));
			Ar.Serialize(&V.normal,  sizeof(FVector));
			Ar.Serialize(&V.color,   sizeof(FVector4));
			Ar.Serialize(&V.tex,     sizeof(FVector2));
			Ar.Serialize(&V.tangent, sizeof(FVector4));
		}

		// 2. Indices
		Ar << Indices;

		// 3. Bones (IBP 제거됨 — cluster로 이동)
		uint32 BCount = (uint32)Bones.size();
		Ar << BCount;
		if (Ar.IsLoading()) Bones.resize(BCount);
		for (auto& B : Bones)
		{
			Ar << B.ParentIndex;
			Ar.Serialize(&B.Scale,       sizeof(FVector));
			Ar.Serialize(&B.Rotation,    sizeof(FQuat));
			Ar.Serialize(&B.Translation, sizeof(FVector));
		}

		// 4. Clusters
		Ar << Clusters;

		// 5. Sections
		Ar << Sections;

		return true;
	}
};
