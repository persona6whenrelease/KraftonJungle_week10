#include "FbxStaticMeshParser.h"

#include "Core/Log.h"
#include "FBXUtil.h"
#include "FbxMeshGeometryBuilder.h"

#include <fbxsdk.h>

bool FFbxStaticMeshParser::Parse(
	TArray<FStaticMesh>& OutStaticMeshes,
	TMap<int32, int32>& OutMeshIdToStaticMeshAssetIndex) const
{
	OutStaticMeshes.clear();
	OutMeshIdToStaticMeshAssetIndex.clear();

	for (const FFbxMeshMeta& MeshMeta : ImportMeta.Meshes)
	{
		if (!MeshMeta.Mesh || !MeshMeta.Node)
		{
			continue;
		}

		FStaticMesh StaticMesh;
		StaticMesh.PathFileName = ImportMeta.SourceFilePath + "#Mesh_" + std::to_string(MeshMeta.MeshId);

		const FMatrix MeshBindGlobal = FBXUtil::ConvertFbxMatrix(MeshMeta.Node->EvaluateGlobalTransform());
		const FMatrix MeshToAssetBindMatrix = FbxMeshGeometryBuilder::BuildMeshToAssetBindMatrix(
			MeshMeta.Node,
			MeshBindGlobal);

		if (!FbxMeshGeometryBuilder::BuildStaticMeshGeometry(MeshMeta, MeshToAssetBindMatrix, StaticMesh))
		{
			UE_LOG("[FBXImporter] Failed to parse static mesh geometry. MeshId=%d Node=%s",
				MeshMeta.MeshId,
				MeshMeta.SourceNodePath.c_str());
			continue;
		}

		const int32 AssetIndex = static_cast<int32>(OutStaticMeshes.size());
		OutStaticMeshes.push_back(std::move(StaticMesh));
		OutMeshIdToStaticMeshAssetIndex[MeshMeta.MeshId] = AssetIndex;
	}

	UE_LOG("[FBXImporter] Parsed static meshes. Total=%u",
		static_cast<uint32>(OutStaticMeshes.size()));
	return true;
}
