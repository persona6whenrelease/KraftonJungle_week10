#pragma once

#include "Core/CoreTypes.h"

class USkeletalMesh;
struct ID3D11Device;

struct FSkeletalMeshAssetListItem
{
	FString DisplayName;
	FString FullPath;
};

class FSkeletalMeshManager
{
public:
	static USkeletalMesh* LoadFbxSkeletalMesh(
		const FString& PathFileName,
		ID3D11Device* InDevice
	);

	static void ScanSkeletalMeshAssets();
	static const TArray<FSkeletalMeshAssetListItem>& GetAvailableMeshFiles();

	static void ReleaseAll();

private:
	static TMap<FString, USkeletalMesh*> SkeletalMeshCache;
	static TArray<FSkeletalMeshAssetListItem> AvailableMeshFiles;
};
