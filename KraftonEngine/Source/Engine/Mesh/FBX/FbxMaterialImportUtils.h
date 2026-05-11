#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "Mesh/MeshCommonTypes.h"
#include "Mesh/SkeletalMeshAsset.h"

namespace FbxMaterialImportUtils
{
	FString NormalizeMaterialSlotName(const FString& SlotName);
	void BuildSkeletalMaterials(FFbxImportMeta& ImportMeta, const FSkeletalMesh& Mesh, TArray<FMeshMaterial>& OutMaterials);
}
