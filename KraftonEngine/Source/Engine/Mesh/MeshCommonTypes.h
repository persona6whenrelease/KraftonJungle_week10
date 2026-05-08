#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Object/FName.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Serialization/Archive.h"

struct FMeshAssetListItem
{
	FString DisplayName;
	FString FullPath;
};

struct FMeshSection
{
	int32 MaterialIndex = -1;
	FString MaterialSlotName;
	uint32 FirstIndex = 0;
	uint32 NumTriangles = 0;

	friend FArchive& operator<<(FArchive& Ar, FMeshSection& Section)
	{
		Ar << Section.MaterialSlotName << Section.FirstIndex << Section.NumTriangles;
		return Ar;
	}
};

struct FMeshMaterial
{
	UMaterial* MaterialInterface = nullptr;
	FString MaterialSlotName = "None";

	friend FArchive& operator<<(FArchive& Ar, FMeshMaterial& Mat)
	{
		Ar << Mat.MaterialSlotName;

		FString JsonPath;
		if (Ar.IsSaving() && Mat.MaterialInterface)
		{
			JsonPath = Mat.MaterialInterface->GetAssetPathFileName();
		}
		Ar << JsonPath;

		if (Ar.IsLoading())
		{
			Mat.MaterialInterface = JsonPath.empty()
				? nullptr
				: FMaterialManager::Get().GetOrCreateMaterial(JsonPath);
		}

		return Ar;
	}
};

using FStaticMeshSection = FMeshSection;
using FStaticMaterial = FMeshMaterial;
