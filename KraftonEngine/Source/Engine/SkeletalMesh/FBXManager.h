#pragma once

#include "Core/CoreTypes.h"
#include "Object/ObjectIterator.h"
#include "Render/Types/RenderTypes.h"
#include "Mesh/ObjManager.h" // For FMeshAssetListItem
#include <map>
#include <string>
#include <memory>

class USkeletalMesh;
struct FSkeletalMesh;

/**
 * FFBXManager
 * 스켈레탈 메시 에셋(USkeletalMesh)의 생명주기 및 바이너리 캐싱을 관리하는 클래스.
 * FObjManager의 설계를 계승하여 FBX 임포트 및 로딩 최적화를 담당함.
 */
class FFBXManager
{
	// path -> USkeletalMesh* 캐시 (Key: BinaryFilePath)
	static TMap<FString, USkeletalMesh*> SkeletalMeshCache;
	
	static TArray<FMeshAssetListItem> AvailableMeshFiles; // .bin 리스트
	static TArray<FMeshAssetListItem> AvailableFbxFiles;  // .fbx 리스트

public:
	// 원본 경로를 기반으로 대응하는 .bin 캐시 파일 경로를 생성
	static FString GetBinaryFilePath(const FString& OriginalPath);

	/**
	 * 스켈레탈 메시를 로드합니다.
	 * 캐시가 있으면 반환하고, 없으면 .bin 로드 혹은 .fbx 재빌드를 수행합니다.
	 */
	static USkeletalMesh* LoadSkeletalMesh(const FString& PathFileName, ID3D11Device* InDevice);
	
	// 에셋 디렉토리 스캔
	static void ScanMeshAssets();
	static const TArray<FMeshAssetListItem>& GetAvailableMeshFiles();
	
	static void ScanFbxSourceFiles();
	static const TArray<FMeshAssetListItem>& GetAvailableFbxFiles();

	// 종료 시 GPU 리소스 해제
	static void ReleaseAllGPU();

private:
	// 내부 헬퍼: 디렉토리 존재 보장
	static void EnsureMeshCacheDirExists();
};
