#pragma once

#include "Core/CoreTypes.h"

struct FStaticMesh;
struct FStaticMaterial;

struct FFbxImporter
{
	static bool CanLoadScene(const FString& FbxFilePath);
	static bool ImportStaticMesh(const FString& FbxFilePath, FStaticMesh& OutMesh,
	                             TArray<FStaticMaterial>& OutMaterials);
};