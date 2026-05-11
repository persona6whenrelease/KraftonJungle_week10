#include "Mesh/FBX/FBXManager.h"

#include "Core/Log.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Runtime/Engine.h"
#include "Materials/MaterialManager.h"
#include "Mesh/FBX/FBXImporter.h"
#include "Mesh/FBX/FBXSceneAsset.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/StaticMesh.h"
#include "Object/Object.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

TMap<FString, USkeletalMesh*> FFBXManager::SkeletalMeshCache;
TMap<FString, UFBXSceneAsset*> FFBXManager::FbxSceneCache;
TArray<FMeshAssetListItem> FFBXManager::AvailableSkeletalMeshFiles;
TArray<FMeshAssetListItem> FFBXManager::AvailableFbxFiles;

namespace
{
	constexpr uint32 FBXCacheMagic = 0x58424653u; // "SFBX"
	constexpr uint32 FBXCacheVersion = 4u;

	struct FFBXCacheHeader
	{
		uint32 Magic = 0;
		uint32 Version = 0;
		FString SourcePath;
		int64 SourceTimestamp = 0;
	};

	static void EnsureSkeletalMeshCacheDirExists()
	{
		static bool bCreated = false;
		if (!bCreated)
		{
			FPaths::CreateDir(FPaths::RootDir() + L"Asset\\SkeletalMeshCache\\");
			bCreated = true;
		}
	}

	static std::wstring ResolveDiskPath(const FString& Path)
	{
		std::wstring DiskPath;
		FString ResolveError;
		if (!FPaths::TryResolvePackagePath(Path, DiskPath, &ResolveError))
		{
			DiskPath = FPaths::ToWide(Path);
		}
		return DiskPath;
	}

	static FString NormalizePackagePath(const FString& Path)
	{
		std::filesystem::path Normalized(FPaths::ToWide(Path));
		return FPaths::ToUtf8(Normalized.lexically_normal().generic_wstring());
	}

	static std::wstring ToLower(std::wstring Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(),
			[](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
		return Text;
	}

	static int64 GetFileTimestamp(const FString& Path)
	{
		const std::filesystem::path DiskPath(ResolveDiskPath(Path));
		if (!std::filesystem::exists(DiskPath))
		{
			return 0;
		}

		return static_cast<int64>(std::filesystem::last_write_time(DiskPath).time_since_epoch().count());
	}

	static bool IsFbxPath(const FString& Path)
	{
		std::filesystem::path FsPath(FPaths::ToWide(Path));
		return ToLower(FsPath.extension().wstring()) == L".fbx";
	}

	static bool IsBinPath(const FString& Path)
	{
		std::filesystem::path FsPath(FPaths::ToWide(Path));
		return ToLower(FsPath.extension().wstring()) == L".bin";
	}

	static void SerializeCacheHeader(FArchive& Ar, FFBXCacheHeader& Header)
	{
		Ar << Header.Magic;
		Ar << Header.Version;
		Ar << Header.SourcePath;
		Ar << Header.SourceTimestamp;
	}

	static bool ReadCacheHeader(const FString& BinPath, FFBXCacheHeader& OutHeader)
	{
		FWindowsBinReader Reader(BinPath);
		if (!Reader.IsValid())
		{
			return false;
		}

		SerializeCacheHeader(Reader, OutHeader);
		return OutHeader.Magic == FBXCacheMagic && OutHeader.Version == FBXCacheVersion;
	}

	static bool WriteCacheHeader(FWindowsBinWriter& Writer, const FString& SourcePath, int64 SourceTimestamp)
	{
		if (!Writer.IsValid())
		{
			return false;
		}

		FFBXCacheHeader Header;
		Header.Magic = FBXCacheMagic;
		Header.Version = FBXCacheVersion;
		Header.SourcePath = NormalizePackagePath(SourcePath);
		Header.SourceTimestamp = SourceTimestamp;
		SerializeCacheHeader(Writer, Header);
		return true;
	}

	static bool IsCacheUsableForRequest(const FString& RequestedPath, const FFBXCacheHeader& Header)
	{
		if (IsBinPath(RequestedPath))
		{
			return true;
		}

		if (!IsFbxPath(RequestedPath))
		{
			return false;
		}

		if (NormalizePackagePath(Header.SourcePath) != NormalizePackagePath(RequestedPath))
		{
			return false;
		}

		const int64 CurrentSourceTimestamp = GetFileTimestamp(RequestedPath);
		return CurrentSourceTimestamp != 0 && Header.SourceTimestamp >= CurrentSourceTimestamp;
	}

	static TArray<FMeshMaterial> BuildMaterialsFromSections(const FSkeletalMesh& Mesh)
	{
		TArray<FMeshMaterial> Materials;
		TSet<FString> AddedSlotNames;
		UMaterial* FallbackMaterial = FMaterialManager::Get().GetOrCreateMaterial("None");

		for (const FMeshSection& Section : Mesh.Sections)
		{
			FString SlotName = Section.MaterialSlotName.empty() ? "None" : Section.MaterialSlotName;
			if (AddedSlotNames.find(SlotName) != AddedSlotNames.end())
			{
				continue;
			}

			FMeshMaterial Material;
			Material.MaterialSlotName = SlotName;
			Material.MaterialInterface = FallbackMaterial;
			Materials.push_back(Material);
			AddedSlotNames.insert(std::move(SlotName));
		}

		if (Materials.empty())
		{
			FMeshMaterial Material;
			Material.MaterialSlotName = "None";
			Material.MaterialInterface = FallbackMaterial;
			Materials.push_back(Material);
		}

		return Materials;
	}

	static void AddFilesWithExtension(
		const std::filesystem::path& Root,
		const std::wstring& Extension,
		TArray<FMeshAssetListItem>& OutFiles)
	{
		OutFiles.clear();
		if (!std::filesystem::exists(Root))
		{
			return;
		}

		const std::filesystem::path ProjectRoot(FPaths::RootDir());
		for (const auto& Entry : std::filesystem::recursive_directory_iterator(Root))
		{
			if (!Entry.is_regular_file())
			{
				continue;
			}

			const std::filesystem::path& Path = Entry.path();
			if (ToLower(Path.extension().wstring()) != Extension)
			{
				continue;
			}

			FMeshAssetListItem Item;
			Item.DisplayName = FPaths::ToUtf8(Path.filename().wstring());
			if (Extension == L".bin")
			{
				Item.DisplayName = FPaths::ToUtf8(Path.stem().wstring());
			}
			Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
			OutFiles.push_back(std::move(Item));
		}
	}
}

FString FFBXManager::GetBinaryFilePath(const FString& OriginalPath)
{
	if (IsBinPath(OriginalPath))
	{
		return OriginalPath;
	}

	EnsureSkeletalMeshCacheDirExists();

	std::wstring OriginalDiskPath;
	FString ResolveError;
	const bool bResolvedOriginal = FPaths::TryResolvePackagePath(OriginalPath, OriginalDiskPath, &ResolveError);
	std::filesystem::path SrcPath(bResolvedOriginal ? OriginalDiskPath : FPaths::ToWide(OriginalPath));

	std::filesystem::path RelPath = std::filesystem::path(L"Asset\\SkeletalMeshCache") / SrcPath.stem();
	RelPath += L".bin";
	return FPaths::ToUtf8(RelPath.generic_wstring());
}

USkeletalMesh* FFBXManager::LoadSkeletalMesh(const FString& PathFileName)
{
	if (PathFileName.empty() || PathFileName == "None")
	{
		return nullptr;
	}

	const FString CacheKey = GetBinaryFilePath(PathFileName);
	auto It = SkeletalMeshCache.find(CacheKey);
	if (It != SkeletalMeshCache.end())
	{
		return It->second;
	}

	const FString BinPath = CacheKey;
	const std::filesystem::path BinDiskPath(ResolveDiskPath(BinPath));
	bool bNeedRebuild = true;

	USkeletalMesh* SkeletalMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();

	if (std::filesystem::exists(BinDiskPath))
	{
		FFBXCacheHeader Header;
		if (ReadCacheHeader(BinPath, Header) && IsCacheUsableForRequest(PathFileName, Header))
		{
			FWindowsBinReader Reader(BinPath);
			if (Reader.IsValid())
			{
				SerializeCacheHeader(Reader, Header);
				SkeletalMesh->Serialize(Reader);
				if (FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset())
				{
					Asset->PathFileName = BinPath;
					if (!Asset->bBoundsValid)
					{
						Asset->CacheBounds();
					}
					bNeedRebuild = false;
				}
			}
		}
	}

	if (bNeedRebuild)
	{
		if (!IsFbxPath(PathFileName))
		{
			UE_LOG("[FBXManager] Cannot rebuild skeletal mesh cache without an FBX source: %s", PathFileName.c_str());
			UObjectManager::Get().DestroyObject(SkeletalMesh);
			return nullptr;
		}

		FFBXAsset ImportedAsset;
		FBXImporter Importer;
		if (!Importer.ImportFbxAsset(PathFileName, ImportedAsset) ||
			ImportedAsset.SkeletalMeshes.empty())
		{
			UE_LOG("[FBXManager] New FBX import failed or produced no skeletal meshes: %s", PathFileName.c_str());
			UObjectManager::Get().DestroyObject(SkeletalMesh);
			return nullptr;
		}

		UE_LOG("[FBXManager] Imported FBX via new importer. Path=%s SkeletalMeshes=%u StaticMeshes=%u",
			PathFileName.c_str(),
			static_cast<uint32>(ImportedAsset.SkeletalMeshes.size()),
			static_cast<uint32>(ImportedAsset.StaticMeshes.size()));

		if (ImportedAsset.SkeletalMeshes.size() > 1)
		{
			UE_LOG("[FBXManager] FBX produced multiple skeletal meshes. Using first for drag-drop preview. Path=%s Count=%u",
				PathFileName.c_str(),
				static_cast<uint32>(ImportedAsset.SkeletalMeshes.size()));
		}

		FSkeletalMesh* ImportedMesh = new FSkeletalMesh(std::move(ImportedAsset.SkeletalMeshes[0]));
		ImportedMesh->PathFileName = BinPath;
		if (!ImportedMesh->bBoundsValid)
		{
			ImportedMesh->CacheBounds();
		}

		const bool bUseImportedSkeletalMaterials = !ImportedAsset.SkeletalMaterials.empty();
		TArray<FMeshMaterial> Materials = bUseImportedSkeletalMaterials
			? std::move(ImportedAsset.SkeletalMaterials)
			: BuildMaterialsFromSections(*ImportedMesh);
		UE_LOG("[FBXManager] Skeletal material source. Path=%s Source=%s Count=%u",
			PathFileName.c_str(),
			bUseImportedSkeletalMaterials ? "ImportedAsset.SkeletalMaterials" : "BuildMaterialsFromSections",
			static_cast<uint32>(Materials.size()));
		SkeletalMesh->SetMaterials(std::move(Materials));
		SkeletalMesh->SetSkeletalMeshAsset(ImportedMesh);

		FWindowsBinWriter Writer(BinPath);
		if (WriteCacheHeader(Writer, PathFileName, GetFileTimestamp(PathFileName)))
		{
			SkeletalMesh->Serialize(Writer);
		}
	}

	SkeletalMeshCache[CacheKey] = SkeletalMesh;
	ScanSkeletalMeshAssets();
	return SkeletalMesh;
}

UFBXSceneAsset* FFBXManager::LoadFbxScene(const FString& PathFileName)
{
	if (PathFileName.empty() || PathFileName == "None" || !IsFbxPath(PathFileName))
	{
		return nullptr;
	}

	const FString CacheKey = NormalizePackagePath(PathFileName);
	auto It = FbxSceneCache.find(CacheKey);
	if (It != FbxSceneCache.end())
	{
		return It->second;
	}

	FFBXAsset ImportedAsset;
	FBXImporter Importer;
	if (!Importer.ImportFbxAsset(PathFileName, ImportedAsset) ||
		ImportedAsset.SceneComponents.empty())
	{
		UE_LOG("[FBXManager] FBX scene import failed or produced no scene components: %s",
			PathFileName.c_str());
		return nullptr;
	}

	UFBXSceneAsset* SceneAsset = UObjectManager::Get().CreateObject<UFBXSceneAsset>();
	SceneAsset->SetSourcePath(PathFileName);

	ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
	for (int32 StaticMeshIndex = 0; StaticMeshIndex < static_cast<int32>(ImportedAsset.StaticMeshes.size()); ++StaticMeshIndex)
	{
		UStaticMesh* StaticMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
		TArray<FStaticMaterial> Materials;
		if (StaticMeshIndex < static_cast<int32>(ImportedAsset.StaticMeshMaterials.size()))
		{
			Materials = std::move(ImportedAsset.StaticMeshMaterials[StaticMeshIndex]);
		}
		StaticMesh->SetStaticMaterials(std::move(Materials));

		FStaticMesh* MeshAsset = new FStaticMesh(std::move(ImportedAsset.StaticMeshes[StaticMeshIndex]));
		StaticMesh->SetStaticMeshAsset(MeshAsset);
		if (Device)
		{
			StaticMesh->InitResources(Device);
		}
		SceneAsset->AddStaticMesh(StaticMesh);
	}

	for (int32 SkeletalMeshIndex = 0; SkeletalMeshIndex < static_cast<int32>(ImportedAsset.SkeletalMeshes.size()); ++SkeletalMeshIndex)
	{
		USkeletalMesh* SkeletalMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
		TArray<FMeshMaterial> Materials;
		if (SkeletalMeshIndex < static_cast<int32>(ImportedAsset.SkeletalMeshMaterials.size()))
		{
			Materials = std::move(ImportedAsset.SkeletalMeshMaterials[SkeletalMeshIndex]);
		}
		SkeletalMesh->SetMaterials(std::move(Materials));
		SkeletalMesh->SetSkeletalMeshAsset(new FSkeletalMesh(std::move(ImportedAsset.SkeletalMeshes[SkeletalMeshIndex])));
		SceneAsset->AddSkeletalMesh(SkeletalMesh);
	}

	SceneAsset->SetSceneComponents(std::move(ImportedAsset.SceneComponents));
	FbxSceneCache[CacheKey] = SceneAsset;

	UE_LOG("[FBXManager] Loaded FBX scene. Path=%s StaticMeshes=%u SkeletalMeshes=%u Components=%u",
		PathFileName.c_str(),
		static_cast<uint32>(SceneAsset->GetStaticMeshes().size()),
		static_cast<uint32>(SceneAsset->GetSkeletalMeshes().size()),
		static_cast<uint32>(SceneAsset->GetSceneComponents().size()));

	return SceneAsset;
}

void FFBXManager::ScanSkeletalMeshAssets()
{
	AddFilesWithExtension(
		std::filesystem::path(FPaths::RootDir()) / L"Asset\\SkeletalMeshCache\\",
		L".bin",
		AvailableSkeletalMeshFiles);
}

const TArray<FMeshAssetListItem>& FFBXManager::GetAvailableSkeletalMeshFiles()
{
	return AvailableSkeletalMeshFiles;
}

void FFBXManager::ScanFbxSourceFiles()
{
	AddFilesWithExtension(
		std::filesystem::path(FPaths::RootDir()) / L"Data\\",
		L".fbx",
		AvailableFbxFiles);
}

const TArray<FMeshAssetListItem>& FFBXManager::GetAvailableFbxSourceFiles()
{
	return AvailableFbxFiles;
}

void FFBXManager::ReleaseAllGPU()
{
	SkeletalMeshCache.clear();
	FbxSceneCache.clear();
}
