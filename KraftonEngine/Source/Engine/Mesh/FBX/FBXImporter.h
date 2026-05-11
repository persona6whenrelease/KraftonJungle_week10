#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "FBXImportTypes.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Mesh/StaticMeshAsset.h"

enum class ELightType
{
	Point,
	Directional,
	Spot,
};

struct FLightAsset
{
	ELightType LightType;
	FMatrix Transform;
};

using FCameraAsset = FMatrix;

struct FFBXAsset
{
	FString PathFileName;
	TArray<FSkeletalMesh> SkeletalMeshes;
	TArray<FMeshMaterial> SkeletalMaterials;
	TArray<FStaticMesh> StaticMeshes;
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
