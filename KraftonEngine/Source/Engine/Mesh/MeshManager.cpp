#include "Mesh/MeshManager.h"

#include "Engine/Platform/Paths.h"
#include "Mesh/FBX/FBXManager.h"
#include "Mesh/ObjManager.h"

#include <filesystem>

namespace
{
	void EnsureFbxSceneCacheDirExists()
	{
		static bool bCreated = false;
		if (!bCreated)
		{
			FPaths::CreateDir(FPaths::RootDir() + L"Asset\\FBXSceneCache\\");
			bCreated = true;
		}
	}
}

bool FMeshManager::IsFbxStaticMeshReference(const FString& PathFileName)
{
	return PathFileName.find("#Mesh_") != FString::npos;
}

bool FMeshManager::IsFbxSkeletalMeshReference(const FString& PathFileName)
{
	return PathFileName.find("#Skeleton_") != FString::npos;
}

FString FMeshManager::GetObjBinaryFilePath(const FString& OriginalPath)
{
	return FObjManager::GetBinaryFilePath(OriginalPath);
}

FString FMeshManager::GetFbxSceneCacheFilePath(const FString& SourcePath)
{
	EnsureFbxSceneCacheDirExists();

	std::wstring SourceDiskPath;
	FString ResolveError;
	const bool bResolvedSource = FPaths::TryResolvePackagePath(SourcePath, SourceDiskPath, &ResolveError);
	const std::filesystem::path SrcPath(bResolvedSource ? SourceDiskPath : FPaths::ToWide(SourcePath));

	std::filesystem::path RelPath = std::filesystem::path(L"Asset\\FBXSceneCache") / SrcPath.stem();
	RelPath += L".fbxscene.bin";
	return FPaths::ToUtf8(RelPath.generic_wstring());
}

UStaticMesh* FMeshManager::LoadStaticMesh(const FString& PathFileName, ID3D11Device* InDevice)
{
	if (PathFileName.empty() || PathFileName == "None")
	{
		return nullptr;
	}

	if (IsFbxStaticMeshReference(PathFileName))
	{
		return FFBXManager::LoadStaticMeshFromFbxSceneReference(PathFileName);
	}

	return FObjManager::LoadObjStaticMesh(PathFileName, InDevice);
}

UStaticMesh* FMeshManager::LoadObjStaticMesh(const FString& PathFileName, ID3D11Device* InDevice)
{
	return FObjManager::LoadObjStaticMesh(PathFileName, InDevice);
}

UStaticMesh* FMeshManager::LoadObjStaticMesh(const FString& PathFileName, const FImportOptions& Options, ID3D11Device* InDevice)
{
	return FObjManager::LoadObjStaticMesh(PathFileName, Options, InDevice);
}

USkeletalMesh* FMeshManager::LoadSkeletalMesh(const FString& PathFileName)
{
	return FFBXManager::LoadSkeletalMesh(PathFileName);
}

UFBXSceneAsset* FMeshManager::LoadFbxScene(const FString& PathFileName)
{
	return FFBXManager::LoadFbxScene(PathFileName);
}

UObject* FMeshManager::ResolveFbxSceneAssetReference(const FString& PathFileName)
{
	return FFBXManager::ResolveFbxSceneAssetReference(PathFileName);
}

void FMeshManager::ScanMeshAssets()
{
	FObjManager::ScanMeshAssets();
}

void FMeshManager::ScanObjSourceFiles()
{
	FObjManager::ScanObjSourceFiles();
}

void FMeshManager::ScanFbxSourceFiles()
{
	FFBXManager::ScanFbxSourceFiles();
}

void FMeshManager::ScanAllAssets()
{
	ScanMeshAssets();
	ScanObjSourceFiles();
	ScanFbxSourceFiles();
}

const TArray<FMeshAssetListItem>& FMeshManager::GetAvailableStaticMeshFiles()
{
	return FObjManager::GetAvailableMeshFiles();
}

const TArray<FMeshAssetListItem>& FMeshManager::GetAvailableObjSourceFiles()
{
	return FObjManager::GetAvailableObjFiles();
}

const TArray<FMeshAssetListItem>& FMeshManager::GetAvailableFbxSourceFiles()
{
	return FFBXManager::GetAvailableFbxSourceFiles();
}

void FMeshManager::ReleaseAllGPU()
{
	FFBXManager::ReleaseAllGPU();
	FObjManager::ReleaseAllGPU();
}
