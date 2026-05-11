#include "FbxSkeletalMeshPartParser.h"

#include "Core/Log.h"
#include "FBXUtil.h"
#include "FbxMeshGeometryBuilder.h"

#include <algorithm>
#include <cmath>
#include <fbxsdk.h>

namespace
{
	struct FTempInfluence
	{
		uint32 BoneIndex = 0;
		float Weight = 0.0f;
	};

	template <typename T>
	bool IsValidIndex(const TArray<T>& Items, int32 Index)
	{
		return Index >= 0 && static_cast<size_t>(Index) < Items.size();
	}

	int32 FindSkeletonBoneIndex(const FFbxSkeletonMeta& SkeletonMeta, int32 BoneId)
	{
		auto BoneIndexIt = SkeletonMeta.BoneIdToSkeletonBoneIndex.find(BoneId);
		return BoneIndexIt != SkeletonMeta.BoneIdToSkeletonBoneIndex.end() ? BoneIndexIt->second : -1;
	}

	int32 GetFallbackSkeletonBoneIndex(const FFbxSkeletonMeta& SkeletonMeta)
	{
		const int32 RootBoneIndex = FindSkeletonBoneIndex(SkeletonMeta, SkeletonMeta.RootBoneId);
		return RootBoneIndex >= 0 ? RootBoneIndex : 0;
	}

	bool NormalizeTop4Influences(const TArray<FTempInfluence>& InInfluences, uint32 OutBoneIDs[4], float OutWeights[4])
	{
		for (int32 i = 0; i < 4; ++i)
		{
			OutBoneIDs[i] = 0;
			OutWeights[i] = 0.0f;
		}

		if (InInfluences.empty())
		{
			return false;
		}

		TArray<FTempInfluence> Sorted = InInfluences;
		std::sort(Sorted.begin(), Sorted.end(),
			[](const FTempInfluence& A, const FTempInfluence& B)
			{
				return A.Weight > B.Weight;
			});

		const int32 Count = static_cast<int32>((std::min)(Sorted.size(), static_cast<size_t>(4)));
		float Sum = 0.0f;
		for (int32 i = 0; i < Count; ++i)
		{
			OutBoneIDs[i] = Sorted[i].BoneIndex;
			OutWeights[i] = Sorted[i].Weight;
			Sum += Sorted[i].Weight;
		}

		if (Sum <= 0.0f)
		{
			return false;
		}

		for (int32 i = 0; i < Count; ++i)
		{
			OutWeights[i] /= Sum;
		}
		return true;
	}

	void AssignSingleBoneWeight(FSkeletalVertex& Vertex, int32 SkeletonBoneIndex)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			Vertex.BoneIDs[i] = 0;
			Vertex.BoneWeights[i] = 0.0f;
		}

		Vertex.BoneIDs[0] = static_cast<uint32>((std::max)(SkeletonBoneIndex, 0));
		Vertex.BoneWeights[0] = 1.0f;
	}

	FMatrix GetMeshNodeGlobalBind(FbxNode* Node)
	{
		return Node ? FBXUtil::ConvertFbxMatrix(Node->EvaluateGlobalTransform()) : FMatrix::Identity;
	}

	bool AreMatricesNearlyEqual(const FMatrix& A, const FMatrix& B, float Tolerance)
	{
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				if (std::abs(A.M[Row][Col] - B.M[Row][Col]) > Tolerance)
				{
					return false;
				}
			}
		}
		return true;
	}

	FMatrix FindSkinnedMeshBindGlobal(
		const FFbxMeshMeta& MeshMeta,
		const FFbxSkinMeta& SkinMeta,
		const FFbxImportMeta& ImportMeta)
	{
		bool bFoundMeshBindMatrix = false;
		bool bLoggedMismatch = false;
		FMatrix FirstMeshBindMatrix = FMatrix::Identity;

		for (int32 ClusterId : SkinMeta.ClusterIds)
		{
			if (IsValidIndex(ImportMeta.Clusters, ClusterId) &&
				ImportMeta.Clusters[ClusterId].bHasMeshBindMatrix)
			{
				const FMatrix& MeshBindMatrix = ImportMeta.Clusters[ClusterId].MeshBindGlobalMatrix;
				if (!bFoundMeshBindMatrix)
				{
					FirstMeshBindMatrix = MeshBindMatrix;
					bFoundMeshBindMatrix = true;
					continue;
				}

				if (!bLoggedMismatch && !AreMatricesNearlyEqual(FirstMeshBindMatrix, MeshBindMatrix, 0.001f))
				{
					UE_LOG("[FBXImporter] Skinned mesh has inconsistent cluster mesh bind matrices. MeshId=%d ClusterId=%d",
						MeshMeta.MeshId,
						ClusterId);
					bLoggedMismatch = true;
				}
			}
		}

		if (bFoundMeshBindMatrix)
		{
			return FirstMeshBindMatrix;
		}

		UE_LOG("[FBXImporter] Skin mesh bind matrix missing; using node global transform. MeshId=%d", MeshMeta.MeshId);
		return GetMeshNodeGlobalBind(MeshMeta.Node);
	}

	FMatrix BuildRigidAttachedMeshBindGlobal(
		const FFbxMeshMeta& MeshMeta,
		const FFbxImportMeta& ImportMeta)
	{
		if (!MeshMeta.Node)
		{
			UE_LOG("[FBXImporter] Rigid attached mesh has null node. MeshId=%d",
				MeshMeta.MeshId);
			return FMatrix::Identity;
		}

		if (!IsValidIndex(ImportMeta.Bones, MeshMeta.AttachedBoneId))
		{
			UE_LOG("[FBXImporter] Rigid attached mesh has invalid attached bone. MeshId=%d BoneId=%d",
				MeshMeta.MeshId,
				MeshMeta.AttachedBoneId);
			return GetMeshNodeGlobalBind(MeshMeta.Node);
		}

		const FFbxBoneMeta& AttachedBone = ImportMeta.Bones[MeshMeta.AttachedBoneId];
		if (!AttachedBone.Node)
		{
			UE_LOG("[FBXImporter] Rigid attached mesh attached bone has null node. MeshId=%d BoneId=%d BoneName=%s",
				MeshMeta.MeshId,
				MeshMeta.AttachedBoneId,
				AttachedBone.Name.c_str());
			return GetMeshNodeGlobalBind(MeshMeta.Node);
		}

		const FMatrix MeshEvaluateGlobal = FBXUtil::ConvertFbxMatrix(MeshMeta.Node->EvaluateGlobalTransform());
		const FMatrix AttachedBoneEvaluateGlobal = FBXUtil::ConvertFbxMatrix(AttachedBone.Node->EvaluateGlobalTransform());
		const FMatrix MeshLocalToAttachedBone = MeshEvaluateGlobal * AttachedBoneEvaluateGlobal.GetInverse();

		UE_LOG("[FBXImporter] Rigid attached bind. MeshId=%d Node=%s AttachedBoneId=%d AttachedBoneName=%s",
			MeshMeta.MeshId,
			MeshMeta.SourceNodePath.c_str(),
			MeshMeta.AttachedBoneId,
			AttachedBone.Name.c_str());

		return MeshLocalToAttachedBone * AttachedBone.BindGlobalMatrix;
	}
}

bool FFbxSkeletalMeshPartParser::Parse(TArray<FFbxSkinnedMeshPart>& OutSkinnedMeshParts) const
{
	OutSkinnedMeshParts.clear();

	for (const FFbxSkeletonMeta& SkeletonMeta : ImportMeta.Skeletons)
	{
		if (!SkeletonMeta.bValid)
		{
			continue;
		}

		int32 ParsedSkinnedCount = 0;
		int32 ParsedRigidCount = 0;

		for (int32 MeshId : SkeletonMeta.SkinnedMeshIds)
		{
			FFbxSkinnedMeshPart Part;
			if (ParseSkinnedMeshPart(MeshId, Part))
			{
				OutSkinnedMeshParts.push_back(Part);
				++ParsedSkinnedCount;
			}
		}

		for (int32 MeshId : SkeletonMeta.RigidAttachedMeshIds)
		{
			FFbxSkinnedMeshPart Part;
			if (ParseRigidAttachedMeshPart(MeshId, Part))
			{
				OutSkinnedMeshParts.push_back(Part);
				++ParsedRigidCount;
			}
		}

		UE_LOG("[FBXImporter] Parsed skeleton parts. SkeletonId=%d Skinned=%d Rigid=%d",
			SkeletonMeta.SkeletonId,
			ParsedSkinnedCount,
			ParsedRigidCount);
	}

	UE_LOG("[FBXImporter] Parsed skeletal mesh parts. Total=%u",
		static_cast<uint32>(OutSkinnedMeshParts.size()));

	return true;
}

bool FFbxSkeletalMeshPartParser::ParseSkinnedMeshPart(int32 MeshId, FFbxSkinnedMeshPart& OutPart) const
{
	if (!IsValidIndex(ImportMeta.Meshes, MeshId))
	{
		UE_LOG("[FBXImporter] Invalid skinned MeshId=%d", MeshId);
		return false;
	}

	const FFbxMeshMeta& MeshMeta = ImportMeta.Meshes[MeshId];
	if (!MeshMeta.bHasSkin ||
		!IsValidIndex(ImportMeta.Skins, MeshMeta.PrimarySkinId) ||
		!IsValidIndex(ImportMeta.Skeletons, MeshMeta.SkeletonId) ||
		!MeshMeta.Mesh)
	{
		UE_LOG("[FBXImporter] Skinned mesh part has invalid meta. MeshId=%d", MeshId);
		return false;
	}

	const FFbxSkinMeta& SkinMeta = ImportMeta.Skins[MeshMeta.PrimarySkinId];
	const FFbxSkeletonMeta& SkeletonMeta = ImportMeta.Skeletons[MeshMeta.SkeletonId];
	if (!SkinMeta.bValid || !SkeletonMeta.bValid)
	{
		UE_LOG("[FBXImporter] Skinned mesh part has invalid skin or skeleton. MeshId=%d SkinId=%d SkeletonId=%d",
			MeshId,
			MeshMeta.PrimarySkinId,
			MeshMeta.SkeletonId);
		return false;
	}

	TArray<TArray<FTempInfluence>> ControlPointInfluences;
	ControlPointInfluences.resize(MeshMeta.Mesh->GetControlPointsCount());

	for (int32 ClusterId : SkinMeta.ClusterIds)
	{
		if (!IsValidIndex(ImportMeta.Clusters, ClusterId))
		{
			continue;
		}

		const FFbxClusterMeta& ClusterMeta = ImportMeta.Clusters[ClusterId];
		if (!ClusterMeta.bValid || !ClusterMeta.Cluster)
		{
			continue;
		}

		const int32 SkeletonBoneIndex = FindSkeletonBoneIndex(SkeletonMeta, ClusterMeta.LinkBoneId);
		if (SkeletonBoneIndex < 0)
		{
			UE_LOG("[FBXImporter] Cluster link bone is not in skeleton. MeshId=%d ClusterId=%d BoneId=%d SkeletonId=%d",
				MeshId,
				ClusterId,
				ClusterMeta.LinkBoneId,
				SkeletonMeta.SkeletonId);
			continue;
		}

		int32* ControlPointIndices = ClusterMeta.Cluster->GetControlPointIndices();
		double* ControlPointWeights = ClusterMeta.Cluster->GetControlPointWeights();
		const int32 ControlPointCount = ClusterMeta.Cluster->GetControlPointIndicesCount();
		for (int32 InfluenceIndex = 0; InfluenceIndex < ControlPointCount; ++InfluenceIndex)
		{
			const int32 ControlPointIndex = ControlPointIndices ? ControlPointIndices[InfluenceIndex] : -1;
			const float Weight = ControlPointWeights ? static_cast<float>(ControlPointWeights[InfluenceIndex]) : 0.0f;
			if (ControlPointIndex < 0 ||
				ControlPointIndex >= static_cast<int32>(ControlPointInfluences.size()) ||
				Weight <= 0.0f)
			{
				continue;
			}

			ControlPointInfluences[ControlPointIndex].push_back(
				{ static_cast<uint32>(SkeletonBoneIndex), Weight });
		}
	}

	const int32 FallbackBoneIndex = GetFallbackSkeletonBoneIndex(SkeletonMeta);
	int32 MissingInfluenceVertexCount = 0;
	const auto AssignWeights = [&](int32 ControlPointIndex, FSkeletalVertex& Vertex)
		{
			if (ControlPointIndex >= 0 &&
				ControlPointIndex < static_cast<int32>(ControlPointInfluences.size()) &&
				NormalizeTop4Influences(ControlPointInfluences[ControlPointIndex], Vertex.BoneIDs, Vertex.BoneWeights))
			{
				return;
			}

			AssignSingleBoneWeight(Vertex, FallbackBoneIndex);
			++MissingInfluenceVertexCount;
		};

	OutPart = {};
	OutPart.MeshId = MeshId;
	OutPart.SkinId = MeshMeta.PrimarySkinId;
	OutPart.SkeletonId = MeshMeta.SkeletonId;
	OutPart.bSkinned = true;
	OutPart.bRigidAttached = false;
	OutPart.SourceNodePath = MeshMeta.SourceNodePath;

	const FMatrix MeshBindGlobal = FindSkinnedMeshBindGlobal(MeshMeta, SkinMeta, ImportMeta);
	const FMatrix MeshToAssetBindMatrix = FbxMeshGeometryBuilder::BuildMeshToAssetBindMatrix(
		MeshMeta.Node,
		MeshBindGlobal);

	const bool bBuilt = FbxMeshGeometryBuilder::BuildSkeletalMeshPartGeometry(
		MeshMeta,
		MeshToAssetBindMatrix,
		AssignWeights,
		OutPart);
	if (!bBuilt)
	{
		UE_LOG("[FBXImporter] Failed to parse skinned mesh part geometry. MeshId=%d Node=%s",
			MeshId,
			MeshMeta.SourceNodePath.c_str());
		return false;
	}

	if (MissingInfluenceVertexCount > 0)
	{
		UE_LOG("[FBXImporter] Skinned mesh has vertices without weights; assigned fallback bone. MeshId=%d Count=%d FallbackSkeletonBoneIndex=%d",
			MeshId,
			MissingInfluenceVertexCount,
			FallbackBoneIndex);
	}

	return true;
}

bool FFbxSkeletalMeshPartParser::ParseRigidAttachedMeshPart(int32 MeshId, FFbxSkinnedMeshPart& OutPart) const
{
	if (!IsValidIndex(ImportMeta.Meshes, MeshId))
	{
		UE_LOG("[FBXImporter] Invalid rigid attached MeshId=%d", MeshId);
		return false;
	}

	const FFbxMeshMeta& MeshMeta = ImportMeta.Meshes[MeshId];
	if (MeshMeta.bHasSkin ||
		!MeshMeta.bRigidAttachedCandidate ||
		!IsValidIndex(ImportMeta.Bones, MeshMeta.AttachedBoneId) ||
		!IsValidIndex(ImportMeta.Skeletons, MeshMeta.AttachedSkeletonId) ||
		!MeshMeta.Mesh)
	{
		UE_LOG("[FBXImporter] Rigid attached mesh part has invalid meta. MeshId=%d", MeshId);
		return false;
	}

	const FFbxSkeletonMeta& SkeletonMeta = ImportMeta.Skeletons[MeshMeta.AttachedSkeletonId];
	const int32 AttachedSkeletonBoneIndex = FindSkeletonBoneIndex(SkeletonMeta, MeshMeta.AttachedBoneId);
	if (AttachedSkeletonBoneIndex < 0)
	{
		UE_LOG("[FBXImporter] Rigid attached mesh bone is not in skeleton. MeshId=%d BoneId=%d SkeletonId=%d",
			MeshId,
			MeshMeta.AttachedBoneId,
			MeshMeta.AttachedSkeletonId);
		return false;
	}

	const auto AssignWeights = [&](int32, FSkeletalVertex& Vertex)
		{
			AssignSingleBoneWeight(Vertex, AttachedSkeletonBoneIndex);
		};

	OutPart = {};
	OutPart.MeshId = MeshId;
	OutPart.SkeletonId = MeshMeta.AttachedSkeletonId;
	OutPart.AttachedBoneId = MeshMeta.AttachedBoneId;
	OutPart.AttachedSkeletonBoneIndex = AttachedSkeletonBoneIndex;
	OutPart.bSkinned = false;
	OutPart.bRigidAttached = true;
	OutPart.SourceNodePath = MeshMeta.SourceNodePath;

	const FMatrix MeshBindGlobal = BuildRigidAttachedMeshBindGlobal(MeshMeta, ImportMeta);
	const FMatrix MeshToAssetBindMatrix = FbxMeshGeometryBuilder::BuildMeshToAssetBindMatrix(
		MeshMeta.Node,
		MeshBindGlobal);

	const bool bBuilt = FbxMeshGeometryBuilder::BuildSkeletalMeshPartGeometry(
		MeshMeta,
		MeshToAssetBindMatrix,
		AssignWeights,
		OutPart);
	if (!bBuilt)
	{
		UE_LOG("[FBXImporter] Failed to parse rigid attached mesh part geometry. MeshId=%d Node=%s",
			MeshId,
			MeshMeta.SourceNodePath.c_str());
		return false;
	}

	UE_LOG("[FBXImporter] Parsed rigid attached mesh part. MeshId=%d Node=%s BoneId=%d BoneName=%s SkeletonBoneIndex=%d",
		MeshId,
		MeshMeta.SourceNodePath.c_str(),
		MeshMeta.AttachedBoneId,
		ImportMeta.Bones[MeshMeta.AttachedBoneId].Name.c_str(),
		AttachedSkeletonBoneIndex);

	return true;
}
