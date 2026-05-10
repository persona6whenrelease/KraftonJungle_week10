#include "Mesh/FBXImporterLegacy.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_set>
#include "FBXUtil.h"
#include <fbxsdk.h>

#include "Core/Log.h"

FBXSkeletalImporter::~FBXSkeletalImporter()
{
	ShutdownSdk();
}

bool FBXSkeletalImporter::InitializeSdk()
{
	ShutdownSdk();

	Manager = FbxManager::Create();
	if (!Manager)
	{
		UE_LOG("[FBXImporter] Failed to create FbxManager.");
		return false;
	}

	FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
	if (!IOSettings)
	{
		UE_LOG("[FBXImporter] Failed to create FbxIOSettings.");
		ShutdownSdk();
		return false;
	}

	Manager->SetIOSettings(IOSettings);
	return true;
}

void FBXSkeletalImporter::ShutdownSdk()
{
	DestroyScene();
	ClearState();

	if (Manager)
	{
		Manager->Destroy();
		Manager = nullptr;
	}
}

bool FBXSkeletalImporter::ImportSkeletalMesh(const FString& InFilePath, FSkeletalMesh& OutMesh)
{
	ClearState();

	OutMesh.PathFileName = InFilePath;
	OutMesh.Vertices.clear();
	OutMesh.Indices.clear();
	OutMesh.Sections.clear();
	OutMesh.Bones.clear();
	OutMesh.BoundsCenter = FVector(0.0f, 0.0f, 0.0f);
	OutMesh.BoundsExtent = FVector(0.0f, 0.0f, 0.0f);
	OutMesh.bBoundsValid = false;

	if (!InitializeSdk())
	{
		return false;
	}

	if (!LoadScene(InFilePath))
	{
		ShutdownSdk();
		return false;
	}

	PreprocessScene();

	FbxNode* MeshNode = nullptr;
	FbxMesh* Mesh = nullptr;
	if (!FindFirstSkeletalMesh(MeshNode, Mesh))
	{
		UE_LOG("[FBXImporter] No skinned mesh found: %s", InFilePath.c_str());
		ShutdownSdk();
		return false;
	}

	GenerateTangents(Mesh);

	if (!BuildBones(Mesh, OutMesh))
	{
		UE_LOG("[FBXImporter] Failed to build bones: %s", InFilePath.c_str());
		ShutdownSdk();
		return false;
	}

	if (!BuildControlPointInfluences(Mesh))
	{
		UE_LOG("[FBXImporter] Failed to build skin weights: %s", InFilePath.c_str());
		ShutdownSdk();
		return false;
	}

	if (!BuildVerticesIndicesAndSections(Mesh, OutMesh))
	{
		UE_LOG("[FBXImporter] Failed to build mesh geometry: %s", InFilePath.c_str());
		ShutdownSdk();
		return false;
	}

	FinalizeMesh(InFilePath, OutMesh);
	const bool bValid = ValidateImportedMesh(OutMesh);
	LogImportSummary(OutMesh);

	ShutdownSdk();
	return bValid;
}

bool FBXSkeletalImporter::FVertexKey::operator==(const FVertexKey& Other) const
{
	return PosX == Other.PosX &&
		PosY == Other.PosY &&
		PosZ == Other.PosZ &&
		NormalX == Other.NormalX &&
		NormalY == Other.NormalY &&
		NormalZ == Other.NormalZ &&
		UVX == Other.UVX &&
		UVY == Other.UVY &&
		TangentX == Other.TangentX &&
		TangentY == Other.TangentY &&
		TangentZ == Other.TangentZ &&
		TangentW == Other.TangentW &&
		BoneIDs[0] == Other.BoneIDs[0] &&
		BoneIDs[1] == Other.BoneIDs[1] &&
		BoneIDs[2] == Other.BoneIDs[2] &&
		BoneIDs[3] == Other.BoneIDs[3] &&
		BoneWeight0 == Other.BoneWeight0 &&
		BoneWeight1 == Other.BoneWeight1 &&
		BoneWeight2 == Other.BoneWeight2 &&
		BoneWeight3 == Other.BoneWeight3;
}

size_t FBXSkeletalImporter::FVertexKeyHasher::operator()(const FVertexKey& Key) const
{
	size_t H = 1469598103934665603ull;

	auto HashCombine = [&H](auto Value)
		{
			using ValueType = decltype(Value);
			const size_t V = std::hash<ValueType>{}(Value);
			H ^= V;
			H *= 1099511628211ull;
		};

	HashCombine(Key.PosX);
	HashCombine(Key.PosY);
	HashCombine(Key.PosZ);
	HashCombine(Key.NormalX);
	HashCombine(Key.NormalY);
	HashCombine(Key.NormalZ);
	HashCombine(Key.UVX);
	HashCombine(Key.UVY);
	HashCombine(Key.TangentX);
	HashCombine(Key.TangentY);
	HashCombine(Key.TangentZ);
	HashCombine(Key.TangentW);

	for (int32 i = 0; i < 4; ++i)
	{
		HashCombine(Key.BoneIDs[i]);
	}

	HashCombine(Key.BoneWeight0);
	HashCombine(Key.BoneWeight1);
	HashCombine(Key.BoneWeight2);
	HashCombine(Key.BoneWeight3);

	return H;
}

void FBXSkeletalImporter::ClearState()
{
	BoneNodeToIndex.clear();
	MeshSpaceBoneMatrix.clear();
	MeshGlobalBindPose = FMatrix::Identity;
	ControlPointInfluences.clear();
	VertexMap.clear();
	IndicesByMaterial.clear();
	MaterialSlotNamesByIndex.clear();
	TotalPolygonCornerCount = 0;
}

void FBXSkeletalImporter::DestroyScene()
{
	if (Scene)
	{
		Scene->Destroy();
		Scene = nullptr;
	}
}

bool FBXSkeletalImporter::LoadScene(const FString& InFilePath)
{
	if (!Manager)
	{
		return false;
	}

	DestroyScene();

	FbxImporter* Importer = FbxImporter::Create(Manager, "");
	if (!Importer)
	{
		UE_LOG("[FBXImporter] Failed to create FbxImporter.");
		return false;
	}

	const bool bInitialized = Importer->Initialize(
		InFilePath.c_str(),
		-1,
		Manager->GetIOSettings());

	if (!bInitialized)
	{
		UE_LOG("[FBXImporter] Initialize failed: %s. Error: %s",
			InFilePath.c_str(),
			Importer->GetStatus().GetErrorString());
		Importer->Destroy();
		return false;
	}

	Scene = FbxScene::Create(Manager, "ImportScene");
	if (!Scene)
	{
		UE_LOG("[FBXImporter] Failed to create FbxScene.");
		Importer->Destroy();
		return false;
	}

	const bool bImported = Importer->Import(Scene);
	if (!bImported)
	{
		UE_LOG("[FBXImporter] Import failed: %s. Error: %s",
			InFilePath.c_str(),
			Importer->GetStatus().GetErrorString());
	}

	Importer->Destroy();
	if (!bImported)
	{
		DestroyScene();
	}
	return bImported;
}

void FBXSkeletalImporter::PreprocessScene()
{
	if (!Scene || !Manager)
	{
		return;
	}

	const FbxAxisSystem DirectXAxisSystem(FbxAxisSystem::eDirectX);
	DirectXAxisSystem.ConvertScene(Scene);

	FbxSystemUnit::ConversionOptions UnitOptions = {};
	UnitOptions.mConvertRrsNodes = true;
	UnitOptions.mConvertLimits = true;
	UnitOptions.mConvertClusters = true;
	UnitOptions.mConvertLightIntensity = true;
	UnitOptions.mConvertPhotometricLProperties = true;
	UnitOptions.mConvertCameraClipPlanes = true;

	const FbxSystemUnit CentimeterUnit(1.0);
	CentimeterUnit.ConvertScene(Scene, UnitOptions);

	FbxGeometryConverter Converter(Manager);
	Converter.Triangulate(Scene, true);
}

void FBXSkeletalImporter::GenerateTangents(FbxMesh* Mesh)
{
	if (Mesh)
	{
		Mesh->GenerateTangentsDataForAllUVSets(true);
	}
}

bool FBXSkeletalImporter::FindFirstSkeletalMesh(FbxNode*& OutNode, FbxMesh*& OutMesh)
{
	OutNode = nullptr;
	OutMesh = nullptr;

	if (!Scene)
	{
		return false;
	}

	FbxNode* Root = Scene->GetRootNode();
	return Root && FindFirstSkeletalMeshRecursive(Root, OutNode, OutMesh);
}

bool FBXSkeletalImporter::FindFirstSkeletalMeshRecursive(FbxNode* Node, FbxNode*& OutNode, FbxMesh*& OutMesh)
{
	if (!Node)
	{
		return false;
	}

	if (FbxMesh* Mesh = Node->GetMesh())
	{
		if (HasSkinDeformer(Mesh))
		{
			OutNode = Node;
			OutMesh = Mesh;
			return true;
		}
	}

	for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
	{
		if (FindFirstSkeletalMeshRecursive(Node->GetChild(ChildIndex), OutNode, OutMesh))
		{
			return true;
		}
	}

	return false;
}

bool FBXSkeletalImporter::HasSkinDeformer(FbxMesh* Mesh) const
{
	return Mesh && Mesh->GetDeformerCount(FbxDeformer::eSkin) > 0;
}

bool FBXSkeletalImporter::BuildBones(FbxMesh* Mesh, FSkeletalMesh& OutMesh)
{
	TArray<FbxNode*> BoneNodes;
	if (!CollectBoneNodes(Mesh, BoneNodes) || BoneNodes.empty())
	{
		return false;
	}

	BuildBoneIndexMap(BoneNodes);
	FillBoneInfos(BoneNodes, OutMesh);
	BuildBoneParentIndices(BoneNodes, OutMesh);
	BuildBindPoseMatrices(BoneNodes, OutMesh);

	return !OutMesh.Bones.empty();
}

bool FBXSkeletalImporter::CollectBoneNodes(FbxMesh* Mesh, TArray<FbxNode*>& OutBoneNodes)
{
	if (!Mesh)
	{
		return false;
	}

	if (FbxNode* MeshNode = Mesh->GetNode())
	{
		FbxTime BindTime;
		BindTime.SetSecondDouble(0.0);
		MeshGlobalBindPose = FBXUtil::ConvertFbxMatrix(MeshNode->EvaluateGlobalTransform(BindTime));
	}

	std::unordered_set<FbxNode*> UniqueNodes;

	const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
	{
		FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
		if (!Skin)
		{
			continue;
		}

		const int32 ClusterCount = Skin->GetClusterCount();
		for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
		{
			FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
			if (!Cluster)
			{
				continue;
			}

			FbxAMatrix MeshBindMatrix;
			Cluster->GetTransformMatrix(MeshBindMatrix);
			MeshGlobalBindPose = FBXUtil::ConvertFbxMatrix(MeshBindMatrix);

			FbxNode* BoneNode = Cluster->GetLink();
			if (!BoneNode)
			{
				continue;
			}

			FbxAMatrix BoneBindMatrix;
			Cluster->GetTransformLinkMatrix(BoneBindMatrix);
			MeshSpaceBoneMatrix[BoneNode] = FBXUtil::ConvertFbxMatrix(BoneBindMatrix);

			if (UniqueNodes.insert(BoneNode).second)
			{
				OutBoneNodes.push_back(BoneNode);
			}
		}
	}

	std::sort(OutBoneNodes.begin(), OutBoneNodes.end(),
		[](FbxNode* A, FbxNode* B)
		{
			const int32 DepthA = FBXUtil::GetNodeDepth(A);
			const int32 DepthB = FBXUtil::GetNodeDepth(B);
			if (DepthA != DepthB)
			{
				return DepthA < DepthB;
			}
			return std::strcmp(A ? A->GetName() : "", B ? B->GetName() : "") < 0;
		});

	return !OutBoneNodes.empty();
}

void FBXSkeletalImporter::BuildBoneIndexMap(const TArray<FbxNode*>& BoneNodes)
{
	BoneNodeToIndex.clear();
	for (int32 i = 0; i < static_cast<int32>(BoneNodes.size()); ++i)
	{
		BoneNodeToIndex[BoneNodes[i]] = i;
	}
}

void FBXSkeletalImporter::FillBoneInfos(const TArray<FbxNode*>& BoneNodes, FSkeletalMesh& OutMesh)
{
	OutMesh.Bones.clear();
	OutMesh.Bones.resize(BoneNodes.size());

	for (int32 i = 0; i < static_cast<int32>(BoneNodes.size()); ++i)
	{
		FBoneInfo& Bone = OutMesh.Bones[i];
		Bone.Name = BoneNodes[i] ? BoneNodes[i]->GetName() : "";
		Bone.ParentIndex = -1;
		Bone.BoneSpaceToMeshSpace = FMatrix::Identity;
		Bone.MeshSpaceToBoneSpace = FMatrix::Identity;
	}
}

void FBXSkeletalImporter::BuildBoneParentIndices(const TArray<FbxNode*>& BoneNodes, FSkeletalMesh& OutMesh)
{
	for (int32 i = 0; i < static_cast<int32>(BoneNodes.size()); ++i)
	{
		FbxNode* BoneNode = BoneNodes[i];
		if (!BoneNode)
		{
			OutMesh.Bones[i].ParentIndex = -1;
			continue;
		}

		auto It = BoneNodeToIndex.find(BoneNode->GetParent());
		OutMesh.Bones[i].ParentIndex = (It != BoneNodeToIndex.end()) ? It->second : -1;
	}
}

void FBXSkeletalImporter::BuildBindPoseMatrices(const TArray<FbxNode*>& BoneNodes, FSkeletalMesh& OutMesh)
{
	TArray<FMatrix> MeshLocalBindGlobals;
	MeshLocalBindGlobals.resize(BoneNodes.size());

	const FMatrix MeshGlobalInverse = MeshGlobalBindPose.GetInverse();
	FbxTime BindTime;
	BindTime.SetSecondDouble(0.0);

	for (int32 i = 0; i < static_cast<int32>(BoneNodes.size()); ++i)
	{
		FMatrix BoneGlobal = FMatrix::Identity;

		auto It = MeshSpaceBoneMatrix.find(BoneNodes[i]);
		if (It != MeshSpaceBoneMatrix.end())
		{
			BoneGlobal = It->second;
		}
		else if (BoneNodes[i])
		{
			BoneGlobal = FBXUtil::ConvertFbxMatrix(BoneNodes[i]->EvaluateGlobalTransform(BindTime));
		}

		MeshLocalBindGlobals[i] = BoneGlobal * MeshGlobalInverse;
	}

	for (int32 i = 0; i < static_cast<int32>(BoneNodes.size()); ++i)
	{
		FBoneInfo& Bone = OutMesh.Bones[i];
		const FMatrix& Global = MeshLocalBindGlobals[i];

		Bone.MeshSpaceToBoneSpace = Global.GetInverse();
		if (Bone.ParentIndex >= 0 && Bone.ParentIndex < static_cast<int32>(MeshLocalBindGlobals.size()))
		{
			Bone.BoneSpaceToMeshSpace = Global * MeshLocalBindGlobals[Bone.ParentIndex].GetInverse();
		}
		else
		{
			Bone.BoneSpaceToMeshSpace = Global;
		}
	}
}

bool FBXSkeletalImporter::BuildControlPointInfluences(FbxMesh* Mesh)
{
	if (!Mesh)
	{
		return false;
	}

	const int32 ControlPointCount = Mesh->GetControlPointsCount();
	ControlPointInfluences.clear();
	ControlPointInfluences.resize(ControlPointCount);

	const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
	{
		FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
		if (!Skin)
		{
			continue;
		}

		const int32 ClusterCount = Skin->GetClusterCount();
		for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
		{
			if (FbxCluster* Cluster = Skin->GetCluster(ClusterIndex))
			{
				AddClusterInfluences(Cluster);
			}
		}
	}

	return true;
}

void FBXSkeletalImporter::AddClusterInfluences(FbxCluster* Cluster)
{
	if (!Cluster)
	{
		return;
	}

	FbxNode* BoneNode = Cluster->GetLink();
	auto BoneIt = BoneNodeToIndex.find(BoneNode);
	if (BoneIt == BoneNodeToIndex.end())
	{
		return;
	}

	const uint32 BoneIndex = static_cast<uint32>(BoneIt->second);
	int* ControlPointIndices = Cluster->GetControlPointIndices();
	double* ControlPointWeights = Cluster->GetControlPointWeights();
	const int32 Count = Cluster->GetControlPointIndicesCount();

	for (int32 i = 0; i < Count; ++i)
	{
		const int32 ControlPointIndex = ControlPointIndices[i];
		const float Weight = static_cast<float>(ControlPointWeights[i]);
		if (ControlPointIndex < 0 ||
			ControlPointIndex >= static_cast<int32>(ControlPointInfluences.size()) ||
			Weight <= 0.0f)
		{
			continue;
		}

		ControlPointInfluences[ControlPointIndex].push_back({ BoneIndex, Weight });
	}
}

void FBXSkeletalImporter::NormalizeTop4Influences(const TArray<FTempInfluence>& InInfluences, uint32 OutBoneIDs[4], float OutWeights[4]) const
{
	for (int32 i = 0; i < 4; ++i)
	{
		OutBoneIDs[i] = 0;
		OutWeights[i] = 0.0f;
	}

	if (InInfluences.empty())
	{
		OutBoneIDs[0] = 0;
		OutWeights[0] = 1.0f;
		return;
	}

	TArray<FTempInfluence> Sorted = InInfluences;
	std::sort(Sorted.begin(), Sorted.end(),
		[](const FTempInfluence& A, const FTempInfluence& B)
		{
			return A.Weight > B.Weight;
		});

	const int32 Count = static_cast<int32>((std::min)(Sorted.size(), static_cast<size_t>(4)));
	float Sum = 0.0f;

	for (int32 i = 0; i < Count; ++i)
	{
		OutBoneIDs[i] = Sorted[i].BoneIndex;
		OutWeights[i] = Sorted[i].Weight;
		Sum += Sorted[i].Weight;
	}

	if (Sum > 0.0f)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			OutWeights[i] /= Sum;
		}
	}
	else
	{
		OutBoneIDs[0] = 0;
		OutWeights[0] = 1.0f;
	}
}

bool FBXSkeletalImporter::BuildVerticesIndicesAndSections(FbxMesh* Mesh, FSkeletalMesh& OutMesh)
{
	if (!Mesh)
	{
		return false;
	}

	OutMesh.Vertices.clear();
	OutMesh.Indices.clear();
	OutMesh.Sections.clear();
	VertexMap.clear();
	IndicesByMaterial.clear();
	TotalPolygonCornerCount = 0;

	CacheMaterialSlotNames(Mesh);

	FbxStringList UVSetNames;
	Mesh->GetUVSetNames(UVSetNames);
	const char* UVSetName = (UVSetNames.GetCount() > 0) ? UVSetNames[0] : nullptr;

	int32 PolygonVertexCounter = 0;
	const int32 PolygonCount = Mesh->GetPolygonCount();

	for (int32 PolyIndex = 0; PolyIndex < PolygonCount; ++PolyIndex)
	{
		const int32 MaterialIndex = FBXUtil::ReadMaterialIndex(Mesh, PolyIndex);
		const int32 PolySize = Mesh->GetPolygonSize(PolyIndex);
		if (PolySize < 3)
		{
			PolygonVertexCounter += PolySize;
			continue;
		}

		TArray<uint32> PolygonVertexIndices;
		PolygonVertexIndices.reserve(PolySize);

		for (int32 CornerIndex = 0; CornerIndex < PolySize; ++CornerIndex)
		{
			FSkeletalVertex Vertex = {};
			if (!BuildVertexFromCorner(Mesh, PolyIndex, CornerIndex, PolygonVertexCounter, UVSetName, Vertex))
			{
				++PolygonVertexCounter;
				continue;
			}

			PolygonVertexIndices.push_back(AddOrReuseSkeletalVertex(Vertex, OutMesh));
			++PolygonVertexCounter;
			++TotalPolygonCornerCount;
		}

		for (int32 i = 1; i + 1 < static_cast<int32>(PolygonVertexIndices.size()); ++i)
		{
			IndicesByMaterial[MaterialIndex].push_back(PolygonVertexIndices[0]);
			IndicesByMaterial[MaterialIndex].push_back(PolygonVertexIndices[i]);
			IndicesByMaterial[MaterialIndex].push_back(PolygonVertexIndices[i + 1]);
		}
	}

	BuildSectionsFromMaterialBuckets(OutMesh);
	return !OutMesh.Vertices.empty() && !OutMesh.Indices.empty();
}

bool FBXSkeletalImporter::BuildVertexFromCorner(
	FbxMesh* Mesh,
	int32 PolyIndex,
	int32 CornerIndex,
	int32 PolygonVertexIndex,
	const char* UVSetName,
	FSkeletalVertex& OutVertex)
{
	const int32 ControlPointIndex = Mesh->GetPolygonVertex(PolyIndex, CornerIndex);
	if (ControlPointIndex < 0 || ControlPointIndex >= Mesh->GetControlPointsCount())
	{
		return false;
	}

	OutVertex.pos = FBXUtil::ReadPosition(Mesh, ControlPointIndex);
	OutVertex.normal = FBXUtil::ReadNormal(Mesh, PolyIndex, CornerIndex);
	OutVertex.tex = FBXUtil::ReadUV(Mesh, PolyIndex, CornerIndex, UVSetName);
	OutVertex.tangent = FBXUtil::ReadTangent(Mesh, ControlPointIndex, PolygonVertexIndex);

	if (ControlPointIndex < static_cast<int32>(ControlPointInfluences.size()))
	{
		NormalizeTop4Influences(ControlPointInfluences[ControlPointIndex], OutVertex.BoneIDs, OutVertex.BoneWeights);
	}
	else
	{
		OutVertex.BoneIDs[0] = 0;
		OutVertex.BoneWeights[0] = 1.0f;
	}

	return true;
}

FBXSkeletalImporter::FVertexKey FBXSkeletalImporter::MakeVertexKey(const FSkeletalVertex& Vertex) const
{
	FVertexKey Key = {};
	Key.PosX = FBXUtil::QuantizeFloat(Vertex.pos.X);
	Key.PosY = FBXUtil::QuantizeFloat(Vertex.pos.Y);
	Key.PosZ = FBXUtil::QuantizeFloat(Vertex.pos.Z);
	Key.NormalX = FBXUtil::QuantizeFloat(Vertex.normal.X);
	Key.NormalY = FBXUtil::QuantizeFloat(Vertex.normal.Y);
	Key.NormalZ = FBXUtil::QuantizeFloat(Vertex.normal.Z);
	Key.UVX = FBXUtil::QuantizeFloat(Vertex.tex.X);
	Key.UVY = FBXUtil::QuantizeFloat(Vertex.tex.Y);
	Key.TangentX = FBXUtil::QuantizeFloat(Vertex.tangent.X);
	Key.TangentY = FBXUtil::QuantizeFloat(Vertex.tangent.Y);
	Key.TangentZ = FBXUtil::QuantizeFloat(Vertex.tangent.Z);
	Key.TangentW = FBXUtil::QuantizeFloat(Vertex.tangent.W);

	for (int32 i = 0; i < 4; ++i)
	{
		Key.BoneIDs[i] = Vertex.BoneIDs[i];
	}

	Key.BoneWeight0 = FBXUtil::QuantizeFloat(Vertex.BoneWeights[0]);
	Key.BoneWeight1 = FBXUtil::QuantizeFloat(Vertex.BoneWeights[1]);
	Key.BoneWeight2 = FBXUtil::QuantizeFloat(Vertex.BoneWeights[2]);
	Key.BoneWeight3 = FBXUtil::QuantizeFloat(Vertex.BoneWeights[3]);
	return Key;
}

uint32 FBXSkeletalImporter::AddOrReuseSkeletalVertex(const FSkeletalVertex& Vertex, FSkeletalMesh& OutMesh)
{
	const FVertexKey Key = MakeVertexKey(Vertex);
	auto It = VertexMap.find(Key);
	if (It != VertexMap.end())
	{
		return It->second;
	}

	const uint32 NewIndex = static_cast<uint32>(OutMesh.Vertices.size());
	OutMesh.Vertices.push_back(Vertex);
	VertexMap.emplace(Key, NewIndex);
	return NewIndex;
}

void FBXSkeletalImporter::CacheMaterialSlotNames(FbxMesh* Mesh)
{
	MaterialSlotNamesByIndex.clear();

	FbxNode* Node = Mesh ? Mesh->GetNode() : nullptr;
	if (!Node || Node->GetMaterialCount() <= 0)
	{
		MaterialSlotNamesByIndex[0] = "None";
		return;
	}

	for (int32 i = 0; i < Node->GetMaterialCount(); ++i)
	{
		FString SlotName = "Material_" + std::to_string(i);
		if (FbxSurfaceMaterial* Material = Node->GetMaterial(i))
		{
			const char* Name = Material->GetName();
			if (Name && Name[0] != '\0')
			{
				SlotName = Name;
			}
		}
		MaterialSlotNamesByIndex[i] = SlotName;
	}
}

void FBXSkeletalImporter::BuildSectionsFromMaterialBuckets(FSkeletalMesh& OutMesh)
{
	OutMesh.Indices.clear();
	OutMesh.Sections.clear();

	TArray<int32> MaterialIndices;
	MaterialIndices.reserve(IndicesByMaterial.size());
	for (const auto& Pair : IndicesByMaterial)
	{
		MaterialIndices.push_back(Pair.first);
	}
	std::sort(MaterialIndices.begin(), MaterialIndices.end());

	for (int32 MaterialIndex : MaterialIndices)
	{
		TArray<uint32>& SectionIndices = IndicesByMaterial[MaterialIndex];
		if (SectionIndices.empty())
		{
			continue;
		}

		FMeshSection Section = {};
		Section.MaterialIndex = MaterialIndex;
		Section.MaterialSlotName = "Material_" + std::to_string(MaterialIndex);
		if (auto NameIt = MaterialSlotNamesByIndex.find(MaterialIndex); NameIt != MaterialSlotNamesByIndex.end())
		{
			Section.MaterialSlotName = NameIt->second;
		}
		Section.FirstIndex = static_cast<uint32>(OutMesh.Indices.size());
		Section.NumTriangles = static_cast<uint32>(SectionIndices.size() / 3);

		OutMesh.Indices.insert(OutMesh.Indices.end(), SectionIndices.begin(), SectionIndices.end());
		OutMesh.Sections.push_back(Section);
	}
}

void FBXSkeletalImporter::FinalizeMesh(const FString& InFilePath, FSkeletalMesh& OutMesh)
{
	OutMesh.PathFileName = InFilePath;
	OutMesh.CacheBounds();
}

bool FBXSkeletalImporter::ValidateImportedMesh(const FSkeletalMesh& Mesh) const
{
	bool bValid = true;

	if (Mesh.Vertices.empty())
	{
		UE_LOG("[FBXImporter] Validation failed: no vertices.");
		bValid = false;
	}
	if (Mesh.Indices.empty())
	{
		UE_LOG("[FBXImporter] Validation failed: no indices.");
		bValid = false;
	}
	if (Mesh.Bones.empty())
	{
		UE_LOG("[FBXImporter] Validation failed: no bones.");
		bValid = false;
	}

	for (const FSkeletalVertex& Vertex : Mesh.Vertices)
	{
		float WeightSum = 0.0f;
		for (int32 i = 0; i < 4; ++i)
		{
			if (Vertex.BoneIDs[i] >= Mesh.Bones.size())
			{
				UE_LOG("[FBXImporter] Validation failed: bone index out of range.");
				bValid = false;
			}
			WeightSum += Vertex.BoneWeights[i];
		}

		if (std::abs(WeightSum - 1.0f) > 0.05f)
		{
			UE_LOG("[FBXImporter] Warning: vertex weight sum is %.4f.", WeightSum);
		}
	}

	return bValid;
}

void FBXSkeletalImporter::LogImportSummary(const FSkeletalMesh& Mesh) const
{
	const uint32 UniqueVertexCount = static_cast<uint32>(Mesh.Vertices.size());
	const uint32 RemovedDuplicateCount = TotalPolygonCornerCount > UniqueVertexCount
		? TotalPolygonCornerCount - UniqueVertexCount
		: 0;

	UE_LOG(
		"[FBXImporter] Imported skeletal mesh: %s | Vertices=%u Indices=%u Sections=%u Bones=%u RawCorners=%u UniqueVertices=%u RemovedDuplicates=%u BoundsValid=%d Center=(%.3f, %.3f, %.3f) Extent=(%.3f, %.3f, %.3f)",
		Mesh.PathFileName.c_str(),
		static_cast<uint32>(Mesh.Vertices.size()),
		static_cast<uint32>(Mesh.Indices.size()),
		static_cast<uint32>(Mesh.Sections.size()),
		static_cast<uint32>(Mesh.Bones.size()),
		TotalPolygonCornerCount,
		UniqueVertexCount,
		RemovedDuplicateCount,
		Mesh.bBoundsValid ? 1 : 0,
		Mesh.BoundsCenter.X,
		Mesh.BoundsCenter.Y,
		Mesh.BoundsCenter.Z,
		Mesh.BoundsExtent.X,
		Mesh.BoundsExtent.Y,
		Mesh.BoundsExtent.Z);
}


