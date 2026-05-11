#include "FBXImporter.h"
#include "SkeletalMesh.h"
#include "SkeletalMeshAsset.h"
#include "Core/Log.h"

// FBX SDK Header
#ifndef FBXSDK_SHARED
#define FBXSDK_SHARED
#endif
#include <fbxsdk.h>

/**
 * FBX 전용 행렬(FbxAMatrix)을 엔진의 FMatrix로 변환하는 헬퍼 함수.
 * 실측 결과 FbxAMatrix는 row 3에 Translation을 저장하는 row-major-호환 메모리 컨벤션을
 * 사용하므로(엔진 FMatrix와 동일), transpose 없이 직접 복사한다.
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

/**
 * Cluster의 (TransformLinkMatrix L, TransformMatrix M)를 본 노드별로 수집.
 * LocalTransform을 TLM 역산으로 만들기 위한 사전 데이터.
 */
struct FBindPoseInfo
{
	fbxsdk::FbxAMatrix TransformLink;   // L: 본 글로벌 (col-major)
	fbxsdk::FbxAMatrix TransformMesh;   // M: 메시 노드 글로벌
	bool bValid = false;
};

static void GatherBindPoseInfo(fbxsdk::FbxNode* Node, TMap<fbxsdk::FbxNode*, FBindPoseInfo>& Out)
{
	if (!Node) return;

	fbxsdk::FbxNodeAttribute* Attr = Node->GetNodeAttribute();
	if (Attr && Attr->GetAttributeType() == fbxsdk::FbxNodeAttribute::eMesh)
	{
		fbxsdk::FbxMesh* Mesh = (fbxsdk::FbxMesh*)Attr;
		int SkinCount = Mesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);
		for (int i = 0; i < SkinCount; ++i)
		{
			fbxsdk::FbxSkin* Skin = (fbxsdk::FbxSkin*)Mesh->GetDeformer(i, fbxsdk::FbxDeformer::eSkin);
			int ClusterCount = Skin->GetClusterCount();
			for (int j = 0; j < ClusterCount; ++j)
			{
				fbxsdk::FbxCluster* Cluster = Skin->GetCluster(j);
				fbxsdk::FbxNode* Link = Cluster->GetLink();
				if (!Link) continue;

				FBindPoseInfo Info;
				Cluster->GetTransformLinkMatrix(Info.TransformLink);
				Cluster->GetTransformMatrix(Info.TransformMesh);
				Info.bValid = true;
				Out[Link] = Info;
			}
		}
	}

	for (int i = 0; i < Node->GetChildCount(); ++i)
	{
		GatherBindPoseInfo(Node->GetChild(i), Out);
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

	// 5. 좌표계 변환 (엔진 규격: Z-Up, Left-Handed, X-Forward)
	FbxAxisSystem EngineAxisSystem(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eLeftHanded);
	EngineAxisSystem.ConvertScene(Scene);

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

			// IBP 계산: 메시 노드 변환(M)과 본 글로벌(L) 모두 반영.
			// FBX(Column-Major)에서 본의 메시 공간 바인드 글로벌 = M^-1 * L 이므로
			// IBP_FBX = (M^-1 * L)^-1 = L^-1 * M.
			// FbxMatrixToFMatrix가 transpose를 적용하므로 FBX 공간에서 합성 후 한 번에 변환.
			{
				FbxAMatrix FbxTransformMatrix;
				Cluster->GetTransformMatrix(FbxTransformMatrix);            // 메시 노드 변환 M

				FbxAMatrix FbxTransformLinkMatrix;
				Cluster->GetTransformLinkMatrix(FbxTransformLinkMatrix);    // 본 글로벌 L

				FbxAMatrix IBP_Fbx = FbxTransformLinkMatrix.Inverse() * FbxTransformMatrix;
				RawMesh->Bones[BoneIndex].InverseBindMatrix = FbxMatrixToFMatrix(IBP_Fbx);

				// 진단 로그: 모든 본의 M, L, IBP Translation 한 줄 출력
				const FMatrix& IBP = RawMesh->Bones[BoneIndex].InverseBindMatrix;
				//UE_LOG("[IBP Diag] Bone[%d] %s M=(%.3f, %.3f, %.3f) L=(%.3f, %.3f, %.3f) IBP.Row3=(%.3f, %.3f, %.3f)",
				//	BoneIndex, Link->GetName(),
				//	(float)FbxTransformMatrix.Get(3, 0), (float)FbxTransformMatrix.Get(3, 1), (float)FbxTransformMatrix.Get(3, 2),
				//	(float)FbxTransformLinkMatrix.Get(3, 0), (float)FbxTransformLinkMatrix.Get(3, 1), (float)FbxTransformLinkMatrix.Get(3, 2),
				//	IBP.M[3][0], IBP.M[3][1], IBP.M[3][2]);

				//// Bone[0] 한정: transpose 가설 검증용 추가 출력
				//if (BoneIndex == 0)
				//{
				//	FbxVector4 TLM_T = FbxTransformLinkMatrix.GetT();
				//	UE_LOG("[IBP Verify] Bone[0] L.GetT()=(%.3f, %.3f, %.3f) L.Get(0,3..2,3)=(%.3f, %.3f, %.3f) IBP.Col3=(%.3f, %.3f, %.3f)",
				//		(float)TLM_T[0], (float)TLM_T[1], (float)TLM_T[2],
				//		(float)FbxTransformLinkMatrix.Get(0, 3), (float)FbxTransformLinkMatrix.Get(1, 3), (float)FbxTransformLinkMatrix.Get(2, 3),
				//		IBP.M[0][3], IBP.M[1][3], IBP.M[2][3]);
				//}
			}

			// 이 본에 영향을 받는 정점 인덱스와 가중치 추출
			int IndexCount = Cluster->GetControlPointIndicesCount();
			int* Indices = Cluster->GetControlPointIndices();
			double* Weights = Cluster->GetControlPointWeights();

			for (int k = 0; k < IndexCount; ++k)
			{
				CPWeights[Indices[k]].push_back({ BoneIndex, (float)Weights[k] });
			}
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
			FSkeletalMeshVertex Vertex = {}; // 초기화 필수 (쓰레기 값 방지)

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

	// 1-b. 본별 BindPose 정보(L, M) 수집 — TLM 역산용
	TMap<FbxNode*, FBindPoseInfo> BoneBindPose;
	GatherBindPoseInfo(Scene->GetRootNode(), BoneBindPose);

	// 2. 본 이름 리스트 및 매핑 구축
	TArray<FName> BoneNames;
	for (FbxNode* JointNode : JointNodes)
	{
		BoneNames.push_back(FName(JointNode->GetName()));
	}
	OutMesh->SetBoneNames(std::move(BoneNames));

	// 3. FBone 데이터 생성 및 계층 구조 설정
	RawMesh->Bones.resize(JointNodes.size());

	int32 NumWithCluster = 0;
	int32 NumFallback = 0;

	for (int32 i = 0; i < (int32)JointNodes.size(); ++i)
	{
		FbxNode* CurrentJoint = JointNodes[i];
		FBone& CurrentBone = RawMesh->Bones[i];

		// 부모 찾기 — Empty/Null 노드를 건너뛰고 본 트리 부모까지 거슬러 올라감
		CurrentBone.ParentIndex = -1;
		FbxNode* ParentNode = CurrentJoint->GetParent();
		while (ParentNode)
		{
			bool bFound = false;
			for (int32 j = 0; j < (int32)JointNodes.size(); ++j)
			{
				if (JointNodes[j] == ParentNode)
				{
					CurrentBone.ParentIndex = j;
					bFound = true;
					break;
				}
			}
			if (bFound) break;
			ParentNode = ParentNode->GetParent();
		}

		// LocalTransform 계산: cluster의 TLM을 부모/자식으로 역산
		// - 루트 본:    Local_col = M^-1 * SelfTLM   (메시 노드 변환 흡수)
		// - 비루트 본:  Local_col = ParentTLM^-1 * SelfTLM   (Empty/Armature 변환 자동 흡수)
		// - cluster 없음: EvaluateLocalTransform() fallback
		FbxAMatrix LocalTransform;
		auto SelfIt = BoneBindPose.find(CurrentJoint);
		bool bUsedFallback = false;

		if (SelfIt != BoneBindPose.end() && SelfIt->second.bValid)
		{
			++NumWithCluster;
			const FbxAMatrix& SelfTLM = SelfIt->second.TransformLink;

			if (CurrentBone.ParentIndex >= 0)
			{
				FbxNode* ParentJoint = JointNodes[CurrentBone.ParentIndex];
				auto ParentIt = BoneBindPose.find(ParentJoint);
				if (ParentIt != BoneBindPose.end() && ParentIt->second.bValid)
				{
					LocalTransform = ParentIt->second.TransformLink.Inverse() * SelfTLM;
				}
				else
				{
					LocalTransform = CurrentJoint->EvaluateLocalTransform();
					bUsedFallback = true;
				}
			}
			else
			{
				LocalTransform = SelfIt->second.TransformMesh.Inverse() * SelfTLM;
			}
		}
		else
		{
			LocalTransform = CurrentJoint->EvaluateLocalTransform();
			bUsedFallback = true;
		}

		if (bUsedFallback) ++NumFallback;

		FbxVector4 T = LocalTransform.GetT();
		FbxQuaternion Q = LocalTransform.GetQ();
		FbxVector4 S = LocalTransform.GetS();

		CurrentBone.Translation = FVector((float)T[0], (float)T[1], (float)T[2]);
		CurrentBone.Rotation = FQuat((float)Q[0], (float)Q[1], (float)Q[2], (float)Q[3]);
		CurrentBone.Scale = FVector((float)S[0], (float)S[1], (float)S[2]);

		// IBP는 ExtractMesh()의 Cluster에서 TransformLinkMatrix 기반으로 덮어씀.
		CurrentBone.InverseBindMatrix = FMatrix::Identity;
	}

	UE_LOG("Successfully extracted %d bones.", RawMesh->Bones.size());
	UE_LOG("[BindPose Diag] Bones with cluster: %d / %d (fallback used: %d)",
		NumWithCluster, (int32)JointNodes.size(), NumFallback);

	// 진단 로그: 본 트리. parent=-1이 비루트에 나타나면 Hierarchy Gap(BUG #2) 의심
	int32 OrphanCount = 0;
	for (int32 i = 0; i < (int32)RawMesh->Bones.size(); ++i)
	{
		const FBone& B = RawMesh->Bones[i];
		const char* Name = JointNodes[i]->GetName();
		const char* ParentName = (B.ParentIndex >= 0) ? JointNodes[B.ParentIndex]->GetName() : "<root>";
		UE_LOG("  [%d] %s parent=%d (%s) T=(%.3f, %.3f, %.3f)",
			i, Name, B.ParentIndex, ParentName, B.Translation.X, B.Translation.Y, B.Translation.Z);
		if (i > 0 && B.ParentIndex == -1) ++OrphanCount;
	}
	if (OrphanCount > 0)
	{
		UE_LOG("[Hierarchy Diag] WARNING: %d non-root bones have ParentIndex=-1. Possible Empty/Null nodes between bones (BUG #2).", OrphanCount);
	}
}
