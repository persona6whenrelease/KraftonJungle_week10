#pragma once

#include "Core/CoreTypes.h"
#include "Mesh/MeshCommonTypes.h"

class USkeletalMesh;
class UStaticMesh;
class UFBXSceneAsset;
class UObject;

class FFBXManager
{
	static TMap<FString, UFBXSceneAsset*> FbxSceneCache;
	static TArray<FMeshAssetListItem> AvailableFbxFiles;

public:
	static USkeletalMesh* LoadSkeletalMesh(const FString& PathFileName);
	static UFBXSceneAsset* LoadFbxScene(const FString& PathFileName);
	static UStaticMesh* ResolveStaticMeshReference(const FString& PathFileName);
	static USkeletalMesh* ResolveSkeletalMeshReference(const FString& PathFileName);
	static UObject* ResolveFbxSceneAssetReference(const FString& PathFileName);
	static UStaticMesh* LoadStaticMeshFromFbxSceneReference(const FString& PathFileName);
	static USkeletalMesh* LoadSkeletalMeshFromFbxSceneReference(const FString& PathFileName);
	static void ScanFbxSourceFiles();
	static const TArray<FMeshAssetListItem>& GetAvailableFbxSourceFiles();

	static void ReleaseAllGPU();
};
