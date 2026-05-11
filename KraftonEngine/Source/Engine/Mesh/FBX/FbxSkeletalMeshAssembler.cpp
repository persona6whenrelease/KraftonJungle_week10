#include "FbxSkeletalMeshAssembler.h"

#include "Core/Log.h"
#include "FbxMaterialImportUtils.h"

#include <cmath>
#include <string>

namespace
{
	template <typename T>
	bool IsValidIndex(const TArray<T>& Items, int32 Index)
	{
		return Index >= 0 && static_cast<size_t>(Index) < Items.size();
	}
}

bool FFbxSkeletalMeshAssembler::Assemble(
	const TArray<FFbxSkinnedMeshPart>& SkinnedMeshParts,
	TArray<FSkeletalMesh>& OutSkeletalMeshAssets) const
{
	OutSkeletalMeshAssets.clear();

	for (const FFbxSkeletonMeta& SkeletonMeta : ImportMeta.Skeletons)
	{
		if (!SkeletonMeta.bValid)
		{
			continue;
		}

		TArray<const FFbxSkinnedMeshPart*> Parts;
		int32 SkinnedPartCount = 0;
		int32 RigidPartCount = 0;
		for (const FFbxSkinnedMeshPart& Part : SkinnedMeshParts)
		{
			if (Part.SkeletonId != SkeletonMeta.SkeletonId)
			{
				continue;
			}

			Parts.push_back(&Part);
			SkinnedPartCount += Part.bSkinned ? 1 : 0;
			RigidPartCount += Part.bRigidAttached ? 1 : 0;
		}

		if (Parts.empty())
		{
			UE_LOG("[FBXImporter] Skeleton has no mesh parts to attach. SkeletonId=%d", SkeletonMeta.SkeletonId);
			continue;
		}

		FSkeletalMesh SkeletalMesh;
		if (!BuildSkeletalMeshFromParts(SkeletonMeta, Parts, SkeletalMesh))
		{
			UE_LOG("[FBXImporter] Failed to build skeletal mesh from parts. SkeletonId=%d", SkeletonMeta.SkeletonId);
			return false;
		}

		UE_LOG("[FBXImporter] Built skeletal mesh. SkeletonId=%d Parts=%d Skinned=%d Rigid=%d Vertices=%u Indices=%u Sections=%u Bones=%u",
			SkeletonMeta.SkeletonId,
			static_cast<int32>(Parts.size()),
			SkinnedPartCount,
			RigidPartCount,
			static_cast<uint32>(SkeletalMesh.Vertices.size()),
			static_cast<uint32>(SkeletalMesh.Indices.size()),
			static_cast<uint32>(SkeletalMesh.Sections.size()),
			static_cast<uint32>(SkeletalMesh.Bones.size()));

		OutSkeletalMeshAssets.push_back(SkeletalMesh);
	}

	return true;
}

bool FFbxSkeletalMeshAssembler::BuildSkeletalMeshFromParts(
	const FFbxSkeletonMeta& SkeletonMeta,
	const TArray<const FFbxSkinnedMeshPart*>& Parts,
	FSkeletalMesh& OutMesh) const
{
	OutMesh = {};
	OutMesh.PathFileName = ImportMeta.SourceFilePath + "#Skeleton_" + std::to_string(SkeletonMeta.SkeletonId);

	if (!IsValidIndex(ImportMeta.Bones, SkeletonMeta.RootBoneId) || SkeletonMeta.BoneIds.empty())
	{
		UE_LOG("[FBXImporter] Cannot build skeletal mesh with invalid skeleton root. SkeletonId=%d RootBoneId=%d",
			SkeletonMeta.SkeletonId,
			SkeletonMeta.RootBoneId);
		return false;
	}

	OutMesh.Bones.resize(SkeletonMeta.BoneIds.size());
	TArray<FMatrix> BoneBindInSkeletonSpace;
	BoneBindInSkeletonSpace.resize(SkeletonMeta.BoneIds.size(), FMatrix::Identity);

	const FMatrix InvSkeletonRootBindGlobal = FMatrix::Identity;

	for (int32 SkeletonBoneIndex = 0; SkeletonBoneIndex < static_cast<int32>(SkeletonMeta.BoneIds.size()); ++SkeletonBoneIndex)
	{
		const int32 BoneId = SkeletonMeta.BoneIds[SkeletonBoneIndex];
		if (!IsValidIndex(ImportMeta.Bones, BoneId))
		{
			UE_LOG("[FBXImporter] Invalid BoneId in skeleton. SkeletonId=%d BoneId=%d",
				SkeletonMeta.SkeletonId,
				BoneId);
			return false;
		}

		const FFbxBoneMeta& BoneMeta = ImportMeta.Bones[BoneId];
		auto BoneIndexIt = SkeletonMeta.BoneIdToSkeletonBoneIndex.find(BoneId);
		if (BoneIndexIt == SkeletonMeta.BoneIdToSkeletonBoneIndex.end() ||
			BoneIndexIt->second != SkeletonBoneIndex ||
			BoneMeta.SkeletonBoneIndex != SkeletonBoneIndex)
		{
			UE_LOG("[FBXImporter] Bone skeleton index mismatch. SkeletonId=%d BoneId=%d Expected=%d BoneMeta=%d",
				SkeletonMeta.SkeletonId,
				BoneId,
				SkeletonBoneIndex,
				BoneMeta.SkeletonBoneIndex);
			return false;
		}

		FBoneInfo& BoneInfo = OutMesh.Bones[SkeletonBoneIndex];
		BoneInfo.Name = BoneMeta.Name;
		BoneInfo.ParentIndex = -1;

		auto ParentIndexIt = SkeletonMeta.BoneIdToSkeletonBoneIndex.find(BoneMeta.ParentBoneId);
		if (ParentIndexIt != SkeletonMeta.BoneIdToSkeletonBoneIndex.end())
		{
			BoneInfo.ParentIndex = ParentIndexIt->second;
			if (BoneInfo.ParentIndex >= SkeletonBoneIndex)
			{
				UE_LOG("[FBXImporter] Bone parent index must precede child for reference pose accumulation. SkeletonId=%d BoneId=%d ParentIndex=%d ChildIndex=%d",
					SkeletonMeta.SkeletonId,
					BoneId,
					BoneInfo.ParentIndex,
					SkeletonBoneIndex);
				return false;
			}
		}

		BoneBindInSkeletonSpace[SkeletonBoneIndex] = BoneMeta.BindGlobalMatrix * InvSkeletonRootBindGlobal;
		BoneInfo.MeshSpaceToBoneSpace = BoneBindInSkeletonSpace[SkeletonBoneIndex].GetInverse();
	}

	for (int32 SkeletonBoneIndex = 0; SkeletonBoneIndex < static_cast<int32>(OutMesh.Bones.size()); ++SkeletonBoneIndex)
	{
		FBoneInfo& BoneInfo = OutMesh.Bones[SkeletonBoneIndex];
		const FMatrix& BoneGlobalInSkeletonSpace = BoneBindInSkeletonSpace[SkeletonBoneIndex];

		// Runtime accumulates BoneSpaceToMeshSpace with the parent to build reference globals.
		// The field name is misleading here: store parent-local bind, not global bind.
		if (BoneInfo.ParentIndex >= 0)
		{
			BoneInfo.BoneSpaceToMeshSpace =
				BoneGlobalInSkeletonSpace * BoneBindInSkeletonSpace[BoneInfo.ParentIndex].GetInverse();
		}
		else
		{
			BoneInfo.BoneSpaceToMeshSpace = BoneGlobalInSkeletonSpace;
		}
	}

	TMap<FString, int32> SlotNameToMaterialIndex;

	for (const FFbxSkinnedMeshPart* Part : Parts)
	{
		if (!Part || !ValidateSkinnedMeshPartForAttach(SkeletonMeta, *Part))
		{
			return false;
		}

		const uint32 VertexBase = static_cast<uint32>(OutMesh.Vertices.size());
		const uint32 IndexBase = static_cast<uint32>(OutMesh.Indices.size());

		OutMesh.Vertices.insert(OutMesh.Vertices.end(), Part->Vertices.begin(), Part->Vertices.end());
		OutMesh.Indices.reserve(OutMesh.Indices.size() + Part->Indices.size());
		for (uint32 Index : Part->Indices)
		{
			OutMesh.Indices.push_back(Index + VertexBase);
		}

		for (const FFbxMeshPartSection& PartSection : Part->Sections)
		{
			const FString SlotName = FbxMaterialImportUtils::NormalizeMaterialSlotName(PartSection.MaterialSlotName);
			int32 MaterialIndex = -1;
			auto MaterialIt = SlotNameToMaterialIndex.find(SlotName);
			if (MaterialIt != SlotNameToMaterialIndex.end())
			{
				MaterialIndex = MaterialIt->second;
			}
			else
			{
				MaterialIndex = static_cast<int32>(SlotNameToMaterialIndex.size());
				SlotNameToMaterialIndex[SlotName] = MaterialIndex;
			}

			FMeshSection Section;
			Section.MaterialIndex = MaterialIndex;
			Section.MaterialSlotName = SlotName;
			Section.FirstIndex = IndexBase + static_cast<uint32>(PartSection.FirstIndex);
			Section.NumTriangles = static_cast<uint32>(PartSection.IndexCount / 3);
			OutMesh.Sections.push_back(Section);

			UE_LOG("[FBXImporter] Skeletal section material. SourceMeshId=%d SourceSlot=%d SlotName=%s MaterialIndex=%d",
				PartSection.SourceMeshId,
				PartSection.MaterialSlotIndex,
				SlotName.c_str(),
				MaterialIndex);
		}
	}

	OutMesh.CacheBounds();
	return !OutMesh.Vertices.empty() && !OutMesh.Indices.empty() && !OutMesh.Bones.empty();
}

bool FFbxSkeletalMeshAssembler::ValidateSkinnedMeshPartForAttach(
	const FFbxSkeletonMeta& SkeletonMeta,
	const FFbxSkinnedMeshPart& Part) const
{
	bool bValid = true;

	if (Part.SkeletonId != SkeletonMeta.SkeletonId)
	{
		UE_LOG("[FBXImporter] Attach validation failed: skeleton mismatch. PartMeshId=%d PartSkeletonId=%d SkeletonId=%d",
			Part.MeshId,
			Part.SkeletonId,
			SkeletonMeta.SkeletonId);
		return false;
	}

	if (Part.Vertices.empty() || Part.Indices.empty())
	{
		UE_LOG("[FBXImporter] Attach validation failed: empty vertices or indices. MeshId=%d Vertices=%u Indices=%u",
			Part.MeshId,
			static_cast<uint32>(Part.Vertices.size()),
			static_cast<uint32>(Part.Indices.size()));
		return false;
	}

	for (uint32 Index : Part.Indices)
	{
		if (Index >= Part.Vertices.size())
		{
			UE_LOG("[FBXImporter] Attach validation failed: index out of range. MeshId=%d Index=%u VertexCount=%u",
				Part.MeshId,
				Index,
				static_cast<uint32>(Part.Vertices.size()));
			bValid = false;
		}
	}

	const uint32 BoneCount = static_cast<uint32>(SkeletonMeta.BoneIds.size());
	int32 BadWeightSumWarningCount = 0;
	float FirstBadWeightSum = 0.0f;
	bool bLoggedWeightSumWarning = false;
	for (const FSkeletalVertex& Vertex : Part.Vertices)
	{
		float WeightSum = 0.0f;
		for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
		{
			const uint32 BoneIndex = Vertex.BoneIDs[InfluenceIndex];
			const float Weight = Vertex.BoneWeights[InfluenceIndex];
			if (Weight > 0.0f && BoneIndex >= BoneCount)
			{
				UE_LOG("[FBXImporter] Attach validation failed: bone index out of range. MeshId=%d BoneIndex=%u BoneCount=%u",
					Part.MeshId,
					BoneIndex,
					BoneCount);
				bValid = false;
			}
			WeightSum += Weight;
		}

		if (WeightSum <= 0.0001f)
		{
			UE_LOG("[FBXImporter] Attach validation failed: vertex has zero total bone weight. MeshId=%d",
				Part.MeshId);
			bValid = false;
		}
		else if (std::abs(WeightSum - 1.0f) > 0.05f)
		{
			++BadWeightSumWarningCount;
			if (!bLoggedWeightSumWarning)
			{
				FirstBadWeightSum = WeightSum;
				UE_LOG("[FBXImporter] Warning: vertex bone weight sum is %.4f. MeshId=%d. Further weight warnings for this mesh will be summarized.",
					WeightSum,
					Part.MeshId);
				bLoggedWeightSumWarning = true;
			}
		}
	}

	if (BadWeightSumWarningCount > 0)
	{
		UE_LOG("[FBXImporter] Mesh has %d vertices with non-normalized bone weights. MeshId=%d FirstWeightSum=%.4f",
			BadWeightSumWarningCount,
			Part.MeshId,
			FirstBadWeightSum);
	}

	for (const FFbxMeshPartSection& Section : Part.Sections)
	{
		if (Section.FirstIndex < 0 ||
			Section.IndexCount <= 0 ||
			Section.IndexCount % 3 != 0 ||
			Section.FirstIndex + Section.IndexCount > static_cast<int32>(Part.Indices.size()))
		{
			UE_LOG("[FBXImporter] Attach validation failed: invalid section range. MeshId=%d SourceMeshId=%d FirstIndex=%d IndexCount=%d PartIndexCount=%u",
				Part.MeshId,
				Section.SourceMeshId,
				Section.FirstIndex,
				Section.IndexCount,
				static_cast<uint32>(Part.Indices.size()));
			bValid = false;
		}
	}

	return bValid;
}
