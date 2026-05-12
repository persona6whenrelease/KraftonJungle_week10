#include "FbxMetaParser.h"
#include <fbxsdk.h>

#include "Core/Log.h"
#include "Engine/Platform/Paths.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <filesystem>


namespace
{
	template <typename T>
	bool IsValidIndex(const TArray<T>& Items, int32 Index)
	{
		return Index >= 0 && static_cast<size_t>(Index) < Items.size();
	}

	bool ContainsId(const TArray<int32>& Items, int32 Id)
	{
		return std::find(Items.begin(), Items.end(), Id) != Items.end();
	}

	void AddUniqueId(TArray<int32>& Items, int32 Id)
	{
		if (Id >= 0 && !ContainsId(Items, Id))
		{
			Items.push_back(Id);
		}
	}

	void RemoveId(TArray<int32>& Items, int32 Id)
	{
		Items.erase(std::remove(Items.begin(), Items.end(), Id), Items.end());
	}

	const char* AttributeTypeToName(FbxNodeAttribute::EType Type)
	{
		switch (Type)
		{
		case FbxNodeAttribute::eUnknown: return "Unknown";
		case FbxNodeAttribute::eNull: return "Null";
		case FbxNodeAttribute::eMarker: return "Marker";
		case FbxNodeAttribute::eSkeleton: return "Skeleton";
		case FbxNodeAttribute::eMesh: return "Mesh";
		case FbxNodeAttribute::eNurbs: return "Nurbs";
		case FbxNodeAttribute::ePatch: return "Patch";
		case FbxNodeAttribute::eCamera: return "Camera";
		case FbxNodeAttribute::eCameraStereo: return "CameraStereo";
		case FbxNodeAttribute::eCameraSwitcher: return "CameraSwitcher";
		case FbxNodeAttribute::eLight: return "Light";
		case FbxNodeAttribute::eOpticalReference: return "OpticalReference";
		case FbxNodeAttribute::eOpticalMarker: return "OpticalMarker";
		case FbxNodeAttribute::eNurbsCurve: return "NurbsCurve";
		case FbxNodeAttribute::eTrimNurbsSurface: return "TrimNurbsSurface";
		case FbxNodeAttribute::eBoundary: return "Boundary";
		case FbxNodeAttribute::eNurbsSurface: return "NurbsSurface";
		case FbxNodeAttribute::eShape: return "Shape";
		case FbxNodeAttribute::eLODGroup: return "LODGroup";
		case FbxNodeAttribute::eSubDiv: return "SubDiv";
		default: return "Other";
		}
	}

	bool HasValidCluster(const FFbxSkinMeta& Skin, const FFbxImportMeta& Meta)
	{
		for (int32 ClusterId : Skin.ClusterIds)
		{
			if (IsValidIndex(Meta.Clusters, ClusterId) && Meta.Clusters[ClusterId].bValid)
			{
				return true;
			}
		}
		return false;
	}

	FString GetFbxMaterialName(FbxSurfaceMaterial* SurfaceMaterial)
	{
		if (!SurfaceMaterial || !SurfaceMaterial->GetName())
		{
			return "None";
		}

		FString Name = SurfaceMaterial->GetName();
		return Name.empty() ? "None" : Name;
	}

	FVector ReadDiffuseColor(FbxSurfaceMaterial* SurfaceMaterial)
	{
		if (!SurfaceMaterial)
		{
			return FVector(1.0f, 0.0f, 1.0f);
		}

		FbxProperty DiffuseProperty = SurfaceMaterial->FindProperty("Diffuse");
		if (!DiffuseProperty.IsValid())
		{
			return FVector(1.0f, 0.0f, 1.0f);
		}

		const FbxDouble3 Diffuse = DiffuseProperty.Get<FbxDouble3>();
		return FVector(
			static_cast<float>(Diffuse[0]),
			static_cast<float>(Diffuse[1]),
			static_cast<float>(Diffuse[2]));
	}

	const char* DiffusePropertyNames[] = {
		"Diffuse",
		"DiffuseColor",
		"BaseColor",
		"Maya|baseColor",
		"Maya|DiffuseColor"
	};

	const char* NormalPropertyNames[] = {
		"NormalMap",
		"Bump",
		"Maya|normalCamera",
		"NormalCamera"
	};

	const char* SpecularPropertyNames[] = {
		"Specular",
		"SpecularColor",
		"Maya|specularColor"
	};

	const char* EmissivePropertyNames[] = {
		"Emissive",
		"EmissiveColor",
		"EmissionColor",
		"Maya|emissionColor"
	};

	bool IsFbxFileTextureObject(FbxObject* Object)
	{
		if (!Object)
		{
			return false;
		}

		const char* ClassName = Object->GetClassId().GetName();
		return ClassName &&
			(std::strcmp(ClassName, "FbxFileTexture") == 0 ||
			 std::strcmp(ClassName, "FileTexture") == 0);
	}

	FString ToLowerAscii(FString Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(),
			[](unsigned char Ch)
			{
				return static_cast<char>(std::tolower(Ch));
			});
		return Text;
	}

	bool ContainsToken(const FString& Text, const char* Token)
	{
		return Text.find(Token) != FString::npos;
	}

	FString GetTextureIdentityText(FbxFileTexture* Texture)
	{
		FString Text;
		if (!Texture)
		{
			return Text;
		}

		if (Texture->GetName())
		{
			Text += Texture->GetName();
			Text += " ";
		}
		if (Texture->GetFileName())
		{
			Text += Texture->GetFileName();
			Text += " ";
		}
		if (Texture->GetRelativeFileName())
		{
			Text += Texture->GetRelativeFileName();
		}
		return ToLowerAscii(Text);
	}

	FbxFileTexture* FindFileTextureRecursive(FbxObject* Object, int32 Depth = 0)
	{
		if (!Object || Depth > 8)
		{
			return nullptr;
		}

		if (IsFbxFileTextureObject(Object))
		{
			return static_cast<FbxFileTexture*>(Object);
		}

		const int32 SourceCount = Object->GetSrcObjectCount();
		for (int32 SourceIndex = 0; SourceIndex < SourceCount; ++SourceIndex)
		{
			if (FbxFileTexture* FileTexture = FindFileTextureRecursive(Object->GetSrcObject(SourceIndex), Depth + 1))
			{
				return FileTexture;
			}
		}

		return nullptr;
	}

	void CollectFileTexturesRecursive(FbxObject* Object, TArray<FbxFileTexture*>& OutTextures, int32 Depth = 0)
	{
		if (!Object || Depth > 8)
		{
			return;
		}

		if (IsFbxFileTextureObject(Object))
		{
			FbxFileTexture* Texture = static_cast<FbxFileTexture*>(Object);
			if (std::find(OutTextures.begin(), OutTextures.end(), Texture) == OutTextures.end())
			{
				OutTextures.push_back(Texture);
			}
			return;
		}

		const int32 SourceCount = Object->GetSrcObjectCount();
		for (int32 SourceIndex = 0; SourceIndex < SourceCount; ++SourceIndex)
		{
			CollectFileTexturesRecursive(Object->GetSrcObject(SourceIndex), OutTextures, Depth + 1);
		}
	}

	template <size_t N>
	FbxFileTexture* FindFileTextureForProperties(FbxSurfaceMaterial* SurfaceMaterial, const char* const (&PropertyNames)[N])
	{
		if (!SurfaceMaterial)
		{
			return nullptr;
		}

		for (const char* PropertyName : PropertyNames)
		{
			FbxProperty Property = SurfaceMaterial->FindProperty(PropertyName);
			if (!Property.IsValid() || Property.GetSrcObjectCount() <= 0)
			{
				continue;
			}

			for (int32 TextureIndex = 0; TextureIndex < Property.GetSrcObjectCount(); ++TextureIndex)
			{
				if (FbxFileTexture* Texture = FindFileTextureRecursive(Property.GetSrcObject(TextureIndex)))
				{
					return Texture;
				}
			}
		}

		return nullptr;
	}

	void CollectFileTexturesForProperty(FbxProperty Property, TArray<FbxFileTexture*>& OutTextures)
	{
		if (!Property.IsValid() || Property.GetSrcObjectCount() <= 0)
		{
			return;
		}

		for (int32 TextureIndex = 0; TextureIndex < Property.GetSrcObjectCount(); ++TextureIndex)
		{
			CollectFileTexturesRecursive(Property.GetSrcObject(TextureIndex), OutTextures);
		}
	}

	TArray<FbxFileTexture*> CollectAllMaterialFileTextures(FbxSurfaceMaterial* SurfaceMaterial)
	{
		TArray<FbxFileTexture*> Textures;
		if (!SurfaceMaterial)
		{
			return Textures;
		}

		FbxProperty Property = SurfaceMaterial->GetFirstProperty();
		while (Property.IsValid())
		{
			CollectFileTexturesForProperty(Property, Textures);
			Property = SurfaceMaterial->GetNextProperty(Property);
		}

		const int32 SourceCount = SurfaceMaterial->GetSrcObjectCount();
		for (int32 SourceIndex = 0; SourceIndex < SourceCount; ++SourceIndex)
		{
			CollectFileTexturesRecursive(SurfaceMaterial->GetSrcObject(SourceIndex), Textures);
		}

		return Textures;
	}

	bool TextureMatchesRole(FbxFileTexture* Texture, const char* TextureRole)
	{
		const FString Identity = GetTextureIdentityText(Texture);
		if (Identity.empty() || !TextureRole)
		{
			return false;
		}

		if (std::strcmp(TextureRole, "Diffuse") == 0)
		{
			return ContainsToken(Identity, "base_color") ||
				ContainsToken(Identity, "basecolor") ||
				ContainsToken(Identity, "diffuse") ||
				ContainsToken(Identity, "albedo") ||
				ContainsToken(Identity, "_d.") ||
				ContainsToken(Identity, "-d.");
		}
		if (std::strcmp(TextureRole, "Normal") == 0)
		{
			return ContainsToken(Identity, "normalmap") ||
				ContainsToken(Identity, "normal") ||
				ContainsToken(Identity, "_n.") ||
				ContainsToken(Identity, "-n.");
		}
		if (std::strcmp(TextureRole, "Specular") == 0)
		{
			return ContainsToken(Identity, "specular") ||
				ContainsToken(Identity, "spec");
		}
		if (std::strcmp(TextureRole, "Emissive") == 0)
		{
			return ContainsToken(Identity, "emission") ||
				ContainsToken(Identity, "emissive") ||
				ContainsToken(Identity, "_e.") ||
				ContainsToken(Identity, "-e.");
		}

		return false;
	}

	FbxFileTexture* FindFileTextureByRoleFallback(FbxSurfaceMaterial* SurfaceMaterial, const char* TextureRole)
	{
		TArray<FbxFileTexture*> Textures = CollectAllMaterialFileTextures(SurfaceMaterial);
		for (FbxFileTexture* Texture : Textures)
		{
			if (TextureMatchesRole(Texture, TextureRole))
			{
				return Texture;
			}
		}
		return nullptr;
	}

	bool TryMakeProjectRelativePath(const std::filesystem::path& DiskPath, FString& OutPath)
	{
		if (!std::filesystem::exists(DiskPath))
		{
			return false;
		}

		const std::filesystem::path Root = std::filesystem::path(FPaths::RootDir()).lexically_normal();
		const std::filesystem::path NormalizedDiskPath = DiskPath.lexically_normal();
		std::filesystem::path RelativePath = NormalizedDiskPath.lexically_relative(Root);
		if (RelativePath.empty() || RelativePath.native().rfind(L"..", 0) == 0)
		{
			RelativePath = std::filesystem::relative(NormalizedDiskPath, Root);
		}

		OutPath = FPaths::ToUtf8(RelativePath.generic_wstring());
		return !OutPath.empty();
	}

	TArray<std::filesystem::path> BuildTextureSearchBaseDirs(const std::filesystem::path& SourceFbxDir)
	{
		TArray<std::filesystem::path> BaseDirs;
		auto AddUniqueDir = [&BaseDirs](const std::filesystem::path& Dir)
		{
			if (Dir.empty())
			{
				return;
			}

			const std::filesystem::path Normalized = Dir.lexically_normal();
			if (std::find(BaseDirs.begin(), BaseDirs.end(), Normalized) == BaseDirs.end())
			{
				BaseDirs.push_back(Normalized);
			}
		};

		const std::filesystem::path ParentDir = SourceFbxDir.parent_path();
		AddUniqueDir(SourceFbxDir);
		AddUniqueDir(ParentDir);
		AddUniqueDir(ParentDir / L"textures");
		AddUniqueDir(ParentDir / L"texture");
		AddUniqueDir(ParentDir / L"tex");
		AddUniqueDir(ParentDir / L"maps");
		AddUniqueDir(ParentDir / L"materials");
		AddUniqueDir(ParentDir / L"images");
		return BaseDirs;
	}

	FString SearchCaseInsensitiveInDirectory(
		const std::filesystem::path& Directory,
		const std::filesystem::path& RelativePath)
	{
		if (Directory.empty() || RelativePath.empty())
		{
			return "";
		}

		std::filesystem::path Current = Directory;
		for (const std::filesystem::path& Part : RelativePath)
		{
			if (Part.empty() || Part == L".")
			{
				continue;
			}

			const std::filesystem::path Direct = Current / Part;
			if (std::filesystem::exists(Direct))
			{
				Current = Direct;
				continue;
			}

			if (!std::filesystem::is_directory(Current))
			{
				return "";
			}

			const FString Wanted = ToLowerAscii(FPaths::ToUtf8(Part.wstring()));
			bool bFound = false;
			for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Current))
			{
				const FString EntryName = ToLowerAscii(FPaths::ToUtf8(Entry.path().filename().wstring()));
				if (EntryName == Wanted)
				{
					Current = Entry.path();
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				return "";
			}
		}

		FString ResolvedPath;
		return TryMakeProjectRelativePath(Current, ResolvedPath) ? ResolvedPath : FString();
	}

	FString TryResolveTextureCandidate(
		const TArray<std::filesystem::path>& SearchBaseDirs,
		const FString& CandidateText)
	{
		if (CandidateText.empty())
		{
			return "";
		}

		const std::filesystem::path Candidate(FPaths::ToWide(CandidateText));
		FString ResolvedPath;
		if (Candidate.is_absolute() && TryMakeProjectRelativePath(Candidate, ResolvedPath))
		{
			return ResolvedPath;
		}

		if (!Candidate.is_absolute())
		{
			for (const std::filesystem::path& BaseDir : SearchBaseDirs)
			{
				if (TryMakeProjectRelativePath(BaseDir / Candidate, ResolvedPath))
				{
					return ResolvedPath;
				}

				ResolvedPath = SearchCaseInsensitiveInDirectory(BaseDir, Candidate);
				if (!ResolvedPath.empty())
				{
					return ResolvedPath;
				}
			}
		}

		return "";
	}

	FString ResolveTexturePathByFileNameHeuristic(
		const TArray<std::filesystem::path>& SearchBaseDirs,
		const FString& RawFileName,
		const FString& RawRelativeFileName)
	{
		std::filesystem::path CleanFileName;
		if (!RawFileName.empty())
		{
			CleanFileName = std::filesystem::path(FPaths::ToWide(RawFileName)).filename();
		}
		if (CleanFileName.empty() && !RawRelativeFileName.empty())
		{
			CleanFileName = std::filesystem::path(FPaths::ToWide(RawRelativeFileName)).filename();
		}
		if (CleanFileName.empty())
		{
			return "";
		}

		static const char* SearchFolders[] = {
			"",
			"texture",
			"textures",
			"tex",
			"maps",
			"materials",
			"images",
			"src"
		};

		for (const std::filesystem::path& BaseDir : SearchBaseDirs)
		{
			for (const char* FolderName : SearchFolders)
			{
				const std::filesystem::path Candidate = FString(FolderName).empty()
					? BaseDir / CleanFileName
					: BaseDir / FPaths::ToWide(FolderName) / CleanFileName;

				FString ResolvedPath;
				if (TryMakeProjectRelativePath(Candidate, ResolvedPath))
				{
					return ResolvedPath;
				}

				ResolvedPath = SearchCaseInsensitiveInDirectory(Candidate.parent_path(), Candidate.filename());
				if (!ResolvedPath.empty())
				{
					return ResolvedPath;
				}
			}
		}

		return "";
	}

	FString FormatSearchBaseDirs(const TArray<std::filesystem::path>& SearchBaseDirs)
	{
		FString Text;
		for (const std::filesystem::path& Dir : SearchBaseDirs)
		{
			if (!Text.empty())
			{
				Text += "; ";
			}
			Text += FPaths::ToUtf8(Dir.generic_wstring());
		}
		return Text;
	}

	FString ReadTexturePath(
		FbxSurfaceMaterial* SurfaceMaterial,
		const FString& SourceFilePath,
		FbxFileTexture* Texture,
		const char* TextureRole)
	{
		if (!Texture)
		{
			return "";
		}

		const FString RawFileName = Texture->GetFileName() ? Texture->GetFileName() : "";
		const FString RawRelativeFileName = Texture->GetRelativeFileName() ? Texture->GetRelativeFileName() : "";
		const std::filesystem::path SourceFbxDir = std::filesystem::path(FPaths::ToWide(SourceFilePath)).parent_path();
		const TArray<std::filesystem::path> SearchBaseDirs = BuildTextureSearchBaseDirs(SourceFbxDir);

		FString ResolvedPath = TryResolveTextureCandidate(SearchBaseDirs, RawFileName);
		if (ResolvedPath.empty())
		{
			ResolvedPath = TryResolveTextureCandidate(SearchBaseDirs, RawRelativeFileName);
		}
		if (!ResolvedPath.empty())
		{
			UE_LOG("[FBXImporter] Resolved %s texture. Material=%s RawFile=%s RawRelative=%s Path=%s",
				TextureRole,
				SurfaceMaterial && SurfaceMaterial->GetName() ? SurfaceMaterial->GetName() : "<null>",
				RawFileName.c_str(),
				RawRelativeFileName.c_str(),
				ResolvedPath.c_str());
			return ResolvedPath;
		}

		ResolvedPath = ResolveTexturePathByFileNameHeuristic(SearchBaseDirs, RawFileName, RawRelativeFileName);
		if (!ResolvedPath.empty())
		{
			UE_LOG("[FBXImporter] Resolved %s texture by filename search. Material=%s RawFile=%s RawRelative=%s Path=%s",
				TextureRole,
				SurfaceMaterial && SurfaceMaterial->GetName() ? SurfaceMaterial->GetName() : "<null>",
				RawFileName.c_str(),
				RawRelativeFileName.c_str(),
				ResolvedPath.c_str());
			return ResolvedPath;
		}

		UE_LOG("[FBXImporter] Texture path not found. Role=%s Material=%s FileName=%s RelativeFileName=%s Source=%s SearchDirs=%s",
			TextureRole,
			SurfaceMaterial && SurfaceMaterial->GetName() ? SurfaceMaterial->GetName() : "<null>",
			RawFileName.c_str(),
			RawRelativeFileName.c_str(),
			SourceFilePath.c_str(),
			FormatSearchBaseDirs(SearchBaseDirs).c_str());
		return "";
	}

	template <size_t N>
	FString ReadTexturePathForProperties(
		FbxSurfaceMaterial* SurfaceMaterial,
		const FString& SourceFilePath,
		const char* const (&PropertyNames)[N],
		const char* TextureRole)
	{
		FbxFileTexture* Texture = FindFileTextureForProperties(SurfaceMaterial, PropertyNames);
		if (!Texture)
		{
			Texture = FindFileTextureByRoleFallback(SurfaceMaterial, TextureRole);
		}

		return ReadTexturePath(SurfaceMaterial, SourceFilePath, Texture, TextureRole);
	}

	FString ReadDiffuseUVSetName(FbxSurfaceMaterial* SurfaceMaterial)
	{
		FbxFileTexture* Texture = FindFileTextureForProperties(SurfaceMaterial, DiffusePropertyNames);
		if (!Texture)
		{
			Texture = FindFileTextureByRoleFallback(SurfaceMaterial, "Diffuse");
		}
		if (!Texture)
		{
			return "";
		}

		const char* UVSetName = Texture->UVSet.Get();
		return (UVSetName && UVSetName[0] != '\0') ? FString(UVSetName) : FString();
	}
}

// 메타 빌드의 전체 실행 순서를 보여주는 진입점입니다.
#pragma region Build Orchestration

bool FFbxMetaParser::BuildFbxMeta(FbxScene* Scene)
{
	const FString SourceFilePath = ImportMeta.SourceFilePath;
	ImportMeta.Clear();
	ImportMeta.SourceFilePath = SourceFilePath;

	if (!Scene || !Scene->GetRootNode())
	{
		UE_LOG("[FBXMetaParser] Scene root is missing.");
		return false;
	}

	// 1. FBX 원본 노드와 mesh node 테이블을 구성합니다.
	RegisterNodeRecursive(Scene->GetRootNode(), -1, "");

	// 2. skin/cluster 테이블을 만들고 cluster link bone을 기록합니다.
	for (int32 MeshId = 0; MeshId < static_cast<int32>(ImportMeta.Meshes.size()); ++MeshId)
	{
		RegisterSkinsForMesh(MeshId);
	}

	// 3. FBX 노드 계층을 바탕으로 BoneMeta parent-child 연결을 보정합니다.
	for (int32 BoneId = 0; BoneId < static_cast<int32>(ImportMeta.Bones.size()); ++BoneId)
	{
		if (ImportMeta.Bones[BoneId].bReferencedByCluster)
		{
			EnsureBoneParentChain(BoneId);
		}
	}
	BuildRegisteredBoneHierarchyLinks();

	// 4. skeleton root 기준으로 bone과 skinned mesh part를 그룹화합니다.
	BuildSkeletonTables();

	// 5. bone 아래 parent 된 non-skinned mesh를 skeletal assembly에 붙입니다.
	AttachRigidMeshesToSkeletons();

	// 6. 최종 import classification 배열을 생성합니다.
	ClassifyMeshes();

	// 7. 생성된 FBX import meta의 ID와 양방향 링크 무결성을 검증합니다.
	return ValidateFbxMeta();
}

#pragma endregion

// FBX 원본 노드와 mesh node 테이블을 구성합니다.
#pragma region Node And Mesh Registration

int32 FFbxMetaParser::RegisterNodeRecursive(FbxNode* Node, int32 ParentNodeId, const FString& ParentPath)
{
	if (!Node)
	{
		return -1;
	}

	const int32 NodeId = static_cast<int32>(ImportMeta.Nodes.size());
	FFbxNodeMeta NodeMeta;
	NodeMeta.NodeId = NodeId;
	NodeMeta.ParentNodeId = ParentNodeId;
	NodeMeta.Node = Node;
	NodeMeta.Name = FBXUtil::GetNodeName(Node);
	NodeMeta.FullPath = ParentPath.empty() ? ("/" + NodeMeta.Name) : (ParentPath + "/" + NodeMeta.Name);
	NodeMeta.LocalTransform = FBXUtil::ConvertFbxMatrix(Node->EvaluateLocalTransform());
	NodeMeta.GlobalTransform = FBXUtil::ConvertFbxMatrix(Node->EvaluateGlobalTransform());

	if (FbxNodeAttribute* Attribute = Node->GetNodeAttribute())
	{
		NodeMeta.bHasAttribute = true;
		const FbxNodeAttribute::EType Type = Attribute->GetAttributeType();
		NodeMeta.AttributeTypeName = AttributeTypeToName(Type);
		NodeMeta.bHasMesh = Type == FbxNodeAttribute::eMesh;
		NodeMeta.bHasSkeleton = Type == FbxNodeAttribute::eSkeleton;
		NodeMeta.bHasLight = Type == FbxNodeAttribute::eLight;
		NodeMeta.bHasCamera = Type == FbxNodeAttribute::eCamera ||
			Type == FbxNodeAttribute::eCameraStereo ||
			Type == FbxNodeAttribute::eCameraSwitcher;
	}

	ImportMeta.Nodes.push_back(NodeMeta);
	ImportMeta.NodeToNodeId[Node] = NodeId;

	if (IsValidIndex(ImportMeta.Nodes, ParentNodeId))
	{
		AddUniqueId(ImportMeta.Nodes[ParentNodeId].ChildNodeIds, NodeId);
	}

	if (NodeMeta.bHasMesh)
	{
		RegisterMeshFromNode(Node, NodeId);
	}
	if (NodeMeta.bHasSkeleton && ParentNodeId >= 0)
	{
		RegisterBoneNode(Node, false, false);
	}
	if (NodeMeta.bHasLight)
	{
		AddUniqueId(ImportMeta.LightNodeIds, NodeId);
	}
	if (NodeMeta.bHasCamera)
	{
		AddUniqueId(ImportMeta.CameraNodeIds, NodeId);
	}

	const int32 ChildCount = Node->GetChildCount();
	for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		RegisterNodeRecursive(Node->GetChild(ChildIndex), NodeId, NodeMeta.FullPath);
	}

	return NodeId;
}

void FFbxMetaParser::RegisterMeshFromNode(FbxNode* Node, int32 NodeId)
{
	if (!Node || !IsValidIndex(ImportMeta.Nodes, NodeId))
	{
		return;
	}

	FbxMesh* Mesh = Node->GetMesh();
	if (!Mesh)
	{
		return;
	}

	const int32 MeshId = static_cast<int32>(ImportMeta.Meshes.size());
	FFbxMeshMeta MeshMeta;
	MeshMeta.MeshId = MeshId;
	MeshMeta.NodeId = NodeId;
	MeshMeta.Node = Node;
	MeshMeta.Mesh = Mesh;
	MeshMeta.Name = FBXUtil::GetNodeName(Node);
	MeshMeta.SourceNodePath = ImportMeta.Nodes[NodeId].FullPath;
	MeshMeta.ControlPointCount = Mesh->GetControlPointsCount();
	MeshMeta.PolygonCount = Mesh->GetPolygonCount();

	const int32 MaterialCount = Node->GetMaterialCount();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		MeshMeta.MaterialSlotIds.push_back(MaterialIndex);
		FbxSurfaceMaterial* SurfaceMaterial = Node->GetMaterial(MaterialIndex);
		const int32 MaterialId = RegisterMaterial(SurfaceMaterial);
		MeshMeta.MaterialIds.push_back(MaterialId);

		FString SlotName = "None";
		FString UVSetName;
		if (IsValidIndex(ImportMeta.Materials, MaterialId))
		{
			SlotName = ImportMeta.Materials[MaterialId].MaterialSlotName;
			UVSetName = ImportMeta.Materials[MaterialId].DiffuseUVSetName;
		}
		MeshMeta.MaterialSlotNames.push_back(std::move(SlotName));
		MeshMeta.MaterialUVSetNames.push_back(std::move(UVSetName));
	}

	if (MeshMeta.MaterialSlotNames.empty())
	{
		MeshMeta.MaterialSlotIds.push_back(0);
		MeshMeta.MaterialIds.push_back(-1);
		MeshMeta.MaterialSlotNames.push_back("None");
		MeshMeta.MaterialUVSetNames.push_back("");
	}

	auto FirstMeshIt = ImportMeta.MeshToMeshId.find(Mesh);
	if (FirstMeshIt != ImportMeta.MeshToMeshId.end())
	{
		UE_LOG("[FBXMetaParser] Duplicate FbxMesh object used by multiple nodes. FirstMeshId=%d NewMeshId=%d Node=%s",
			FirstMeshIt->second,
			MeshId,
			MeshMeta.SourceNodePath.c_str());
	}
	else
	{
		ImportMeta.MeshToMeshId[Mesh] = MeshId;
	}

	ImportMeta.FbxMeshToMeshIds[Mesh].push_back(MeshId);
	ImportMeta.Meshes.push_back(MeshMeta);
}

int32 FFbxMetaParser::RegisterMaterial(FbxSurfaceMaterial* SurfaceMaterial)
{
	if (!SurfaceMaterial)
	{
		return -1;
	}

	auto PtrIt = ImportMeta.MaterialToMaterialId.find(SurfaceMaterial);
	if (PtrIt != ImportMeta.MaterialToMaterialId.end())
	{
		return PtrIt->second;
	}

	const FString SlotName = GetFbxMaterialName(SurfaceMaterial);
	auto NameIt = ImportMeta.MaterialNameToMaterialId.find(SlotName);
	if (NameIt != ImportMeta.MaterialNameToMaterialId.end())
	{
		// Same-name FBX material collisions are currently shared. Suffix disambiguation is deferred.
		ImportMeta.MaterialToMaterialId[SurfaceMaterial] = NameIt->second;
		return NameIt->second;
	}

	const int32 MaterialId = static_cast<int32>(ImportMeta.Materials.size());
	FFbxMaterialInfo MaterialInfo;
	MaterialInfo.MaterialId = MaterialId;
	MaterialInfo.MaterialSlotName = SlotName;
	MaterialInfo.DiffuseColor = ReadDiffuseColor(SurfaceMaterial);
	MaterialInfo.DiffuseTexturePath = ReadTexturePathForProperties(
		SurfaceMaterial,
		ImportMeta.SourceFilePath,
		DiffusePropertyNames,
		"Diffuse");
	MaterialInfo.NormalTexturePath = ReadTexturePathForProperties(
		SurfaceMaterial,
		ImportMeta.SourceFilePath,
		NormalPropertyNames,
		"Normal");
	MaterialInfo.SpecularTexturePath = ReadTexturePathForProperties(
		SurfaceMaterial,
		ImportMeta.SourceFilePath,
		SpecularPropertyNames,
		"Specular");
	MaterialInfo.EmissiveTexturePath = ReadTexturePathForProperties(
		SurfaceMaterial,
		ImportMeta.SourceFilePath,
		EmissivePropertyNames,
		"Emissive");
	MaterialInfo.DiffuseUVSetName = ReadDiffuseUVSetName(SurfaceMaterial);

	ImportMeta.Materials.push_back(std::move(MaterialInfo));
	ImportMeta.MaterialToMaterialId[SurfaceMaterial] = MaterialId;
	ImportMeta.MaterialNameToMaterialId[SlotName] = MaterialId;
	return MaterialId;
}

#pragma endregion

// skin/cluster 테이블을 만들고 cluster link bone을 기록합니다.
#pragma region Skin And Cluster Registration

void FFbxMetaParser::RegisterSkinsForMesh(int32 MeshId)
{
	if (!IsValidIndex(ImportMeta.Meshes, MeshId))
	{
		return;
	}

	FFbxMeshMeta& MeshMeta = ImportMeta.Meshes[MeshId];
	if (!MeshMeta.Mesh)
	{
		return;
	}

	const int32 SkinCount = MeshMeta.Mesh->GetDeformerCount(FbxDeformer::eSkin);
	MeshMeta.bHasSkin = SkinCount > 0;
	if (SkinCount > 1)
	{
		UE_LOG("[FBXMetaParser] Mesh has multiple skins. MeshId=%d Node=%s SkinCount=%d",
			MeshId,
			MeshMeta.SourceNodePath.c_str(),
			SkinCount);
	}

	for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
	{
		FbxSkin* Skin = static_cast<FbxSkin*>(MeshMeta.Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
		if (!Skin)
		{
			continue;
		}

		const int32 SkinId = static_cast<int32>(ImportMeta.Skins.size());
		FFbxSkinMeta SkinMeta;
		SkinMeta.SkinId = SkinId;
		SkinMeta.MeshId = MeshId;
		SkinMeta.Skin = Skin;
		SkinMeta.ClusterCount = Skin->GetClusterCount();

		ImportMeta.Skins.push_back(SkinMeta);
		MeshMeta.SkinIds.push_back(SkinId);
		if (ImportMeta.SkinToSkinId.find(Skin) == ImportMeta.SkinToSkinId.end())
		{
			ImportMeta.SkinToSkinId[Skin] = SkinId;
		}

		for (int32 ClusterIndex = 0; ClusterIndex < Skin->GetClusterCount(); ++ClusterIndex)
		{
			RegisterCluster(SkinId, Skin->GetCluster(ClusterIndex));
		}

		FFbxSkinMeta& RegisteredSkin = ImportMeta.Skins[SkinId];
		RegisteredSkin.bValid = HasValidCluster(RegisteredSkin, ImportMeta);
		if (RegisteredSkin.bValid)
		{
			MeshMeta.bHasValidSkin = true;
			if (MeshMeta.PrimarySkinId < 0)
			{
				MeshMeta.PrimarySkinId = SkinId;
			}
		}
	}
}

int32 FFbxMetaParser::RegisterCluster(int32 SkinId, FbxCluster* Cluster)
{
	if (!IsValidIndex(ImportMeta.Skins, SkinId))
	{
		return -1;
	}

	FFbxSkinMeta& SkinMeta = ImportMeta.Skins[SkinId];
	const int32 ClusterId = static_cast<int32>(ImportMeta.Clusters.size());
	FFbxClusterMeta ClusterMeta;
	ClusterMeta.ClusterId = ClusterId;
	ClusterMeta.SkinId = SkinId;
	ClusterMeta.MeshId = SkinMeta.MeshId;
	ClusterMeta.Cluster = Cluster;

	if (Cluster)
	{
		if (ImportMeta.ClusterToClusterId.find(Cluster) == ImportMeta.ClusterToClusterId.end())
		{
			ImportMeta.ClusterToClusterId[Cluster] = ClusterId;
		}

		FbxAMatrix MeshBindMatrix;
		Cluster->GetTransformMatrix(MeshBindMatrix);
		ClusterMeta.MeshBindGlobalMatrix = FBXUtil::ConvertFbxMatrix(MeshBindMatrix);
		ClusterMeta.bHasMeshBindMatrix = true;

		FbxAMatrix BoneBindMatrix;
		Cluster->GetTransformLinkMatrix(BoneBindMatrix);
		ClusterMeta.BoneBindGlobalMatrix = FBXUtil::ConvertFbxMatrix(BoneBindMatrix);
		ClusterMeta.bHasBoneBindMatrix = true;

		ClusterMeta.ControlPointInfluenceCount = Cluster->GetControlPointIndicesCount();
		const double* Weights = Cluster->GetControlPointWeights();
		for (int32 InfluenceIndex = 0; InfluenceIndex < ClusterMeta.ControlPointInfluenceCount; ++InfluenceIndex)
		{
			if (Weights && Weights[InfluenceIndex] > 0.0)
			{
				++ClusterMeta.PositiveWeightCount;
			}
		}

		ClusterMeta.LinkNode = Cluster->GetLink();
		if (!ClusterMeta.LinkNode)
		{
			UE_LOG("[FBXMetaParser] Cluster link is null. SkinId=%d ClusterId=%d", SkinId, ClusterId);
		}
		else
		{
			ClusterMeta.LinkNodeName = FBXUtil::GetNodeName(ClusterMeta.LinkNode);
			auto NodeIt = ImportMeta.NodeToNodeId.find(ClusterMeta.LinkNode);
			if (NodeIt != ImportMeta.NodeToNodeId.end())
			{
				ClusterMeta.LinkNodeId = NodeIt->second;
			}
			else
			{
				UE_LOG("[FBXMetaParser] Cluster link node is missing from node table. Link=%s",
					ClusterMeta.LinkNodeName.c_str());
			}

			ClusterMeta.LinkBoneId = RegisterBoneNode(ClusterMeta.LinkNode, true, false);
			if (IsValidIndex(ImportMeta.Bones, ClusterMeta.LinkBoneId))
			{
				FFbxBoneMeta& BoneMeta = ImportMeta.Bones[ClusterMeta.LinkBoneId];
				if (ClusterMeta.bHasBoneBindMatrix)
				{
					BoneMeta.BindGlobalMatrix = ClusterMeta.BoneBindGlobalMatrix;
					BoneMeta.InvBindGlobalMatrix = BoneMeta.BindGlobalMatrix.GetInverse();
				}
				AddUniqueId(SkinMeta.BoneIds, ClusterMeta.LinkBoneId);
			}
		}
	}

	ClusterMeta.bValid = ClusterMeta.LinkBoneId >= 0 &&
		ClusterMeta.ControlPointInfluenceCount > 0 &&
		ClusterMeta.PositiveWeightCount > 0 &&
		ClusterMeta.LinkNodeId >= 0;

	if (!ClusterMeta.bValid && ClusterMeta.LinkNode)
	{
		UE_LOG("[FBXMetaParser] Invalid cluster. Link=%s LinkBoneId=%d InfluenceCount=%d PositiveWeightCount=%d LinkNodeId=%d",
			ClusterMeta.LinkNodeName.c_str(),
			ClusterMeta.LinkBoneId,
			ClusterMeta.ControlPointInfluenceCount,
			ClusterMeta.PositiveWeightCount,
			ClusterMeta.LinkNodeId);
	}

	SkinMeta.ClusterIds.push_back(ClusterId);
	SkinMeta.TotalInfluenceCount += ClusterMeta.PositiveWeightCount;
	ImportMeta.Clusters.push_back(ClusterMeta);
	return ClusterId;
}

#pragma endregion

// FBX 노드 계층을 바탕으로 BoneMeta parent-child 연결을 보정합니다.
#pragma region Bone Hierarchy

void FFbxMetaParser::EnsureBoneParentChain(int32 BoneId)
{
	LinkBoneToNearestValidParent(BoneId);
}

bool FFbxMetaParser::IsSceneRootNode(FbxNode* Node) const
{
	if (!Node)
	{
		return true;
	}

	auto NodeIt = ImportMeta.NodeToNodeId.find(Node);
	if (NodeIt == ImportMeta.NodeToNodeId.end())
	{
		return false;
	}

	const int32 NodeId = NodeIt->second;
	return IsValidIndex(ImportMeta.Nodes, NodeId) && ImportMeta.Nodes[NodeId].ParentNodeId < 0;
}

bool FFbxMetaParser::CanPromoteNodeToBoneParent(FbxNode* Node) const
{
	if (!Node || IsSceneRootNode(Node))
	{
		return false;
	}

	if (ImportMeta.NodeToNodeId.find(Node) == ImportMeta.NodeToNodeId.end())
	{
		return false;
	}

	FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
	if (!Attribute)
	{
		return true;
	}

	switch (Attribute->GetAttributeType())
	{
	case FbxNodeAttribute::eSkeleton:
	case FbxNodeAttribute::eNull:
		return true;
	case FbxNodeAttribute::eMesh:
	case FbxNodeAttribute::eCamera:
	case FbxNodeAttribute::eCameraStereo:
	case FbxNodeAttribute::eCameraSwitcher:
	case FbxNodeAttribute::eLight:
		return false;
	default:
		return false;
	}
}

void FFbxMetaParser::LinkBoneParentChild(int32 ParentBoneId, int32 ChildBoneId)
{
	if (!IsValidIndex(ImportMeta.Bones, ParentBoneId) ||
		!IsValidIndex(ImportMeta.Bones, ChildBoneId) ||
		ParentBoneId == ChildBoneId)
	{
		return;
	}

	FFbxBoneMeta& ChildBone = ImportMeta.Bones[ChildBoneId];
	if (ChildBone.ParentBoneId >= 0 &&
		ChildBone.ParentBoneId != ParentBoneId &&
		IsValidIndex(ImportMeta.Bones, ChildBone.ParentBoneId))
	{
		RemoveId(ImportMeta.Bones[ChildBone.ParentBoneId].ChildBoneIds, ChildBoneId);
	}

	ChildBone.ParentBoneId = ParentBoneId;
	AddUniqueId(ImportMeta.Bones[ParentBoneId].ChildBoneIds, ChildBoneId);
}

void FFbxMetaParser::LinkBoneToNearestValidParent(int32 BoneId)
{
	if (!IsValidIndex(ImportMeta.Bones, BoneId))
	{
		return;
	}

	FFbxBoneMeta& BoneMeta = ImportMeta.Bones[BoneId];
	if (!BoneMeta.Node)
	{
		return;
	}

	FbxNode* ParentNode = BoneMeta.Node->GetParent();
	while (ParentNode)
	{
		if (IsSceneRootNode(ParentNode))
		{
			return;
		}

		FbxNodeAttribute* Attribute = ParentNode->GetNodeAttribute();
		if (Attribute)
		{
			const FbxNodeAttribute::EType Type = Attribute->GetAttributeType();
			if (Type == FbxNodeAttribute::eMesh ||
				Type == FbxNodeAttribute::eCamera ||
				Type == FbxNodeAttribute::eCameraStereo ||
				Type == FbxNodeAttribute::eCameraSwitcher ||
				Type == FbxNodeAttribute::eLight)
			{
				return;
			}
		}

		int32 ParentBoneId = -1;
		auto ParentBoneIt = ImportMeta.BoneNodeToBoneId.find(ParentNode);
		if (ParentBoneIt != ImportMeta.BoneNodeToBoneId.end())
		{
			ParentBoneId = ParentBoneIt->second;
		}
		else if (CanPromoteNodeToBoneParent(ParentNode))
		{
			ParentBoneId = RegisterBoneNode(ParentNode, false, true);
		}

		if (IsValidIndex(ImportMeta.Bones, ParentBoneId))
		{
			LinkBoneParentChild(ParentBoneId, BoneId);
			LinkBoneToNearestValidParent(ParentBoneId);
			return;
		}

		ParentNode = ParentNode->GetParent();
	}
}

void FFbxMetaParser::BuildRegisteredBoneHierarchyLinks()
{
	for (int32 BoneId = 0; BoneId < static_cast<int32>(ImportMeta.Bones.size()); ++BoneId)
	{
		LinkBoneToNearestValidParent(BoneId);
	}
}

int32 FFbxMetaParser::RegisterBoneNode(FbxNode* Node, bool bReferencedByCluster, bool bInsertedAsParentChain)
{
	if (!Node)
	{
		return -1;
	}

	auto ExistingIt = ImportMeta.BoneNodeToBoneId.find(Node);
	if (ExistingIt != ImportMeta.BoneNodeToBoneId.end())
	{
		FFbxBoneMeta& ExistingBone = ImportMeta.Bones[ExistingIt->second];
		ExistingBone.bReferencedByCluster = ExistingBone.bReferencedByCluster || bReferencedByCluster;
		ExistingBone.bInsertedAsParentChain = ExistingBone.bInsertedAsParentChain || bInsertedAsParentChain;
		return ExistingIt->second;
	}

	const int32 BoneId = static_cast<int32>(ImportMeta.Bones.size());
	FFbxBoneMeta BoneMeta;
	BoneMeta.BoneId = BoneId;
	BoneMeta.Node = Node;
	BoneMeta.Name = FBXUtil::GetNodeName(Node);
	BoneMeta.bReferencedByCluster = bReferencedByCluster;
	BoneMeta.bInsertedAsParentChain = bInsertedAsParentChain;
	BoneMeta.ModelLocalMatrix = FBXUtil::ConvertFbxMatrix(Node->EvaluateLocalTransform());
	BoneMeta.ModelGlobalMatrix = FBXUtil::ConvertFbxMatrix(Node->EvaluateGlobalTransform());
	BoneMeta.BindGlobalMatrix = BoneMeta.ModelGlobalMatrix;
	BoneMeta.InvBindGlobalMatrix = BoneMeta.BindGlobalMatrix.GetInverse();

	auto NodeIt = ImportMeta.NodeToNodeId.find(Node);
	if (NodeIt != ImportMeta.NodeToNodeId.end())
	{
		BoneMeta.NodeId = NodeIt->second;
		if (IsValidIndex(ImportMeta.Nodes, BoneMeta.NodeId))
		{
			BoneMeta.FullPath = ImportMeta.Nodes[BoneMeta.NodeId].FullPath;
		}
	}

	ImportMeta.Bones.push_back(BoneMeta);
	ImportMeta.BoneNodeToBoneId[Node] = BoneId;
	return BoneId;
}

int32 FFbxMetaParser::FindTopRootBone(int32 BoneId) const
{
	if (!IsValidIndex(ImportMeta.Bones, BoneId))
	{
		return -1;
	}

	int32 CurrentId = BoneId;
	while (IsValidIndex(ImportMeta.Bones, ImportMeta.Bones[CurrentId].ParentBoneId))
	{
		CurrentId = ImportMeta.Bones[CurrentId].ParentBoneId;
	}

	return CurrentId;
}

#pragma endregion

// skeleton root 기준으로 bone과 skinned mesh part를 그룹화합니다.
#pragma region Skeleton Grouping

void FFbxMetaParser::BuildSkeletonTables()
{
	TMap<int32, int32> RootBoneIdToSkeletonId;

	for (FFbxSkinMeta& SkinMeta : ImportMeta.Skins)
	{
		if (!SkinMeta.bValid)
		{
			continue;
		}

		int32 RootBoneId = FindSkeletonRootBoneForSkin(SkinMeta.BoneIds);
		bool bSyntheticRoot = false;
		if (RootBoneId < 0)
		{
			UE_LOG("[FBXMetaParser] Failed to find skeleton root. SkinId=%d", SkinMeta.SkinId);

			RootBoneId = static_cast<int32>(ImportMeta.Bones.size());
			FFbxBoneMeta SyntheticRoot;
			SyntheticRoot.BoneId = RootBoneId;
			SyntheticRoot.Name = "SyntheticRoot_" + std::to_string(RootBoneId);
			SyntheticRoot.FullPath = SyntheticRoot.Name;
			SyntheticRoot.bSyntheticRoot = true;
			ImportMeta.Bones.push_back(SyntheticRoot);
			bSyntheticRoot = true;

			for (int32 BoneId : SkinMeta.BoneIds)
			{
				if (!IsValidIndex(ImportMeta.Bones, BoneId))
				{
					continue;
				}

				int32 TopBoneId = BoneId;
				while (IsValidIndex(ImportMeta.Bones, ImportMeta.Bones[TopBoneId].ParentBoneId))
				{
					TopBoneId = ImportMeta.Bones[TopBoneId].ParentBoneId;
				}

				AddUniqueId(ImportMeta.Bones[RootBoneId].ChildBoneIds, TopBoneId);
				ImportMeta.Bones[TopBoneId].ParentBoneId = RootBoneId;
			}
		}

		const int32 SkeletonId = FindOrCreateSkeletonForRoot(
			RootBoneId,
			true,
			!bSyntheticRoot,
			RootBoneIdToSkeletonId);

		SkinMeta.SkeletonId = SkeletonId;
		if (IsValidIndex(ImportMeta.Meshes, SkinMeta.MeshId))
		{
			FFbxMeshMeta& MeshMeta = ImportMeta.Meshes[SkinMeta.MeshId];
			MeshMeta.SkeletonId = SkeletonId;
			if (IsValidIndex(ImportMeta.Skeletons, SkeletonId))
			{
				AddUniqueId(ImportMeta.Skeletons[SkeletonId].SkinnedMeshIds, SkinMeta.MeshId);
				AddUniqueId(ImportMeta.Skeletons[SkeletonId].MeshIds, SkinMeta.MeshId);
			}
		}
	}

	for (int32 BoneId = 0; BoneId < static_cast<int32>(ImportMeta.Bones.size()); ++BoneId)
	{
		const int32 RootBoneId = FindTopRootBone(BoneId);
		if (!IsValidIndex(ImportMeta.Bones, RootBoneId) ||
			RootBoneIdToSkeletonId.find(RootBoneId) != RootBoneIdToSkeletonId.end())
		{
			continue;
		}

		if (!ShouldBuildRigidSkeletonForRoot(RootBoneId))
		{
			continue;
		}

		FindOrCreateSkeletonForRoot(
			RootBoneId,
			false,
			true,
			RootBoneIdToSkeletonId);
	}
}

int32 FFbxMetaParser::FindOrCreateSkeletonForRoot(
	int32 RootBoneId,
	bool bBuiltFromSkinClusters,
	bool bHasSingleRoot,
	TMap<int32, int32>& RootBoneIdToSkeletonId)
{
	if (!IsValidIndex(ImportMeta.Bones, RootBoneId))
	{
		return -1;
	}

	auto ExistingSkeletonIt = RootBoneIdToSkeletonId.find(RootBoneId);
	if (ExistingSkeletonIt != RootBoneIdToSkeletonId.end())
	{
		const int32 ExistingSkeletonId = ExistingSkeletonIt->second;
		if (IsValidIndex(ImportMeta.Skeletons, ExistingSkeletonId))
		{
			FFbxSkeletonMeta& SkeletonMeta = ImportMeta.Skeletons[ExistingSkeletonId];
			SkeletonMeta.bBuiltFromSkinClusters = SkeletonMeta.bBuiltFromSkinClusters || bBuiltFromSkinClusters;
			SkeletonMeta.bHasSingleRoot = SkeletonMeta.bHasSingleRoot && bHasSingleRoot;
		}
		return ExistingSkeletonId;
	}

	const int32 SkeletonId = static_cast<int32>(ImportMeta.Skeletons.size());
	FFbxSkeletonMeta SkeletonMeta;
	SkeletonMeta.SkeletonId = SkeletonId;
	SkeletonMeta.RootBoneId = RootBoneId;
	SkeletonMeta.bValid = true;
	SkeletonMeta.bBuiltFromSkinClusters = bBuiltFromSkinClusters;
	SkeletonMeta.bHasSingleRoot = bHasSingleRoot;

	const FFbxBoneMeta& RootBone = ImportMeta.Bones[RootBoneId];
	SkeletonMeta.RootNodeId = RootBone.NodeId;
	SkeletonMeta.Name = RootBone.Name.empty()
		? "Skeleton_" + std::to_string(SkeletonId)
		: RootBone.Name;

	AddBoneDfs(RootBoneId, SkeletonMeta, SkeletonId);
	ImportMeta.Skeletons.push_back(SkeletonMeta);
	RootBoneIdToSkeletonId[RootBoneId] = SkeletonId;

	UE_LOG("[FBXMetaParser] Built %s skeleton table. SkeletonId=%d RootBoneId=%d RootName=%s Bones=%u",
		bBuiltFromSkinClusters ? "skinned" : "rigid",
		SkeletonId,
		RootBoneId,
		RootBone.Name.c_str(),
		static_cast<uint32>(ImportMeta.Skeletons.back().BoneIds.size()));

	return SkeletonId;
}

void FFbxMetaParser::AddBoneDfs(int32 CurrentBoneId, FFbxSkeletonMeta& SkeletonMeta, uint32 SkeletonId)
{
	if (!IsValidIndex(ImportMeta.Bones, CurrentBoneId) ||
		ContainsId(SkeletonMeta.BoneIds, CurrentBoneId))
	{
		return;
	}

	const int32 SkeletonBoneIndex = static_cast<int32>(SkeletonMeta.BoneIds.size());
	SkeletonMeta.BoneIds.push_back(CurrentBoneId);
	SkeletonMeta.BoneIdToSkeletonBoneIndex[CurrentBoneId] = SkeletonBoneIndex;

	FFbxBoneMeta& BoneMeta = ImportMeta.Bones[CurrentBoneId];
	BoneMeta.SkeletonId = SkeletonId;
	BoneMeta.SkeletonBoneIndex = SkeletonBoneIndex;
	if (BoneMeta.Node)
	{
		SkeletonMeta.BoneNodeToSkeletonBoneIndex[BoneMeta.Node] = SkeletonBoneIndex;
	}

	for (int32 ChildBoneId : BoneMeta.ChildBoneIds)
	{
		AddBoneDfs(ChildBoneId, SkeletonMeta, SkeletonId);
	}
};

int32 FFbxMetaParser::FindSkeletonRootBoneForSkin(const TArray<int32>& BoneIds) const
{
	int32 SharedRootBoneId = -1;
	for (int32 BoneId : BoneIds)
	{
		const int32 RootBoneId = FindTopRootBone(BoneId);
		if (!IsValidIndex(ImportMeta.Bones, RootBoneId))
		{
			continue;
		}

		if (SharedRootBoneId < 0)
		{
			SharedRootBoneId = RootBoneId;
			continue;
		}

		if (SharedRootBoneId != RootBoneId)
		{
			return -1;
		}
	}

	return SharedRootBoneId;
}

bool FFbxMetaParser::ShouldBuildRigidSkeletonForRoot(int32 RootBoneId) const
{
	if (!IsValidIndex(ImportMeta.Bones, RootBoneId))
	{
		return false;
	}

	for (const FFbxMeshMeta& MeshMeta : ImportMeta.Meshes)
	{
		if (MeshMeta.bHasSkin || !MeshMeta.Node)
		{
			continue;
		}

		const int32 AttachedBoneId = FindNearestParentBoneIdForNode(MeshMeta.Node);
		if (!IsValidIndex(ImportMeta.Bones, AttachedBoneId))
		{
			continue;
		}

		if (FindTopRootBone(AttachedBoneId) == RootBoneId)
		{
			return true;
		}
	}

	return false;
}

int32 FFbxMetaParser::FindSkeletonIdForBone(int32 BoneId) const
{
	if (!IsValidIndex(ImportMeta.Bones, BoneId))
	{
		return -1;
	}

	const int32 BoneSkeletonId = ImportMeta.Bones[BoneId].SkeletonId;
	if (IsValidIndex(ImportMeta.Skeletons, BoneSkeletonId))
	{
		return BoneSkeletonId;
	}

	for (const FFbxSkeletonMeta& SkeletonMeta : ImportMeta.Skeletons)
	{
		if (ContainsId(SkeletonMeta.BoneIds, BoneId))
		{
			return SkeletonMeta.SkeletonId;
		}
	}

	return -1;
}

#pragma endregion

// bone 아래 parent 된 non-skinned mesh를 skeletal assembly에 붙입니다.
#pragma region Rigid Attached Meshes

void FFbxMetaParser::AttachRigidMeshesToSkeletons()
{
	for (FFbxMeshMeta& MeshMeta : ImportMeta.Meshes)
	{
		if (MeshMeta.bHasSkin)
		{
			continue;
		}

		MeshMeta.AttachedSkeletonId = -1;
		MeshMeta.AttachedBoneId = -1;
		MeshMeta.bAttachedToSkeleton = false;
		MeshMeta.bRigidAttachedCandidate = false;
		MeshMeta.bIndependentStaticCandidate = false;

		const int32 AttachedBoneId = FindNearestParentBoneIdForNode(MeshMeta.Node);
		const int32 AttachedSkeletonId = FindSkeletonIdForBone(AttachedBoneId);
		if (!IsValidIndex(ImportMeta.Bones, AttachedBoneId) ||
			!IsValidIndex(ImportMeta.Skeletons, AttachedSkeletonId))
		{
			MeshMeta.bIndependentStaticCandidate = true;
			continue;
		}

		MeshMeta.bAttachedToSkeleton = true;
		MeshMeta.bRigidAttachedCandidate = true;
		MeshMeta.bIndependentStaticCandidate = false;
		MeshMeta.AttachedBoneId = AttachedBoneId;
		MeshMeta.AttachedSkeletonId = AttachedSkeletonId;
		MeshMeta.SkeletonId = AttachedSkeletonId;

		FFbxSkeletonMeta& SkeletonMeta = ImportMeta.Skeletons[AttachedSkeletonId];
		AddUniqueId(SkeletonMeta.RigidAttachedMeshIds, MeshMeta.MeshId);
		AddUniqueId(SkeletonMeta.MeshIds, MeshMeta.MeshId);

		UE_LOG("[FBXMetaParser] Rigid mesh attached to skeleton. MeshId=%d Node=%s BoneId=%d SkeletonId=%d",
			MeshMeta.MeshId,
			MeshMeta.SourceNodePath.c_str(),
			AttachedBoneId,
			AttachedSkeletonId);
	}
}

int32 FFbxMetaParser::FindNearestParentBoneIdForNode(FbxNode* Node) const
{
	if (!Node)
	{
		return -1;
	}

	FbxNode* ParentNode = Node->GetParent();
	while (ParentNode)
	{
		if (IsSceneRootNode(ParentNode))
		{
			return -1;
		}

		FbxNodeAttribute* Attribute = ParentNode->GetNodeAttribute();
		if (Attribute)
		{
			const FbxNodeAttribute::EType Type = Attribute->GetAttributeType();
			if (Type == FbxNodeAttribute::eMesh ||
				Type == FbxNodeAttribute::eCamera ||
				Type == FbxNodeAttribute::eCameraStereo ||
				Type == FbxNodeAttribute::eCameraSwitcher ||
				Type == FbxNodeAttribute::eLight)
			{
				return -1;
			}
		}

		auto BoneIt = ImportMeta.BoneNodeToBoneId.find(ParentNode);
		if (BoneIt != ImportMeta.BoneNodeToBoneId.end())
		{
			return BoneIt->second;
		}

		ParentNode = ParentNode->GetParent();
	}

	return -1;
}

#pragma endregion

// 최종 import classification 배열을 생성합니다.
#pragma region Mesh Classification

void FFbxMetaParser::ClassifyMeshes()
{
	ImportMeta.StaticMeshIds.clear();
	ImportMeta.SkeletalMeshIds.clear();
	ImportMeta.RigidAttachedMeshIds.clear();
	ImportMeta.IndependentStaticMeshIds.clear();

	for (FFbxMeshMeta& MeshMeta : ImportMeta.Meshes)
	{
		MeshMeta.bStaticCandidate = false;
		MeshMeta.bSkeletalCandidate = false;
		MeshMeta.bIndependentStaticCandidate = false;

		if (MeshMeta.bHasSkin)
		{
			if (IsValidIndex(ImportMeta.Skins, MeshMeta.PrimarySkinId) &&
				IsValidIndex(ImportMeta.Skeletons, MeshMeta.SkeletonId) &&
				HasValidCluster(ImportMeta.Skins[MeshMeta.PrimarySkinId], ImportMeta))
			{
				MeshMeta.bSkeletalCandidate = true;
				AddUniqueId(ImportMeta.SkeletalMeshIds, MeshMeta.MeshId);
				AddUniqueId(ImportMeta.Skeletons[MeshMeta.SkeletonId].SkinnedMeshIds, MeshMeta.MeshId);
				AddUniqueId(ImportMeta.Skeletons[MeshMeta.SkeletonId].MeshIds, MeshMeta.MeshId);
				continue;
			}

			UE_LOG("[FBXMetaParser] Mesh has skin but no valid cluster; skipping classification. MeshId=%d Node=%s",
				MeshMeta.MeshId,
				MeshMeta.SourceNodePath.c_str());
			UE_LOG("[FBXMetaParser] Mesh was not classified as static or skeletal. MeshId=%d Node=%s",
				MeshMeta.MeshId,
				MeshMeta.SourceNodePath.c_str());
			continue;
		}

		if (MeshMeta.bAttachedToSkeleton && IsValidIndex(ImportMeta.Skeletons, MeshMeta.AttachedSkeletonId))
		{
			MeshMeta.bRigidAttachedCandidate = true;
			MeshMeta.bStaticCandidate = false;
			MeshMeta.bSkeletalCandidate = true;
			MeshMeta.bIndependentStaticCandidate = false;
			AddUniqueId(ImportMeta.RigidAttachedMeshIds, MeshMeta.MeshId);
			AddUniqueId(ImportMeta.Skeletons[MeshMeta.AttachedSkeletonId].RigidAttachedMeshIds, MeshMeta.MeshId);
			AddUniqueId(ImportMeta.Skeletons[MeshMeta.AttachedSkeletonId].MeshIds, MeshMeta.MeshId);
			continue;
		}

		MeshMeta.bAttachedToSkeleton = false;
		MeshMeta.bRigidAttachedCandidate = false;
		MeshMeta.bIndependentStaticCandidate = true;
		MeshMeta.bStaticCandidate = true;
		MeshMeta.AttachedBoneId = -1;
		MeshMeta.AttachedSkeletonId = -1;
		if (MeshMeta.SkeletonId < 0 || !IsValidIndex(ImportMeta.Skeletons, MeshMeta.SkeletonId))
		{
			MeshMeta.SkeletonId = -1;
		}
		AddUniqueId(ImportMeta.IndependentStaticMeshIds, MeshMeta.MeshId);
		AddUniqueId(ImportMeta.StaticMeshIds, MeshMeta.MeshId);
	}
}

#pragma endregion

// 생성된 FBX import meta의 ID와 양방향 링크 무결성을 검증합니다.
#pragma region Validation

bool FFbxMetaParser::ValidateFbxMeta() const
{
	bool bValid = true;

	for (const FFbxMeshMeta& MeshMeta : ImportMeta.Meshes)
	{
		if (!IsValidIndex(ImportMeta.Nodes, MeshMeta.NodeId))
		{
			UE_LOG("[FBXMetaParser] Validation failed: invalid MeshMeta.NodeId. MeshId=%d", MeshMeta.MeshId);
			bValid = false;
		}
	}

	for (int32 MeshId : ImportMeta.SkeletalMeshIds)
	{
		if (!IsValidIndex(ImportMeta.Meshes, MeshId))
		{
			UE_LOG("[FBXMetaParser] Validation failed: invalid skeletal MeshId=%d", MeshId);
			bValid = false;
			continue;
		}

		const FFbxMeshMeta& MeshMeta = ImportMeta.Meshes[MeshId];
		if (!IsValidIndex(ImportMeta.Skins, MeshMeta.PrimarySkinId) ||
			!IsValidIndex(ImportMeta.Skeletons, MeshMeta.SkeletonId))
		{
			UE_LOG("[FBXMetaParser] Validation failed: skeletal mesh missing primary skin or skeleton. MeshId=%d",
				MeshId);
			bValid = false;
		}
	}

	for (int32 MeshId : ImportMeta.RigidAttachedMeshIds)
	{
		if (!IsValidIndex(ImportMeta.Meshes, MeshId))
		{
			UE_LOG("[FBXMetaParser] Validation failed: invalid rigid attached MeshId=%d", MeshId);
			bValid = false;
			continue;
		}

		const FFbxMeshMeta& MeshMeta = ImportMeta.Meshes[MeshId];
		if (MeshMeta.bHasSkin)
		{
			UE_LOG("[FBXMetaParser] Validation failed: rigid attached mesh has skin. MeshId=%d", MeshId);
			bValid = false;
		}
		if (!IsValidIndex(ImportMeta.Bones, MeshMeta.AttachedBoneId))
		{
			UE_LOG("[FBXMetaParser] Validation failed: rigid attached mesh has invalid AttachedBoneId. MeshId=%d BoneId=%d",
				MeshId,
				MeshMeta.AttachedBoneId);
			bValid = false;
		}
		if (!IsValidIndex(ImportMeta.Skeletons, MeshMeta.AttachedSkeletonId))
		{
			UE_LOG("[FBXMetaParser] Validation failed: rigid attached mesh has invalid AttachedSkeletonId. MeshId=%d SkeletonId=%d",
				MeshId,
				MeshMeta.AttachedSkeletonId);
			bValid = false;
			continue;
		}

		const FFbxSkeletonMeta& SkeletonMeta = ImportMeta.Skeletons[MeshMeta.AttachedSkeletonId];
		if (!ContainsId(SkeletonMeta.RigidAttachedMeshIds, MeshId) ||
			!ContainsId(SkeletonMeta.MeshIds, MeshId))
		{
			UE_LOG("[FBXMetaParser] Validation failed: rigid attached mesh missing from skeleton mesh lists. MeshId=%d SkeletonId=%d",
				MeshId,
				MeshMeta.AttachedSkeletonId);
			bValid = false;
		}
	}

	for (int32 MeshId : ImportMeta.IndependentStaticMeshIds)
	{
		if (!IsValidIndex(ImportMeta.Meshes, MeshId))
		{
			UE_LOG("[FBXMetaParser] Validation failed: invalid independent static MeshId=%d", MeshId);
			bValid = false;
			continue;
		}

		const FFbxMeshMeta& MeshMeta = ImportMeta.Meshes[MeshId];
		if (MeshMeta.AttachedSkeletonId >= 0)
		{
			UE_LOG("[FBXMetaParser] Validation failed: independent static mesh has AttachedSkeletonId. MeshId=%d SkeletonId=%d",
				MeshId,
				MeshMeta.AttachedSkeletonId);
			bValid = false;
		}
		if (ContainsId(ImportMeta.RigidAttachedMeshIds, MeshId))
		{
			UE_LOG("[FBXMetaParser] Validation failed: mesh classified as both independent static and rigid attached. MeshId=%d",
				MeshId);
			bValid = false;
		}
	}

	for (const FFbxSkinMeta& SkinMeta : ImportMeta.Skins)
	{
		for (int32 ClusterId : SkinMeta.ClusterIds)
		{
			if (!IsValidIndex(ImportMeta.Clusters, ClusterId))
			{
				UE_LOG("[FBXMetaParser] Validation failed: invalid ClusterId=%d in SkinId=%d",
					ClusterId,
					SkinMeta.SkinId);
				bValid = false;
			}
		}
	}

	for (const FFbxClusterMeta& ClusterMeta : ImportMeta.Clusters)
	{
		if (ClusterMeta.bValid && !IsValidIndex(ImportMeta.Bones, ClusterMeta.LinkBoneId))
		{
			UE_LOG("[FBXMetaParser] Validation failed: valid cluster has invalid LinkBoneId. ClusterId=%d",
				ClusterMeta.ClusterId);
			bValid = false;
		}
	}

	for (const FFbxSkeletonMeta& SkeletonMeta : ImportMeta.Skeletons)
	{
		if (!IsValidIndex(ImportMeta.Bones, SkeletonMeta.RootBoneId))
		{
			UE_LOG("[FBXMetaParser] Validation failed: invalid Skeleton RootBoneId. SkeletonId=%d",
				SkeletonMeta.SkeletonId);
			bValid = false;
		}

		if (SkeletonMeta.MeshIds.empty())
		{
			UE_LOG("[FBXMetaParser] Skeleton has no meshes. SkeletonId=%d", SkeletonMeta.SkeletonId);
		}
	}

	for (const FFbxBoneMeta& BoneMeta : ImportMeta.Bones)
	{
		for (int32 ChildBoneId : BoneMeta.ChildBoneIds)
		{
			if (!IsValidIndex(ImportMeta.Bones, ChildBoneId))
			{
				UE_LOG("[FBXMetaParser] Validation failed: invalid child bone. BoneId=%d ChildBoneId=%d",
					BoneMeta.BoneId,
					ChildBoneId);
				bValid = false;
				continue;
			}

			if (ImportMeta.Bones[ChildBoneId].ParentBoneId != BoneMeta.BoneId)
			{
				UE_LOG("[FBXMetaParser] Bone parent-child relation mismatch. ParentBoneId=%d ChildBoneId=%d",
					BoneMeta.BoneId,
					ChildBoneId);
				bValid = false;
			}
		}
	}

	return bValid;
}

#pragma endregion
