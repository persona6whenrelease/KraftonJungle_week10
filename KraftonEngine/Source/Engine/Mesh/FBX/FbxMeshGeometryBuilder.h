#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "FBXImportTypes.h"
#include "Mesh/StaticMeshAsset.h"

#include <functional>

namespace FbxMeshGeometryBuilder
{
	FMatrix BuildGeometricTransform(FbxNode* Node);
	FMatrix BuildMeshToAssetBindMatrix(FbxNode* MeshNode, const FMatrix& MeshBindGlobal);

	bool BuildSkeletalMeshPartGeometry(
		const FFbxMeshMeta& MeshMeta,
		const FMatrix& MeshToAssetBindMatrix,
		const std::function<void(int32, FSkeletalVertex&)>& AssignWeights,
		FFbxSkinnedMeshPart& OutPart);

	bool BuildStaticMeshGeometry(
		const FFbxMeshMeta& MeshMeta,
		const FMatrix& MeshToAssetBindMatrix,
		FStaticMesh& OutMesh);
}
