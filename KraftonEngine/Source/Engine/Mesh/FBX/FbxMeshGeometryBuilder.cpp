#include "FbxMeshGeometryBuilder.h"

#include "Core/Log.h"
#include "FBXUtil.h"
#include "FbxMaterialImportUtils.h"

#include <algorithm>
#include <cmath>
#include <fbxsdk.h>

namespace
{
	template <typename T>
	bool IsValidIndex(const TArray<T>& Items, int32 Index)
	{
		return Index >= 0 && static_cast<size_t>(Index) < Items.size();
	}

	FVector NormalizeSafe(const FVector& Vector, const FVector& Fallback)
	{
		return Vector.IsNearlyZero() ? Fallback : Vector.Normalized();
	}

	bool IsZeroUV(const FVector2& UV)
	{
		constexpr float UVZeroTolerance = 1.e-6f;
		return std::fabs(UV.X) <= UVZeroTolerance && std::fabs(UV.Y) <= UVZeroTolerance;
	}

	struct FFbxGeometryBuildStats
	{
		FFbxUVReadStats UVReadStats;
		int32 UVZeroCount = 0;
		int32 UVNonZeroCount = 0;
		bool bHasUVSample = false;
		bool bHasFirstNonZeroUV = false;
		FVector2 UVMin = FVector2(0.0f, 0.0f);
		FVector2 UVMax = FVector2(0.0f, 0.0f);
		FVector2 FirstNonZeroUV = FVector2(0.0f, 0.0f);
	};

	FString BuildUVSetNameList(FbxMesh* Mesh)
	{
		if (!Mesh)
		{
			return "None";
		}

		FbxStringList UVSetNames;
		Mesh->GetUVSetNames(UVSetNames);

		FString UVSetNameList;
		for (int32 UVSetIndex = 0; UVSetIndex < UVSetNames.GetCount(); ++UVSetIndex)
		{
			if (!UVSetNameList.empty())
			{
				UVSetNameList += ", ";
			}
			const char* CurrentUVSetName = UVSetNames[UVSetIndex].Buffer();
			UVSetNameList += CurrentUVSetName ? CurrentUVSetName : "<null>";
		}

		return UVSetNameList.empty() ? "None" : UVSetNameList;
	}

	FString BuildPreferredUVSetNameList(const FFbxMeshMeta& MeshMeta)
	{
		FString PreferredUVSetNameList;
		for (const FString& MaterialUVSetName : MeshMeta.MaterialUVSetNames)
		{
			if (MaterialUVSetName.empty())
			{
				continue;
			}

			if (PreferredUVSetNameList.find(MaterialUVSetName) != FString::npos)
			{
				continue;
			}

			if (!PreferredUVSetNameList.empty())
			{
				PreferredUVSetNameList += ", ";
			}
			PreferredUVSetNameList += MaterialUVSetName;
		}

		return PreferredUVSetNameList.empty() ? "None" : PreferredUVSetNameList;
	}

	void UpdateUVStats(FFbxGeometryBuildStats& Stats, const FVector2& UV)
	{
		if (!Stats.bHasUVSample)
		{
			Stats.UVMin = UV;
			Stats.UVMax = UV;
			Stats.bHasUVSample = true;
		}
		else
		{
			Stats.UVMin.X = std::min(Stats.UVMin.X, UV.X);
			Stats.UVMin.Y = std::min(Stats.UVMin.Y, UV.Y);
			Stats.UVMax.X = std::max(Stats.UVMax.X, UV.X);
			Stats.UVMax.Y = std::max(Stats.UVMax.Y, UV.Y);
		}

		if (IsZeroUV(UV))
		{
			++Stats.UVZeroCount;
			return;
		}

		++Stats.UVNonZeroCount;
		if (!Stats.bHasFirstNonZeroUV)
		{
			Stats.FirstNonZeroUV = UV;
			Stats.bHasFirstNonZeroUV = true;
		}
	}

	void LogUVStatsIfEnabled(const FFbxMeshMeta& MeshMeta, FbxMesh* Mesh, const FFbxGeometryBuildStats& Stats)
	{
		FbxStringList UVSetNames;
		if (Mesh)
		{
			Mesh->GetUVSetNames(UVSetNames);
		}

		const char* FirstUVSetName = (UVSetNames.GetCount() > 0) ? UVSetNames[0] : nullptr;
		const FString UVSetNameList = BuildUVSetNameList(Mesh);
		const FString PreferredUVSetNameList = BuildPreferredUVSetNameList(MeshMeta);

		UE_LOG("[FBXImporter] Mesh UV sets. MeshId=%d Node=%s Count=%d First=%s Names=%s PreferredMaterialUVSets=%s",
			MeshMeta.MeshId,
			MeshMeta.SourceNodePath.c_str(),
			UVSetNames.GetCount(),
			FirstUVSetName ? FirstUVSetName : "<null>",
			UVSetNameList.c_str(),
			PreferredUVSetNameList.c_str());

		UE_LOG("[FBXImporter] UV summary. MeshId=%d Node=%s UVSets=%d Preferred=%s Vertices=%d PreferredOK=%d SetFallbackOK=%d ManualOK=%d Default=%d GetPolygonVertexUVFailed=%d Unmapped=%d UVZero=%d UVNonZero=%d UVMin=(%.6f, %.6f) UVMax=(%.6f, %.6f) FirstNonZero=(%.6f, %.6f)",
			MeshMeta.MeshId,
			MeshMeta.SourceNodePath.c_str(),
			UVSetNames.GetCount(),
			PreferredUVSetNameList.c_str(),
			Stats.UVZeroCount + Stats.UVNonZeroCount,
			Stats.UVReadStats.PreferredSuccessCount,
			Stats.UVReadStats.UVSetFallbackSuccessCount,
			Stats.UVReadStats.ManualElementFallbackSuccessCount,
			Stats.UVReadStats.DefaultUVCount,
			Stats.UVReadStats.GetPolygonVertexUVFailedCount,
			Stats.UVReadStats.UnmappedCount,
			Stats.UVZeroCount,
			Stats.UVNonZeroCount,
			Stats.UVMin.X,
			Stats.UVMin.Y,
			Stats.UVMax.X,
			Stats.UVMax.Y,
			Stats.FirstNonZeroUV.X,
			Stats.FirstNonZeroUV.Y);
	}

	void TransformSkeletalVertexToAssetSpace(FSkeletalVertex& Vertex, const FMatrix& MeshToAssetBindMatrix)
	{
		Vertex.pos = MeshToAssetBindMatrix.TransformPositionWithW(Vertex.pos);
		Vertex.normal = NormalizeSafe(
			MeshToAssetBindMatrix.TransformVector(Vertex.normal),
			FVector(0.0f, 0.0f, 1.0f));

		const FVector TangentDirection = NormalizeSafe(
			MeshToAssetBindMatrix.TransformVector(FVector(Vertex.tangent.X, Vertex.tangent.Y, Vertex.tangent.Z)),
			FVector(1.0f, 0.0f, 0.0f));
		Vertex.tangent = FVector4(TangentDirection, Vertex.tangent.W);
	}

	void TransformStaticVertexToAssetSpace(FNormalVertex& Vertex, const FMatrix& MeshToAssetBindMatrix)
	{
		Vertex.pos = MeshToAssetBindMatrix.TransformPositionWithW(Vertex.pos);
		Vertex.normal = NormalizeSafe(
			MeshToAssetBindMatrix.TransformVector(Vertex.normal),
			FVector(0.0f, 0.0f, 1.0f));

		const FVector TangentDirection = NormalizeSafe(
			MeshToAssetBindMatrix.TransformVector(FVector(Vertex.tangent.X, Vertex.tangent.Y, Vertex.tangent.Z)),
			FVector(1.0f, 0.0f, 0.0f));
		Vertex.tangent = FVector4(TangentDirection, Vertex.tangent.W);
	}

	FString GetMaterialSlotName(const FFbxMeshMeta& MeshMeta, int32 MaterialIndex)
	{
		return IsValidIndex(MeshMeta.MaterialSlotNames, MaterialIndex)
			? FbxMaterialImportUtils::NormalizeMaterialSlotName(MeshMeta.MaterialSlotNames[MaterialIndex])
			: "None";
	}
}

namespace FbxMeshGeometryBuilder
{
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

	FMatrix BuildMeshToAssetBindMatrix(FbxNode* MeshNode, const FMatrix& MeshBindGlobal)
	{
		const FMatrix GeometricTransform = BuildGeometricTransform(MeshNode);
		return GeometricTransform * MeshBindGlobal;
	}

	bool BuildSkeletalMeshPartGeometry(
		const FFbxMeshMeta& MeshMeta,
		const FMatrix& MeshToAssetBindMatrix,
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

		FFbxGeometryBuildStats GeometryStats;
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

			const int32 MaterialSlotIndex = FBXUtil::ReadMaterialIndex(Mesh, PolyIndex);
			const char* PreferredUVSetName = IsValidIndex(MeshMeta.MaterialUVSetNames, MaterialSlotIndex) &&
				!MeshMeta.MaterialUVSetNames[MaterialSlotIndex].empty()
				? MeshMeta.MaterialUVSetNames[MaterialSlotIndex].c_str()
				: nullptr;

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
				Vertex.tex = FBXUtil::ReadUV(
					Mesh,
					PolyIndex,
					CornerIndex,
					ControlPointIndex,
					PolygonVertexCounter,
					PreferredUVSetName,
					&GeometryStats.UVReadStats);
				Vertex.tex.V = 1.0f - Vertex.tex.V;
				Vertex.tangent = FBXUtil::ReadTangent(Mesh, ControlPointIndex, PolygonVertexCounter);

				UpdateUVStats(GeometryStats, Vertex.tex);
				TransformSkeletalVertexToAssetSpace(Vertex, MeshToAssetBindMatrix);
				AssignWeights(ControlPointIndex, Vertex);

				PolygonVertexIndices.push_back(static_cast<uint32>(OutPart.Vertices.size()));
				OutPart.Vertices.push_back(Vertex);
				++PolygonVertexCounter;
			}

			if (PolygonVertexIndices.size() < 3)
			{
				continue;
			}

			TArray<uint32>& SectionIndices = IndicesByMaterial[MaterialSlotIndex];
			for (int32 i = 1; i + 1 < static_cast<int32>(PolygonVertexIndices.size()); ++i)
			{
				SectionIndices.push_back(PolygonVertexIndices[0]);
				SectionIndices.push_back(PolygonVertexIndices[i + 1]);
				SectionIndices.push_back(PolygonVertexIndices[i]);
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
			Section.SourceMaterialId = IsValidIndex(MeshMeta.MaterialIds, MaterialIndex)
				? MeshMeta.MaterialIds[MaterialIndex]
				: -1;
			Section.MaterialSlotName = GetMaterialSlotName(MeshMeta, MaterialIndex);
			Section.FirstIndex = static_cast<int32>(OutPart.Indices.size());
			Section.IndexCount = static_cast<int32>(SectionIndices.size());

			OutPart.Indices.insert(OutPart.Indices.end(), SectionIndices.begin(), SectionIndices.end());
			OutPart.Sections.push_back(Section);
		}

		LogUVStatsIfEnabled(MeshMeta, Mesh, GeometryStats);

		return !OutPart.Vertices.empty() && !OutPart.Indices.empty();
	}

	bool BuildStaticMeshGeometry(
		const FFbxMeshMeta& MeshMeta,
		const FMatrix& MeshToAssetBindMatrix,
		FStaticMesh& OutMesh)
	{
		FbxMesh* Mesh = MeshMeta.Mesh;
		if (!Mesh)
		{
			return false;
		}

		OutMesh.Vertices.clear();
		OutMesh.Indices.clear();
		OutMesh.Sections.clear();

		FFbxGeometryBuildStats GeometryStats;
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

			const int32 MaterialSlotIndex = FBXUtil::ReadMaterialIndex(Mesh, PolyIndex);
			const char* PreferredUVSetName = IsValidIndex(MeshMeta.MaterialUVSetNames, MaterialSlotIndex) &&
				!MeshMeta.MaterialUVSetNames[MaterialSlotIndex].empty()
				? MeshMeta.MaterialUVSetNames[MaterialSlotIndex].c_str()
				: nullptr;

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

				FNormalVertex Vertex = {};
				Vertex.pos = FBXUtil::ReadPosition(Mesh, ControlPointIndex);
				Vertex.normal = FBXUtil::ReadNormal(Mesh, PolyIndex, CornerIndex);
				Vertex.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
				Vertex.tex = FBXUtil::ReadUV(
					Mesh,
					PolyIndex,
					CornerIndex,
					ControlPointIndex,
					PolygonVertexCounter,
					PreferredUVSetName,
					&GeometryStats.UVReadStats);
				Vertex.tex.V = 1.0f - Vertex.tex.V;
				Vertex.tangent = FBXUtil::ReadTangent(Mesh, ControlPointIndex, PolygonVertexCounter);

				UpdateUVStats(GeometryStats, Vertex.tex);
				TransformStaticVertexToAssetSpace(Vertex, MeshToAssetBindMatrix);

				PolygonVertexIndices.push_back(static_cast<uint32>(OutMesh.Vertices.size()));
				OutMesh.Vertices.push_back(Vertex);
				++PolygonVertexCounter;
			}

			if (PolygonVertexIndices.size() < 3)
			{
				continue;
			}

			TArray<uint32>& SectionIndices = IndicesByMaterial[MaterialSlotIndex];
			for (int32 i = 1; i + 1 < static_cast<int32>(PolygonVertexIndices.size()); ++i)
			{
				SectionIndices.push_back(PolygonVertexIndices[0]);
				SectionIndices.push_back(PolygonVertexIndices[i + 1]);
				SectionIndices.push_back(PolygonVertexIndices[i]);
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

			FStaticMeshSection Section;
			Section.MaterialIndex = MaterialIndex;
			Section.MaterialSlotName = GetMaterialSlotName(MeshMeta, MaterialIndex);
			Section.FirstIndex = static_cast<uint32>(OutMesh.Indices.size());
			Section.NumTriangles = static_cast<uint32>(SectionIndices.size() / 3);

			OutMesh.Indices.insert(OutMesh.Indices.end(), SectionIndices.begin(), SectionIndices.end());
			OutMesh.Sections.push_back(Section);
		}

		LogUVStatsIfEnabled(MeshMeta, Mesh, GeometryStats);
		OutMesh.CacheBounds();

		return !OutMesh.Vertices.empty() && !OutMesh.Indices.empty();
	}
}
