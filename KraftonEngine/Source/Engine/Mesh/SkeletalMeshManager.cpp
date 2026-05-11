#include "Mesh/SkeletalMeshManager.h"

#include "Core/Log.h"
#include "Mesh/FbxImporter.h"
#include "Mesh/SkeletalMesh.h"
#include "Object/ObjectFactory.h"
#include "Platform/Paths.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

TMap<FString, USkeletalMesh*> FSkeletalMeshManager::SkeletalMeshCache;
TArray<FSkeletalMeshAssetListItem> FSkeletalMeshManager::AvailableMeshFiles;

void FSkeletalMeshManager::ScanSkeletalMeshAssets()
{
	AvailableMeshFiles.clear();

	const std::filesystem::path DataRoot = FPaths::RootDir() + L"Data\\";
	if (!std::filesystem::exists(DataRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(DataRoot))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		const std::filesystem::path& Path = Entry.path();
		std::wstring Ext = Path.extension().wstring();
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
		if (Ext != L".fbx")
		{
			continue;
		}

		FSkeletalMeshAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Path.stem().wstring());
		Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
		AvailableMeshFiles.push_back(std::move(Item));
	}

	std::sort(AvailableMeshFiles.begin(), AvailableMeshFiles.end(),
		[](const FSkeletalMeshAssetListItem& A, const FSkeletalMeshAssetListItem& B)
		{
			return A.DisplayName < B.DisplayName;
		});

	for (FSkeletalMeshAssetListItem& Item : AvailableMeshFiles)
	{
		const FString BaseDisplayName = Item.DisplayName;
		const bool bHasDuplicateName = std::any_of(
			AvailableMeshFiles.begin(),
			AvailableMeshFiles.end(),
			[&Item, &BaseDisplayName](const FSkeletalMeshAssetListItem& Other)
			{
				return &Other != &Item && Other.DisplayName == BaseDisplayName;
			}
		);

		if (bHasDuplicateName)
		{
			Item.DisplayName = BaseDisplayName + " (" + Item.FullPath + ")";
		}
	}
}

const TArray<FSkeletalMeshAssetListItem>& FSkeletalMeshManager::GetAvailableMeshFiles()
{
	if (AvailableMeshFiles.empty())
	{
		ScanSkeletalMeshAssets();
	}

	return AvailableMeshFiles;
}

USkeletalMesh* FSkeletalMeshManager::LoadFbxSkeletalMesh(
	const FString& PathFileName,
	ID3D11Device* InDevice
)
{
	(void)InDevice;

	if (PathFileName.empty() || PathFileName == "None")
	{
		return nullptr;
	}

	auto Found = SkeletalMeshCache.find(PathFileName);

	if (Found != SkeletalMeshCache.end())
	{
		return Found->second;
	}

	auto NewMeshAsset = new FSkeletalMesh();
	TArray<FStaticMaterial> ParsedMaterials;

	if (!FFbxImporter::ImportSkeletalMesh(PathFileName, *NewMeshAsset, ParsedMaterials))
	{
		UE_LOG("[SkeletalMesh] Failed to import FBX: %s", PathFileName.c_str());
		delete NewMeshAsset;
		return nullptr;
	}

	USkeletalMesh* NewMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
	NewMesh->SetSkeletalMeshAsset(NewMeshAsset);
	NewMesh->SetStaticMaterials(std::move(ParsedMaterials));

	SkeletalMeshCache[PathFileName] = NewMesh;
	ScanSkeletalMeshAssets();

	UE_LOG(
		"[SkeletalMesh] Loaded FBX: %s, Vertices=%zu, Indices=%zu, Bones=%zu",
		PathFileName.c_str(),
		NewMeshAsset->Vertices.size(),
		NewMeshAsset->Indices.size(),
		NewMeshAsset->Bones.size()
	);

	return NewMesh;
}

void FSkeletalMeshManager::ReleaseAll()
{
	for (auto& Pair : SkeletalMeshCache)
	{
		if (Pair.second)
		{
			UObjectManager::Get().DestroyObject(Pair.second);
		}
	}

	SkeletalMeshCache.clear();
}
