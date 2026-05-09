#include "FBXImporter.h"
#include "SkeletalMesh.h"
#include "SkeletalMeshAsset.h"
 #include "Core/Log.h"

// FBX SDK Header
#include <fbxsdk.h>

/** 
 * FBX 전용 행렬(FbxAMatrix)을 엔진의 FMatrix로 변환하는 헬퍼 함수.
 */
static FMatrix FbxMatrixToFMatrix(const fbxsdk::FbxAMatrix& FbxMat)
{
	FMatrix OutMat;
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			OutMat.M[r][c] = (float)FbxMat.Get(r, c);
		}
	}
	return OutMat;
}

/**
 * Scene 내에서 eSkeleton 속성을 가진 모든 조인트 노드를 재귀적으로 수집합니다.
 */
static void GatherJoints(fbxsdk::FbxNode* Node, TArray<fbxsdk::FbxNode*>& OutJoints)
{
	if (!Node) return;

	fbxsdk::FbxNodeAttribute* Attr = Node->GetNodeAttribute();
	if (Attr && Attr->GetAttributeType() == fbxsdk::FbxNodeAttribute::eSkeleton)
	{
		OutJoints.push_back(Node);
	}

	for (int i = 0; i < Node->GetChildCount(); ++i)
	{
		GatherJoints(Node->GetChild(i), OutJoints);
	}
}

bool FFbxImporter::ImportSkeletalMesh(const FString& FilePath, USkeletalMesh* OutMesh)
{
	if (!OutMesh) return false;

	// 1. FbxManager 생성
	FbxManager* SdkManager = FbxManager::Create();
	if (!SdkManager)
	{
		UE_LOG("Failed to create FBX Manager.");
		return false;
	}

	// 2. IO Settings 설정
	FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
	SdkManager->SetIOSettings(ios);

	// 3. Importer 생성 및 파일 로드
	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
	if (!Importer->Initialize(FilePath.c_str(), -1, SdkManager->GetIOSettings()))
	{
		UE_LOG("Failed to initialize FBX Importer: %s", FilePath.c_str());
		Importer->Destroy();
		SdkManager->Destroy();
		return false;
	}

	// 4. Scene 생성 및 데이터 채우기
	FbxScene* Scene = FbxScene::Create(SdkManager, "MyScene");
	Importer->Import(Scene);
	Importer->Destroy();

	// 5. 좌표계 변환 (DirectX 스타일: Y-Up, Left-Handed 로직은 추후 상세 구현)
	// FbxAxisSystem::DirectX.ConvertScene(Scene);

	// 6. 데이터 추출을 위한 Raw Mesh 구조체 준비
	FSkeletalMesh* RawMesh = new FSkeletalMesh();
	RawMesh->PathFileName = FilePath;

	// 7. 스켈레톤 먼저 추출 (본 인덱스 매핑을 위해)
	ExtractSkeleton(Scene, OutMesh, RawMesh);

	// 8. 노드 순회하며 메시 데이터 추출
	FbxNode* RootNode = Scene->GetRootNode();
	if (RootNode)
	{
		ProcessNode(RootNode, OutMesh, RawMesh);
	}

	// 9. 추출된 데이터를 에셋에 주입
	OutMesh->SetSkeletalMeshAsset(RawMesh);

	// 10. SDK 자원 해제
	SdkManager->Destroy();

	UE_LOG("Successfully imported FBX: %s", FilePath.c_str());
	return true;
}

void FFbxImporter::ProcessNode(FbxNode* Node, USkeletalMesh* OutMesh, FSkeletalMesh* RawMesh)
{
	if (!Node) return;

	// 노드의 속성 확인
	FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
	if (Attribute)
	{
		if (Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			ExtractMesh((FbxMesh*)Attribute, RawMesh, OutMesh);
		}
	}

	// 자식 노드 재귀 순회
	for (int i = 0; i < Node->GetChildCount(); ++i)
	{
		ProcessNode(Node->GetChild(i), OutMesh, RawMesh);
	}
}

void FFbxImporter::ExtractMesh(FbxMesh* Mesh, FSkeletalMesh* RawMesh, USkeletalMesh* OutMesh)
{
	if (!Mesh || !RawMesh || !OutMesh) return;

	UE_LOG("Extracting Mesh: %s", Mesh->GetName());

	// 1. 삼각형화 (Triangulate) 확인
	// FBX SDK의 GeometryConverter를 사용하여 모든 폴리곤을 삼각형으로 변환할 수 있습니다.
	// 여기서는 이미 삼각형화 되어있다고 가정하거나, 수동으로 인덱스를 처리합니다.

	int ControlPointsCount = Mesh->GetControlPointsCount();
	FbxVector4* ControlPoints = Mesh->GetControlPoints();

	// 2. 스키닝 데이터 추출 (Control Point Index -> Bone Weights)
	struct FWeightInfo {
		int32 BoneIndex = 0;
		float Weight = 0.0f;
	};
	TArray<TArray<FWeightInfo>> CPWeights;
	CPWeights.resize(ControlPointsCount);

	int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	for (int i = 0; i < SkinCount; ++i)
	{
		FbxSkin* Skin = (FbxSkin*)Mesh->GetDeformer(i, FbxDeformer::eSkin);
		int ClusterCount = Skin->GetClusterCount();
		for (int j = 0; j < ClusterCount; ++j)
		{
			FbxCluster* Cluster = Skin->GetCluster(j);
			FbxNode* Link = Cluster->GetLink();
			if (!Link) continue;

			int32 BoneIndex = OutMesh->GetBoneIndex(FName(Link->GetName()));
			if (BoneIndex == -1) continue;

			// IBP 추출: TransformLinkMatrix는 바인딩 시점의 본 글로벌 행렬 (T-Pose 고정값).
			// 수식: IBP = TransformLinkMatrix^-1 * TransformMatrix
			// TransformMatrix     = 바인딩 시점의 메시 글로벌 행렬 (메시가 원점에 없을 때 보정용)
			// TransformLinkMatrix = 바인딩 시점의 본 글로벌 행렬
			// 결과적으로 IBP는 "정점을 본의 로컬 공간으로 가져오는 행렬"
			{
				FbxAMatrix TransformMatrix;
				FbxAMatrix TransformLinkMatrix;
				Cluster->GetTransformMatrix(TransformMatrix);
				Cluster->GetTransformLinkMatrix(TransformLinkMatrix);

				FbxAMatrix BindPoseMatrix = TransformLinkMatrix.Inverse() * TransformMatrix;
				RawMesh->Bones[BoneIndex].InverseBindMatrix = FbxMatrixToFMatrix(BindPoseMatrix);
			}

			// 이 본에 영향을 받는 정점 인덱스와 가중치 추출
			int IndexCount = Cluster->GetControlPointIndicesCount();
			int* Indices = Cluster->GetControlPointIndices();
			double* Weights = Cluster->GetControlPointWeights();

			for (int k = 0; k < IndexCount; ++k)
			{
				CPWeights[Indices[k]].push_back({ BoneIndex, (float)Weights[k] });
			}

			// (Optional) 여기서 Cluster의 TransformLinkMatrix를 이용해 BindPose Matrix를 보정할 수 있습니다.
		}
	}

	// 3. 정점 및 인덱스 버퍼 구축
	int PolygonCount = Mesh->GetPolygonCount();
	uint32 VertexOffset = (uint32)RawMesh->Vertices.size();

	for (int i = 0; i < PolygonCount; ++i)
	{
		int PolygonSize = Mesh->GetPolygonSize(i);
		// 삼각형만 처리 (삼각형화가 미리 되어있어야 함)
		if (PolygonSize != 3) continue;

		for (int j = 0; j < 3; ++j)
		{
			int CPIndex = Mesh->GetPolygonVertex(i, j);
			FSkeletalMeshVertex Vertex;

			// Position
			FbxVector4 Pos = ControlPoints[CPIndex];
			Vertex.Position = FVector((float)Pos[0], (float)Pos[1], (float)Pos[2]);

			// Normal (간단히 첫 번째 레이어 사용)
			FbxVector4 Normal;
			if (Mesh->GetPolygonVertexNormal(i, j, Normal))
			{
				Vertex.Normal = FVector((float)Normal[0], (float)Normal[1], (float)Normal[2]);
			}

			// UV (간단히 첫 번째 레이어 사용)
			FbxVector2 UV;
			bool bUnmapped;
			if (Mesh->GetPolygonVertexUV(i, j, "", UV, bUnmapped))
			{
				Vertex.UV = FVector2((float)UV[0], 1.0f - (float)UV[1]); // DirectX UV Flip
			}

			// Color (기본 흰색)
			Vertex.Color = FVector4(1, 1, 1, 1);

			// Skinning Weights (최대 4개)
			const auto& Weights = CPWeights[CPIndex];
			float TotalWeight = 0.0f;
			for (int k = 0; k < (int)Weights.size() && k < 4; ++k)
			{
				Vertex.boneIndices[k] = Weights[k].BoneIndex;
				Vertex.boneWeights[k] = Weights[k].Weight;
				TotalWeight += Vertex.boneWeights[k];
			}

			// 가중치 정규화
			if (TotalWeight > 0.0f)
			{
				for (int k = 0; k < 4; ++k) Vertex.boneWeights[k] /= TotalWeight;
			}

			RawMesh->Vertices.push_back(Vertex);
			RawMesh->Indices.push_back((uint32)RawMesh->Indices.size()); // 간단한 인덱싱
		}
	}

	// 섹션 추가 (전체 메시를 하나의 섹션으로 처리)
	FStaticMeshSection Section;
	Section.MaterialSlotName = "Default";
	Section.FirstIndex = VertexOffset;
	Section.NumTriangles = (uint32)(RawMesh->Indices.size() - VertexOffset) / 3;
	RawMesh->Sections.push_back(Section);

	UE_LOG("Extracted %d vertices and %d indices.", RawMesh->Vertices.size(), RawMesh->Indices.size());
}

void FFbxImporter::ExtractSkeleton(FbxScene* Scene, USkeletalMesh* OutMesh, FSkeletalMesh* RawMesh)
{
	UE_LOG("Extracting Skeleton from Scene.");

	// 1. 모든 조인트 노드 수집
	TArray<FbxNode*> JointNodes;
	GatherJoints(Scene->GetRootNode(), JointNodes);

	if (JointNodes.empty())
	{
		UE_LOG("No skeleton found in FBX scene.");
		return;
	}

	// 2. 본 이름 리스트 및 매핑 구축
	TArray<FName> BoneNames;
	for (FbxNode* JointNode : JointNodes)
	{
		BoneNames.push_back(FName(JointNode->GetName()));
	}
	OutMesh->SetBoneNames(std::move(BoneNames));

	// 3. FBone 데이터 생성 및 계층 구조 설정
	RawMesh->Bones.resize(JointNodes.size());
	for (int32 i = 0; i < (int32)JointNodes.size(); ++i)
	{
		FbxNode* CurrentJoint = JointNodes[i];
		FBone& CurrentBone = RawMesh->Bones[i];

		// 부모 찾기
		FbxNode* ParentNode = CurrentJoint->GetParent();
		CurrentBone.ParentIndex = -1;

		if (ParentNode)
		{
			// 수집된 조인트 리스트에서 부모의 인덱스를 찾음
			for (int32 j = 0; j < (int32)JointNodes.size(); ++j)
			{
				if (JointNodes[j] == ParentNode)
				{
					CurrentBone.ParentIndex = j;
					break;
				}
			}
		}

		// 초기 트랜스폼 정보 추출 (Local)
		FbxAMatrix LocalTransform = CurrentJoint->EvaluateLocalTransform();
		FbxVector4 T = LocalTransform.GetT();
		FbxQuaternion Q = LocalTransform.GetQ();
		FbxVector4 S = LocalTransform.GetS();

		CurrentBone.Translation = FVector((float)T[0], (float)T[1], (float)T[2]);
		CurrentBone.Rotation = FQuat((float)Q[0], (float)Q[1], (float)Q[2], (float)Q[3]);
		CurrentBone.Scale = FVector((float)S[0], (float)S[1], (float)S[2]);

		// IBP는 ExtractMesh()의 Cluster에서 TransformLinkMatrix 기반으로 덮어씀.
		// EvaluateGlobalTransform()은 현재 시간 기준 행렬이므로 IBP 계산에 부적합.
		CurrentBone.InverseBindMatrix = FMatrix::Identity;

	}

	UE_LOG("Successfully extracted %d bones.", RawMesh->Bones.size());
}
