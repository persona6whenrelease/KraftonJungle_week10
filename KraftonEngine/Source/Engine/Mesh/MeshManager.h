#pragma once

#include "Core/CoreTypes.h"
#include "Mesh/MeshCommonTypes.h"

struct FImportOptions;
struct ID3D11Device;
class UFBXSceneAsset;
class UObject;
class USkeletalMesh;
class UStaticMesh;

class FMeshManager
{
public:
	static constexpr uint32 FbxSceneCacheMagic = 0x4E435346u; // "FSCN"
	static constexpr uint32 FbxSceneCacheVersion = 2u;

	static bool IsFbxStaticMeshReference(const FString& PathFileName);
	static bool IsFbxSkeletalMeshReference(const FString& PathFileName);

	static FString GetObjBinaryFilePath(const FString& OriginalPath);
	static FString GetFbxSceneCacheFilePath(const FString& SourcePath);

	static UStaticMesh* LoadStaticMesh(const FString& PathFileName, ID3D11Device* InDevice);
	static UStaticMesh* LoadObjStaticMesh(const FString& PathFileName, ID3D11Device* InDevice);
	static UStaticMesh* LoadObjStaticMesh(const FString& PathFileName, const FImportOptions& Options, ID3D11Device* InDevice);
	static USkeletalMesh* LoadSkeletalMesh(const FString& PathFileName);
	static UFBXSceneAsset* LoadFbxScene(const FString& PathFileName);
	static UObject* ResolveFbxSceneAssetReference(const FString& PathFileName);

	static void ScanMeshAssets();
	static void ScanObjSourceFiles();
	static void ScanFbxSourceFiles();
	static void ScanAllAssets();

	static const TArray<FMeshAssetListItem>& GetAvailableStaticMeshFiles();
	static const TArray<FMeshAssetListItem>& GetAvailableObjSourceFiles();
	static const TArray<FMeshAssetListItem>& GetAvailableFbxSourceFiles();

	static void ReleaseAllGPU();
};
