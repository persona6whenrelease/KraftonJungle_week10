#pragma once

#include "Core/CoreTypes.h"
#include "Mesh/SkeletalMeshAsset.h"

#include <cstddef>
#include <unordered_map>

namespace fbxsdk
{
	class FbxAMatrix;
	class FbxCluster;
	class FbxManager;
	class FbxMesh;
	class FbxNode;
	class FbxScene;
	class FbxVector2;
	class FbxVector4;
}

using FbxAMatrix = fbxsdk::FbxAMatrix;
using FbxCluster = fbxsdk::FbxCluster;
using FbxManager = fbxsdk::FbxManager;
using FbxMesh = fbxsdk::FbxMesh;
using FbxNode = fbxsdk::FbxNode;
using FbxScene = fbxsdk::FbxScene;
using FbxVector2 = fbxsdk::FbxVector2;
using FbxVector4 = fbxsdk::FbxVector4;

class FBXImporter
{
#pragma region InternalPrivateStruct
private:
	struct FTempInfluence
	{
		uint32 BoneIndex = 0;
		float Weight = 0.0f;
	};

	struct FVertexKey
	{
		int32 PosX = 0;
		int32 PosY = 0;
		int32 PosZ = 0;
		int32 NormalX = 0;
		int32 NormalY = 0;
		int32 NormalZ = 0;
		int32 UVX = 0;
		int32 UVY = 0;
		int32 TangentX = 0;
		int32 TangentY = 0;
		int32 TangentZ = 0;
		int32 TangentW = 0;
		uint32 BoneIDs[4] = { 0, 0, 0, 0 };
		int32 BoneWeight0 = 0;
		int32 BoneWeight1 = 0;
		int32 BoneWeight2 = 0;
		int32 BoneWeight3 = 0;

		bool operator==(const FVertexKey& Other) const;
	};

	struct FVertexKeyHasher
	{
		size_t operator()(const FVertexKey& Key) const;
	};
#pragma endregion

public:
	FBXImporter() = default;
	~FBXImporter();

	bool ImportSkeletalMesh(const FString& InFilePath, FSkeletalMesh& OutMesh);

private:
	void ClearState();
	bool InitializeSdk();
	void ShutdownSdk();
	void DestroyScene();

	bool LoadScene(const FString& InFilePath);
	void PreprocessScene();
	void GenerateTangents(FbxMesh* Mesh);

	bool FindFirstSkeletalMesh(FbxNode*& OutNode, FbxMesh*& OutMesh);
	bool FindFirstSkeletalMeshRecursive(FbxNode* Node, FbxNode*& OutNode, FbxMesh*& OutMesh);
	bool HasSkinDeformer(FbxMesh* Mesh) const;

	bool BuildBones(FbxMesh* Mesh, FSkeletalMesh& OutMesh);
	bool CollectBoneNodes(FbxMesh* Mesh, TArray<FbxNode*>& OutBoneNodes);
	void BuildBoneIndexMap(const TArray<FbxNode*>& BoneNodes);
	void FillBoneInfos(const TArray<FbxNode*>& BoneNodes, FSkeletalMesh& OutMesh);
	void BuildBoneParentIndices(const TArray<FbxNode*>& BoneNodes, FSkeletalMesh& OutMesh);
	void BuildBindPoseMatrices(const TArray<FbxNode*>& BoneNodes, FSkeletalMesh& OutMesh);

	bool BuildControlPointInfluences(FbxMesh* Mesh);
	void AddClusterInfluences(FbxCluster* Cluster);
	void NormalizeTop4(const TArray<FTempInfluence>& InInfluences, uint32 OutBoneIDs[4], float OutWeights[4]) const;

	bool BuildVerticesIndicesAndSections(FbxMesh* Mesh, FSkeletalMesh& OutMesh);
	bool BuildVertexFromCorner(
		FbxMesh* Mesh,
		int32 PolyIndex,
		int32 CornerIndex,
		int32 PolygonVertexIndex,
		const char* UVSetName,
		FSkeletalVertex& OutVertex);

	FVector ReadPosition(FbxMesh* Mesh, int32 ControlPointIndex) const;
	FVector ReadNormal(FbxMesh* Mesh, int32 PolyIndex, int32 CornerIndex) const;
	FVector2 ReadUV(FbxMesh* Mesh, int32 PolyIndex, int32 CornerIndex, const char* UVSetName) const;
	FVector4 ReadTangent(FbxMesh* Mesh, int32 ControlPointIndex, int32 PolygonVertexIndex) const;
	int32 ReadMaterialIndex(FbxMesh* Mesh, int32 PolyIndex) const;

	FVertexKey MakeVertexKey(const FSkeletalVertex& Vertex) const;
	int32 QuantizeFloat(float Value) const;
	uint32 AddOrReuseSkeletalVertex(const FSkeletalVertex& Vertex, FSkeletalMesh& OutMesh);

	void CacheMaterialSlotNames(FbxMesh* Mesh);
	void BuildSectionsFromMaterialBuckets(FSkeletalMesh& OutMesh);

	void FinalizeMesh(const FString& InFilePath, FSkeletalMesh& OutMesh);
	bool ValidateImportedMesh(const FSkeletalMesh& Mesh) const;
	void LogImportSummary(const FSkeletalMesh& Mesh) const;

	FVector ConvertFbxVector(const FbxVector4& V) const;
	FVector2 ConvertFbxVector2(const FbxVector2& V) const;
	FMatrix ConvertFbxMatrix(const FbxAMatrix& M) const;

private:
	FbxManager* Manager = nullptr;
	FbxScene* Scene = nullptr;

	std::unordered_map<FbxNode*, int32> BoneNodeToIndex;
	std::unordered_map<FbxNode*, FMatrix> BoneGlobalBindPoseByNode;
	FMatrix MeshGlobalBindPose = FMatrix::Identity;

	TArray<TArray<FTempInfluence>> ControlPointInfluences;
	std::unordered_map<FVertexKey, uint32, FVertexKeyHasher> VertexMap;
	std::unordered_map<int32, TArray<uint32>> IndicesByMaterial;
	std::unordered_map<int32, FString> MaterialSlotNamesByIndex;

	uint32 TotalPolygonCornerCount = 0;
};
