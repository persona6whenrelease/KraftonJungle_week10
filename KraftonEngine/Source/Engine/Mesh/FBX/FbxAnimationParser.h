#pragma once

#include "Core/CoreTypes.h"
#include "FBXImportMeta.h"
#include "Mesh/SkeletalMeshAsset.h"

namespace fbxsdk { class FbxScene; }

// Bakes FBX AnimStacks into per-bone local-transform tracks on a built FSkeletalMesh.
// Designed to be called once per skeleton after FFbxSkeletalMeshAssembler has filled the mesh's bone list.
class FFbxAnimationParser final
{
public:
	explicit FFbxAnimationParser(const FFbxImportMeta& InImportMeta)
		: ImportMeta(InImportMeta)
	{
	}

	// SampleRate is in samples-per-second. Caller owns the FbxScene; it must still be alive.
	void ParseSkeletonAnimations(
		fbxsdk::FbxScene* Scene,
		const FFbxSkeletonMeta& SkeletonMeta,
		FSkeletalMesh& OutMesh,
		float SampleRate = 30.0f) const;

private:
	const FFbxImportMeta& ImportMeta;
};
