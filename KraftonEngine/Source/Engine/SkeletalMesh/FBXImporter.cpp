#include "FBXImporter.h"
#include "SkeletalMesh.h"
#include "SkeletalMeshAsset.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"
#include "Core/Log.h"

// FBX SDK Header
#ifndef FBXSDK_SHARED
#define FBXSDK_SHARED
#endif
#include <fbxsdk.h>

/**
 * FBX 전용 행렬(FbxAMatrix)을 엔진의 FMatrix로 변환.
 * FbxAMatrix는 row 3에 Translation을 저장하는 row-major-호환 메모리 컨벤션이므로
 * (엔진 FMatrix와 동일), transpose 없이 직접 복사한다.
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
 * Scene 내에서 eSkeleton 속성을 가진 모든 조인트 노드를 재귀적으로 수집.
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
	fbxsdk::FbxAMatrix TransformLink;   // L: 본 글로벌
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

bool FFbxImporter::ImportFbx(const FString& FilePath,
                             USkeletalMesh* OutSkeletal,
                             UStaticMesh*   OutStatic)
{
	if (!OutSkeletal && !OutStatic) return false;

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

	// 6. Raw output 컨테이너 준비
	FSkeletalMesh* RawSkel   = OutSkeletal ? new FSkeletalMesh() : nullptr;
	FStaticMesh*   RawStatic = OutStatic   ? new FStaticMesh()   : nullptr;
	if (RawSkel)   RawSkel->PathFileName   = FilePath;
	if (RawStatic) RawStatic->PathFileName = FilePath;

	// 7. 스켈레톤 먼저 추출 (본 인덱스 매핑을 위해)
	if (OutSkeletal)
	{
		ExtractSkeleton(Scene, OutSkeletal, RawSkel);
	}

	// 8. 노드 순회 — skin 여부에 따라 분기
	FbxNode* RootNode = Scene->GetRootNode();
	if (RootNode)
	{
		ProcessNode(RootNode, OutSkeletal, RawSkel, OutStatic, RawStatic);
	}

	// 9. cluster weight 정규화 (per-vertex sum = 1.0)
	if (RawSkel)
	{
		NormalizeClusterWeights(RawSkel);
	}

	// 10. 결과를 asset에 주입 (비어 있으면 해제)
	bool bHaveSkel   = (RawSkel   && !RawSkel->Vertices.empty());
	bool bHaveStatic = (RawStatic && !RawStatic->Vertices.empty());

	if (OutSkeletal)
	{
		if (bHaveSkel) OutSkeletal->SetSkeletalMeshAsset(RawSkel);
		else           { delete RawSkel; }
	}
	if (OutStatic)
	{
		if (bHaveStatic) OutStatic->SetStaticMeshAsset(RawStatic);
		else             { delete RawStatic; }
	}

	// 11. SDK 자원 해제
	SdkManager->Destroy();

	if (!bHaveSkel && !bHaveStatic)
	{
		UE_LOG("FBX import produced no mesh data: %s", FilePath.c_str());
		return false;
	}

	UE_LOG("Successfully imported FBX: %s (skeletal=%d, static=%d)",
		FilePath.c_str(), bHaveSkel ? 1 : 0, bHaveStatic ? 1 : 0);
	return true;
}

bool FFbxImporter::HasValidSkinDeformer(FbxMesh* Mesh, USkeletalMesh* OutMesh)
{
	if (!Mesh) return false;
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
			// OutMesh가 주어진 경우 본 이름이 실제 스켈레톤에 매핑되는지 확인.
			if (OutMesh && OutMesh->GetBoneIndex(FName(Link->GetName())) < 0) continue;
			return true;
		}
	}
	return false;
}

void FFbxImporter::ProcessNode(FbxNode* Node,
                               USkeletalMesh* OutSkeletal, FSkeletalMesh* RawSkel,
                               UStaticMesh*   OutStatic,   FStaticMesh*   RawStatic)
{
	if (!Node) return;

	FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
	if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
	{
		FbxMesh* Mesh = (FbxMesh*)Attribute;
		const bool bSkinned = HasValidSkinDeformer(Mesh, OutSkeletal);

		if (bSkinned)
		{
			if (RawSkel && OutSkeletal)
			{
				ExtractSkeletalMesh(Mesh, RawSkel, OutSkeletal);
			}
			else
			{
				UE_LOG("Skipping skinned mesh node '%s' (no skeletal output bound).", Node->GetName());
			}
		}
		else
		{
			if (RawStatic)
			{
				ExtractStaticMesh(Mesh, RawStatic);
			}
			else
			{
				UE_LOG("Skipping unskinned mesh node '%s' (no static output bound).", Node->GetName());
			}
		}
	}

	for (int i = 0; i < Node->GetChildCount(); ++i)
	{
		ProcessNode(Node->GetChild(i), OutSkeletal, RawSkel, OutStatic, RawStatic);
	}
}

void FFbxImporter::ExtractSkeletalMesh(FbxMesh* Mesh, FSkeletalMesh* RawMesh, USkeletalMesh* OutMesh)
{
	if (!Mesh || !RawMesh || !OutMesh) return;

	UE_LOG("Extracting Skeletal Mesh: %s", Mesh->GetName());

	int ControlPointsCount = Mesh->GetControlPointsCount();
	FbxVector4* ControlPoints = Mesh->GetControlPoints();

	// 1. 정점/인덱스 버퍼 구축 (bind-pose FNormalVertex, bone 정보 없음).
	//    동시에 control-point index → expanded vertex index 매핑을 만든다.
	TArray<TArray<uint32>> CPToExpanded;
	CPToExpanded.resize(ControlPointsCount);

	const uint32 IndexBase   = (uint32)RawMesh->Indices.size();
	const uint32 VertexBase  = (uint32)RawMesh->Vertices.size();

	int PolygonCount = Mesh->GetPolygonCount();
	for (int i = 0; i < PolygonCount; ++i)
	{
		int PolygonSize = Mesh->GetPolygonSize(i);
		if (PolygonSize != 3) continue; // 삼각형만 처리 (삼각형화 가정)

		for (int j = 0; j < 3; ++j)
		{
			int CPIndex = Mesh->GetPolygonVertex(i, j);
			FNormalVertex Vertex = {}; // zero-init

			FbxVector4 Pos = ControlPoints[CPIndex];
			Vertex.pos = FVector((float)Pos[0], (float)Pos[1], (float)Pos[2]);

			FbxVector4 Normal;
			if (Mesh->GetPolygonVertexNormal(i, j, Normal))
			{
				Vertex.normal = FVector((float)Normal[0], (float)Normal[1], (float)Normal[2]);
			}

			FbxVector2 UV;
			bool bUnmapped;
			if (Mesh->GetPolygonVertexUV(i, j, "", UV, bUnmapped))
			{
				Vertex.tex = FVector2((float)UV[0], 1.0f - (float)UV[1]); // DirectX UV Flip
			}

			Vertex.color   = FVector4(1, 1, 1, 1);
			Vertex.tangent = FVector4(0, 0, 0, 1); // tangent 계산은 후속 작업

			uint32 ExpandedIndex = (uint32)RawMesh->Vertices.size();
			RawMesh->Vertices.push_back(Vertex);
			RawMesh->Indices.push_back(ExpandedIndex);
			CPToExpanded[CPIndex].push_back(ExpandedIndex);
		}
	}

	// 2. Section 등록 (전체 메시를 단일 섹션으로 — 머티리얼 분할은 후속 작업)
	{
		FStaticMeshSection Section;
		Section.MaterialSlotName = "Default";
		Section.FirstIndex   = IndexBase;
		Section.NumTriangles = (uint32)(RawMesh->Indices.size() - IndexBase) / 3;
		if (Section.NumTriangles > 0)
		{
			RawMesh->Sections.push_back(Section);
		}
	}

	// 3. Cluster 추출 → FBoneCluster 직접 생성
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

			// IBP 계산:
			//   FBX 공간에서 본의 메시 바인드 글로벌 = M^-1 * L 이므로
			//   IBP = (M^-1 * L)^-1 = L^-1 * M.
			FbxAMatrix FbxTransformMatrix;
			Cluster->GetTransformMatrix(FbxTransformMatrix);            // M
			FbxAMatrix FbxTransformLinkMatrix;
			Cluster->GetTransformLinkMatrix(FbxTransformLinkMatrix);    // L
			FbxAMatrix IBP_Fbx = FbxTransformLinkMatrix.Inverse() * FbxTransformMatrix;

			FBoneCluster NewCluster;
			NewCluster.BoneIndex         = BoneIndex;
			NewCluster.InverseBindMatrix = FbxMatrixToFMatrix(IBP_Fbx);

			int CPCount = Cluster->GetControlPointIndicesCount();
			int*    CPIndices = Cluster->GetControlPointIndices();
			double* CPWeights = Cluster->GetControlPointWeights();

			// expanded vertex 단위로 펼친다 (한 control point가 여러 expanded vertex로 복제됨)
			for (int k = 0; k < CPCount; ++k)
			{
				int cp = CPIndices[k];
				float w = (float)CPWeights[k];
				if (cp < 0 || cp >= (int)CPToExpanded.size()) continue;
				for (uint32 ev : CPToExpanded[cp])
				{
					NewCluster.VertexIndices.push_back(ev);
					NewCluster.Weights.push_back(w);
				}
			}

			if (!NewCluster.VertexIndices.empty())
			{
				RawMesh->Clusters.push_back(std::move(NewCluster));
			}
		}
	}

	UE_LOG("Extracted skel mesh '%s': +%u verts, +%u indices, +%d clusters",
		Mesh->GetName(),
		(uint32)RawMesh->Vertices.size() - VertexBase,
		(uint32)RawMesh->Indices.size()  - IndexBase,
		(int)RawMesh->Clusters.size());
}

void FFbxImporter::ExtractStaticMesh(FbxMesh* Mesh, FStaticMesh* RawMesh)
{
	if (!Mesh || !RawMesh) return;

	UE_LOG("Extracting Static Mesh: %s", Mesh->GetName());

	int ControlPointsCount = Mesh->GetControlPointsCount();
	FbxVector4* ControlPoints = Mesh->GetControlPoints();

	// Mesh node의 global transform을 vertex pos/normal에 베이크
	// (skeleton에 attach 되지 않으므로 actor transform과 함께 평탄화)
	FbxNode* Owner = Mesh->GetNode();
	FbxAMatrix NodeGlobal;
	NodeGlobal.SetIdentity();
	if (Owner)
	{
		NodeGlobal = Owner->EvaluateGlobalTransform();
	}
	const FMatrix BakeXform = FbxMatrixToFMatrix(NodeGlobal);

	const uint32 IndexBase = (uint32)RawMesh->Indices.size();

	int PolygonCount = Mesh->GetPolygonCount();
	for (int i = 0; i < PolygonCount; ++i)
	{
		int PolygonSize = Mesh->GetPolygonSize(i);
		if (PolygonSize != 3) continue;

		for (int j = 0; j < 3; ++j)
		{
			int CPIndex = Mesh->GetPolygonVertex(i, j);
			FNormalVertex Vertex = {};

			FbxVector4 Pos = ControlPoints[CPIndex];
			FVector LocalPos((float)Pos[0], (float)Pos[1], (float)Pos[2]);
			Vertex.pos = BakeXform.TransformPositionWithW(LocalPos);

			FbxVector4 Normal;
			if (Mesh->GetPolygonVertexNormal(i, j, Normal))
			{
				FVector LocalN((float)Normal[0], (float)Normal[1], (float)Normal[2]);
				Vertex.normal = BakeXform.TransformVector(LocalN);
			}

			FbxVector2 UV;
			bool bUnmapped;
			if (Mesh->GetPolygonVertexUV(i, j, "", UV, bUnmapped))
			{
				Vertex.tex = FVector2((float)UV[0], 1.0f - (float)UV[1]);
			}

			Vertex.color   = FVector4(1, 1, 1, 1);
			Vertex.tangent = FVector4(0, 0, 0, 1);

			uint32 ExpandedIndex = (uint32)RawMesh->Vertices.size();
			RawMesh->Vertices.push_back(Vertex);
			RawMesh->Indices.push_back(ExpandedIndex);
		}
	}

	FStaticMeshSection Section;
	Section.MaterialSlotName = "Default";
	Section.FirstIndex   = IndexBase;
	Section.NumTriangles = (uint32)(RawMesh->Indices.size() - IndexBase) / 3;
	if (Section.NumTriangles > 0)
	{
		RawMesh->Sections.push_back(Section);
	}

	UE_LOG("Extracted static mesh '%s': total verts=%u, indices=%u",
		Mesh->GetName(),
		(uint32)RawMesh->Vertices.size(),
		(uint32)RawMesh->Indices.size());
}

void FFbxImporter::NormalizeClusterWeights(FSkeletalMesh* RawMesh)
{
	if (!RawMesh) return;
	if (RawMesh->Clusters.empty() || RawMesh->Vertices.empty()) return;

	// 1. per-vertex total weight 계산
	TArray<float> Totals;
	Totals.assign(RawMesh->Vertices.size(), 0.0f);
	for (const FBoneCluster& C : RawMesh->Clusters)
	{
		for (size_t i = 0; i < C.VertexIndices.size(); ++i)
		{
			uint32 vi = C.VertexIndices[i];
			if (vi < Totals.size()) Totals[vi] += C.Weights[i];
		}
	}

	// 2. 각 cluster의 weight를 vertex 기준으로 정규화
	for (FBoneCluster& C : RawMesh->Clusters)
	{
		for (size_t i = 0; i < C.VertexIndices.size(); ++i)
		{
			uint32 vi = C.VertexIndices[i];
			float  t  = (vi < Totals.size()) ? Totals[vi] : 0.0f;
			if (t > 0.0f) C.Weights[i] /= t;
		}
	}
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

	// 1-b. 본별 BindPose 정보(L, M) 수집 — TLM 역산용 (LocalTransform 산출에만 사용)
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
	int32 NumFallback    = 0;

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
		//  - 루트 본:   Local = M^-1 * SelfTLM   (메시 노드 변환 흡수)
		//  - 비루트:    Local = ParentTLM^-1 * SelfTLM
		//  - cluster 없음: EvaluateLocalTransform() fallback
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

		FbxVector4    T = LocalTransform.GetT();
		FbxQuaternion Q = LocalTransform.GetQ();
		FbxVector4    S = LocalTransform.GetS();

		CurrentBone.Translation = FVector((float)T[0], (float)T[1], (float)T[2]);
		CurrentBone.Rotation    = FQuat((float)Q[0], (float)Q[1], (float)Q[2], (float)Q[3]);
		CurrentBone.Scale       = FVector((float)S[0], (float)S[1], (float)S[2]);
		// IBP는 더 이상 FBone에 저장되지 않음 — cluster의 InverseBindMatrix로 이동.
	}

	UE_LOG("Successfully extracted %d bones.", RawMesh->Bones.size());
	UE_LOG("[BindPose Diag] Bones with cluster: %d / %d (fallback used: %d)",
		NumWithCluster, (int32)JointNodes.size(), NumFallback);

	// 진단 로그: 본 트리. parent=-1이 비루트에 나타나면 Hierarchy Gap 의심.
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
		UE_LOG("[Hierarchy Diag] WARNING: %d non-root bones have ParentIndex=-1. "
			"Possible Empty/Null nodes between bones.", OrphanCount);
	}
}
