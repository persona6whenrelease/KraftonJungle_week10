#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "FBXImportTypes.h"

class FFbxSkeletalMeshPartParser final
{
public:
	explicit FFbxSkeletalMeshPartParser(const FFbxImportMeta& InImportMeta)
		: ImportMeta(InImportMeta)
	{
	}

	bool Parse(TArray<FFbxSkinnedMeshPart>& OutSkinnedMeshParts) const;

private:
	bool ParseSkinnedMeshPart(int32 MeshId, FFbxSkinnedMeshPart& OutPart) const;
	bool ParseRigidAttachedMeshPart(int32 MeshId, FFbxSkinnedMeshPart& OutPart) const;

private:
	const FFbxImportMeta& ImportMeta;
};
