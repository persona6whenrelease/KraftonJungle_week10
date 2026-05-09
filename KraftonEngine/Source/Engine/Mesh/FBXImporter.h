#pragma once
#include <fbxsdk.h>
#include <DirectXMath.h>
#include "Engine/Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Mesh/SkeletalMeshAsset.h"

// 스키닝 정보를 담을 구조체
struct VertexBlendingInfo {
	int mBlendingIndex;
	float mBlendingWeight;
};

// 자체 엔진용 거대 정점 구조체 (PNTIW)
struct PNTIWVertex {
	DirectX::XMFLOAT3 mPosition;
	DirectX::XMFLOAT3 mNormal;
	DirectX::XMFLOAT2 mUV;
	std::vector<VertexBlendingInfo> mVertexBlendingInfos;

	// 가중치 순으로 정렬하는 함수 (4개만 남기기 위해)
	void SortBlendingInfoByWeight() {
		// ... 정렬 로직 ...
	}
};



class FFBXImporter {
public:
	FFBXImporter() = default;
	~FFBXImporter() = default;

	bool Initialize();

	bool Import(const char* fileName, FStkeletalMesh& OutMesh);

	TArray<FSkeletalSourceVertex> GetVertexPos() { return m_Vertices; }
	TArray<uint32> GetVertexIdx() { return m_Indices; }

private:
	void FindMesh(FbxNode* InNode);
	bool BuildReferenceSkeleton(FbxNode* InNode, TArray<FBoneInfo>& OutBoneInfo, int32 ParentIndex);
	bool BuildSkinningWeight(FbxMesh* InMesh, TArray<TArray<VertexBlendingInfo>>& OutWeights, const TArray<FBoneInfo>& InBones);
	bool SaveVertexData(FbxMesh* InMesh, const TArray<TArray<VertexBlendingInfo>>& InWeights, const TArray<FBoneInfo>& InBones);
	void Shutdown();

private:
	FbxManager* m_manager;    
	FbxImporter* m_importer; 

	FbxScene* m_scene;
	TArray<FbxMesh*> m_meshes;

	TArray<FSkeletalSourceVertex> m_Vertices;
	TArray<uint32> m_Indices;
};
