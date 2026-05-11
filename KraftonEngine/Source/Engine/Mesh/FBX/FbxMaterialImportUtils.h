#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "Mesh/MeshCommonTypes.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Mesh/StaticMeshAsset.h"

namespace FbxMaterialImportUtils
{
	FString NormalizeMaterialSlotName(const FString& SlotName);
	void BuildStaticMaterials(FFbxImportMeta& ImportMeta, const FStaticMesh& Mesh, TArray<FMeshMaterial>& OutMaterials);
	void BuildSkeletalMaterials(FFbxImportMeta& ImportMeta, const FSkeletalMesh& Mesh, TArray<FMeshMaterial>& OutMaterials);
}
