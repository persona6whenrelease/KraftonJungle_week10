#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Math/Matrix.h"
#include "FBXUtil.h"

struct FFbxNodeMeta
{
	int32 NodeId = -1;
	int32 ParentNodeId = -1;
	TArray<int32> ChildNodeIds;
	FbxNode* Node = nullptr;
	FString Name;
	FString FullPath;
	FString AttributeTypeName;
	bool bHasAttribute = false;
	bool bHasMesh = false;
	bool bHasSkeleton = false;
	bool bHasLight = false;
	bool bHasCamera = false;
	FMatrix LocalTransform = FMatrix::Identity;
	FMatrix GlobalTransform = FMatrix::Identity;
};

struct FFbxMeshMeta
{
	int32 MeshId = -1;
	int32 NodeId = -1;
	FbxNode* Node = nullptr;
	FbxMesh* Mesh = nullptr;
	FString Name;
	FString SourceNodePath;
	TArray<int32> MaterialSlotIds;
	TArray<FString> MaterialSlotNames;
	TArray<FString> MaterialUVSetNames;
	TArray<int32> MaterialIds;
	TArray<int32> SkinIds;
	int32 PrimarySkinId = -1;
	int32 SkeletonId = -1;
	int32 AttachedSkeletonId = -1;
	int32 AttachedBoneId = -1;
	bool bHasSkin = false;
	bool bHasValidSkin = false;
	bool bStaticCandidate = false;
	bool bSkeletalCandidate = false;
	bool bAttachedToSkeleton = false;
	bool bRigidAttachedCandidate = false;
	bool bIndependentStaticCandidate = false;
	int32 ControlPointCount = 0;
	int32 PolygonCount = 0;
};

struct FFbxSkinMeta
{
	int32 SkinId = -1;
	int32 MeshId = -1;
	FbxSkin* Skin = nullptr;
	TArray<int32> ClusterIds;
	TArray<int32> BoneIds;
	int32 SkeletonId = -1;
	bool bValid = false;
	int32 ClusterCount = 0;
	int32 TotalInfluenceCount = 0;
};

struct FFbxClusterMeta
{
	int32 ClusterId = -1;
	int32 SkinId = -1;
	int32 MeshId = -1;
	FbxCluster* Cluster = nullptr;
	FbxNode* LinkNode = nullptr;
	int32 LinkNodeId = -1;
	int32 LinkBoneId = -1;
	FString LinkNodeName;
	FMatrix MeshBindGlobalMatrix = FMatrix::Identity;
	FMatrix BoneBindGlobalMatrix = FMatrix::Identity;
	bool bHasMeshBindMatrix = false;
	bool bHasBoneBindMatrix = false;
	int32 ControlPointInfluenceCount = 0;
	int32 PositiveWeightCount = 0;
	bool bValid = false;
};

struct FFbxBoneMeta
{
	int32 BoneId = -1;
	int32 NodeId = -1;
	FbxNode* Node = nullptr;
	FString Name;
	FString FullPath;
	int32 ParentBoneId = -1;
	TArray<int32> ChildBoneIds;
	int32 SkeletonId = -1;
	int32 SkeletonBoneIndex = -1;
	FMatrix ModelLocalMatrix = FMatrix::Identity;
	FMatrix ModelGlobalMatrix = FMatrix::Identity;
	FMatrix BindGlobalMatrix = FMatrix::Identity;
	FMatrix InvBindGlobalMatrix = FMatrix::Identity;
	bool bReferencedByCluster = false;
	bool bInsertedAsParentChain = false;
	bool bSyntheticRoot = false;
};

struct FFbxSkeletonMeta
{
	int32 SkeletonId = -1;
	int32 RootBoneId = -1;
	int32 RootNodeId = -1;
	FString Name;
	TArray<int32> BoneIds;
	TArray<int32> MeshIds;
	TArray<int32> SkinnedMeshIds;
	TArray<int32> RigidAttachedMeshIds;
	TMap<int32, int32> BoneIdToSkeletonBoneIndex;
	TMap<FbxNode*, int32> BoneNodeToSkeletonBoneIndex;
	bool bValid = false;
	bool bBuiltFromSkinClusters = false;
	bool bHasSingleRoot = true;
};

struct FFbxMaterialInfo
{
	int32 MaterialId = -1;
	FString MaterialSlotName = "None";
	FString MaterialAssetPath;
	FVector DiffuseColor = FVector(1.0f, 0.0f, 1.0f);
	FString DiffuseTexturePath;
	FString NormalTexturePath;
	FString SpecularTexturePath;
	FString EmissiveTexturePath;
	FString DiffuseUVSetName;
};

struct FFbxImportMeta
{
	FString SourceFilePath;
	TArray<FFbxNodeMeta> Nodes;
	TArray<FFbxMeshMeta> Meshes;
	TArray<FFbxSkinMeta> Skins;
	TArray<FFbxClusterMeta> Clusters;
	TArray<FFbxBoneMeta> Bones;
	TArray<FFbxSkeletonMeta> Skeletons;
	TArray<FFbxMaterialInfo> Materials;
	TArray<int32> StaticMeshIds;
	TArray<int32> SkeletalMeshIds;
	TArray<int32> RigidAttachedMeshIds;
	TArray<int32> IndependentStaticMeshIds;
	TArray<int32> LightNodeIds;
	TArray<int32> CameraNodeIds;
	TMap<FbxNode*, int32> NodeToNodeId;
	TMap<FbxMesh*, int32> MeshToMeshId;
	TMap<FbxMesh*, TArray<int32>> FbxMeshToMeshIds;
	TMap<FbxSkin*, int32> SkinToSkinId;
	TMap<FbxCluster*, int32> ClusterToClusterId;
	TMap<FbxNode*, int32> BoneNodeToBoneId;
	TMap<FbxSurfaceMaterial*, int32> MaterialToMaterialId;
	TMap<FString, int32> MaterialNameToMaterialId;

	void Clear()
	{
		SourceFilePath.clear();
		Nodes.clear();
		Meshes.clear();
		Skins.clear();
		Clusters.clear();
		Bones.clear();
		Skeletons.clear();
		Materials.clear();
		StaticMeshIds.clear();
		SkeletalMeshIds.clear();
		RigidAttachedMeshIds.clear();
		IndependentStaticMeshIds.clear();
		LightNodeIds.clear();
		CameraNodeIds.clear();
		NodeToNodeId.clear();
		MeshToMeshId.clear();
		FbxMeshToMeshIds.clear();
		SkinToSkinId.clear();
		ClusterToClusterId.clear();
		BoneNodeToBoneId.clear();
		MaterialToMaterialId.clear();
		MaterialNameToMaterialId.clear();
	}

	bool IsAncestorOf(int32 AncestorId, int32 BoneId)
	{
		int32 CurrentId = BoneId;
		while (CurrentId >= 0 && static_cast<size_t>(CurrentId) < Bones.size())
		{
			if (CurrentId == AncestorId)
			{
				return true;
			}
			CurrentId = Bones[CurrentId].ParentBoneId;
		}
		return false;
	};
};
