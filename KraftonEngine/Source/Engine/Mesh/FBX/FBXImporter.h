#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
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
	TArray<FStaticMesh> StaticMeshes;
	TArray<FLightAsset> LightAssets;
	TArray<FCameraAsset> CameraAssets;
};

struct FFbxSkinnedMeshPart
{
	int32 MeshId = -1;
	int32 SkinId = -1;
	int32 SkeletonId = -1;
	FbxNode* Node = nullptr;
	FbxMesh* Mesh = nullptr;
};

class FBXImporter
{
public:
	bool ImportFbxAsset(const FString& InFilePath, FFBXAsset& OutFBXAsset);

private:
	bool InitializeSdk();
	bool LoadScene(const FString& InFilePath);
	bool ParseStaticMeshes(TArray<FStaticMesh>& OutStaticMeshAsset);
	bool ParseSkeletalMeshes(TArray<FFbxSkinnedMeshPart>& OutSkinnedMeshParts);
	bool AttachMeshes(const TArray<FFbxSkinnedMeshPart>& SkinnedMeshParts, TArray<FSkeletalMesh>& OutSkeletalMeshAssets);
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
