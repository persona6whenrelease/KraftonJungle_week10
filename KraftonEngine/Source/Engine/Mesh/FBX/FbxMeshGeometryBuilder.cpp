#include "FbxMeshGeometryBuilder.h"

#include "Core/Log.h"
#include "FBXUtil.h"
#include "FbxMaterialImportUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fbxsdk.h>
#include <unordered_map>

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

	float GetUpper3x3Determinant(const FMatrix& Matrix)
	{
		return
			Matrix.M[0][0] * (Matrix.M[1][1] * Matrix.M[2][2] - Matrix.M[1][2] * Matrix.M[2][1]) -
			Matrix.M[0][1] * (Matrix.M[1][0] * Matrix.M[2][2] - Matrix.M[1][2] * Matrix.M[2][0]) +
			Matrix.M[0][2] * (Matrix.M[1][0] * Matrix.M[2][1] - Matrix.M[1][1] * Matrix.M[2][0]);
	}

	bool HasMirroredHandedness(const FMatrix& Matrix)
	{
		constexpr float DeterminantEpsilon = 1.e-6f;
		return GetUpper3x3Determinant(Matrix) < -DeterminantEpsilon;
	}

	void AppendTriangleIndices(TArray<uint32>& OutIndices, uint32 I0, uint32 I1, uint32 I2, bool bFlipWinding)
	{
		OutIndices.push_back(I0);
		if (bFlipWinding)
		{
			OutIndices.push_back(I2);
			OutIndices.push_back(I1);
		}
		else
		{
			OutIndices.push_back(I1);
			OutIndices.push_back(I2);
		}
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

	uint32 FloatToBitKey(float Value)
	{
		uint32 Bits = 0;
		static_assert(sizeof(Bits) == sizeof(Value));
		std::memcpy(&Bits, &Value, sizeof(Value));
		return Bits;
	}

	void HashCombine(size_t& Seed, uint32 Value)
	{
		Seed ^= std::hash<uint32>{}(Value) + 0x9e3779b9u + (Seed << 6) + (Seed >> 2);
	}

	template <size_t Count>
	bool EqualArray(const std::array<uint32, Count>& A, const std::array<uint32, Count>& B)
	{
		return std::equal(A.begin(), A.end(), B.begin());
	}

	template <size_t Count>
	void HashArray(size_t& Seed, const std::array<uint32, Count>& Values)
	{
		for (uint32 Value : Values)
		{
			HashCombine(Seed, Value);
		}
	}

	struct FFbxStaticVertexKey
	{
		std::array<uint32, 3> Position = {};
		std::array<uint32, 3> Normal = {};
		std::array<uint32, 4> Color = {};
		std::array<uint32, 2> UV = {};
		std::array<uint32, 4> Tangent = {};

		bool operator==(const FFbxStaticVertexKey& Other) const
		{
			return EqualArray(Position, Other.Position) &&
				EqualArray(Normal, Other.Normal) &&
				EqualArray(Color, Other.Color) &&
				EqualArray(UV, Other.UV) &&
				EqualArray(Tangent, Other.Tangent);
		}
	};

	struct FFbxSkeletalVertexKey
	{
		std::array<uint32, 3> Position = {};
		std::array<uint32, 3> Normal = {};
		std::array<uint32, 2> UV = {};
		std::array<uint32, 4> Tangent = {};
		std::array<uint32, 4> BoneIDs = {};
		std::array<uint32, 4> BoneWeights = {};

		bool operator==(const FFbxSkeletalVertexKey& Other) const
		{
			return EqualArray(Position, Other.Position) &&
				EqualArray(Normal, Other.Normal) &&
				EqualArray(UV, Other.UV) &&
				EqualArray(Tangent, Other.Tangent) &&
				EqualArray(BoneIDs, Other.BoneIDs) &&
				EqualArray(BoneWeights, Other.BoneWeights);
		}
	};

	struct FFbxStaticVertexKeyHasher
	{
		size_t operator()(const FFbxStaticVertexKey& Key) const
		{
			size_t Seed = 0;
			HashArray(Seed, Key.Position);
			HashArray(Seed, Key.Normal);
			HashArray(Seed, Key.Color);
			HashArray(Seed, Key.UV);
			HashArray(Seed, Key.Tangent);
			return Seed;
		}
	};

	struct FFbxSkeletalVertexKeyHasher
	{
		size_t operator()(const FFbxSkeletalVertexKey& Key) const
		{
			size_t Seed = 0;
			HashArray(Seed, Key.Position);
			HashArray(Seed, Key.Normal);
			HashArray(Seed, Key.UV);
			HashArray(Seed, Key.Tangent);
			HashArray(Seed, Key.BoneIDs);
			HashArray(Seed, Key.BoneWeights);
			return Seed;
		}
	};

	FFbxStaticVertexKey MakeStaticVertexKey(const FNormalVertex& Vertex)
	{
		FFbxStaticVertexKey Key;
		Key.Position = { FloatToBitKey(Vertex.pos.X), FloatToBitKey(Vertex.pos.Y), FloatToBitKey(Vertex.pos.Z) };
		Key.Normal = { FloatToBitKey(Vertex.normal.X), FloatToBitKey(Vertex.normal.Y), FloatToBitKey(Vertex.normal.Z) };
		Key.Color = { FloatToBitKey(Vertex.color.X), FloatToBitKey(Vertex.color.Y), FloatToBitKey(Vertex.color.Z), FloatToBitKey(Vertex.color.W) };
		Key.UV = { FloatToBitKey(Vertex.tex.X), FloatToBitKey(Vertex.tex.Y) };
		Key.Tangent = { FloatToBitKey(Vertex.tangent.X), FloatToBitKey(Vertex.tangent.Y), FloatToBitKey(Vertex.tangent.Z), FloatToBitKey(Vertex.tangent.W) };
		return Key;
	}

	FFbxSkeletalVertexKey MakeSkeletalVertexKey(const FSkeletalVertex& Vertex)
	{
		FFbxSkeletalVertexKey Key;
		Key.Position = { FloatToBitKey(Vertex.pos.X), FloatToBitKey(Vertex.pos.Y), FloatToBitKey(Vertex.pos.Z) };
		Key.Normal = { FloatToBitKey(Vertex.normal.X), FloatToBitKey(Vertex.normal.Y), FloatToBitKey(Vertex.normal.Z) };
		Key.UV = { FloatToBitKey(Vertex.tex.X), FloatToBitKey(Vertex.tex.Y) };
		Key.Tangent = { FloatToBitKey(Vertex.tangent.X), FloatToBitKey(Vertex.tangent.Y), FloatToBitKey(Vertex.tangent.Z), FloatToBitKey(Vertex.tangent.W) };
		for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
		{
			Key.BoneIDs[InfluenceIndex] = Vertex.BoneIDs[InfluenceIndex];
			Key.BoneWeights[InfluenceIndex] = FloatToBitKey(Vertex.BoneWeights[InfluenceIndex]);
		}
		return Key;
	}

	uint32 FindOrAddStaticVertex(
		const FNormalVertex& Vertex,
		TArray<FNormalVertex>& Vertices,
		std::unordered_map<FFbxStaticVertexKey, uint32, FFbxStaticVertexKeyHasher>& VertexToIndex)
	{
		const FFbxStaticVertexKey Key = MakeStaticVertexKey(Vertex);
		auto ExistingIt = VertexToIndex.find(Key);
		if (ExistingIt != VertexToIndex.end())
		{
			return ExistingIt->second;
		}

		const uint32 NewIndex = static_cast<uint32>(Vertices.size());
		Vertices.push_back(Vertex);
		VertexToIndex.emplace(Key, NewIndex);
		return NewIndex;
	}

	uint32 FindOrAddSkeletalVertex(
		const FSkeletalVertex& Vertex,
		TArray<FSkeletalVertex>& Vertices,
		std::unordered_map<FFbxSkeletalVertexKey, uint32, FFbxSkeletalVertexKeyHasher>& VertexToIndex)
	{
		const FFbxSkeletalVertexKey Key = MakeSkeletalVertexKey(Vertex);
		auto ExistingIt = VertexToIndex.find(Key);
		if (ExistingIt != VertexToIndex.end())
		{
			return ExistingIt->second;
		}

		const uint32 NewIndex = static_cast<uint32>(Vertices.size());
		Vertices.push_back(Vertex);
		VertexToIndex.emplace(Key, NewIndex);
		return NewIndex;
	}

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

	void TransformSkeletalVertexToAssetSpace(
		FSkeletalVertex& Vertex,
		const FMatrix& MeshToAssetBindMatrix,
		bool bFlipHandedness)
	{
		Vertex.pos = MeshToAssetBindMatrix.TransformPositionWithW(Vertex.pos);
		Vertex.normal = NormalizeSafe(
			MeshToAssetBindMatrix.TransformVector(Vertex.normal),
			FVector(0.0f, 0.0f, 1.0f));

		const FVector TangentDirection = NormalizeSafe(
			MeshToAssetBindMatrix.TransformVector(FVector(Vertex.tangent.X, Vertex.tangent.Y, Vertex.tangent.Z)),
			FVector(1.0f, 0.0f, 0.0f));
		Vertex.tangent = FVector4(TangentDirection, bFlipHandedness ? -Vertex.tangent.W : Vertex.tangent.W);
	}

	void TransformStaticVertexToAssetSpace(
		FNormalVertex& Vertex,
		const FMatrix& MeshToAssetBindMatrix,
		bool bFlipHandedness)
	{
		Vertex.pos = MeshToAssetBindMatrix.TransformPositionWithW(Vertex.pos);
		Vertex.normal = NormalizeSafe(
			MeshToAssetBindMatrix.TransformVector(Vertex.normal),
			FVector(0.0f, 0.0f, 1.0f));

		const FVector TangentDirection = NormalizeSafe(
			MeshToAssetBindMatrix.TransformVector(FVector(Vertex.tangent.X, Vertex.tangent.Y, Vertex.tangent.Z)),
			FVector(1.0f, 0.0f, 0.0f));
		Vertex.tangent = FVector4(TangentDirection, bFlipHandedness ? -Vertex.tangent.W : Vertex.tangent.W);
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
		int32 SourceVertexCount = 0;
		int32 ReusedVertexCount = 0;
		bool bLoggedFanTriangulation = false;

		const int32 PolygonCount = Mesh->GetPolygonCount();
		std::unordered_map<FFbxSkeletalVertexKey, uint32, FFbxSkeletalVertexKeyHasher> VertexToIndex;
		VertexToIndex.reserve(static_cast<size_t>(PolygonCount) * 3);
		const bool bFlipWinding = HasMirroredHandedness(MeshToAssetBindMatrix);
		if (bFlipWinding)
		{
			UE_LOG("[FBXImporter] Mirrored skeletal mesh transform detected; flipping winding. MeshId=%d Node=%s",
				MeshMeta.MeshId,
				MeshMeta.SourceNodePath.c_str());
		}

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
				TransformSkeletalVertexToAssetSpace(Vertex, MeshToAssetBindMatrix, bFlipWinding);
				AssignWeights(ControlPointIndex, Vertex);

				const uint32 VertexCountBefore = static_cast<uint32>(OutPart.Vertices.size());
				const uint32 VertexIndex = FindOrAddSkeletalVertex(Vertex, OutPart.Vertices, VertexToIndex);
				if (static_cast<uint32>(OutPart.Vertices.size()) == VertexCountBefore)
				{
					++ReusedVertexCount;
				}
				++SourceVertexCount;
				PolygonVertexIndices.push_back(VertexIndex);
				++PolygonVertexCounter;
			}

			if (PolygonVertexIndices.size() < 3)
			{
				continue;
			}

			TArray<uint32>& SectionIndices = IndicesByMaterial[MaterialSlotIndex];
			for (int32 i = 1; i + 1 < static_cast<int32>(PolygonVertexIndices.size()); ++i)
			{
				AppendTriangleIndices(
					SectionIndices,
					PolygonVertexIndices[0],
					PolygonVertexIndices[i],
					PolygonVertexIndices[i + 1],
					bFlipWinding);
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
		UE_LOG("[FBXImporter] Skeletal vertex dedup. MeshId=%d Node=%s SourceCorners=%d UniqueVertices=%u Reused=%d",
			MeshMeta.MeshId,
			MeshMeta.SourceNodePath.c_str(),
			SourceVertexCount,
			static_cast<uint32>(OutPart.Vertices.size()),
			ReusedVertexCount);

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
		int32 SourceVertexCount = 0;
		int32 ReusedVertexCount = 0;
		bool bLoggedFanTriangulation = false;

		const int32 PolygonCount = Mesh->GetPolygonCount();
		std::unordered_map<FFbxStaticVertexKey, uint32, FFbxStaticVertexKeyHasher> VertexToIndex;
		VertexToIndex.reserve(static_cast<size_t>(PolygonCount) * 3);
		const bool bFlipWinding = HasMirroredHandedness(MeshToAssetBindMatrix);
		if (bFlipWinding)
		{
			UE_LOG("[FBXImporter] Mirrored static mesh transform detected; flipping winding. MeshId=%d Node=%s",
				MeshMeta.MeshId,
				MeshMeta.SourceNodePath.c_str());
		}

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
				TransformStaticVertexToAssetSpace(Vertex, MeshToAssetBindMatrix, bFlipWinding);

				const uint32 VertexCountBefore = static_cast<uint32>(OutMesh.Vertices.size());
				const uint32 VertexIndex = FindOrAddStaticVertex(Vertex, OutMesh.Vertices, VertexToIndex);
				if (static_cast<uint32>(OutMesh.Vertices.size()) == VertexCountBefore)
				{
					++ReusedVertexCount;
				}
				++SourceVertexCount;
				PolygonVertexIndices.push_back(VertexIndex);
				++PolygonVertexCounter;
			}

			if (PolygonVertexIndices.size() < 3)
			{
				continue;
			}

			TArray<uint32>& SectionIndices = IndicesByMaterial[MaterialSlotIndex];
			for (int32 i = 1; i + 1 < static_cast<int32>(PolygonVertexIndices.size()); ++i)
			{
				AppendTriangleIndices(
					SectionIndices,
					PolygonVertexIndices[0],
					PolygonVertexIndices[i],
					PolygonVertexIndices[i + 1],
					bFlipWinding);
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
		UE_LOG("[FBXImporter] Static vertex dedup. MeshId=%d Node=%s SourceCorners=%d UniqueVertices=%u Reused=%d",
			MeshMeta.MeshId,
			MeshMeta.SourceNodePath.c_str(),
			SourceVertexCount,
			static_cast<uint32>(OutMesh.Vertices.size()),
			ReusedVertexCount);
		OutMesh.CacheBounds();

		return !OutMesh.Vertices.empty() && !OutMesh.Indices.empty();
	}
}
