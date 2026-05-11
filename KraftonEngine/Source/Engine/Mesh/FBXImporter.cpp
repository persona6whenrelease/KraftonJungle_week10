#include "FBXImporter.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Math/Transform.h"
#include "Materials/Material.h"
#include "Engine/Platform/Paths.h"
#include "SimpleJSON/json.hpp"
#include "Materials/MaterialManager.h"
#include "Core/Log.h"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <charconv>
#include <chrono>
namespace
{
	constexpr float GFBXImportUniformScale = 0.01f;

	FVector ConvertFbxVectorToEngineVector(const FbxVector4& Vector)
	{
		return FVector(
			static_cast<float>(Vector[0]),
			static_cast<float>(Vector[1]),
			static_cast<float>(Vector[2])
		);
	}

	// FBX SDK의 행렬 타입인 FbxAMatrix를 우리 엔진 행렬 타입인 FMatrix로 복사 변환하는 함수
	FMatrix ConvertFbxMatrixToEngineMatrix(const FbxAMatrix& Matrix)
	{
		FMatrix Result;
		for (int Row = 0; Row < 4; ++Row)
		{
			for (int Col = 0; Col < 4; ++Col)
			{
				Result.M[Row][Col] = static_cast<float>(Matrix.Get(Row, Col));
			}
		}

		return Result;
	}
	
	//FBX에서 가져온 global/bind matrix에 엔진용 import scale(현재 0.01)을 추가로 곱하는 함수
	FMatrix ApplyImportScaleToGlobalMatrix(const FMatrix& Matrix)
	{
		if (GFBXImportUniformScale == 1.0f)
		{
			return Matrix;
		}

		const FVector Scale(
			GFBXImportUniformScale,
			GFBXImportUniformScale,
			GFBXImportUniformScale
		);
		return Matrix * FMatrix::MakeScaleMatrix(Scale);
	}

	float GetBasisDeterminant(const FMatrix& Matrix)
	{
		return Matrix.M[0][0] * (Matrix.M[1][1] * Matrix.M[2][2] - Matrix.M[1][2] * Matrix.M[2][1])
			- Matrix.M[0][1] * (Matrix.M[1][0] * Matrix.M[2][2] - Matrix.M[1][2] * Matrix.M[2][0])
			+ Matrix.M[0][2] * (Matrix.M[1][0] * Matrix.M[2][1] - Matrix.M[1][1] * Matrix.M[2][0]);
	}

	int32 FindBoneIndexByName(const FString& BoneName, const TArray<FBoneInfo>& InBones)
	{
		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(InBones.size()); ++BoneIndex)
		{
			if (InBones[BoneIndex].Name == BoneName)
			{
				return BoneIndex;
			}
		}

		return -1;
	}

	int32 FindAncestorBoneIndex(FbxNode* MeshNode, const TArray<FBoneInfo>& InBones)
	{
		for (FbxNode* Parent = MeshNode ? MeshNode->GetParent() : nullptr; Parent; Parent = Parent->GetParent())
		{
			FbxNodeAttribute* Attribute = Parent->GetNodeAttribute();
			if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
			{
				const int32 BoneIndex = FindBoneIndexByName(Parent->GetName(), InBones);
				if (BoneIndex >= 0)
				{
					return BoneIndex;
				}
			}
		}

		return -1;
	}
}

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
	
	// Convert the scene once and then use FBX skinning spaces consistently.
	// Skinned vertices stay in mesh-local space; CPU skinning applies
	// MeshBindGlobal * InverseBoneBindGlobal * CurrentBoneGlobal.
	FbxAxisSystem EngineAxisSystem;
	FbxAxisSystem::ParseAxisSystem("yzx", EngineAxisSystem); // +Y right, +Z up, +X forward
	EngineAxisSystem.DeepConvertScene(m_scene);

	// Normalize file units first. The engine import scale is applied later to
	// mesh/bone global matrices, so avoid mixing size policy into FBX units.
	FbxSystemUnit SceneSystemUnit = m_scene->GetGlobalSettings().GetSystemUnit();

	if (SceneSystemUnit != FbxSystemUnit::cm)
	{
		FbxSystemUnit::cm.ConvertScene(m_scene);
	}

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

	//fbx node를 순회하면서 메시의 재질 데이터 저장
	int materialCount = InNode->GetMaterialCount();
	for (int i = 0; i < materialCount; ++i)
	{
		FbxSurfaceMaterial* material = InNode->GetMaterial(i);
		ConvertSurfaceMatToMaterialJSON(material);

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
		FbxAMatrix Global = InNode->EvaluateGlobalTransform();

		FBoneInfo newBone = {};
		newBone.Name = InNode->GetName();
		newBone.ParentIndex = ParentIndex;
		newBone.BindPoseGlobal = ApplyImportScaleToGlobalMatrix(ConvertFbxMatrixToEngineMatrix(Global));
		newBone.InverseBindPose = newBone.BindPoseGlobal.GetInverse();
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

bool FFBXImporter::BuildSkinningWeight(FbxMesh* InMesh, TArray<TArray<VertexBlendingInfo>>& OutWeights, TArray<FBoneInfo>& InBones)
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

			FbxAMatrix LinkBindMatrix;
			cluster->GetTransformLinkMatrix(LinkBindMatrix);
			InBones[boneIndex].BindPoseGlobal = ApplyImportScaleToGlobalMatrix(ConvertFbxMatrixToEngineMatrix(LinkBindMatrix));
			InBones[boneIndex].InverseBindPose = InBones[boneIndex].BindPoseGlobal.GetInverse();

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

	//좌표축 변환
	FbxNode* meshNode = InMesh->GetNode();
	
	FbxAMatrix meshGlobalTransform = meshNode->EvaluateGlobalTransform();

	FbxVector4 geoT = meshNode->GetGeometricTranslation(FbxNode::eSourcePivot);
	FbxVector4 geoR = meshNode->GetGeometricRotation(FbxNode::eSourcePivot);
	FbxVector4 geoS = meshNode->GetGeometricScaling(FbxNode::eSourcePivot);
	FbxAMatrix geoTransform(geoT, geoR, geoS);

	// 최종 정점을 변환할  행렬
	FbxAMatrix finalMeshTransform = meshGlobalTransform * geoTransform;
	FbxAMatrix meshBindTransform = finalMeshTransform;
	const bool bSkinnedMesh = InMesh->GetDeformerCount(FbxDeformer::eSkin) > 0;
	int32 RigidBindBoneIndex = -1;
	if (bSkinnedMesh)
	{
		FbxSkin* skin = static_cast<FbxSkin*>(InMesh->GetDeformer(0, FbxDeformer::eSkin));
		if (skin && skin->GetClusterCount() > 0)
		{
			skin->GetCluster(0)->GetTransformMatrix(meshBindTransform);
		}
	}
	FMatrix MeshBindGlobal = ApplyImportScaleToGlobalMatrix(ConvertFbxMatrixToEngineMatrix(meshBindTransform));
	if (!bSkinnedMesh)
	{
		// Rigid child meshes follow their ancestor bone, while root-level
		// unskinned meshes stay in their converted node space.
		RigidBindBoneIndex = FindAncestorBoneIndex(meshNode, InBones);
	}
	const bool bRigidBoundMesh = RigidBindBoneIndex >= 0;
	const bool bReverseWinding = GetBasisDeterminant(MeshBindGlobal) < 0.0f;

	// 2. 모든 삼각형을 순회합니다.
	for (int i = 0; i < polygonCount; ++i)
	{
		uint32 TriangleIndices[3] = {};

		// 3. 하나의 삼각형은 3개의 꼭짓점(Vertex)으로 이루어져 있습니다.
		for (int j = 0; j <3; ++j)
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
			vertex.MeshBindGlobal = MeshBindGlobal;

			// --- A. 위치 (Position) 추출 ---
			FbxVector4 localPos = controlPoints[ctrlPointIndex];
			FbxVector4 vertexPos = geoTransform.MultT(localPos);

			vertex.Position = ConvertFbxVectorToEngineVector(vertexPos);

			//--- B. 법선 (Normal) 추출  ---
			FbxVector4 fbxNormal;
			InMesh->GetPolygonVertexNormal(i, j, fbxNormal);
			fbxNormal[3] = 0.0; // 노멀은 방향이므로 이동(Translation)을 무시하기 위해 W를 0으로 설정
			FbxVector4 globalNormal = geoTransform.MultT(fbxNormal);
			globalNormal.Normalize(); // 크기를 1로 재정규화

			vertex.Normal = ConvertFbxVectorToEngineVector(globalNormal);


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
				if (bRigidBoundMesh)
				{
					vertex.BoneIndices[0] = RigidBindBoneIndex;
					vertex.BoneWeights[0] = 1.0f;
				}
				// 이 점에 영향을 주는 뼈가 없으면 UpdateSkinning에서 원본 정점을 그대로 씁니다.
			}

			// 5. 완성된 정점을 엔진 버퍼 배열에 밀어 넣기
			TriangleIndices[j] = static_cast<uint32>(m_Vertices.size());
			m_Vertices.push_back(vertex);
		}

		if (bReverseWinding)
		{
			m_Indices.push_back(TriangleIndices[0]);
			m_Indices.push_back(TriangleIndices[2]);
			m_Indices.push_back(TriangleIndices[1]);
		}
		else
		{
			m_Indices.push_back(TriangleIndices[0]);
			m_Indices.push_back(TriangleIndices[1]);
			m_Indices.push_back(TriangleIndices[2]);
		}
	}

	return true;
}

FString FFBXImporter::ConvertSurfaceMatToMaterialJSON(FbxSurfaceMaterial* InMaterial)
{

	const FString materialName = InMaterial->GetName();

	FString MatPath = "Asset/Materials/Auto/" + materialName + ".mat";
	std::wstring MatDiskPath;
	FString Error;
	if (!FPaths::TryResolvePackagePath(MatPath, MatDiskPath, &Error))
	{
		return "";
	}

	// 이미 존재하면 덮어쓰지 않음 (에디터에서 수정했을 수 있으므로)
	if (std::filesystem::exists(std::filesystem::path(MatDiskPath)))
		return MatPath;

	// Auto/ 디렉토리 보장
	std::filesystem::create_directories(std::filesystem::path(MatDiskPath).parent_path());

	json::JSON JsonData;
	JsonData["PathFileName"] = MatPath;
	JsonData["Origin"] = "FbxImport";
	JsonData["ShaderPath"] = "Shaders/Geometry/UberLit.hlsl";
	JsonData["RenderPass"] = "Opaque";

	//SurfaceMaterial의 종류에 따라 컬러 정보를 추출하여 JSON에 넣어줍니다.
	if (InMaterial->GetClassId().Is(FbxSurfacePhong::ClassId))
	{

		//컬러 정보
		FbxSurfacePhong* phong = (FbxSurfacePhong*)InMaterial;
		FbxDouble3 diffuse = phong->Diffuse.Get();

		JsonData["Parameters"]["SectionColor"][0] = static_cast<float>(diffuse[0]);
		JsonData["Parameters"]["SectionColor"][1] = static_cast<float>(diffuse[1]);
		JsonData["Parameters"]["SectionColor"][2] = static_cast<float>(diffuse[2]);
		JsonData["Parameters"]["SectionColor"][3] = 1.0f;
	}
	else if (InMaterial->GetClassId().Is(FbxSurfaceLambert::ClassId))
	{

		//컬러 정보
		FbxSurfaceLambert* lambert = (FbxSurfaceLambert*)InMaterial;
		FbxDouble3 diffuse = lambert->Diffuse.Get();

		JsonData["Parameters"]["SectionColor"][0] = static_cast<float>(diffuse[0]);
		JsonData["Parameters"]["SectionColor"][1] = static_cast<float>(diffuse[1]);
		JsonData["Parameters"]["SectionColor"][2] = static_cast<float>(diffuse[2]);
		JsonData["Parameters"]["SectionColor"][3] = 1.0f;
	}

	//Textue 정보 추출
	// 추출하고 싶은 Property 이름 배열 (디퓨즈, 노말, 에미시브 등)
	const char* propertyNames[] = {
		FbxSurfaceMaterial::sDiffuse,
		FbxSurfaceMaterial::sNormalMap, // 또는 "NormalMap"
		FbxSurfaceMaterial::sSpecular,
		FbxSurfaceMaterial::sEmissive
	};
	//  모델러들이 흔히 쓰는 텍스처 폴더 이름 후보군
	TArray<FString> searchFolders = {
		"",           // 1순위: FBX와 같은 폴더에 있을 경우
		"texture",    // 2순위
		"textures",
		"tex",
		"maps",
		"materials",
		"images",
		"src"
	};
	for (const char* propName : propertyNames)
	{
		FbxProperty prop = InMaterial->FindProperty(propName);

		if (prop.IsValid())
		{
			// 이 Property에 연결된 텍스처 개수 확인
			int textureCount = prop.GetSrcObjectCount<FbxTexture>();

			for (int j = 0; j < textureCount; ++j)
			{
				// 텍스처 객체 가져오기
				FbxTexture* texture = prop.GetSrcObject<FbxTexture>(j);

				// 파일 텍스처(이미지 파일)인지 확인
				FbxFileTexture* fileTexture = FbxCast<FbxFileTexture>(texture);

				if (fileTexture)
				{
					// 텍스처 파일 경로 추출!
					const char* absolutePath = fileTexture->GetFileName();
					const char* relativePath = fileTexture->GetRelativeFileName();

					// 2. 경로에서 "파일명.확장자" (예: "albedo.png")만 쏙 빼내기
					std::filesystem::path texturePath(absolutePath);
					std::string fileNameOnly = texturePath.filename().string();

					std::filesystem::path finalTexturePath;
					bool isFound = false;
					//for (const FString& folder : searchFolders)
					//{
					//	std::filesystem::path checkPath = fbxDir / folder / fileNameOnly;

					//	if (std::filesystem::exists(checkPath))
					//	{
					//		finalTexturePath = checkPath;
					//		isFound = true;
					//		break; // 찾았으면 탐색 종료!
					//	}
					//}
					std::filesystem::path fbxDirectory = "Data/";
					if (isFound) {
						// TODO: 엔진의 Material 구조체에 이 파일 경로를 저장합니다.
						JsonData["Textures"]["DiffuseTexture"] = "Data/" + FString(relativePath);
						UE_LOG("Get TextureMap file path: %s", "Data/" + FString(relativePath));

					}
					else {
						UE_LOG("Invalid TextureMap file path: %s", FPaths::ResolveAssetPath(absolutePath, relativePath));
					}

				}
			}
		}
	}


#if IS_GAME_CLIENT
	return MatPath;
#else
	std::ofstream File(std::filesystem::path(MatDiskPath), std::ios::binary);
	File << JsonData.dump();

	return MatPath;
#endif

}
