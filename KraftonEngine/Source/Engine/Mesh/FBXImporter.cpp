#include "FBXImporter.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Math/Transform.h"

bool FFBXImporter::Initialize()
{
	// 관리자 객체 생성
	m_manager = FbxManager::Create();

	// 가져올 씬의 요소(camera, light, mesh, texture, ...) 세팅
	FbxIOSettings* ios = FbxIOSettings::Create(m_manager, IOSROOT);
	m_manager->SetIOSettings(ios);

	m_scene = FbxScene::Create(m_manager, "My Scene");

	m_importer = FbxImporter::Create(m_manager, "");

	return true;
}

bool FFBXImporter::Import(const char* fileName, FStkeletalMesh& OutMesh)
{
	bool ret;

	// 1. 임포터 초기화
	ret = m_importer->Initialize(fileName, -1, m_manager->GetIOSettings());
	m_importer->Import(m_scene);

	// 임포트 후 임포터는 해제하여 메모리 사용량을 줄입니다.
	m_importer->Destroy();

	// 2. 삼각형화할 수 있는 노드를 삼각형화 시키기
	FbxGeometryConverter converter(m_manager);
	converter.Triangulate(m_scene, true);

	// 3. 메시 데이터 저장
	FindMesh(m_scene->GetRootNode());

	if (m_meshes.size() == 0)
		return false;

	// 4. control point별 VertexBlendingInfo 채우기
	TArray<FBoneInfo> ExtractedBones;
	BuildReferenceSkeleton(m_scene->GetRootNode(), ExtractedBones, -1);
	OutMesh.MeshAsset.Bones = ExtractedBones;

	// 4. control point별 VertexBlendingInfo 채우기
	// -> 이제 ExtractedBones 뼈대들의 이름을 기반으로 Cluster(가중치) 데이터를 찾아 매칭할 수 있습니다!
	for (int i = 0; i < m_meshes.size(); i++) {
		TArray<TArray<VertexBlendingInfo>> ControlPointWeights;
		BuildSkinningWeight(m_meshes[i], ControlPointWeights, ExtractedBones);

		// 5. 메시의 정점 데이터(위치, 인덱스) 저장
		SaveVertexData(m_meshes[i], ControlPointWeights, ExtractedBones);
	}

	//outMesh 완전히 채우기
	FSkeletalMeshAsset& OutAsset = OutMesh.MeshAsset;
	OutAsset.Bones = ExtractedBones;
	OutAsset.SourceVertices = m_Vertices;
	OutAsset.Indices = m_Indices;

	// 섹션 추가 로직
	TArray<FSkeletalMeshSection> BuiltSections;
	FSkeletalMeshSection SingleSection;
	SingleSection.MaterialIndex = 0;
	SingleSection.MaterialSlotName = "DefaultSlot";
	SingleSection.FirstIndex = 0;
	SingleSection.NumTriangles = m_Indices.size() / 3;

	BuiltSections.push_back(SingleSection);
	OutAsset.Sections = BuiltSections;

	OutMesh.PathFileName = fileName;
	OutMesh.CacheBounds();
	return true;
}
void FFBXImporter::FindMesh(FbxNode* InNode)
{
	// 찾은 노드가 메시타입이면 메시에 저장 후 반환합니다.
	FbxNodeAttribute* attribute = InNode->GetNodeAttribute();
	if (InNode->GetNodeAttribute() != nullptr)
	{
		if (InNode->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			m_meshes.push_back(InNode->GetMesh());
		}
	}

	// 노드의 자식의 수만큼 반복
	int childCnt = InNode->GetChildCount();
	for (int i = 0; i < childCnt; ++i)
	{
		FindMesh(InNode->GetChild(i));
	}

}

bool FFBXImporter::BuildReferenceSkeleton(FbxNode* InNode, TArray<FBoneInfo>& OutBoneInfo, int32 ParentIndex)
{
	FbxNodeAttribute* attribute = InNode->GetNodeAttribute();

	int32 CurrentIndex = ParentIndex;

	if (attribute && attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
	{
		FString BoneName = InNode->GetName();
		FbxAMatrix Local = InNode->EvaluateLocalTransform();
		FTransform LocalBonePose(
			FVector(Local.GetT()[0], Local.GetT()[1], Local.GetT()[2]) * 0.01f,
			FQuat(Local.GetQ()[0], Local.GetQ()[1], Local.GetQ()[2], Local.GetQ()[3]),
			FVector(Local.GetS()[0], Local.GetS()[1], Local.GetS()[2])
		);

		FbxAMatrix Global = InNode->EvaluateGlobalTransform();
		FTransform GlobalBonePose(
			FVector(Global.GetT()[0], Global.GetT()[1], Global.GetT()[2]) * 0.01f,
			FQuat(Global.GetQ()[0], Global.GetQ()[1], Global.GetQ()[2], Global.GetQ()[3]),
			FVector(Global.GetS()[0], Global.GetS()[1], Global.GetS()[2])
		);

		FBoneInfo newBone = {};
		newBone.Name = InNode->GetName();
		newBone.ParentIndex = ParentIndex;
		newBone.BindPoseGlobal = LocalBonePose.ToMatrix();
		newBone.InverseBindPose = GlobalBonePose.ToMatrix();
		OutBoneInfo.push_back(newBone);

		CurrentIndex = OutBoneInfo.size() - 1;
	}

	//자식 노드들을 순회하며 재귀 호출
	int childCnt = InNode->GetChildCount();
	for (int i = 0; i < childCnt; ++i)
	{
		// 내 인덱스(CurrentIndex)를 자식의 ParentIndex로 넘겨줍니다.
		BuildReferenceSkeleton(InNode->GetChild(i), OutBoneInfo, CurrentIndex);
	}

	return true;
}

bool FFBXImporter::BuildSkinningWeight(FbxMesh* InMesh, TArray<TArray<VertexBlendingInfo>>& OutWeights, const TArray<FBoneInfo>& InBones)
{
	// 1. 점(Control Point)의 총 개수만큼 우편함을 만듭니다.
	int cpCount = InMesh->GetControlPointsCount();
	OutWeights.resize(cpCount);

	int skinCount = InMesh->GetDeformerCount(FbxDeformer::eSkin);
	for (int i = 0; i < skinCount; ++i)
	{
		FbxSkin* skin = static_cast<FbxSkin*>(InMesh->GetDeformer(i, FbxDeformer::eSkin));
		int clusterCount = skin->GetClusterCount();

		for (int j = 0; j < clusterCount; ++j)
		{
			FbxCluster* cluster = skin->GetCluster(j);
			FbxNode* boneNode = cluster->GetLink();
			if (!boneNode) continue;

			// 2. 이 뼈대의 이름으로 아까 추출한 InBones 배열을 뒤져서 '뼈대 번호(인덱스)'를 알아냅니다.
			int boneIndex = -1;
			FString boneName = boneNode->GetName();
			for (int b = 0; b < InBones.size(); ++b) {
				if (InBones[b].Name == boneName) {
					boneIndex = b;
					break;
				}
			}
			if (boneIndex == -1) continue; // 목록에 없는 뼈면 무시

			// 3. 점들을 순회하며 Weight에 데이터를 넣습니다.
			int indexCount = cluster->GetControlPointIndicesCount();
			int* indices = cluster->GetControlPointIndices();
			double* weights = cluster->GetControlPointWeights();

			for (int k = 0; k < indexCount; ++k)
			{
				int ctrlPointIndex = indices[k];
				float weight = static_cast<float>(weights[k]);

				// Weight[ctrlPointIndex] 위치에 가중치 정보를 밀어 넣습니다!
				OutWeights[ctrlPointIndex].push_back({ boneIndex, weight });
			}
		}
	}
	return true;
}


bool FFBXImporter::SaveVertexData(FbxMesh* InMesh, const TArray<TArray<VertexBlendingInfo>>& InWeights, const TArray<FBoneInfo>& InBones)
{
	// 1. 삼각형(폴리곤)의 총 개수를 가져옵니다.
	int polygonCount = InMesh->GetPolygonCount();

	// 순수 위치 데이터가 담긴 창고(Control Points)를 가져옵니다.
	FbxVector4* controlPoints = InMesh->GetControlPoints();

	// 2. 모든 삼각형을 순회합니다.
	for (int i = 0; i < polygonCount; ++i)
	{
		// 3. 하나의 삼각형은 3개의 꼭짓점(Vertex)으로 이루어져 있습니다.
		for (int j = 0; j < 3; ++j)
		{
			// 이 꼭짓점이 Control Points 창고의 몇 번째 점인지 인덱스를 알아냅니다.
			int ctrlPointIndex = InMesh->GetPolygonVertex(i, j);

			FSkeletalSourceVertex vertex = {};

			// 초기화 (가중치 0, 인덱스 0으로 안전하게 초기화)
			for (int k = 0; k < 4; ++k) {
				vertex.BoneIndices[k] = 0;
				vertex.BoneWeights[k] = 0.0f;
			}
			vertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f); // 기본 흰색
			vertex.Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f); // 기본 탄젠트

			// --- A. 위치 (Position) 추출 ---
			// FBX(오른손, Y-up) -> DX(왼손, Y-up) 변환 (Z를 반전시키는 것이 일반적입니다)
			float x = static_cast<float>(controlPoints[ctrlPointIndex][0]);
			float y = static_cast<float>(controlPoints[ctrlPointIndex][1]);
			float z = static_cast<float>(-controlPoints[ctrlPointIndex][2]);
			vertex.Position = FVector(x, y, z);

			//--- B. 법선 (Normal) 추출 (임시 수도코드) ---
			FbxVector4 fbxNormal;
			InMesh->GetPolygonVertexNormal(i, j, fbxNormal);
			vertex.Normal.X = fbxNormal.mData[0];
			vertex.Normal.Y = fbxNormal.mData[1];
			vertex.Normal.Z = -fbxNormal.mData[2];


			// --- C. 텍스처 좌표 (UV) 추출 ---
			FbxVector2 fbxUV;
			bool unmapped;
			FbxStringList uvSetNameList;
			InMesh->GetUVSetNames(uvSetNameList); // 현재 메시가 가진 UV 세트들의 이름을 모두 가져옴

			// UV 세트가 하나라도 존재한다면, 첫 번째 UV 세트의 이름을 사용
			if (uvSetNameList.GetCount() > 0)
			{
				const char* uvSetName = uvSetNameList.GetStringAt(0);
				InMesh->GetPolygonVertexUV(i, j, uvSetName, fbxUV, unmapped);

				// DirectX는 UV의 원점(0,0)이 좌상단이지만 FBX는 좌하단일 때가 많아 V(y)축을 반전시키기도 합니다.
				// 모델링 툴(Max/Maya/Blender) 세팅에 따라 다르지만 보통 1.0f - v 를 해줍니다.
				vertex.UV.X = static_cast<float>(fbxUV.mData[0]);
				vertex.UV.Y = static_cast<float>(1.0f - fbxUV.mData[1]); // V 반전 (필요에 따라 적용)


			}
			else
			{
				// UV가 없는 모델일 경우 기본값 세팅
				vertex.UV = FVector2(0.0f, 0.0f);
			}

			//임시값
			vertex.Tangent = FVector4(1, 0, 0, 1);
			vertex.Color = FVector4(1, 1, 1, 1);

			// --- D. 스키닝 가중치---
			if (ctrlPointIndex < InWeights.size() && InWeights[ctrlPointIndex].size() > 0)
			{
				// 1. 내 번호의 우편함(배열)을 통째로 복사해옵니다.
				// (정렬을 해야 하므로 원본이 아닌 복사본을 씁니다)
				TArray<VertexBlendingInfo> tempWeights;
				for (const auto& w : InWeights[ctrlPointIndex]) {
					tempWeights.push_back(w);
				}

				// 2. 가중치가 높은 순(내림차순)으로 정렬
				std::sort(tempWeights.begin(), tempWeights.end(), [](const VertexBlendingInfo& a, const VertexBlendingInfo& b) {
					return a.mBlendingWeight > b.mBlendingWeight;
					});

				// 3. 상위 4개까지만 뽑아서 넣고 총합 구하기
				float totalWeight = 0.0f;
				int weightCount = (std::min)(static_cast<int>(tempWeights.size()), 4);

				for (int w = 0; w < weightCount; ++w) {
					vertex.BoneIndices[w] = tempWeights[w].mBlendingIndex;
					vertex.BoneWeights[w] = tempWeights[w].mBlendingWeight;
					totalWeight += tempWeights[w].mBlendingWeight;
				}

				// 4. 정규화 (총합이 1.0이 되도록 비율 조정)
				if (totalWeight > 0.0f) {
					for (int w = 0; w < 4; ++w) {
						vertex.BoneWeights[w] /= totalWeight;
					}
				}
				else {
					vertex.BoneIndices[0] = 0;
					vertex.BoneWeights[0] = 1.0f;
				}
			}
			else
			{
				// 이 점에 영향을 주는 뼈가 하나도 없을 때의 안전장치
				vertex.BoneIndices[0] = 0;
				vertex.BoneWeights[0] = 1.0f;
			}

			// 5. 완성된 정점을 엔진 버퍼 배열에 밀어 넣기
			m_Vertices.push_back(vertex);
			m_Indices.push_back(m_Vertices.size() - 1);
		}
	}

	return true;
}