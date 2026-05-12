#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "Mesh/StaticMeshAsset.h"

class FFbxStaticMeshParser final
{
public:
	explicit FFbxStaticMeshParser(const FFbxImportMeta& InImportMeta)
		: ImportMeta(InImportMeta)
	{
	}

	bool Parse(
		TArray<FStaticMesh>& OutStaticMeshes,
		TMap<int32, int32>& OutMeshIdToStaticMeshAssetIndex) const;

private:
	const FFbxImportMeta& ImportMeta;
};
