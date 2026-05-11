#pragma once

#include "Core/CoreTypes.h"

struct FStaticMesh;
struct FStaticMaterial;
struct FSkeletalMesh;

struct FFbxImporter
{
	static bool CanLoadScene(const FString& FbxFilePath);
	static bool ImportStaticMesh(const FString& FbxFilePath, FStaticMesh& OutMesh,
	                             TArray<FStaticMaterial>& OutMaterials);

	static bool ImportSkeletalMesh(const FString& FbxFilePath, FSkeletalMesh& OutMesh,
	                               TArray<FStaticMaterial>& OutMaterials);
};