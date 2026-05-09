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
struct FSkeletalMesh;

/**
 * FFbxImporter
 * FBX SDK를 사용하여 FBX 파일에서 Skeletal Mesh 데이터를 추출하는 유틸리티 클래스.
 */
class FFbxImporter
{
public:
	/**
	 * FBX 파일을 읽어 USkeletalMesh 에셋을 채웁니다.
	 * @param FilePath FBX 파일 경로
	 * @param OutMesh 데이터를 채울 대상 에셋
	 * @return 임포트 성공 여부
	 */
	static bool ImportSkeletalMesh(const FString& FilePath, USkeletalMesh* OutMesh);

private:
	// 재귀적으로 노드를 순회하며 데이터를 수집합니다.
	static void ProcessNode(FbxNode* Node, USkeletalMesh* OutMesh, FSkeletalMesh* RawMesh);

	// 메시 데이터(정점, 인덱스, 스키닝 가중치)를 추출합니다.
	static void ExtractMesh(FbxMesh* Mesh, FSkeletalMesh* RawMesh, USkeletalMesh* OutMesh);

	// 스켈레톤 구조 및 Inverse Bind Matrix를 추출합니다.
	static void ExtractSkeleton(FbxScene* Scene, USkeletalMesh* OutMesh, FSkeletalMesh* RawMesh);
};
