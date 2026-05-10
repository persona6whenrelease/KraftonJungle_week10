#pragma once

#include "Core/CoreTypes.h"
#include "Mesh/MeshCommonTypes.h"

class USkeletalMesh;

class FFBXManager
{
	static TMap<FString, USkeletalMesh*> SkeletalMeshCache;
	static TArray<FMeshAssetListItem> AvailableSkeletalMeshFiles;
	static TArray<FMeshAssetListItem> AvailableFbxFiles;

public:
	static FString GetBinaryFilePath(const FString& OriginalPath);
	static USkeletalMesh* LoadSkeletalMesh(const FString& PathFileName);
	static void ScanSkeletalMeshAssets();
	static const TArray<FMeshAssetListItem>& GetAvailableSkeletalMeshFiles();
	static void ScanFbxSourceFiles();
	static const TArray<FMeshAssetListItem>& GetAvailableFbxSourceFiles();

	static void ReleaseAllGPU();
};
