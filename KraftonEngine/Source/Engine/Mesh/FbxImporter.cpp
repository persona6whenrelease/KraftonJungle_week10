#include "FbxImporter.h"

#include "Core/Log.h"
#include "Engine/Platform/Paths.h"
#include "Math/Vector.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Mesh/StaticMeshAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#ifndef WITH_FBX_SDK
#define WITH_FBX_SDK 0
#endif

#if WITH_FBX_SDK
#include <fbxsdk.h>
#endif

#if WITH_FBX_SDK

struct FFbxSceneContext
{
	FbxManager* Manager = nullptr;
	FbxScene* Scene = nullptr;
};

struct FFbxMaterialInfo
{
	FString MaterialSlotName = "None";
	FString DiffuseTexturePath;
	FVector DiffuseColor = FVector(1.0f, 1.0f, 1.0f);
};

static const char* GetFbxAxisName(int32 Axis)
{
	switch (std::abs(Axis))
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

static const char* GetFbxCoordSystemName(FbxAxisSystem::ECoordSystem CoordSystem)
{
	return CoordSystem == FbxAxisSystem::eLeftHanded ? "LeftHanded" : "RightHanded";
}

static void LogFbxAxisSystem(const char* Label, const FbxAxisSystem& AxisSystem)
{
	int UpSign = 1;
	int FrontSign = 1;

	const int32 UpAxis = static_cast<int32>(AxisSystem.GetUpVector(UpSign));
	const int32 FrontAxis = static_cast<int32>(AxisSystem.GetFrontVector(FrontSign));

	UE_LOG(
		"[FBX] %s Axis. Up=%s%s, Front=%s%s, Coord=%s",
		Label,
		UpSign < 0 ? "-" : "+",
		GetFbxAxisName(UpAxis),
		FrontSign < 0 ? "-" : "+",
		GetFbxAxisName(FrontAxis),
		GetFbxCoordSystemName(AxisSystem.GetCoorSystem())
	);
}

static FbxAxisSystem MakeUnrealAxisSystem()
{
	FbxAxisSystem UnrealAxisSystem;

	if (!FbxAxisSystem::ParseAxisSystem("yzx", UnrealAxisSystem))
	{
		UnrealAxisSystem = FbxAxisSystem(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eLeftHanded);
	}

	return UnrealAxisSystem;
}

static void NormalizeFbxSceneForEngine(FbxScene* Scene)
{
	if (!Scene)
	{
		return;
	}

	const FbxAxisSystem SourceAxisSystem =
		Scene->GetGlobalSettings().GetAxisSystem();
	const FbxAxisSystem UnrealAxisSystem = MakeUnrealAxisSystem();

	LogFbxAxisSystem("Source", SourceAxisSystem);
	LogFbxAxisSystem("Target", UnrealAxisSystem);

	UnrealAxisSystem.DeepConvertScene(Scene);
	FbxSystemUnit::cm.ConvertScene(Scene);

	LogFbxAxisSystem(
		"Converted",
		Scene->GetGlobalSettings().GetAxisSystem()
	);
}

static void DestroyFbxSceneContext(FFbxSceneContext& Context)
{
	if (Context.Manager)
	{
		Context.Manager->Destroy();
	}
	Context.Manager = nullptr;
	Context.Scene = nullptr;
}

static bool LoadFbxScene(const FString& FbxFilePath, FFbxSceneContext& OutContext)
{
	OutContext = FFbxSceneContext();

	std::wstring DiskPath;
	FString Error;

	if (!FPaths::TryResolvePackagePath(FbxFilePath, DiskPath, &Error))
	{
		UE_LOG("Invalid FBX file path: %s", Error.c_str());
		return false;
	}

	FbxManager* Manager = FbxManager::Create();
	if (!Manager)
	{
		UE_LOG("Failed to create FBX manager");
		return false;
	}

	FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
	Manager->SetIOSettings(IOSettings);

	FbxScene* Scene = FbxScene::Create(Manager, "ImportScene");
	FbxImporter* Importer = FbxImporter::Create(Manager, "FbxImporter");

	const FString Utf8DiskPath = FPaths::ToUtf8(DiskPath);

	if (!Importer->Initialize(Utf8DiskPath.c_str(), -1, Manager->GetIOSettings()))
	{
		UE_LOG("FBX Importer initialize failed: %s", Importer->GetStatus().GetErrorString());
		Importer->Destroy();
		Manager->Destroy();
		return false;
	}

	if (!Importer->Import(Scene))
	{
		UE_LOG("FBX scene import failed: %s", Importer->GetStatus().GetErrorString());
		Importer->Destroy();
		Manager->Destroy();
		return false;
	}

	Importer->Destroy();

	// 삼각형이 아닌 polygon에 대해서 Scene을 Triangulation
	NormalizeFbxSceneForEngine(Scene);

	FbxGeometryConverter GeometryConverter(Manager);
	GeometryConverter.Triangulate(Scene, true);

	OutContext.Manager = Manager;
	OutContext.Scene = Scene;

	return true;
}

static const char* GetFbxAttributeTypeName(FbxNodeAttribute::EType Type)
{
	switch (Type)
	{
	case FbxNodeAttribute::eMesh:
		return "Mesh";
	case FbxNodeAttribute::eSkeleton:
		return "Skeleton";
	case FbxNodeAttribute::eCamera:
		return "Camera";
	case FbxNodeAttribute::eLight:
		return "Light";
	case FbxNodeAttribute::eNull:
		return "Null";
	default:
		return "Other";
	}
}

static FVector ToEngineVector(const FbxVector4& Value)
{
	return FVector(static_cast<float>(Value[0]), static_cast<float>(Value[1]), static_cast<float>(Value[2]));
}

static FVector TransformFbxPosition(const FbxAMatrix& Transform, const FbxVector4& Position)
{
	return ToEngineVector(Transform.MultT(Position));
}

static FVector TransformFbxNormal(const FbxAMatrix& Transform, const FbxVector4& Normal)
{
	return ToEngineVector(Transform.MultR(Normal)).Normalized();
}

static FMatrix ToEngineMatrix(const FbxAMatrix& Value)
{
	return FMatrix(
		static_cast<float>(Value.Get(0, 0)),
		static_cast<float>(Value.Get(0, 1)),
		static_cast<float>(Value.Get(0, 2)),
		static_cast<float>(Value.Get(0, 3)),

		static_cast<float>(Value.Get(1, 0)),
		static_cast<float>(Value.Get(1, 1)),
		static_cast<float>(Value.Get(1, 2)),
		static_cast<float>(Value.Get(1, 3)),

		static_cast<float>(Value.Get(2, 0)),
		static_cast<float>(Value.Get(2, 1)),
		static_cast<float>(Value.Get(2, 2)),
		static_cast<float>(Value.Get(2, 3)),

		static_cast<float>(Value.Get(3, 0)),
		static_cast<float>(Value.Get(3, 1)),
		static_cast<float>(Value.Get(3, 2)),
		static_cast<float>(Value.Get(3, 3))
	);
}

static void TraverseFbxNode(FbxNode* Node, int32 Depth)
{
	if (!Node) return;

	std::string Indent(Depth * 2, ' ');
	
	const char* NodeName = Node->GetName();
	
	FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
	const char* AttributeTypeName = "NoAttribute";
	
	// 모든 Node가 Attribute를 가지는 것은 아님.
	// 그래서 검사 안 하면 Attribute가 없는 노드에서 null pointer 에러 날 수 있음.
	if (Attribute)
	{
		AttributeTypeName = GetFbxAttributeTypeName(Attribute->GetAttributeType());

		if(Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			FbxMesh* Mesh = Node->GetMesh();
			if (Mesh)
			{
				const int32 ControlPointCount = static_cast<int32>(Mesh->GetControlPointsCount());
				const int32 PolygonCount = static_cast<int32>(Mesh->GetPolygonCount());

				UE_LOG("%s  MeshInfo: ControlPoints=%d, Polygons=%d", Indent.c_str(), ControlPointCount, PolygonCount);

				// Polygon의 Vertex가 ControlPoint 몇 번을 가리키는지 확인용
				const int32 PreviewPolygonCount = PolygonCount < 5 ? PolygonCount : 5;
				for (int32 PolygonIndex = 0; PolygonIndex < PreviewPolygonCount; ++PolygonIndex)
				{
					const int32 PolygonSize = static_cast<int32>(Mesh->GetPolygonSize(PolygonIndex));
					UE_LOG("%s   Polygon[%d]: Size=%d", Indent.c_str(), PolygonIndex, PolygonSize);

					for (int32 VertexIndex = 0; VertexIndex < PolygonSize; ++VertexIndex)
					{
						const int32 ControlPointIndex = static_cast<int32>(Mesh->GetPolygonVertex(
							PolygonIndex, VertexIndex));
						UE_LOG("%s   Vertex[%d] -> ControlPoint[%d]", Indent.c_str(), VertexIndex, ControlPointIndex);

						// ControlPoint의 실제 위치 확인용
						FbxVector4* ControlPoints = Mesh->GetControlPoints();
						if (ControlPointIndex >= 0 && ControlPointIndex < ControlPointCount)
						{
							const FbxVector4 FbxPosition = ControlPoints[ControlPointIndex];
							const FVector EnginePosition = ToEngineVector(FbxPosition);
							UE_LOG("%s      Position: X=%.3f, Y=%.3f, Z=%.3f", Indent.c_str(),
							       EnginePosition.X, EnginePosition.Y, EnginePosition.Z);

							// StaticMesh를 위한 임시 데이터
							FNormalVertex PreviewVertex;
							PreviewVertex.pos = EnginePosition;
							PreviewVertex.normal = FVector(0.0f, 0.0f, 1.0f);
							PreviewVertex.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
							PreviewVertex.tex = FVector2(0.0f, 0.0f);
							PreviewVertex.tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
						}
					}
				}
			}
		}
	}

	UE_LOG("%s Node: %s, Type: %s", Indent.c_str(), NodeName, AttributeTypeName);
	
	const int32 ChildCount = static_cast<int32>(Node->GetChildCount());
	for (int32 ChildIndex  = 0; ChildIndex  < ChildCount; ++ChildIndex)
	{
		TraverseFbxNode(Node->GetChild(ChildIndex), Depth + 1);
	}
}

#endif

static void CollectFbxMeshNodes(FbxNode* Node, TArray<FbxNode*>& OutMeshNodes)
{
	if (!Node)
	{
		return;
	}

	FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
	if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh && Node->GetMesh())
	{
		FbxMesh* Mesh = Node->GetMesh();
		if (Mesh->GetPolygonCount() > 0 && Mesh->GetControlPointsCount() > 0)
		{
			OutMeshNodes.push_back(Node);
		}
	}

	const int32 ChildCount = static_cast<int32>(Node->GetChildCount());
	for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		CollectFbxMeshNodes(Node->GetChild(ChildIndex), OutMeshNodes);
	}
}

// Polygon Index를 넣으면 사용할 MaterialIndex를 반환
static int32 GetPolygonMaterialIndex(FbxMesh* Mesh, int32 PolygonIndex)
{
	if (!Mesh)
	{
		return 0;
	}

	FbxLayerElementMaterial* MaterialElement = Mesh->GetElementMaterial();
	if (!MaterialElement)
	{
		return 0;
	}

	if (MaterialElement->GetMappingMode() != FbxLayerElement::eByPolygon)
	{
		return 0;
	}

	if (MaterialElement->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
	{
		return MaterialElement->GetIndexArray().GetAt(PolygonIndex);
	}

	if (MaterialElement->GetReferenceMode() == FbxLayerElement::eDirect)
	{
		return PolygonIndex;
	}

	return 0;
}

// MaterialIndex의 실제 Material Name 가져오기
static FString GetFbxMaterialSlotName(FbxSurfaceMaterial* Material, int32 MaterialIndex)
{
	if (Material)
	{
		const char* MaterialName = Material->GetName();

		if (MaterialName && MaterialName[0] != '\0')
		{
			return FString(MaterialName);
		}
	}

	return "FBX_Material_" + std::to_string(MaterialIndex);
}


static FString GetFbxMaterialTextureFilePath(FbxSurfaceMaterial* Material, const char* PropertyName)
{
	if (!Material || !PropertyName)
	{
		return "";
	}

	FbxProperty Property = Material->FindProperty(PropertyName);
	if (!Property.IsValid())
	{
		return "";
	}

	const int32 TextureCount = static_cast<int32>(Property.GetSrcObjectCount<FbxTexture>());

	for (int32 TextureIndex = 0; TextureIndex < TextureCount; ++TextureIndex)
	{
		FbxTexture* Texture = Property.GetSrcObject<FbxTexture>(TextureIndex);
		FbxFileTexture* FileTexture = FbxCast<FbxFileTexture>(Texture);

		if (!FileTexture)
		{
			continue;
		}

		const char* FileName = FileTexture->GetFileName();

		if (FileName && FileName[0] != '\0')
		{
			return FString(FileName);
		}

		const char* RelativeFileName = FileTexture->GetRelativeFileName();

		if (RelativeFileName && RelativeFileName[0] != '\0')
		{
			return FString(RelativeFileName);
		}
	}

	return "";
}

static FVector GetFbxMaterialDiffuseColor(FbxSurfaceMaterial* Material)
{
	if (!Material)
	{
		return FVector(1.0f, 1.0f, 1.0f);
	}

	FbxProperty Property = Material->FindProperty(FbxSurfaceMaterial::sDiffuse);
	if (!Property.IsValid())
	{
		return FVector(1.0f, 1.0f, 1.0f);
	}

	auto Color = Property.Get<FbxDouble3>();

	return FVector(
		static_cast<float>(Color[0]),
		static_cast<float>(Color[1]),
		static_cast<float>(Color[2])
	);
}

static bool IsSameFbxMaterialInfo(const FFbxMaterialInfo& A, const FFbxMaterialInfo& B)
{
	return A.MaterialSlotName == B.MaterialSlotName
		&& A.DiffuseTexturePath == B.DiffuseTexturePath
		&& A.DiffuseColor.X == B.DiffuseColor.X
		&& A.DiffuseColor.Y == B.DiffuseColor.Y
		&& A.DiffuseColor.Z == B.DiffuseColor.Z;
}

static int32 FindOrAddFbxMaterialInfo(
	TArray<FFbxMaterialInfo>& MaterialInfos,
	const FFbxMaterialInfo& NewInfo
)
{
	for (int32 Index = 0; Index < static_cast<int32>(MaterialInfos.size()); ++Index)
	{
		if (IsSameFbxMaterialInfo(MaterialInfos[Index], NewInfo))
		{
			return Index;
		}
	}

	MaterialInfos.push_back(NewInfo);
	return static_cast<int32>(MaterialInfos.size()) - 1;
}

static TArray<int32> BuildMaterialRemapForMeshNode(
	FbxNode* MeshNode,
	TArray<FFbxMaterialInfo>& GlobalMaterialInfos
)
{
	TArray<int32> LocalToGlobalMaterialIndices;

	if (!MeshNode)
	{
		return LocalToGlobalMaterialIndices;
	}

	const int32 MaterialCount = static_cast<int32>(MeshNode->GetMaterialCount());

	if (MaterialCount <= 0)
	{
		FFbxMaterialInfo DefaultMaterialInfo;
		DefaultMaterialInfo.MaterialSlotName = "None";

		const int32 GlobalMaterialIndex =
			FindOrAddFbxMaterialInfo(GlobalMaterialInfos, DefaultMaterialInfo);

		LocalToGlobalMaterialIndices.push_back(GlobalMaterialIndex);
		return LocalToGlobalMaterialIndices;
	}

	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		FbxSurfaceMaterial* FbxMaterial = MeshNode->GetMaterial(MaterialIndex);

		FFbxMaterialInfo MaterialInfo;
		MaterialInfo.MaterialSlotName = GetFbxMaterialSlotName(FbxMaterial, MaterialIndex);
		MaterialInfo.DiffuseTexturePath = GetFbxMaterialTextureFilePath(FbxMaterial, FbxSurfaceMaterial::sDiffuse);
		MaterialInfo.DiffuseColor = GetFbxMaterialDiffuseColor(FbxMaterial);

		const int32 GlobalMaterialIndex =
			FindOrAddFbxMaterialInfo(GlobalMaterialInfos, MaterialInfo);

		LocalToGlobalMaterialIndices.push_back(GlobalMaterialIndex);
	}

	return LocalToGlobalMaterialIndices;
}


static FString SanitizeAssetFileName(const FString& Name)
{
	FString Result = Name.empty() ? "None" : Name;

	for (char& Ch : Result)
	{
		const bool bInvalid =
			Ch == '<' || Ch == '>' || Ch == ':' || Ch == '"' ||
			Ch == '/' || Ch == '\\' || Ch == '|' || Ch == '?' || Ch == '*';

		if (bInvalid)
		{
			Ch = '_';
		}
	}

	return Result;
}

static FString ConvertFbxMaterialInfoToMat(const FFbxMaterialInfo& Info)
{
	const FString SafeName = SanitizeAssetFileName(Info.MaterialSlotName);
	const FString MatPath = "Asset/Materials/Auto/" + SafeName + ".mat";

	std::wstring MatDiskPath;
	FString Error;

	if (!FPaths::TryResolvePackagePath(MatPath, MatDiskPath, &Error))
	{
		UE_LOG("[FBX] Failed to resolve material path: %s", Error.c_str());
		return "";
	}

	std::filesystem::create_directories(std::filesystem::path(MatDiskPath).parent_path());

	json::JSON JsonData;
	JsonData["PathFileName"] = MatPath;
	JsonData["Origin"] = "FbxImport";
	JsonData["ShaderPath"] = "Shaders/Geometry/UberLit.hlsl";
	JsonData["RenderPass"] = "Opaque";

	if (!Info.DiffuseTexturePath.empty())
	{
		JsonData["Textures"]["DiffuseTexture"] = Info.DiffuseTexturePath;

		JsonData["Parameters"]["SectionColor"][0] = 1.0f;
		JsonData["Parameters"]["SectionColor"][1] = 1.0f;
		JsonData["Parameters"]["SectionColor"][2] = 1.0f;
		JsonData["Parameters"]["SectionColor"][3] = 1.0f;
	}
	else
	{
		JsonData["Parameters"]["SectionColor"][0] = Info.DiffuseColor.X;
		JsonData["Parameters"]["SectionColor"][1] = Info.DiffuseColor.Y;
		JsonData["Parameters"]["SectionColor"][2] = Info.DiffuseColor.Z;
		JsonData["Parameters"]["SectionColor"][3] = 1.0f;
	}

	JsonData["Parameters"]["HasNormalMap"] = 0.0f;
	JsonData["Parameters"]["_pad"][0] = 0.0f;
	JsonData["Parameters"]["_pad"][1] = 0.0f;
	JsonData["Parameters"]["_pad"][2] = 0.0f;

#if !IS_GAME_CLIENT
	std::ofstream File(std::filesystem::path(MatDiskPath), std::ios::binary);
	if (!File.is_open())
	{
		UE_LOG("[FBX] Failed to write material file: %s", MatPath.c_str());
		return "";
	}

	File << JsonData.dump();
#endif

	return MatPath;
}

static void BuildStaticMaterialsFromFbxInfos(const TArray<FFbxMaterialInfo>& MaterialInfos,
                                             TArray<FStaticMaterial>& OutMaterials)
{
	OutMaterials.clear();

	for (const FFbxMaterialInfo& MaterialInfo : MaterialInfos)
	{
		const FString MatPath = ConvertFbxMaterialInfoToMat(MaterialInfo);

		FStaticMaterial StaticMaterial;
		StaticMaterial.MaterialInterface = nullptr;
		StaticMaterial.MaterialSlotName = MaterialInfo.MaterialSlotName;

		if (!MatPath.empty())
		{
			StaticMaterial.MaterialInterface =
				FMaterialManager::Get().GetOrCreateMaterial(MatPath);
		}

		OutMaterials.push_back(StaticMaterial);

		UE_LOG("[FBX] StaticMaterial: Slot=%s, Mat=%s",
		       StaticMaterial.MaterialSlotName.c_str(),
		       MatPath.empty() ? "None" : MatPath.c_str());
	}
}

static void BuildTangents(FStaticMesh& Mesh)
{
	TArray<FVector> TangentSums(Mesh.Vertices.size(), FVector(0.0f, 0.0f, 0.0f));
	TArray<FVector> BitangentSums(Mesh.Vertices.size(), FVector(0.0f, 0.0f, 0.0f));

	for (size_t Index = 0; Index + 2 < Mesh.Indices.size(); Index += 3)
	{
		const uint32 I0 = Mesh.Indices[Index + 0];
		const uint32 I1 = Mesh.Indices[Index + 1];
		const uint32 I2 = Mesh.Indices[Index + 2];

		if (I0 >= Mesh.Vertices.size() || I1 >= Mesh.Vertices.size() || I2 >= Mesh.Vertices.size())
		{
			continue;
		}

		const FNormalVertex& V0 = Mesh.Vertices[I0];
		const FNormalVertex& V1 = Mesh.Vertices[I1];
		const FNormalVertex& V2 = Mesh.Vertices[I2];

		const FVector Edge1 = V1.pos - V0.pos;
		const FVector Edge2 = V2.pos - V0.pos;

		const FVector2 DeltaUV1 = V1.tex - V0.tex;
		const FVector2 DeltaUV2 = V2.tex - V0.tex;

		const float Det = DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X;

		if (std::abs(Det) < 1e-8f)
		{
			continue;
		}

		const float InvDet = 1.0f / Det;

		const FVector Tangent =
			(Edge1 * DeltaUV2.Y - Edge2 * DeltaUV1.Y) * InvDet;

		const FVector Bitangent =
			(Edge2 * DeltaUV1.X - Edge1 * DeltaUV2.X) * InvDet;

		TangentSums[I0] += Tangent;
		TangentSums[I1] += Tangent;
		TangentSums[I2] += Tangent;

		BitangentSums[I0] += Bitangent;
		BitangentSums[I1] += Bitangent;
		BitangentSums[I2] += Bitangent;
	}

	for (size_t VertexIndex = 0; VertexIndex < Mesh.Vertices.size(); ++VertexIndex)
	{
		FNormalVertex& Vertex = Mesh.Vertices[VertexIndex];

		FVector Normal = Vertex.normal.Normalized();
		FVector Tangent = TangentSums[VertexIndex];

		Tangent = Tangent - Normal * Normal.Dot(Tangent);

		if (Tangent.Length() < 1e-8f)
		{
			const FVector FallbackAxis =
				std::abs(Normal.Z) < 0.999f
					? FVector(0.0f, 0.0f, 1.0f)
					: FVector(0.0f, 1.0f, 0.0f);

			Tangent = FallbackAxis.Cross(Normal).Normalized();
		}
		else
		{
			Tangent.Normalize();
		}

		const FVector Bitangent = BitangentSums[VertexIndex];
		const float Handedness = Normal.Cross(Tangent).Dot(Bitangent) < 0.0f ? -1.0f : 1.0f;

		Vertex.tangent = FVector4(Tangent, Handedness);
	}
}

static void AppendFbxMeshNodeToStaticMesh(
	FbxNode* MeshNode,
	FStaticMesh& OutMesh,
	const TArray<FStaticMaterial>& OutMaterials,
	const TArray<int32>& LocalToGlobalMaterialIndices
)
{
	if (!MeshNode || !MeshNode->GetMesh())
	{
		return;
	}

	FbxMesh* Mesh = MeshNode->GetMesh();

	const int32 ControlPointCount = static_cast<int32>(Mesh->GetControlPointsCount());
	const int32 PolygonCount = static_cast<int32>(Mesh->GetPolygonCount());

	FbxStringList UVSetNames;
	Mesh->GetUVSetNames(UVSetNames);

	const char* UVSetName = nullptr;

	if (UVSetNames.GetCount() > 0)
	{
		UVSetName = UVSetNames[0];
		UE_LOG("[FBX] Mesh=%s, Using UV Set: %s", MeshNode->GetName(), UVSetName);
	}
	else
	{
		UE_LOG("[FBX] Mesh=%s has no UV set.", MeshNode->GetName());
	}

	const FbxAMatrix NodeGlobalTransform = MeshNode->EvaluateGlobalTransform();

	int32 CurrentGlobalMaterialIndex = -1;
	FStaticMeshSection* CurrentSection = nullptr;

	FbxVector4* ControlPoints = Mesh->GetControlPoints();
	for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
	{
		const int32 PolygonSize = static_cast<int32>(Mesh->GetPolygonSize(PolygonIndex));

		if (PolygonSize != 3)
		{
			UE_LOG("[FBX] Skip non-triangle polygon. Mesh=%s, Polygon=%d, Size=%d",
			       MeshNode->GetName(),
			       PolygonIndex,
			       PolygonSize);
			continue;
		}

		const int32 LocalMaterialIndex = GetPolygonMaterialIndex(Mesh, PolygonIndex);

		int32 GlobalMaterialIndex = 0;
		if (LocalMaterialIndex >= 0 &&
			LocalMaterialIndex < static_cast<int32>(LocalToGlobalMaterialIndices.size()))
		{
			GlobalMaterialIndex = LocalToGlobalMaterialIndices[LocalMaterialIndex];
		}

		if (GlobalMaterialIndex < 0 || GlobalMaterialIndex >= static_cast<int32>(OutMaterials.size()))
		{
			GlobalMaterialIndex = 0;
		}

		if (GlobalMaterialIndex != CurrentGlobalMaterialIndex)
		{
			if (CurrentSection)
			{
				CurrentSection->NumTriangles =
					(static_cast<uint32>(OutMesh.Indices.size()) - CurrentSection->FirstIndex) / 3;
			}

			FStaticMeshSection NewSection;
			NewSection.MaterialIndex = GlobalMaterialIndex;
			NewSection.MaterialSlotName = OutMaterials[GlobalMaterialIndex].MaterialSlotName;
			NewSection.FirstIndex = static_cast<uint32>(OutMesh.Indices.size());
			NewSection.NumTriangles = 0;

			OutMesh.Sections.push_back(NewSection);

			CurrentSection = &OutMesh.Sections.back();
			CurrentGlobalMaterialIndex = GlobalMaterialIndex;
		}

		for (int32 VertexIndex = 0; VertexIndex < PolygonSize; ++VertexIndex)
		{
			const int32 ControlPointIndex =
				static_cast<int32>(Mesh->GetPolygonVertex(PolygonIndex, VertexIndex));

			if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
			{
				UE_LOG("[FBX] Invalid ControlPointIndex. Mesh=%s, Polygon=%d, Vertex=%d, ControlPoint=%d",
				       MeshNode->GetName(),
				       PolygonIndex,
				       VertexIndex,
				       ControlPointIndex);
				continue;
			}

			const FbxVector4& FbxPosition = ControlPoints[ControlPointIndex];
			const FVector EnginePosition = TransformFbxPosition(NodeGlobalTransform, FbxPosition);

			FbxVector4 FbxNormal;
			const bool bHasNormal = Mesh->GetPolygonVertexNormal(PolygonIndex, VertexIndex, FbxNormal);

			FVector EngineNormal = FVector(0.0f, 0.0f, 1.0f);
			if (bHasNormal)
			{
				EngineNormal = TransformFbxNormal(NodeGlobalTransform, FbxNormal);
			}

			FVector2 EngineUV(0.0f, 0.0f);
			if (UVSetName)
			{
				FbxVector2 FbxUV;
				bool bUnmapped = false;

				const bool bHasUV = Mesh->GetPolygonVertexUV(
					PolygonIndex,
					VertexIndex,
					UVSetName,
					FbxUV,
					bUnmapped
				);

				if (bHasUV && !bUnmapped)
				{
					EngineUV = FVector2(
						static_cast<float>(FbxUV[0]),
						1.0f - static_cast<float>(FbxUV[1])
					);
				}
			}

			FNormalVertex NewVertex;
			NewVertex.pos = EnginePosition;
			NewVertex.normal = EngineNormal;
			NewVertex.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			NewVertex.tex = EngineUV;
			NewVertex.tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);

			const uint32 NewVertexIndex = static_cast<uint32>(OutMesh.Vertices.size());
			OutMesh.Vertices.push_back(NewVertex);
			OutMesh.Indices.push_back(NewVertexIndex);
		}
	}

	if (CurrentSection)
	{
		CurrentSection->NumTriangles =
			(static_cast<uint32>(OutMesh.Indices.size()) - CurrentSection->FirstIndex) / 3;
	}

	UE_LOG("[FBX] Appended Mesh=%s, Polygons=%d", MeshNode->GetName(), PolygonCount);
}

bool FFbxImporter::CanLoadScene(const FString& FbxFilePath)
{
#if !WITH_FBX_SDK
	UE_LOG("FBX SDK is not configured. File: %s", FbxFilePath.c_str());
	return false;
#else
	FFbxSceneContext Context;

	if (!LoadFbxScene(FbxFilePath, Context))
	{
		return false;
	}

	FbxNode* RootNode = Context.Scene->GetRootNode();
	const int32 RootChildCount = RootNode ? static_cast<int32>(RootNode->GetChildCount()) : 0;
	
	UE_LOG("FBX scene loaded. File: %s. Root children: %d", FbxFilePath.c_str(), RootChildCount);
	
	if (RootNode)
	{
		TraverseFbxNode(RootNode, 0);
	}

	DestroyFbxSceneContext(Context);
	
	return true;
#endif
}

bool FFbxImporter::ImportStaticMesh(const FString& FbxFilePath, FStaticMesh& OutMesh,
                                    TArray<FStaticMaterial>& OutMaterials)
{
#if !WITH_FBX_SDK
	UE_LOG("FBX SDK is not configured. File: %s", FbxFilePath.c_str());
	return false;
#else
	OutMesh = FStaticMesh();
	OutMaterials.clear();
	OutMesh.PathFileName = FbxFilePath;

	FFbxSceneContext Context;

	if (!LoadFbxScene(FbxFilePath, Context))
	{
		return false;
	}

	UE_LOG("[FBX] ImportStaticMesh scene loaded. File: %s", FbxFilePath.c_str());

	FbxNode* RootNode = Context.Scene->GetRootNode();
	TArray<FbxNode*> MeshNodes;
	CollectFbxMeshNodes(RootNode, MeshNodes);

	if (MeshNodes.empty())
	{
		UE_LOG("[FBX] ImportStaticMesh failed: mesh node not found. File: %s", FbxFilePath.c_str());
		DestroyFbxSceneContext(Context);
		return false;
	}

	UE_LOG("[FBX] ImportStaticMesh found mesh nodes: %zu", MeshNodes.size());

	TArray<FFbxMaterialInfo> GlobalMaterialInfos;
	TArray<TArray<int32>> MeshMaterialRemaps;
	MeshMaterialRemaps.reserve(MeshNodes.size());

	for (FbxNode* MeshNode : MeshNodes)
	{
		MeshMaterialRemaps.push_back(
			BuildMaterialRemapForMeshNode(MeshNode, GlobalMaterialInfos)
		);
	}

	BuildStaticMaterialsFromFbxInfos(GlobalMaterialInfos, OutMaterials);

	for (size_t MeshNodeIndex = 0; MeshNodeIndex < MeshNodes.size(); ++MeshNodeIndex)
	{
		AppendFbxMeshNodeToStaticMesh(
			MeshNodes[MeshNodeIndex],
			OutMesh,
			OutMaterials,
			MeshMaterialRemaps[MeshNodeIndex]
		);
	}

	BuildTangents(OutMesh);
	OutMesh.CacheBounds();

	UE_LOG("[FBX] StaticMesh built. Vertices=%zu, Indices=%zu, Sections=%zu, Materials=%zu",
	       OutMesh.Vertices.size(),
	       OutMesh.Indices.size(),
	       OutMesh.Sections.size(),
	       OutMaterials.size());

	for (int32 SectionIndex = 0; SectionIndex < static_cast<int32>(OutMesh.Sections.size()); ++SectionIndex)
	{
		const FStaticMeshSection& Section = OutMesh.Sections[SectionIndex];

		UE_LOG("[FBX] Section[%d]: FirstIndex=%u, NumTriangles=%u, MaterialIndex=%d, Slot=%s",
		       SectionIndex,
		       Section.FirstIndex,
		       Section.NumTriangles,
		       Section.MaterialIndex,
		       Section.MaterialSlotName.c_str());
	}

	const bool bHasMeshData =
		!OutMesh.Vertices.empty() && !OutMesh.Indices.empty();

	UE_LOG("[FBX] ImportStaticMesh result detail: Vertices=%zu, Indices=%zu",
	       OutMesh.Vertices.size(),
	       OutMesh.Indices.size());

	DestroyFbxSceneContext(Context);

	return bHasMeshData;
#endif
}

// --- Skeletal Mesh ---
// 이 노드가 Bone인지 검사하면서 Skeleton Node만 모은다.
static void CollectFbxSkeletonNodes(FbxNode* Node, TArray<FbxNode*>& OutSkeletonNodes)
{
	if (!Node)
	{
		return;
	}

	FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
	// Attribute가 없는 노드도 있기 때문에 Attribute 검사해주어야 한다.
	if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
	{
		OutSkeletonNodes.push_back(Node);
	}

	const int32 ChildCount = static_cast<int32>(Node->GetChildCount());
	for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		CollectFbxSkeletonNodes(Node->GetChild(ChildIndex), OutSkeletonNodes);
	}
}

// Skeleton 배열에서 Index 찾기.
// 흠....이 친구는....음....TMap이 더 어울릴 거 같은데
static int32 FindSkeletonNodeIndex(const TArray<FbxNode*>& SkeletonNodes, FbxNode* TargetNode)
{
	for (int32 Index = 0; Index < static_cast<int32>(SkeletonNodes.size()); ++Index)
	{
		if (SkeletonNodes[Index] == TargetNode)
		{
			return Index;
		}
	}
	return -1;
}

// 현재 BoneNode의 부모 Bone이 skeleton 배열에서 몇 번째 index인지 찾기.
static int32 FindParentBoneIndex(FbxNode* BoneNode, const TMap<FbxNode*, int32>& NodeToBoneIndex)
{
	if (!BoneNode)
	{
		return -1;
	}

	FbxNode* ParentNode = BoneNode->GetParent();

	// FBX 노드 트리에서 바로 위 부모가 항상 Bone이라는 보장이 없기 때문에 나->부모->...->(Bone)부모 올라감으로써 부모 Bone 찾기.
	while (ParentNode)
	{
		auto Found = NodeToBoneIndex.find(ParentNode);
		if (Found != NodeToBoneIndex.end())
		{
			return Found->second;
		}
		ParentNode = ParentNode->GetParent();
	}
	return -1;
}

static TMap<FbxNode*, int32> BuildBoneNodeIndexMap(const TArray<FbxNode*>& SkeletonNodes)
{
	TMap<FbxNode*, int32> NodeToBoneIndex;

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(SkeletonNodes.size()); ++BoneIndex)
	{
		FbxNode* BoneNode = SkeletonNodes[BoneIndex];

		if (!BoneNode)
		{
			continue;
		}

		NodeToBoneIndex[BoneNode] = BoneIndex;
	}

	return NodeToBoneIndex;
}

static int32 FindNearestParentBoneIndex(FbxNode* Node, const TMap<FbxNode*, int32>& NodeToBoneIndex)
{
	if (!Node)
	{
		return -1;
	}

	FbxNode* ParentNode = Node->GetParent();
	while (ParentNode)
	{
		auto Found = NodeToBoneIndex.find(ParentNode);
		if (Found != NodeToBoneIndex.end())
		{
			return Found->second;
		}

		ParentNode = ParentNode->GetParent();
	}

	return -1;
}

static void AddBoneInfluenceToControlPoint(TArray<TArray<FFbxBoneInfluence>>& ControlPointInfluences,
                                           int32 ControlPointIndex, int32 BoneIndex, float Weight)
{
	if (ControlPointIndex < 0 || ControlPointIndex >= static_cast<int32>(ControlPointInfluences.size()))
	{
		return;
	}

	if (BoneIndex < 0 || Weight <= 0.0f)
	{
		return;
	}

	FFbxBoneInfluence Influence;
	Influence.BoneIndex = BoneIndex;
	Influence.Weight = Weight;

	ControlPointInfluences[ControlPointIndex].push_back(Influence);
}

static int32 AddFallbackInfluenceToMissingControlPoints(
	TArray<TArray<FFbxBoneInfluence>>& ControlPointInfluences,
	int32 FallbackBoneIndex
)
{
	if (FallbackBoneIndex < 0)
	{
		return 0;
	}

	int32 AddedCount = 0;

	for (int32 ControlPointIndex = 0;
	     ControlPointIndex < static_cast<int32>(ControlPointInfluences.size());
	     ++ControlPointIndex)
	{
		if (!ControlPointInfluences[ControlPointIndex].empty())
		{
			continue;
		}

		AddBoneInfluenceToControlPoint(
			ControlPointInfluences,
			ControlPointIndex,
			FallbackBoneIndex,
			1.0f
		);

		++AddedCount;
	}

	return AddedCount;
}

static void BuildControlPointInfluences(FbxMesh* Mesh, const TMap<FbxNode*, int32>& NodeToBoneIndex,
                                        TArray<TArray<FFbxBoneInfluence>>& OutControlPointInfluences)
{
	OutControlPointInfluences.clear();
	if (!Mesh)
	{
		return;
	}

	const int32 ControlPointCount = static_cast<int32>(Mesh->GetControlPointsCount());

	OutControlPointInfluences.resize(ControlPointCount);

	const int32 DeformerCount = static_cast<int32>(Mesh->GetDeformerCount(FbxDeformer::eSkin));
	for (int32 DeformerIndex = 0; DeformerIndex < DeformerCount; ++DeformerIndex)
	{
		auto Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(DeformerIndex, FbxDeformer::eSkin));
		if (!Skin)
		{
			continue;
		}

		const int32 ClusterCount = static_cast<int32>(Skin->GetClusterCount());
		for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
		{
			FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
			if (!Cluster)
			{
				continue;
			}
			FbxNode* LinkNode = Cluster->GetLink();
			auto FoundBoneIndex = NodeToBoneIndex.find(LinkNode);
			if (FoundBoneIndex == NodeToBoneIndex.end())
			{
				continue;
			}
			const int32 BoneIndex = FoundBoneIndex->second;

			const int32 InfluenceCount = static_cast<int32>(Cluster->GetControlPointIndicesCount());
			int* ControlPointIndices = Cluster->GetControlPointIndices();
			double* ControlPointWeights = Cluster->GetControlPointWeights();
			for (int32 InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
			{
				const int32 ControlPointIndex = static_cast<int32>(ControlPointIndices[InfluenceIndex]);
				const float Weight = static_cast<float>(ControlPointWeights[InfluenceIndex]);
				AddBoneInfluenceToControlPoint(OutControlPointInfluences, ControlPointIndex, BoneIndex, Weight);
			}
		}
	}
}

static void BuildBoneInfosFromSkeletonNodes(const TArray<FbxNode*>& SkeletonNodes, TArray<FBoneInfo>& OutBones)
{
	OutBones.clear();

	TMap<FbxNode*, int32> NodeToBoneIndex = BuildBoneNodeIndexMap(SkeletonNodes);

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(SkeletonNodes.size()); ++BoneIndex)
	{
		FbxNode* BoneNode = SkeletonNodes[BoneIndex];

		if (!BoneNode)
		{
			continue;
		}
		NodeToBoneIndex[BoneNode] = BoneIndex;
	}

	OutBones.resize(SkeletonNodes.size());

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(SkeletonNodes.size()); ++BoneIndex)
	{
		FbxNode* BoneNode = SkeletonNodes[BoneIndex];

		if (!BoneNode)
		{
			continue;
		}

		FBoneInfo& BoneInfo = OutBones[BoneIndex];

		const FbxAMatrix GlobalBindPose = BoneNode->EvaluateGlobalTransform();

		const char* BoneName = BoneNode->GetName();
		BoneInfo.Name = BoneName ? FString(BoneName) : "None";
		BoneInfo.ParentIndex = FindParentBoneIndex(BoneNode, NodeToBoneIndex);
		BoneInfo.GlobalBindPose = ToEngineMatrix(GlobalBindPose);
	}

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(OutBones.size()); ++BoneIndex)
	{
		FBoneInfo& BoneInfo = OutBones[BoneIndex];

		if (BoneInfo.ParentIndex >= 0 &&
			BoneInfo.ParentIndex < static_cast<int32>(OutBones.size()))
		{
			const FMatrix ParentGlobalInverse =
				OutBones[BoneInfo.ParentIndex].GlobalBindPose.GetInverse();

			BoneInfo.LocalBindPose =
				BoneInfo.GlobalBindPose * ParentGlobalInverse;
		}
		else
		{
			BoneInfo.LocalBindPose = BoneInfo.GlobalBindPose;
		}

		BoneInfo.InverseBindPose = BoneInfo.GlobalBindPose.GetInverse();
	}
}

// weight 선별 후 합 1 정규화
static void AssignTopBoneInfluencesToVertex(const TArray<FFbxBoneInfluence>& SourceInfluences,
                                            FSkeletalVertex& OutVertex)
{
	for (int32 InfluenceIndex = 0; InfluenceIndex < MaxBoneInfluences; ++InfluenceIndex)
	{
		OutVertex.BoneIndices[InfluenceIndex] = 0;
		OutVertex.BoneWeights[InfluenceIndex] = 0.0f;
	}

	TArray<FFbxBoneInfluence> SortedInfluences = SourceInfluences;

	std::sort(SortedInfluences.begin(), SortedInfluences.end(),
	          [](const FFbxBoneInfluence& A, const FFbxBoneInfluence& B)
	          {
		          return A.Weight > B.Weight;
	          });

	float WeightSum = 0.0f;
	const int32 CopyCount = static_cast<int32>(std::min<size_t>(MaxBoneInfluences, SortedInfluences.size()));

	for (int32 InfluenceIndex = 0; InfluenceIndex < CopyCount; ++InfluenceIndex)
	{
		OutVertex.BoneIndices[InfluenceIndex] = static_cast<uint32>(SortedInfluences[InfluenceIndex].BoneIndex);
		OutVertex.BoneWeights[InfluenceIndex] = SortedInfluences[InfluenceIndex].Weight;
		WeightSum += OutVertex.BoneWeights[InfluenceIndex];
	}

	if (WeightSum > 0.0001f)
	{
		for (int32 InfluenceIndex = 0; InfluenceIndex < CopyCount; ++InfluenceIndex)
		{
			OutVertex.BoneWeights[InfluenceIndex] /= WeightSum;
		}
	}
	else
	{
		OutVertex.BoneIndices[0] = 0;
		OutVertex.BoneWeights[0] = 1.0f;
	}
}

static void AppendFbxMeshNodeToSkeletalMesh(FbxNode* MeshNode, FSkeletalMesh& OutMesh,
                                            const TArray<FStaticMaterial>& OutMaterials,
                                            const TArray<int32>& LocalToGlobalMaterialIndices,
                                            const TArray<TArray<FFbxBoneInfluence>>& ControlPointInfluences)
{
	if (!MeshNode || !MeshNode->GetMesh())
	{
		return;
	}

	FbxMesh* Mesh = MeshNode->GetMesh();

	const int32 ControlPointCount =
		static_cast<int32>(Mesh->GetControlPointsCount());

	const int32 PolygonCount =
		static_cast<int32>(Mesh->GetPolygonCount());

	FbxStringList UVSetNames;
	Mesh->GetUVSetNames(UVSetNames);

	const char* UVSetName = nullptr;

	if (UVSetNames.GetCount() > 0)
	{
		UVSetName = UVSetNames[0];
		UE_LOG("[FBX] SkeletalMesh=%s, Using UV Set: %s", MeshNode->GetName(), UVSetName);
	}
	else
	{
		UE_LOG("[FBX] SkeletalMesh=%s has no UV set.", MeshNode->GetName());
	}

	const FbxAMatrix NodeGlobalTransform = MeshNode->EvaluateGlobalTransform();

	int32 CurrentGlobalMaterialIndex = -1;
	FStaticMeshSection* CurrentSection = nullptr;

	FbxVector4* ControlPoints = Mesh->GetControlPoints();

	for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
	{
		const int32 PolygonSize =
			static_cast<int32>(Mesh->GetPolygonSize(PolygonIndex));

		if (PolygonSize != 3)
		{
			UE_LOG(
				"[FBX] Skip non-triangle skeletal polygon. Mesh=%s, Polygon=%d, Size=%d",
				MeshNode->GetName(),
				PolygonIndex,
				PolygonSize
			);
			continue;
		}

		const int32 LocalMaterialIndex =
			GetPolygonMaterialIndex(Mesh, PolygonIndex);

		int32 GlobalMaterialIndex = 0;

		if (LocalMaterialIndex >= 0 &&
			LocalMaterialIndex < static_cast<int32>(LocalToGlobalMaterialIndices.size()))
		{
			GlobalMaterialIndex = LocalToGlobalMaterialIndices[LocalMaterialIndex];
		}

		if (GlobalMaterialIndex < 0 ||
			GlobalMaterialIndex >= static_cast<int32>(OutMaterials.size()))
		{
			GlobalMaterialIndex = 0;
		}

		if (GlobalMaterialIndex != CurrentGlobalMaterialIndex)
		{
			if (CurrentSection)
			{
				CurrentSection->NumTriangles =
					(static_cast<uint32>(OutMesh.Indices.size()) - CurrentSection->FirstIndex) / 3;
			}

			FStaticMeshSection NewSection;
			NewSection.MaterialIndex = GlobalMaterialIndex;
			NewSection.MaterialSlotName = OutMaterials[GlobalMaterialIndex].MaterialSlotName;
			NewSection.FirstIndex = static_cast<uint32>(OutMesh.Indices.size());
			NewSection.NumTriangles = 0;

			OutMesh.Sections.push_back(NewSection);

			CurrentSection = &OutMesh.Sections.back();
			CurrentGlobalMaterialIndex = GlobalMaterialIndex;
		}

		for (int32 VertexIndex = 0; VertexIndex < PolygonSize; ++VertexIndex)
		{
			const int32 ControlPointIndex =
				static_cast<int32>(Mesh->GetPolygonVertex(PolygonIndex, VertexIndex));

			if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
			{
				UE_LOG(
					"[FBX] Invalid skeletal ControlPointIndex. Mesh=%s, Polygon=%d, Vertex=%d, ControlPoint=%d",
					MeshNode->GetName(),
					PolygonIndex,
					VertexIndex,
					ControlPointIndex
				);
				continue;
			}

			const FbxVector4& FbxPosition = ControlPoints[ControlPointIndex];
			const FVector EnginePosition =
				TransformFbxPosition(NodeGlobalTransform, FbxPosition);

			FbxVector4 FbxNormal;
			const bool bHasNormal =
				Mesh->GetPolygonVertexNormal(PolygonIndex, VertexIndex, FbxNormal);

			auto EngineNormal = FVector(0.0f, 0.0f, 1.0f);

			if (bHasNormal)
			{
				EngineNormal = TransformFbxNormal(NodeGlobalTransform, FbxNormal);
			}

			FVector2 EngineUV(0.0f, 0.0f);

			if (UVSetName)
			{
				FbxVector2 FbxUV;
				bool bUnmapped = false;

				const bool bHasUV = Mesh->GetPolygonVertexUV(
					PolygonIndex,
					VertexIndex,
					UVSetName,
					FbxUV,
					bUnmapped
				);

				if (bHasUV && !bUnmapped)
				{
					EngineUV = FVector2(
						static_cast<float>(FbxUV[0]),
						1.0f - static_cast<float>(FbxUV[1])
					);
				}
			}

			FSkeletalVertex NewVertex;
			NewVertex.pos = EnginePosition;
			NewVertex.normal = EngineNormal;
			NewVertex.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			NewVertex.tex = EngineUV;
			NewVertex.tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);

			if (ControlPointIndex < static_cast<int32>(ControlPointInfluences.size()))
			{
				AssignTopBoneInfluencesToVertex(
					ControlPointInfluences[ControlPointIndex],
					NewVertex
				);
			}
			else
			{
				TArray<FFbxBoneInfluence> EmptyInfluences;
				AssignTopBoneInfluencesToVertex(EmptyInfluences, NewVertex);
			}

			const uint32 NewVertexIndex =
				static_cast<uint32>(OutMesh.Vertices.size());

			OutMesh.Vertices.push_back(NewVertex);
			OutMesh.Indices.push_back(NewVertexIndex);
		}
	}

	if (CurrentSection)
	{
		CurrentSection->NumTriangles =
			(static_cast<uint32>(OutMesh.Indices.size()) - CurrentSection->FirstIndex) / 3;
	}

	UE_LOG(
		"[FBX] Appended SkeletalMesh=%s, Vertices=%zu, Indices=%zu, Sections=%zu",
		MeshNode->GetName(),
		OutMesh.Vertices.size(),
		OutMesh.Indices.size(),
		OutMesh.Sections.size()
	);
}

bool FFbxImporter::ImportSkeletalMesh(const FString& FbxFilePath, FSkeletalMesh& OutMesh,
                                      TArray<FStaticMaterial>& OutMaterials)
{
#if !WITH_FBX_SDK
	UE_LOG("FBX SDK is not configured. File: %s", FbxFilePath.c_str());
	return false;
#else
	OutMesh = FSkeletalMesh();
	OutMaterials.clear();
	OutMesh.PathFileName = FbxFilePath;

	FFbxSceneContext Context;

	if (!LoadFbxScene(FbxFilePath, Context))
	{
		return false;
	}

	FbxNode* RootNode = Context.Scene->GetRootNode();
	TArray<FbxNode*> SkeletonNodes;
	// Skeleton Nodes 수집
	CollectFbxSkeletonNodes(RootNode, SkeletonNodes);
	// UE_LOG("[FBX] ImportSkeletalMesh found skeleton nodes: %zu", SkeletonNodes.size());
	if (SkeletonNodes.empty())
	{
		UE_LOG("[FBX] ImportSkeletalMesh failed: skeleton node not found. File: %s", FbxFilePath.c_str());
		DestroyFbxSceneContext(Context);
		return false;
	}

	BuildBoneInfosFromSkeletonNodes(SkeletonNodes, OutMesh.Bones);
	// UE_LOG("[FBX] ImportSkeletalMesh built bones: %zu", OutMesh.Bones.size());
	// for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(OutMesh.Bones.size()); ++BoneIndex)
	// {
	// 	const FBoneInfo& Bone = OutMesh.Bones[BoneIndex];
	//
	// 	UE_LOG(
	// 		"[FBX] Bone[%d]: Name=%s, ParentIndex=%d",
	// 		BoneIndex,
	// 		Bone.Name.c_str(),
	// 		Bone.ParentIndex
	// 	);
	// }

	TArray<FbxNode*> MeshNodes;
	CollectFbxMeshNodes(RootNode, MeshNodes);
	if (MeshNodes.empty())
	{
		UE_LOG("[FBX] ImportSkeletalMesh failed: mesh node not found. File: %s", FbxFilePath.c_str());
		DestroyFbxSceneContext(Context);
		return false;
	}

	UE_LOG("[FBX] ImportSkeletalMesh found mesh nodes: %zu", MeshNodes.size());

	TArray<FFbxMaterialInfo> GlobalMaterialInfos;
	TArray<TArray<int32>> MeshMaterialRemaps;
	MeshMaterialRemaps.reserve(MeshNodes.size());

	for (FbxNode* MeshNode : MeshNodes)
	{
		MeshMaterialRemaps.push_back(
			BuildMaterialRemapForMeshNode(MeshNode, GlobalMaterialInfos)
		);
	}

	BuildStaticMaterialsFromFbxInfos(GlobalMaterialInfos, OutMaterials);

	TMap<FbxNode*, int32> NodeToBoneIndex = BuildBoneNodeIndexMap(SkeletonNodes);

	for (size_t MeshNodeIndex = 0; MeshNodeIndex < MeshNodes.size(); ++MeshNodeIndex)
	{
		FbxNode* MeshNode = MeshNodes[MeshNodeIndex];
		FbxMesh* Mesh = MeshNode ? MeshNode->GetMesh() : nullptr;

		if (!Mesh)
		{
			continue;
		}

		TArray<TArray<FFbxBoneInfluence>> ControlPointInfluences;
		BuildControlPointInfluences(Mesh, NodeToBoneIndex, ControlPointInfluences);

		const int32 FallbackBoneIndex =
			FindNearestParentBoneIndex(MeshNode, NodeToBoneIndex);

		const int32 FallbackInfluenceCount =
			AddFallbackInfluenceToMissingControlPoints(
				ControlPointInfluences,
				FallbackBoneIndex >= 0 ? FallbackBoneIndex : 0
			);

		UE_LOG(
			"[FBX] ImportSkeletalMesh node[%zu]=%s, control point influences=%zu, fallback bone=%d, fallback control points=%d",
			MeshNodeIndex,
			MeshNode->GetName(),
			ControlPointInfluences.size(),
			FallbackBoneIndex >= 0 ? FallbackBoneIndex : 0,
			FallbackInfluenceCount
		);

		AppendFbxMeshNodeToSkeletalMesh(
			MeshNode,
			OutMesh,
			OutMaterials,
			MeshMaterialRemaps[MeshNodeIndex],
			ControlPointInfluences
		);
	}

	const bool bHasMeshData =
		!OutMesh.Vertices.empty() &&
		!OutMesh.Indices.empty() &&
		!OutMesh.Bones.empty();

	UE_LOG(
		"[FBX] ImportSkeletalMesh result detail: Vertices=%zu, Indices=%zu, Sections=%zu, Bones=%zu",
		OutMesh.Vertices.size(),
		OutMesh.Indices.size(),
		OutMesh.Sections.size(),
		OutMesh.Bones.size()
	);

	DestroyFbxSceneContext(Context);

	return bHasMeshData;
#endif
}
