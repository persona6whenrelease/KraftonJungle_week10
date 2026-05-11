#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "FBXImportTypes.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Mesh/StaticMeshAsset.h"

enum class EFBXLightType
{
	Point,
	Directional,
	Spot,
};

struct FLightAsset
{
	EFBXLightType LightType;
	FMatrix Transform;
};

using FCameraAsset = FMatrix;

enum class EFBXSceneComponentType
{
	StaticMesh,
	SkeletalMesh
};

struct FFBXSceneComponentDesc
{
	EFBXSceneComponentType Type = EFBXSceneComponentType::StaticMesh;
	FString Name;
	int32 SourceNodeId = -1;
	int32 SourceMeshId = -1;
	int32 SourceSkeletonId = -1;
	int32 StaticMeshAssetIndex = -1;
	int32 SkeletalMeshAssetIndex = -1;
	FMatrix RelativeTransform = FMatrix::Identity;
};

struct FFBXAsset
{
	FString PathFileName;
	TArray<FStaticMesh> StaticMeshes;
	TArray<FSkeletalMesh> SkeletalMeshes;
	TArray<TArray<FMeshMaterial>> StaticMeshMaterials;
	TArray<TArray<FMeshMaterial>> SkeletalMeshMaterials;
	TArray<FMeshMaterial> SkeletalMaterials;
	TArray<FFBXSceneComponentDesc> SceneComponents;
	TMap<int32, int32> MeshIdToStaticMeshAssetIndex;
	TMap<int32, int32> SkeletonIdToSkeletalMeshAssetIndex;
	TArray<FLightAsset> LightAssets;
	TArray<FCameraAsset> CameraAssets;
};

class FBXImporter
{
public:
	bool ImportFbxAsset(const FString& InFilePath, FFBXAsset& OutFBXAsset);

private:
	bool InitializeSdk();
	bool LoadScene(const FString& InFilePath);
	bool FinalizeAsset();
	void ShutdownSdk();

private:
	void ClearState();
	void PreprocessScene();
	void DestroyScene();


private:
	FFbxImportMeta ImportMeta;

private:
	FbxManager* Manager = nullptr;
	FbxScene* Scene = nullptr;
};
