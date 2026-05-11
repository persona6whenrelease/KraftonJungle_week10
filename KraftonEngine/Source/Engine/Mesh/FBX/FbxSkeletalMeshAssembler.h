#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "FBXImportTypes.h"
#include "Mesh/SkeletalMeshAsset.h"

class FFbxSkeletalMeshAssembler final
{
public:
	explicit FFbxSkeletalMeshAssembler(const FFbxImportMeta& InImportMeta)
		: ImportMeta(InImportMeta)
	{
	}

	bool Assemble(
		const TArray<FFbxSkinnedMeshPart>& SkinnedMeshParts,
		TArray<FSkeletalMesh>& OutSkeletalMeshAssets) const;

private:
	bool BuildSkeletalMeshFromParts(
		const FFbxSkeletonMeta& SkeletonMeta,
		const TArray<const FFbxSkinnedMeshPart*>& Parts,
		FSkeletalMesh& OutMesh) const;

	bool ValidateSkinnedMeshPartForAttach(
		const FFbxSkeletonMeta& SkeletonMeta,
		const FFbxSkinnedMeshPart& Part) const;

private:
	const FFbxImportMeta& ImportMeta;
};
