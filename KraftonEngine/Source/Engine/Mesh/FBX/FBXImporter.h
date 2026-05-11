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
	TArray<FMeshMaterial> SkeletalMaterials;
	TArray<FStaticMesh> StaticMeshes;
	TArray<FLightAsset> LightAssets;
	TArray<FCameraAsset> CameraAssets;
};

struct FFbxMeshPartSection
{
	int32 SourceMeshId = -1;
	int32 MaterialSlotIndex = 0;
	int32 SourceMaterialId = -1;
	FString MaterialSlotName = "None";
	int32 FirstIndex = 0;
	int32 IndexCount = 0;
};

struct FFbxSkinnedMeshPart
{
	int32 MeshId = -1;
	int32 SkinId = -1;
	int32 SkeletonId = -1;
	int32 AttachedBoneId = -1;
	int32 AttachedSkeletonBoneIndex = -1;
	bool bRigidAttached = false;
	bool bSkinned = false;
	FString SourceNodePath;
	TArray<FSkeletalVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FFbxMeshPartSection> Sections;
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
	bool ParseSkinnedMeshPart(int32 MeshId, FFbxSkinnedMeshPart& OutPart);
	bool ParseRigidAttachedMeshPart(int32 MeshId, FFbxSkinnedMeshPart& OutPart);
	bool AttachMeshes(const TArray<FFbxSkinnedMeshPart>& SkinnedMeshParts, TArray<FSkeletalMesh>& OutSkeletalMeshAssets);
	bool BuildSkeletalMeshFromParts(
		const FFbxSkeletonMeta& SkeletonMeta,
		const TArray<const FFbxSkinnedMeshPart*>& Parts,
		FSkeletalMesh& OutMesh);
	bool ValidateSkinnedMeshPartForAttach(
		const FFbxSkeletonMeta& SkeletonMeta,
		const FFbxSkinnedMeshPart& Part) const;
	void BuildSkeletalMaterials(const FSkeletalMesh& Mesh, TArray<FMeshMaterial>& OutMaterials);
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
