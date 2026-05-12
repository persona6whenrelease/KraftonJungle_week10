#pragma once
#include "FBXImportMeta.h"
#include "FBXUtil.h"

class FFbxMetaParser final
{
public:
	// 외부에서 호출하는 FBX meta parser 진입점입니다.
	FFbxMetaParser(FFbxImportMeta& InImportMeta) : ImportMeta(InImportMeta) {}
	~FFbxMetaParser() = default;

	bool BuildFbxMeta(FbxScene* Scene);

private:
	// BuildFbxMeta()가 순서대로 호출하는 주요 단계입니다.
	int32 RegisterNodeRecursive(FbxNode* Node, int32 ParentNodeId, const FString& ParentPath);
	void RegisterSkinsForMesh(int32 MeshId);
	void EnsureBoneParentChain(int32 BoneId);
	void BuildRegisteredBoneHierarchyLinks();
	void BuildSkeletonTables();
	void AttachRigidMeshesToSkeletons();
	void ClassifyMeshes();
	bool ValidateFbxMeta() const;

private:
	// node/mesh/skin/cluster/bone 테이블 등록을 보조합니다.
	void RegisterMeshFromNode(FbxNode* Node, int32 NodeId);
	int32 RegisterMaterial(FbxSurfaceMaterial* SurfaceMaterial);
	int32 RegisterCluster(int32 SkinId, FbxCluster* Cluster);
	int32 RegisterBoneNode(FbxNode* Node, bool bReferencedByCluster, bool bInsertedAsParentChain);

private:
	// FBX node parent chain을 BoneMeta hierarchy로 보정합니다.
	bool IsSceneRootNode(FbxNode* Node) const;
	bool CanPromoteNodeToBoneParent(FbxNode* Node) const;
	void LinkBoneParentChild(int32 ParentBoneId, int32 ChildBoneId);
	void LinkBoneToNearestValidParent(int32 BoneId);
	int32 FindTopRootBone(int32 BoneId) const;

private:
	// skeleton grouping과 rigid attached mesh 연결을 보조합니다.
	int32 FindOrCreateSkeletonForRoot(int32 RootBoneId, bool bBuiltFromSkinClusters, bool bHasSingleRoot, TMap<int32, int32>& RootBoneIdToSkeletonId);
	void AddBoneDfs(int32 CurrentBoneId, FFbxSkeletonMeta& SkeletonMeta, uint32 SkeletonId);
	int32 FindSkeletonRootBoneForSkin(const TArray<int32>& BoneIds) const;
	bool ShouldBuildRigidSkeletonForRoot(int32 RootBoneId) const;
	int32 FindSkeletonIdForBone(int32 BoneId) const;
	int32 FindNearestParentBoneIdForNode(FbxNode* Node) const;

private:
	// parser가 채우는 import meta 참조입니다.
	FFbxImportMeta& ImportMeta;
};

