#pragma once

#include "Core/CoreTypes.h"

// Forward Declarations for FBX SDK
namespace fbxsdk {
	class FbxNode;
	class FbxMesh;
	class FbxScene;
	class FbxManager;
}

// Using alias for convenience if needed, but we'll use Fbx prefix
using FbxNode = fbxsdk::FbxNode;
using FbxMesh = fbxsdk::FbxMesh;
using FbxScene = fbxsdk::FbxScene;
using FbxManager = fbxsdk::FbxManager;

class USkeletalMesh;
class UStaticMesh;
struct FSkeletalMesh;
struct FStaticMesh;

/**
 * FFbxImporter
 * FBX SDK를 사용하여 FBX 파일에서 Skeletal/Static 메시 데이터를 추출하는 유틸리티.
 *
 * 한 .fbx 파일은 다음 중 하나로 분류된다:
 *  - Pure skeletal: skin이 있는 mesh만 존재 → USkeletalMesh 만 채워짐.
 *  - Pure static:   skin이 전혀 없음        → UStaticMesh   만 채워짐 (OutStatic != null 일 때).
 *  - Hybrid:        둘 다 존재             → 양쪽 모두 채워짐.
 */
class FFbxImporter
{
public:
	/**
	 * 단일 진입점.
	 * @param FilePath    FBX 파일 경로
	 * @param OutSkeletal 선택. nullptr이면 skinned 데이터는 폐기.
	 * @param OutStatic   선택. nullptr이면 unskinned mesh node 데이터는 폐기.
	 * @return 임포트 성공 여부 (적어도 하나의 출력에 유효 데이터 생성 시 true).
	 */
	static bool ImportFbx(const FString& FilePath,
	                      USkeletalMesh* OutSkeletal,
	                      UStaticMesh*   OutStatic);

	/** Backward-compatible wrapper: skinned 데이터만 받는다. */
	static bool ImportSkeletalMesh(const FString& FilePath, USkeletalMesh* OutMesh)
	{
		return ImportFbx(FilePath, OutMesh, nullptr);
	}

private:
	// 재귀 traversal. 각 mesh node를 skin 여부에 따라 분기한다.
	static void ProcessNode(FbxNode* Node,
	                        USkeletalMesh* OutSkeletal, FSkeletalMesh* RawSkel,
	                        UStaticMesh*   OutStatic,   FStaticMesh*   RawStatic);

	// Skinned mesh: cluster를 직접 추출하여 FSkeletalMesh::Clusters에 보존.
	static void ExtractSkeletalMesh(FbxMesh* Mesh, FSkeletalMesh* RawMesh, USkeletalMesh* OutMesh);

	// Unskinned mesh: mesh node global transform을 baking하여 FStaticMesh에 누적.
	static void ExtractStaticMesh(FbxMesh* Mesh, FStaticMesh* RawMesh);

	// 스켈레톤 계층(parent + SRT) 추출. IBP는 cluster에 저장되므로 여기서는 다루지 않음.
	static void ExtractSkeleton(FbxScene* Scene, USkeletalMesh* OutMesh, FSkeletalMesh* RawMesh);

	// 헬퍼: FbxMesh에 유효한 skin deformer(valid bone link 가진 cluster ≥ 1)가 있는지.
	static bool HasValidSkinDeformer(FbxMesh* Mesh, USkeletalMesh* OutMesh);

	// 헬퍼: 모든 cluster의 weight를 per-vertex sum = 1.0 으로 정규화.
	static void NormalizeClusterWeights(FSkeletalMesh* RawMesh);
};
