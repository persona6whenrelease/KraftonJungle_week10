#include "FbxImporter.h"

#include "Core/Log.h"
#include "Engine/Platform/Paths.h"
#include "Math/Vector.h"
#include "Mesh/StaticMeshAsset.h"

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

static FbxNode* FindFirstMeshNode(FbxNode* Node)
{
	if (!Node)
	{
		return nullptr;
	}

	FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
	if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh && Node->GetMesh())
	{
		return Node;
	}

	const int32 ChildCount = static_cast<int32>(Node->GetChildCount());
	for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		if (FbxNode* FoundNode = FindFirstMeshNode(Node->GetChild(ChildIndex)))
		{
			return FoundNode;
		}
	}
	return nullptr;
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

static void ExtractFbxStaticMaterials(FbxNode* MeshNode, TArray<FStaticMaterial>& OutMaterials)
{
	OutMaterials.clear();

	if (!MeshNode)
	{
		return;
	}

	const int32 MaterialCount = static_cast<int32>(MeshNode->GetMaterialCount());

	if (MaterialCount <= 0)
	{
		FStaticMaterial DefaultMaterial;
		DefaultMaterial.MaterialInterface = nullptr;
		DefaultMaterial.MaterialSlotName = "None";

		OutMaterials.push_back(DefaultMaterial);
		return;
	}

	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		FbxSurfaceMaterial* FbxMaterial = MeshNode->GetMaterial(MaterialIndex);

		const FString DiffuseTexturePath = GetFbxMaterialTextureFilePath(FbxMaterial, FbxSurfaceMaterial::sDiffuse);

		FStaticMaterial StaticMaterial;
		StaticMaterial.MaterialInterface = nullptr;
		StaticMaterial.MaterialSlotName = GetFbxMaterialSlotName(FbxMaterial, MaterialIndex);

		OutMaterials.push_back(StaticMaterial);

		UE_LOG("[FBX] Material[%d]: %s, DiffuseTexture=%s",
		       MaterialIndex,
		       StaticMaterial.MaterialSlotName.c_str(),
		       DiffuseTexturePath.empty() ? "None" : DiffuseTexturePath.c_str());
	}
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

static void ExtractFbxMaterialInfos(FbxNode* MeshNode, TArray<FFbxMaterialInfo>& OutMaterialInfo)
{
	OutMaterialInfo.clear();

	if (!MeshNode)
	{
		return;
	}

	const int32 MaterialCount = static_cast<int32>(MeshNode->GetMaterialCount());

	if (MaterialCount <= 0)
	{
		FFbxMaterialInfo DefaultMaterialInfo;
		DefaultMaterialInfo.MaterialSlotName = "None";
		OutMaterialInfo.push_back(DefaultMaterialInfo);
		return;
	}

	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		FbxSurfaceMaterial* FbxMaterial = MeshNode->GetMaterial(MaterialIndex);

		FFbxMaterialInfo MaterialInfo;
		MaterialInfo.MaterialSlotName = GetFbxMaterialSlotName(FbxMaterial, MaterialIndex);
		MaterialInfo.DiffuseTexturePath = GetFbxMaterialTextureFilePath(FbxMaterial, FbxSurfaceMaterial::sDiffuse);
		MaterialInfo.DiffuseColor = GetFbxMaterialDiffuseColor(FbxMaterial);

		OutMaterialInfo.push_back(MaterialInfo);

		UE_LOG("[FBX] MaterialInfo[%d]: Slot=%s, Texture=%s, Color=(%.3f, %.3f, %.3f)",
		       MaterialIndex,
		       MaterialInfo.MaterialSlotName.c_str(),
		       MaterialInfo.DiffuseTexturePath.empty() ? "None" : MaterialInfo.DiffuseTexturePath.c_str(),
		       MaterialInfo.DiffuseColor.X,
		       MaterialInfo.DiffuseColor.Y,
		       MaterialInfo.DiffuseColor.Z);
	}
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

	// // Z-up
	// FbxAxisSystem EngineAxisSystem(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityOdd, FbxAxisSystem::eRightHanded);
	// EngineAxisSystem.ConvertScene(Scene);
	// // 1 unit = 1 cm
	// FbxSystemUnit::cm.ConvertScene(Scene);

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
	FbxNode* MeshNode = FindFirstMeshNode(RootNode);
	if (!MeshNode)
	{
		UE_LOG("[FBX] ImportStaticMesh failed: mesh node not found. File: %s", FbxFilePath.c_str());
		DestroyFbxSceneContext(Context);
		return false;
	}

	FbxMesh* Mesh = MeshNode->GetMesh();

	ExtractFbxStaticMaterials(MeshNode, OutMaterials);

	TArray<FFbxMaterialInfo> FbxMaterialInfos;
	ExtractFbxMaterialInfos(MeshNode, FbxMaterialInfos);
	BuildStaticMaterialsFromFbxInfos(FbxMaterialInfos, OutMaterials);

	const int32 ControlPointCount = static_cast<int32>(Mesh->GetControlPointsCount());
	const int32 PolygonCount = static_cast<int32>(Mesh->GetPolygonCount());

	FbxStringList UVSetNames;
	Mesh->GetUVSetNames(UVSetNames);

	const char* UVSetName = nullptr;

	if (UVSetNames.GetCount() > 0)
	{
		UVSetName = UVSetNames[0];
		UE_LOG("[FBX] Using UV Set: %s", UVSetName);
	}
	else
	{
		UE_LOG("[FBX] Mesh has no UV set.");
	}

	// UE_LOG("[FBX] ImportStaticMesh target mesh: Node=%s, ControlPoints=%d, Polygons=%d",
	//        MeshNode->GetName(), ControlPointCount, PolygonCount);

	int32 CurrentMaterialIndex = -1;
	FStaticMeshSection* CurrentSection = nullptr;

	FbxVector4* ControlPoints = Mesh->GetControlPoints();
	for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
	{
		const int32 PolygonSize = static_cast<int32>(Mesh->GetPolygonSize(PolygonIndex));

		if (PolygonSize != 3)
		{
			UE_LOG("[FBX] Skip non-triangle polygon. Polygon=%d, Size=%d", PolygonIndex, PolygonSize);
			continue;
		}

		const int32 MaterialIndex = GetPolygonMaterialIndex(Mesh, PolygonIndex);

		if (MaterialIndex != CurrentMaterialIndex)
		{
			if (CurrentSection)
			{
				CurrentSection->NumTriangles = (static_cast<uint32>(OutMesh.Indices.size()) - CurrentSection->
					FirstIndex) / 3;
			}

			// BuildTangents(OutMesh);
			//
			// UE_LOG("[FBX] StaticMesh built. Vertices=%zu, Indices=%zu, Sections=%zu, Materials=%zu",
			//        OutMesh.Vertices.size(),
			//        OutMesh.Indices.size(),
			//        OutMesh.Sections.size(),
			//        OutMaterials.size());

			int32 SafeMaterialIndex = MaterialIndex;

			if (SafeMaterialIndex < 0 || SafeMaterialIndex >= static_cast<int32>(OutMaterials.size()))
			{
				SafeMaterialIndex = 0;
			}

			FStaticMeshSection NewSection;
			NewSection.MaterialIndex = SafeMaterialIndex;
			NewSection.MaterialSlotName = OutMaterials[SafeMaterialIndex].MaterialSlotName;
			NewSection.FirstIndex = static_cast<uint32>(OutMesh.Indices.size());
			NewSection.NumTriangles = 0;

			OutMesh.Sections.push_back(NewSection);

			CurrentSection = &OutMesh.Sections.back();
			CurrentMaterialIndex = MaterialIndex;
		}

		for (int32 VertexIndex = 0; VertexIndex < PolygonSize; ++VertexIndex)
		{
			const int32 ControlPointIndex = static_cast<int32>(Mesh->GetPolygonVertex(PolygonIndex, VertexIndex));

			if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
			{
				UE_LOG("[FBX] Invalid ControlPointIndex. Polygon=%d, Vertex=%d, ControlPoint=%d",
				       PolygonIndex,
				       VertexIndex,
				       ControlPointIndex);
				continue;
			}

			const FbxVector4& FbxPosition = ControlPoints[ControlPointIndex];
			const FVector EnginePosition = ToEngineVector(FbxPosition);
			FbxVector4 FbxNormal;
			const bool bHasNormal = Mesh->GetPolygonVertexNormal(PolygonIndex, VertexIndex, FbxNormal);
			auto EngineNormal = FVector(0.0f, 0.0f, 1.0f);
			if (bHasNormal)
			{
				EngineNormal = ToEngineVector(FbxNormal).Normalized();
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

	BuildTangents(OutMesh);

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
