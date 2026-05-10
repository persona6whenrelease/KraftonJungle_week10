#include "FBXImporter.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <fbxsdk.h>
#include "FbxMetaParser.h"

#include "Core/Log.h"

namespace
{
	struct FTempInfluence
	{
		uint32 BoneIndex = 0;
		float Weight = 0.0f;
	};

	template <typename T>
	bool IsValidIndex(const TArray<T>& Items, int32 Index)
	{
		return Index >= 0 && static_cast<size_t>(Index) < Items.size();
	}

	FVector NormalizeSafe(const FVector& Vector, const FVector& Fallback)
	{
		return Vector.IsNearlyZero() ? Fallback : Vector.Normalized();
	}

	int32 FindSkeletonBoneIndex(const FFbxSkeletonMeta& SkeletonMeta, int32 BoneId)
	{
		auto BoneIndexIt = SkeletonMeta.BoneIdToSkeletonBoneIndex.find(BoneId);
		return BoneIndexIt != SkeletonMeta.BoneIdToSkeletonBoneIndex.end() ? BoneIndexIt->second : -1;
	}

	int32 GetFallbackSkeletonBoneIndex(const FFbxSkeletonMeta& SkeletonMeta)
	{
		const int32 RootBoneIndex = FindSkeletonBoneIndex(SkeletonMeta, SkeletonMeta.RootBoneId);
		return RootBoneIndex >= 0 ? RootBoneIndex : 0;
	}

	bool NormalizeTop4Influences(const TArray<FTempInfluence>& InInfluences, uint32 OutBoneIDs[4], float OutWeights[4])
	{
		for (int32 i = 0; i < 4; ++i)
		{
			OutBoneIDs[i] = 0;
			OutWeights[i] = 0.0f;
		}

		if (InInfluences.empty())
		{
			return false;
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

		if (Sum <= 0.0f)
		{
			return false;
		}

		for (int32 i = 0; i < Count; ++i)
		{
			OutWeights[i] /= Sum;
		}
		return true;
	}

	void AssignSingleBoneWeight(FSkeletalVertex& Vertex, int32 SkeletonBoneIndex)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			Vertex.BoneIDs[i] = 0;
			Vertex.BoneWeights[i] = 0.0f;
		}

		Vertex.BoneIDs[0] = static_cast<uint32>((std::max)(SkeletonBoneIndex, 0));
		Vertex.BoneWeights[0] = 1.0f;
	}

	FMatrix BuildGeometricTransform(FbxNode* Node)
	{
		if (!Node)
		{
			return FMatrix::Identity;
		}

		FbxAMatrix GeometricTransform;
		GeometricTransform.SetT(Node->GetGeometricTranslation(FbxNode::eSourcePivot));
		GeometricTransform.SetR(Node->GetGeometricRotation(FbxNode::eSourcePivot));
		GeometricTransform.SetS(Node->GetGeometricScaling(FbxNode::eSourcePivot));
		return FBXUtil::ConvertFbxMatrix(GeometricTransform);
	}

	FMatrix GetMeshNodeGlobalBind(FbxNode* Node)
	{
		return Node ? FBXUtil::ConvertFbxMatrix(Node->EvaluateGlobalTransform()) : FMatrix::Identity;
	}

	bool AreMatricesNearlyEqual(const FMatrix& A, const FMatrix& B, float Tolerance)
	{
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				if (std::abs(A.M[Row][Col] - B.M[Row][Col]) > Tolerance)
				{
					return false;
				}
			}
		}
		return true;
	}

	const char* AxisToString(FbxAxisSystem::EUpVector Axis)
	{
		switch (Axis)
		{
		case FbxAxisSystem::eXAxis:
			return "X";
		case FbxAxisSystem::eYAxis:
			return "Y";
		case FbxAxisSystem::eZAxis:
			return "Z";
		default:
			return "Unknown";
		}
	}

	const char* FrontParityToString(FbxAxisSystem::EFrontVector Front)
	{
		switch (Front)
		{
		case FbxAxisSystem::eParityEven:
			return "ParityEven";
		case FbxAxisSystem::eParityOdd:
			return "ParityOdd";
		default:
			return "Unknown";
		}
	}

	const char* CoordSystemToString(FbxAxisSystem::ECoordSystem CoordSystem)
	{
		return CoordSystem == FbxAxisSystem::eLeftHanded ? "LeftHanded" : "RightHanded";
	}

	void LogAxisSystem(const char* Prefix, const FbxAxisSystem& AxisSystem)
	{
		int32 UpSign = 0;
		int32 FrontSign = 0;
		const FbxAxisSystem::EUpVector UpAxis = AxisSystem.GetUpVector(UpSign);
		const FbxAxisSystem::EFrontVector FrontParity = AxisSystem.GetFrontVector(FrontSign);
		const FbxAxisSystem::ECoordSystem CoordSystem = AxisSystem.GetCoorSystem();

		UE_LOG("[FBXImporter] %s AxisSystem Up=%s Sign=%d Front=%s Sign=%d Coord=%s",
			Prefix,
			AxisToString(UpAxis),
			UpSign,
			FrontParityToString(FrontParity),
			FrontSign,
			CoordSystemToString(CoordSystem));
	}

	FMatrix GetSkeletonRootBindGlobal(const FFbxImportMeta& ImportMeta, const FFbxSkeletonMeta& SkeletonMeta)
	{
		if (IsValidIndex(ImportMeta.Bones, SkeletonMeta.RootBoneId))
		{
			return ImportMeta.Bones[SkeletonMeta.RootBoneId].BindGlobalMatrix;
		}
		return FMatrix::Identity;
	}

	FMatrix FindSkinnedMeshBindGlobal(
		const FFbxMeshMeta& MeshMeta,
		const FFbxSkinMeta& SkinMeta,
		const FFbxImportMeta& ImportMeta)
	{
		bool bFoundMeshBindMatrix = false;
		bool bLoggedMismatch = false;
		FMatrix FirstMeshBindMatrix = FMatrix::Identity;

		for (int32 ClusterId : SkinMeta.ClusterIds)
		{
			if (IsValidIndex(ImportMeta.Clusters, ClusterId) &&
				ImportMeta.Clusters[ClusterId].bHasMeshBindMatrix)
			{
				const FMatrix& MeshBindMatrix = ImportMeta.Clusters[ClusterId].MeshBindGlobalMatrix;
				if (!bFoundMeshBindMatrix)
				{
					FirstMeshBindMatrix = MeshBindMatrix;
					bFoundMeshBindMatrix = true;
					continue;
				}

				if (!bLoggedMismatch && !AreMatricesNearlyEqual(FirstMeshBindMatrix, MeshBindMatrix, 0.001f))
				{
					UE_LOG("[FBXImporter] Skinned mesh has inconsistent cluster mesh bind matrices. MeshId=%d ClusterId=%d",
						MeshMeta.MeshId,
						ClusterId);
					bLoggedMismatch = true;
				}
			}
		}

		if (bFoundMeshBindMatrix)
		{
			return FirstMeshBindMatrix;
		}

		UE_LOG("[FBXImporter] Skin mesh bind matrix missing; using node global transform. MeshId=%d", MeshMeta.MeshId);
		return GetMeshNodeGlobalBind(MeshMeta.Node);
	}

	FMatrix BuildMeshToSkeletonBindMatrix(
		FbxNode* MeshNode,
		const FMatrix& MeshBindGlobal,
		const FFbxImportMeta& ImportMeta,
		const FFbxSkeletonMeta& SkeletonMeta)
	{
		const FMatrix GeometricTransform = BuildGeometricTransform(MeshNode);
		const FMatrix SkeletonRootBindGlobal = GetSkeletonRootBindGlobal(ImportMeta, SkeletonMeta);
		return GeometricTransform * MeshBindGlobal * SkeletonRootBindGlobal.GetInverse();
	}

	void TransformVertexToSkeletonSpace(FSkeletalVertex& Vertex, const FMatrix& MeshToSkeletonBindMatrix)
	{
		Vertex.pos = MeshToSkeletonBindMatrix.TransformPositionWithW(Vertex.pos);
		Vertex.normal = NormalizeSafe(
			MeshToSkeletonBindMatrix.TransformVector(Vertex.normal),
			FVector(0.0f, 0.0f, 1.0f));

		// TODO: non-uniform scale에는 inverse-transpose normal/tangent matrix를 적용해야 한다.
		const FVector TangentDirection = NormalizeSafe(
			MeshToSkeletonBindMatrix.TransformVector(FVector(Vertex.tangent.X, Vertex.tangent.Y, Vertex.tangent.Z)),
			FVector(1.0f, 0.0f, 0.0f));
		Vertex.tangent = FVector4(TangentDirection, Vertex.tangent.W);
	}

	bool BuildMeshPartGeometry(
		const FFbxMeshMeta& MeshMeta,
		const FMatrix& MeshToSkeletonBindMatrix,
		const std::function<void(int32, FSkeletalVertex&)>& AssignWeights,
		FFbxSkinnedMeshPart& OutPart)
	{
		FbxMesh* Mesh = MeshMeta.Mesh;
		if (!Mesh)
		{
			return false;
		}

		OutPart.Vertices.clear();
		OutPart.Indices.clear();
		OutPart.Sections.clear();

		FbxStringList UVSetNames;
		Mesh->GetUVSetNames(UVSetNames);
		const char* UVSetName = (UVSetNames.GetCount() > 0) ? UVSetNames[0] : nullptr;

		TMap<int32, TArray<uint32>> IndicesByMaterial;
		int32 PolygonVertexCounter = 0;
		bool bLoggedFanTriangulation = false;

		const int32 PolygonCount = Mesh->GetPolygonCount();
		for (int32 PolyIndex = 0; PolyIndex < PolygonCount; ++PolyIndex)
		{
			const int32 PolySize = Mesh->GetPolygonSize(PolyIndex);
			if (PolySize < 3)
			{
				PolygonVertexCounter += PolySize;
				continue;
			}

			if (PolySize > 3 && !bLoggedFanTriangulation)
			{
				UE_LOG("[FBXImporter] Polygon has more than 3 vertices after preprocess; using fan triangulation fallback. MeshId=%d Node=%s",
					MeshMeta.MeshId,
					MeshMeta.SourceNodePath.c_str());
				bLoggedFanTriangulation = true;
			}

			TArray<uint32> PolygonVertexIndices;
			PolygonVertexIndices.reserve(PolySize);

			for (int32 CornerIndex = 0; CornerIndex < PolySize; ++CornerIndex)
			{
				const int32 ControlPointIndex = Mesh->GetPolygonVertex(PolyIndex, CornerIndex);
				if (ControlPointIndex < 0 || ControlPointIndex >= Mesh->GetControlPointsCount())
				{
					++PolygonVertexCounter;
					continue;
				}

				FSkeletalVertex Vertex = {};
				Vertex.pos = FBXUtil::ReadPosition(Mesh, ControlPointIndex);
				Vertex.normal = FBXUtil::ReadNormal(Mesh, PolyIndex, CornerIndex);
				Vertex.tex = FBXUtil::ReadUV(Mesh, PolyIndex, CornerIndex, UVSetName);
				Vertex.tangent = FBXUtil::ReadTangent(Mesh, ControlPointIndex, PolygonVertexCounter);

				TransformVertexToSkeletonSpace(Vertex, MeshToSkeletonBindMatrix);
				AssignWeights(ControlPointIndex, Vertex);

				PolygonVertexIndices.push_back(static_cast<uint32>(OutPart.Vertices.size()));
				OutPart.Vertices.push_back(Vertex);
				++PolygonVertexCounter;
			}

			if (PolygonVertexIndices.size() < 3)
			{
				continue;
			}

			const int32 MaterialIndex = FBXUtil::ReadMaterialIndex(Mesh, PolyIndex);
			TArray<uint32>& SectionIndices = IndicesByMaterial[MaterialIndex];
			for (int32 i = 1; i + 1 < static_cast<int32>(PolygonVertexIndices.size()); ++i)
			{
				SectionIndices.push_back(PolygonVertexIndices[0]);
				SectionIndices.push_back(PolygonVertexIndices[i]);
				SectionIndices.push_back(PolygonVertexIndices[i + 1]);
				//SectionIndices.push_back(PolygonVertexIndices[0]);
				//SectionIndices.push_back(PolygonVertexIndices[i + 1]);
				//SectionIndices.push_back(PolygonVertexIndices[i]);
			}
		}

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

			FFbxMeshPartSection Section;
			Section.SourceMeshId = MeshMeta.MeshId;
			Section.MaterialSlotIndex = MaterialIndex;
			Section.FirstIndex = static_cast<int32>(OutPart.Indices.size());
			Section.IndexCount = static_cast<int32>(SectionIndices.size());

			OutPart.Indices.insert(OutPart.Indices.end(), SectionIndices.begin(), SectionIndices.end());
			OutPart.Sections.push_back(Section);
		}

		return !OutPart.Vertices.empty() && !OutPart.Indices.empty();
	}
}

bool FBXImporter::ImportFbxAsset(const FString& InFilePath, FFBXAsset& OutFBXAsset)
{
	if (!InitializeSdk())
	{
		return false;
	}

	if (!LoadScene(InFilePath))
	{
		ShutdownSdk();
		return false;
	}

	FFbxMetaParser MetaParser(ImportMeta);
	if (!MetaParser.BuildFbxMeta(Scene))
	{
		ShutdownSdk();
		return false;
	}

	ImportMeta.SourceFilePath = InFilePath;
	OutFBXAsset.PathFileName = InFilePath;

	if (!ParseStaticMeshes(OutFBXAsset.StaticMeshes))
	{
		ShutdownSdk();
		return false;
	}

	TArray<FFbxSkinnedMeshPart> SkinnedMeshParts;
	if (!ParseSkeletalMeshes(SkinnedMeshParts))
	{
		ShutdownSdk();
		return false;
	}

	if (!AttachMeshes(SkinnedMeshParts, OutFBXAsset.SkeletalMeshes))
	{
		ShutdownSdk();
		return false;
	}

	FinalizeAsset();

	ShutdownSdk();

	return true;
}

bool FBXImporter::InitializeSdk()
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

void FBXImporter::ShutdownSdk()
{
	DestroyScene();
	ClearState();

	if (Manager)
	{
		Manager->Destroy();
		Manager = nullptr;
	}
}

bool FBXImporter::LoadScene(const FString& InFilePath)
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
	if (bImported)
	{
		PreprocessScene();
	}
	else
	{
		DestroyScene();
	}
	return bImported;
}

bool FBXImporter::ParseStaticMeshes(TArray<FStaticMesh>& OutStaticMeshAsset)
{
	OutStaticMeshAsset.clear();
	//for (int32 MeshId : ImportMeta.StaticMeshIds)
	//{
	//	if (!IsValidIndex(ImportMeta.Meshes, MeshId))
	//	{
	//		continue;
	//	}

	//	// TODO: Build FStaticMesh vertex/index/section data from ImportMeta.Meshes[MeshId].
	//}
	return true;
}

bool FBXImporter::ParseSkeletalMeshes(TArray<FFbxSkinnedMeshPart>& OutSkinnedMeshParts)
{
	OutSkinnedMeshParts.clear();

	for (const FFbxSkeletonMeta& SkeletonMeta : ImportMeta.Skeletons)
	{
		if (!SkeletonMeta.bValid)
		{
			continue;
		}

		int32 ParsedSkinnedCount = 0;
		int32 ParsedRigidCount = 0;

		for (int32 MeshId : SkeletonMeta.SkinnedMeshIds)
		{
			FFbxSkinnedMeshPart Part;
			if (ParseSkinnedMeshPart(MeshId, Part))
			{
				OutSkinnedMeshParts.push_back(Part);
				++ParsedSkinnedCount;
			}
		}

		for (int32 MeshId : SkeletonMeta.RigidAttachedMeshIds)
		{
			FFbxSkinnedMeshPart Part;
			if (ParseRigidAttachedMeshPart(MeshId, Part))
			{
				OutSkinnedMeshParts.push_back(Part);
				++ParsedRigidCount;
			}
		}

		UE_LOG("[FBXImporter] Parsed skeleton parts. SkeletonId=%d Skinned=%d Rigid=%d",
			SkeletonMeta.SkeletonId,
			ParsedSkinnedCount,
			ParsedRigidCount);
	}

	UE_LOG("[FBXImporter] Parsed skeletal mesh parts. Total=%u",
		static_cast<uint32>(OutSkinnedMeshParts.size()));

	return true;
}

bool FBXImporter::ParseSkinnedMeshPart(int32 MeshId, FFbxSkinnedMeshPart& OutPart)
{
	if (!IsValidIndex(ImportMeta.Meshes, MeshId))
	{
		UE_LOG("[FBXImporter] Invalid skinned MeshId=%d", MeshId);
		return false;
	}

	const FFbxMeshMeta& MeshMeta = ImportMeta.Meshes[MeshId];
	if (!MeshMeta.bHasSkin ||
		!IsValidIndex(ImportMeta.Skins, MeshMeta.PrimarySkinId) ||
		!IsValidIndex(ImportMeta.Skeletons, MeshMeta.SkeletonId) ||
		!MeshMeta.Mesh)
	{
		UE_LOG("[FBXImporter] Skinned mesh part has invalid meta. MeshId=%d", MeshId);
		return false;
	}

	const FFbxSkinMeta& SkinMeta = ImportMeta.Skins[MeshMeta.PrimarySkinId];
	const FFbxSkeletonMeta& SkeletonMeta = ImportMeta.Skeletons[MeshMeta.SkeletonId];
	if (!SkinMeta.bValid || !SkeletonMeta.bValid)
	{
		UE_LOG("[FBXImporter] Skinned mesh part has invalid skin or skeleton. MeshId=%d SkinId=%d SkeletonId=%d",
			MeshId,
			MeshMeta.PrimarySkinId,
			MeshMeta.SkeletonId);
		return false;
	}

	TArray<TArray<FTempInfluence>> ControlPointInfluences;
	ControlPointInfluences.resize(MeshMeta.Mesh->GetControlPointsCount());

	for (int32 ClusterId : SkinMeta.ClusterIds)
	{
		if (!IsValidIndex(ImportMeta.Clusters, ClusterId))
		{
			continue;
		}

		const FFbxClusterMeta& ClusterMeta = ImportMeta.Clusters[ClusterId];
		if (!ClusterMeta.bValid || !ClusterMeta.Cluster)
		{
			continue;
		}

		const int32 SkeletonBoneIndex = FindSkeletonBoneIndex(SkeletonMeta, ClusterMeta.LinkBoneId);
		if (SkeletonBoneIndex < 0)
		{
			UE_LOG("[FBXImporter] Cluster link bone is not in skeleton. MeshId=%d ClusterId=%d BoneId=%d SkeletonId=%d",
				MeshId,
				ClusterId,
				ClusterMeta.LinkBoneId,
				SkeletonMeta.SkeletonId);
			continue;
		}

		int32* ControlPointIndices = ClusterMeta.Cluster->GetControlPointIndices();
		double* ControlPointWeights = ClusterMeta.Cluster->GetControlPointWeights();
		const int32 ControlPointCount = ClusterMeta.Cluster->GetControlPointIndicesCount();
		for (int32 InfluenceIndex = 0; InfluenceIndex < ControlPointCount; ++InfluenceIndex)
		{
			const int32 ControlPointIndex = ControlPointIndices ? ControlPointIndices[InfluenceIndex] : -1;
			const float Weight = ControlPointWeights ? static_cast<float>(ControlPointWeights[InfluenceIndex]) : 0.0f;
			if (ControlPointIndex < 0 ||
				ControlPointIndex >= static_cast<int32>(ControlPointInfluences.size()) ||
				Weight <= 0.0f)
			{
				continue;
			}

			ControlPointInfluences[ControlPointIndex].push_back(
				{ static_cast<uint32>(SkeletonBoneIndex), Weight });
		}
	}

	const int32 FallbackBoneIndex = GetFallbackSkeletonBoneIndex(SkeletonMeta);
	int32 MissingInfluenceVertexCount = 0;
	const auto AssignWeights = [&](int32 ControlPointIndex, FSkeletalVertex& Vertex)
		{
			if (ControlPointIndex >= 0 &&
				ControlPointIndex < static_cast<int32>(ControlPointInfluences.size()) &&
				NormalizeTop4Influences(ControlPointInfluences[ControlPointIndex], Vertex.BoneIDs, Vertex.BoneWeights))
			{
				return;
			}

			AssignSingleBoneWeight(Vertex, FallbackBoneIndex);
			++MissingInfluenceVertexCount;
		};

	OutPart = {};
	OutPart.MeshId = MeshId;
	OutPart.SkinId = MeshMeta.PrimarySkinId;
	OutPart.SkeletonId = MeshMeta.SkeletonId;
	OutPart.bSkinned = true;
	OutPart.bRigidAttached = false;
	OutPart.SourceNodePath = MeshMeta.SourceNodePath;

	const FMatrix MeshBindGlobal = FindSkinnedMeshBindGlobal(MeshMeta, SkinMeta, ImportMeta);
	const FMatrix MeshToSkeletonBindMatrix = BuildMeshToSkeletonBindMatrix(
		MeshMeta.Node,
		MeshBindGlobal,
		ImportMeta,
		SkeletonMeta);

	const bool bBuilt = BuildMeshPartGeometry(MeshMeta, MeshToSkeletonBindMatrix, AssignWeights, OutPart);
	if (!bBuilt)
	{
		UE_LOG("[FBXImporter] Failed to parse skinned mesh part geometry. MeshId=%d Node=%s",
			MeshId,
			MeshMeta.SourceNodePath.c_str());
		return false;
	}

	if (MissingInfluenceVertexCount > 0)
	{
		UE_LOG("[FBXImporter] Skinned mesh has vertices without weights; assigned fallback bone. MeshId=%d Count=%d FallbackSkeletonBoneIndex=%d",
			MeshId,
			MissingInfluenceVertexCount,
			FallbackBoneIndex);
	}

	return true;
}

bool FBXImporter::ParseRigidAttachedMeshPart(int32 MeshId, FFbxSkinnedMeshPart& OutPart)
{
	if (!IsValidIndex(ImportMeta.Meshes, MeshId))
	{
		UE_LOG("[FBXImporter] Invalid rigid attached MeshId=%d", MeshId);
		return false;
	}

	const FFbxMeshMeta& MeshMeta = ImportMeta.Meshes[MeshId];
	if (MeshMeta.bHasSkin ||
		!MeshMeta.bRigidAttachedCandidate ||
		!IsValidIndex(ImportMeta.Bones, MeshMeta.AttachedBoneId) ||
		!IsValidIndex(ImportMeta.Skeletons, MeshMeta.AttachedSkeletonId) ||
		!MeshMeta.Mesh)
	{
		UE_LOG("[FBXImporter] Rigid attached mesh part has invalid meta. MeshId=%d", MeshId);
		return false;
	}

	const FFbxSkeletonMeta& SkeletonMeta = ImportMeta.Skeletons[MeshMeta.AttachedSkeletonId];
	const int32 AttachedSkeletonBoneIndex = FindSkeletonBoneIndex(SkeletonMeta, MeshMeta.AttachedBoneId);
	if (AttachedSkeletonBoneIndex < 0)
	{
		UE_LOG("[FBXImporter] Rigid attached mesh bone is not in skeleton. MeshId=%d BoneId=%d SkeletonId=%d",
			MeshId,
			MeshMeta.AttachedBoneId,
			MeshMeta.AttachedSkeletonId);
		return false;
	}

	const auto AssignWeights = [&](int32, FSkeletalVertex& Vertex)
		{
			AssignSingleBoneWeight(Vertex, AttachedSkeletonBoneIndex);
		};

	OutPart = {};
	OutPart.MeshId = MeshId;
	OutPart.SkeletonId = MeshMeta.AttachedSkeletonId;
	OutPart.AttachedBoneId = MeshMeta.AttachedBoneId;
	OutPart.AttachedSkeletonBoneIndex = AttachedSkeletonBoneIndex;
	OutPart.bSkinned = false;
	OutPart.bRigidAttached = true;
	OutPart.SourceNodePath = MeshMeta.SourceNodePath;

	const FMatrix MeshBindGlobal = GetMeshNodeGlobalBind(MeshMeta.Node);
	const FMatrix MeshToSkeletonBindMatrix = BuildMeshToSkeletonBindMatrix(
		MeshMeta.Node,
		MeshBindGlobal,
		ImportMeta,
		SkeletonMeta);

	const bool bBuilt = BuildMeshPartGeometry(MeshMeta, MeshToSkeletonBindMatrix, AssignWeights, OutPart);
	if (!bBuilt)
	{
		UE_LOG("[FBXImporter] Failed to parse rigid attached mesh part geometry. MeshId=%d Node=%s",
			MeshId,
			MeshMeta.SourceNodePath.c_str());
		return false;
	}

	UE_LOG("[FBXImporter] Parsed rigid attached mesh part. MeshId=%d Node=%s BoneId=%d BoneName=%s SkeletonBoneIndex=%d",
		MeshId,
		MeshMeta.SourceNodePath.c_str(),
		MeshMeta.AttachedBoneId,
		ImportMeta.Bones[MeshMeta.AttachedBoneId].Name.c_str(),
		AttachedSkeletonBoneIndex);

	return true;
}

bool FBXImporter::AttachMeshes(const TArray<FFbxSkinnedMeshPart>& SkinnedMeshParts, TArray<FSkeletalMesh>& OutSkeletalMeshAssets)
{
	OutSkeletalMeshAssets.clear();
	for (const FFbxSkeletonMeta& SkeletonMeta : ImportMeta.Skeletons)
	{
		if (!SkeletonMeta.bValid)
		{
			continue;
		}

		TArray<const FFbxSkinnedMeshPart*> Parts;
		int32 SkinnedPartCount = 0;
		int32 RigidPartCount = 0;
		for (const FFbxSkinnedMeshPart& Part : SkinnedMeshParts)
		{
			if (Part.SkeletonId != SkeletonMeta.SkeletonId)
			{
				continue;
			}

			Parts.push_back(&Part);
			SkinnedPartCount += Part.bSkinned ? 1 : 0;
			RigidPartCount += Part.bRigidAttached ? 1 : 0;
		}

		if (Parts.empty())
		{
			UE_LOG("[FBXImporter] Skeleton has no mesh parts to attach. SkeletonId=%d", SkeletonMeta.SkeletonId);
			continue;
		}

		FSkeletalMesh SkeletalMesh;
		if (!BuildSkeletalMeshFromParts(SkeletonMeta, Parts, SkeletalMesh))
		{
			UE_LOG("[FBXImporter] Failed to build skeletal mesh from parts. SkeletonId=%d", SkeletonMeta.SkeletonId);
			return false;
		}

		UE_LOG("[FBXImporter] Built skeletal mesh. SkeletonId=%d Parts=%d Skinned=%d Rigid=%d Vertices=%u Indices=%u Sections=%u Bones=%u",
			SkeletonMeta.SkeletonId,
			static_cast<int32>(Parts.size()),
			SkinnedPartCount,
			RigidPartCount,
			static_cast<uint32>(SkeletalMesh.Vertices.size()),
			static_cast<uint32>(SkeletalMesh.Indices.size()),
			static_cast<uint32>(SkeletalMesh.Sections.size()),
			static_cast<uint32>(SkeletalMesh.Bones.size()));

		OutSkeletalMeshAssets.push_back(SkeletalMesh);
	}
	return true;
}

bool FBXImporter::BuildSkeletalMeshFromParts(
	const FFbxSkeletonMeta& SkeletonMeta,
	const TArray<const FFbxSkinnedMeshPart*>& Parts,
	FSkeletalMesh& OutMesh)
{
	OutMesh = {};
	OutMesh.PathFileName = ImportMeta.SourceFilePath + "#Skeleton_" + std::to_string(SkeletonMeta.SkeletonId);

	if (!IsValidIndex(ImportMeta.Bones, SkeletonMeta.RootBoneId) || SkeletonMeta.BoneIds.empty())
	{
		UE_LOG("[FBXImporter] Cannot build skeletal mesh with invalid skeleton root. SkeletonId=%d RootBoneId=%d",
			SkeletonMeta.SkeletonId,
			SkeletonMeta.RootBoneId);
		return false;
	}

	OutMesh.Bones.resize(SkeletonMeta.BoneIds.size());
	TArray<FMatrix> BoneBindInSkeletonSpace;
	BoneBindInSkeletonSpace.resize(SkeletonMeta.BoneIds.size(), FMatrix::Identity);

	const FMatrix SkeletonRootBindGlobal = ImportMeta.Bones[SkeletonMeta.RootBoneId].BindGlobalMatrix;
	const FMatrix InvSkeletonRootBindGlobal = SkeletonRootBindGlobal.GetInverse();

	for (int32 SkeletonBoneIndex = 0; SkeletonBoneIndex < static_cast<int32>(SkeletonMeta.BoneIds.size()); ++SkeletonBoneIndex)
	{
		const int32 BoneId = SkeletonMeta.BoneIds[SkeletonBoneIndex];
		if (!IsValidIndex(ImportMeta.Bones, BoneId))
		{
			UE_LOG("[FBXImporter] Invalid BoneId in skeleton. SkeletonId=%d BoneId=%d",
				SkeletonMeta.SkeletonId,
				BoneId);
			return false;
		}

		const FFbxBoneMeta& BoneMeta = ImportMeta.Bones[BoneId];
		auto BoneIndexIt = SkeletonMeta.BoneIdToSkeletonBoneIndex.find(BoneId);
		if (BoneIndexIt == SkeletonMeta.BoneIdToSkeletonBoneIndex.end() ||
			BoneIndexIt->second != SkeletonBoneIndex ||
			BoneMeta.SkeletonBoneIndex != SkeletonBoneIndex)
		{
			UE_LOG("[FBXImporter] Bone skeleton index mismatch. SkeletonId=%d BoneId=%d Expected=%d BoneMeta=%d",
				SkeletonMeta.SkeletonId,
				BoneId,
				SkeletonBoneIndex,
				BoneMeta.SkeletonBoneIndex);
			return false;
		}

		FBoneInfo& BoneInfo = OutMesh.Bones[SkeletonBoneIndex];
		BoneInfo.Name = BoneMeta.Name;
		BoneInfo.ParentIndex = -1;

		auto ParentIndexIt = SkeletonMeta.BoneIdToSkeletonBoneIndex.find(BoneMeta.ParentBoneId);
		if (ParentIndexIt != SkeletonMeta.BoneIdToSkeletonBoneIndex.end())
		{
			BoneInfo.ParentIndex = ParentIndexIt->second;
			if (BoneInfo.ParentIndex >= SkeletonBoneIndex)
			{
				UE_LOG("[FBXImporter] Bone parent index must precede child for reference pose accumulation. SkeletonId=%d BoneId=%d ParentIndex=%d ChildIndex=%d",
					SkeletonMeta.SkeletonId,
					BoneId,
					BoneInfo.ParentIndex,
					SkeletonBoneIndex);
				return false;
			}
		}

		BoneBindInSkeletonSpace[SkeletonBoneIndex] = BoneMeta.BindGlobalMatrix * InvSkeletonRootBindGlobal;
		BoneInfo.MeshSpaceToBoneSpace = BoneBindInSkeletonSpace[SkeletonBoneIndex].GetInverse();
	}

	for (int32 SkeletonBoneIndex = 0; SkeletonBoneIndex < static_cast<int32>(OutMesh.Bones.size()); ++SkeletonBoneIndex)
	{
		FBoneInfo& BoneInfo = OutMesh.Bones[SkeletonBoneIndex];
		const FMatrix& BoneGlobalInSkeletonSpace = BoneBindInSkeletonSpace[SkeletonBoneIndex];

		// Runtime accumulates BoneSpaceToMeshSpace with the parent to build reference globals.
		// The field name is misleading here: store parent-local bind, not global bind.
		if (BoneInfo.ParentIndex >= 0)
		{
			BoneInfo.BoneSpaceToMeshSpace =
				BoneGlobalInSkeletonSpace * BoneBindInSkeletonSpace[BoneInfo.ParentIndex].GetInverse();
		}
		else
		{
			BoneInfo.BoneSpaceToMeshSpace = BoneGlobalInSkeletonSpace;
		}
	}

	for (const FFbxSkinnedMeshPart* Part : Parts)
	{
		if (!Part || !ValidateSkinnedMeshPartForAttach(SkeletonMeta, *Part))
		{
			return false;
		}

		const uint32 VertexBase = static_cast<uint32>(OutMesh.Vertices.size());
		const uint32 IndexBase = static_cast<uint32>(OutMesh.Indices.size());

		OutMesh.Vertices.insert(OutMesh.Vertices.end(), Part->Vertices.begin(), Part->Vertices.end());
		OutMesh.Indices.reserve(OutMesh.Indices.size() + Part->Indices.size());
		for (uint32 Index : Part->Indices)
		{
			OutMesh.Indices.push_back(Index + VertexBase);
		}

		for (const FFbxMeshPartSection& PartSection : Part->Sections)
		{
			FMeshSection Section;
			// TODO: Replace this section-local placeholder with a real (SourceMeshId, MaterialSlotIndex) material remap.
			const int32 GlobalMaterialIndex = static_cast<int32>(OutMesh.Sections.size());
			Section.MaterialIndex = GlobalMaterialIndex;
			Section.MaterialSlotName =
				"Mesh_" + std::to_string(PartSection.SourceMeshId) +
				"_Mat" + std::to_string(PartSection.MaterialSlotIndex);
			Section.FirstIndex = IndexBase + static_cast<uint32>(PartSection.FirstIndex);
			Section.NumTriangles = static_cast<uint32>(PartSection.IndexCount / 3);
			OutMesh.Sections.push_back(Section);
		}
	}

	OutMesh.CacheBounds();
	return !OutMesh.Vertices.empty() && !OutMesh.Indices.empty() && !OutMesh.Bones.empty();
}

bool FBXImporter::ValidateSkinnedMeshPartForAttach(
	const FFbxSkeletonMeta& SkeletonMeta,
	const FFbxSkinnedMeshPart& Part) const
{
	bool bValid = true;

	if (Part.SkeletonId != SkeletonMeta.SkeletonId)
	{
		UE_LOG("[FBXImporter] Attach validation failed: skeleton mismatch. PartMeshId=%d PartSkeletonId=%d SkeletonId=%d",
			Part.MeshId,
			Part.SkeletonId,
			SkeletonMeta.SkeletonId);
		return false;
	}

	if (Part.Vertices.empty() || Part.Indices.empty())
	{
		UE_LOG("[FBXImporter] Attach validation failed: empty vertices or indices. MeshId=%d Vertices=%u Indices=%u",
			Part.MeshId,
			static_cast<uint32>(Part.Vertices.size()),
			static_cast<uint32>(Part.Indices.size()));
		return false;
	}

	for (uint32 Index : Part.Indices)
	{
		if (Index >= Part.Vertices.size())
		{
			UE_LOG("[FBXImporter] Attach validation failed: index out of range. MeshId=%d Index=%u VertexCount=%u",
				Part.MeshId,
				Index,
				static_cast<uint32>(Part.Vertices.size()));
			bValid = false;
		}
	}

	const uint32 BoneCount = static_cast<uint32>(SkeletonMeta.BoneIds.size());
	int32 BadWeightSumWarningCount = 0;
	float FirstBadWeightSum = 0.0f;
	bool bLoggedWeightSumWarning = false;
	for (const FSkeletalVertex& Vertex : Part.Vertices)
	{
		float WeightSum = 0.0f;
		for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
		{
			const uint32 BoneIndex = Vertex.BoneIDs[InfluenceIndex];
			const float Weight = Vertex.BoneWeights[InfluenceIndex];
			if (Weight > 0.0f && BoneIndex >= BoneCount)
			{
				UE_LOG("[FBXImporter] Attach validation failed: bone index out of range. MeshId=%d BoneIndex=%u BoneCount=%u",
					Part.MeshId,
					BoneIndex,
					BoneCount);
				bValid = false;
			}
			WeightSum += Weight;
		}

		if (WeightSum <= 0.0001f)
		{
			UE_LOG("[FBXImporter] Attach validation failed: vertex has zero total bone weight. MeshId=%d",
				Part.MeshId);
			bValid = false;
		}
		else if (std::abs(WeightSum - 1.0f) > 0.05f)
		{
			++BadWeightSumWarningCount;
			if (!bLoggedWeightSumWarning)
			{
				FirstBadWeightSum = WeightSum;
				UE_LOG("[FBXImporter] Warning: vertex bone weight sum is %.4f. MeshId=%d. Further weight warnings for this mesh will be summarized.",
					WeightSum,
					Part.MeshId);
				bLoggedWeightSumWarning = true;
			}
		}
	}

	if (BadWeightSumWarningCount > 0)
	{
		UE_LOG("[FBXImporter] Mesh has %d vertices with non-normalized bone weights. MeshId=%d FirstWeightSum=%.4f",
			BadWeightSumWarningCount,
			Part.MeshId,
			FirstBadWeightSum);
	}

	for (const FFbxMeshPartSection& Section : Part.Sections)
	{
		if (Section.FirstIndex < 0 ||
			Section.IndexCount <= 0 ||
			Section.IndexCount % 3 != 0 ||
			Section.FirstIndex + Section.IndexCount > static_cast<int32>(Part.Indices.size()))
		{
			UE_LOG("[FBXImporter] Attach validation failed: invalid section range. MeshId=%d SourceMeshId=%d FirstIndex=%d IndexCount=%d PartIndexCount=%u",
				Part.MeshId,
				Section.SourceMeshId,
				Section.FirstIndex,
				Section.IndexCount,
				static_cast<uint32>(Part.Indices.size()));
			bValid = false;
		}
	}

	return bValid;
}

bool FBXImporter::FinalizeAsset()
{
	return true;
}

void FBXImporter::ClearState()
{
	ImportMeta.Clear();
}

void FBXImporter::PreprocessScene()
{
	if (!Scene || !Manager)
	{
		return;
	}

	LogAxisSystem("Source", Scene->GetGlobalSettings().GetAxisSystem());

	FbxAxisSystem EngineAxisSystem;
	if (!FbxAxisSystem::ParseAxisSystem("yzx", EngineAxisSystem))
	{
		UE_LOG("[FBXImporter] Failed to parse engine axis system.");
		return;
	}

	LogAxisSystem("Target", EngineAxisSystem);

	// Engine convention: +X forward, +Y right, +Z up, left-handed.
	// ConvertScene() only rotates roots and cannot faithfully represent handedness changes.
	// DeepConvertScene() converts transforms, geometry, animation curves, and clusters consistently.
	EngineAxisSystem.ConvertScene(Scene);
	LogAxisSystem("Converted", Scene->GetGlobalSettings().GetAxisSystem());

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

void FBXImporter::DestroyScene()
{
	if (Scene)
	{
		Scene->Destroy();
		Scene = nullptr;
	}
}
