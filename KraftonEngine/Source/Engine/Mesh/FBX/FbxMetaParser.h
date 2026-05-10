#pragma once
#include "FBXImportMeta.h"
#include "FBXUtil.h"

class FFbxMetaParser final
{
public:
	FFbxMetaParser(FFbxImportMeta& InImportMeta) : ImportMeta(InImportMeta) {}
	~FFbxMetaParser() = default;

	bool BuildFbxMeta(FbxScene* Scene);

private:
	int32 RegisterNodeRecursive(FbxNode* Node, int32 ParentNodeId, const FString& ParentPath);
	void RegisterSkinsForMesh(int32 MeshId);
	void EnsureBoneParentChain(int32 BoneId);
	void BuildRegisteredBoneHierarchyLinks();
	void BuildSkeletonTables();
	void ClassifyMeshes();
	bool ValidateFbxMeta() const;

private:
	int32 FindNearestParentBoneIdForNode(FbxNode* Node) const;
	int32 FindSkeletonIdForBone(int32 BoneId) const;
	void AttachRigidMeshesToSkeletons();
	bool IsSceneRootNode(FbxNode* Node) const;
	bool CanPromoteNodeToBoneParent(FbxNode* Node) const;
	void LinkBoneParentChild(int32 ParentBoneId, int32 ChildBoneId);
	void LinkBoneToNearestValidParent(int32 BoneId);
	int32 RegisterCluster(int32 SkinId, FbxCluster* Cluster);
	int32 RegisterBoneNode(FbxNode* Node, bool bReferencedByCluster, bool bInsertedAsParentChain);
	void RegisterMeshFromNode(FbxNode* Node, int32 NodeId);
	int32 FindTopRootBone(int32 BoneId) const;
	int32 FindSkeletonRootBoneForSkin(const TArray<int32>& BoneIds) const;
	void AddBoneDfs(int32 CurrentBoneId, FFbxSkeletonMeta& SkeletonMeta, uint32 SkeletonId);

private:
	FFbxImportMeta& ImportMeta;
};

