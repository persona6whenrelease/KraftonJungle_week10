#include "SkeletalMesh/FBXManager.h"
#include "SkeletalMesh/SkeletalMesh.h"
#include "SkeletalMesh/FBXImporter.h"
#include "Mesh/StaticMesh.h"
#include "Materials/Material.h"
#include "Object/ObjectFactory.h"
#include "Core/Log.h"
#include "Serialization/WindowsArchive.h"
#include "Engine/Platform/Paths.h"
#include "Materials/MaterialManager.h"
#include "Engine/Runtime/Engine.h"

#include <filesystem>
#include <algorithm>

TMap<FString, USkeletalMesh*> FFBXManager::SkeletalMeshCache;
TArray<FMeshAssetListItem> FFBXManager::AvailableMeshFiles;
TArray<FMeshAssetListItem> FFBXManager::AvailableFbxFiles;

void FFBXManager::EnsureMeshCacheDirExists()
{
	static bool bCreated = false;
	if (!bCreated)
	{
		std::wstring CacheDir = FPaths::RootDir() + L"Asset\\MeshCache\\";
		FPaths::CreateDir(CacheDir);
		bCreated = true;
	}
}

FString FFBXManager::GetBinaryFilePath(const FString& OriginalPath)
{
	std::wstring OriginalDiskPath;
	FString ResolveError;
	const bool bResolvedOriginal = FPaths::TryResolvePackagePath(OriginalPath, OriginalDiskPath, &ResolveError);
	
	std::filesystem::path SrcPath(bResolvedOriginal ? OriginalDiskPath : FPaths::ToWide(OriginalPath));
	std::wstring Ext = SrcPath.extension().wstring();

	// 이미 .bin 경로인 경우 그대로 사용
	if (Ext == L".bin")
	{
		return OriginalPath;
	}

	EnsureMeshCacheDirExists();

	// Asset/MeshCache/하위에 .bin으로 변환된 경로 반환
	std::filesystem::path RelPath = std::filesystem::path(L"Asset\\MeshCache") / SrcPath.stem();
	RelPath += L".bin";

	return FPaths::ToUtf8(RelPath.generic_wstring());
}

void FFBXManager::ScanMeshAssets()
{
	AvailableMeshFiles.clear();

	const std::filesystem::path MeshCacheRoot = FPaths::RootDir() + L"Asset\\MeshCache\\";
	if (!std::filesystem::exists(MeshCacheRoot)) return;

	const std::filesystem::path ProjectRoot(FPaths::RootDir());

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(MeshCacheRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();
		// SkeletalMesh용 바이너리 식별자가 따로 없다면 확장자로만 관리
		if (Path.extension() != L".bin") continue;

		FMeshAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Path.stem().wstring());
		Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
		AvailableMeshFiles.push_back(std::move(Item));
	}
}

void FFBXManager::ScanFbxSourceFiles()
{
	AvailableFbxFiles.clear();

	const std::filesystem::path DataRoot = FPaths::RootDir() + L"Data\\";
	if (!std::filesystem::exists(DataRoot)) return;

	const std::filesystem::path ProjectRoot(FPaths::RootDir());

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(DataRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();
		std::wstring Ext = Path.extension().wstring();
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
		
		if (Ext != L".fbx") continue;

		FMeshAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Path.filename().wstring());
		Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
		AvailableFbxFiles.push_back(std::move(Item));
	}
}

const TArray<FMeshAssetListItem>& FFBXManager::GetAvailableMeshFiles()
{
	return AvailableMeshFiles;
}

const TArray<FMeshAssetListItem>& FFBXManager::GetAvailableFbxFiles()
{
	return AvailableFbxFiles;
}

USkeletalMesh* FFBXManager::LoadSkeletalMesh(const FString& PathFileName, ID3D11Device* InDevice)
{
	if (PathFileName.empty() || PathFileName == "None") return nullptr;

	FString SourcePath = PathFileName;

	// 0. 외부 파일 처리: 프로젝트 루트 외부의 파일이면 Data/SkeletalMesh/ 하위로 복사
	try {
		std::filesystem::path InputPath(FPaths::ToWide(PathFileName));
		std::filesystem::path ProjectRoot(FPaths::RootDir());

		if (InputPath.is_absolute())
		{
			// 프로젝트 루트 상대 경로 계산 시도
			auto Rel = std::filesystem::relative(InputPath, ProjectRoot);
			// 상대 경로가 ..으로 시작하거나 비어있으면 외부 파일로 간주
			if (Rel.empty() || Rel.wstring().find(L"..") == 0)
			{
				std::filesystem::path DestDir = ProjectRoot / L"Data\\SkeletalMesh\\";
				std::filesystem::create_directories(DestDir);
				std::filesystem::path DestPath = DestDir / InputPath.filename();

				// 파일이 다르거나 존재하지 않을 때만 복사
				if (!std::filesystem::exists(DestPath) || 
					std::filesystem::last_write_time(InputPath) != std::filesystem::last_write_time(DestPath))
				{
					std::filesystem::copy_file(InputPath, DestPath, std::filesystem::copy_options::overwrite_existing);
				}
				
				// 엔진 내부용 상대 경로로 전환
				SourcePath = FPaths::ToUtf8(std::filesystem::relative(DestPath, ProjectRoot).generic_wstring());
			}
		}
	}
	catch (...) {
		UE_LOG("Error handling external path: %s", PathFileName.c_str());
	}

	FString BinPath = GetBinaryFilePath(SourcePath);

	// 1. 메모리 캐시 확인
	auto It = SkeletalMeshCache.find(BinPath);
	if (It != SkeletalMeshCache.end())
	{
		return It->second;
	}

	// 2. 에셋 오브젝트 생성
	USkeletalMesh* SkeletalMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
	bool bNeedRebuild = true;

	// 3. 타임스탬프 비교를 통한 디스크 캐시 유효성 확인
	std::wstring BinDiskPath;
	std::wstring SourceDiskPath;
	FString ResolveError;
	
	if (!FPaths::TryResolvePackagePath(BinPath, BinDiskPath, &ResolveError))
		BinDiskPath = FPaths::ToWide(BinPath);
	if (!FPaths::TryResolvePackagePath(SourcePath, SourceDiskPath, &ResolveError))
		SourceDiskPath = FPaths::ToWide(SourcePath);

	std::filesystem::path BinPathW(BinDiskPath);
	std::filesystem::path SourcePathW(SourceDiskPath);

	if (std::filesystem::exists(BinPathW))
	{
		// 원본이 없거나, 캐시가 원본보다 최신인 경우
		if (!std::filesystem::exists(SourcePathW) || SourcePath == BinPath ||
			std::filesystem::last_write_time(BinPathW) >= std::filesystem::last_write_time(SourcePathW))
		{
			bNeedRebuild = false;
		}
	}

	// 4. 로드 또는 재빌드
	if (!bNeedRebuild)
	{
		FWindowsBinReader Reader(BinPath);
		if (Reader.IsValid())
		{
			SkeletalMesh->Serialize(Reader);
			// 직렬화 도중 버전 미스매치 등으로 SkeletalMeshAsset이 비었으면 재빌드한다.
			if (!SkeletalMesh->GetSkeletalMeshAsset())
			{
				UE_LOG("Skeletal mesh cache invalid (version mismatch?), rebuilding: %s", BinPath.c_str());
				bNeedRebuild = true;
			}
		}
		else
		{
			bNeedRebuild = true;
		}
	}

	if (bNeedRebuild)
	{
		FString FbxPath = SourcePath;
		// 캐시에서 로드 시도했다 실패한 경우 내부의 원본 경로 사용 시도
		if (SkeletalMesh->GetSkeletalMeshAsset() && !SkeletalMesh->GetSkeletalMeshAsset()->PathFileName.empty())
			FbxPath = SkeletalMesh->GetSkeletalMeshAsset()->PathFileName;

		// hybrid FBX의 static 파트를 받기 위한 임시 UStaticMesh.
		// importer가 데이터를 채우지 않으면 빈 객체로 남고 UObjectManager가 GC한다.
		UStaticMesh* PendingStatic = UObjectManager::Get().CreateObject<UStaticMesh>();

		if (FFbxImporter::ImportFbx(FbxPath, SkeletalMesh, PendingStatic))
		{
			if (PendingStatic && PendingStatic->GetStaticMeshAsset())
			{
				SkeletalMesh->SetEmbeddedStaticMesh(PendingStatic);
			}

			// 파싱 결과 저장
			FWindowsBinWriter Writer(BinPath);
			if (Writer.IsValid())
			{
				SkeletalMesh->Serialize(Writer);
			}
		}
		else
		{
			return nullptr;
		}
	}

	// 5. GPU 리소스 초기화 및 캐시 등록
	SkeletalMesh->InitResources(InDevice);
	SkeletalMeshCache[BinPath] = SkeletalMesh;

	// 목록 갱신
	ScanMeshAssets();
	ScanFbxSourceFiles();
	FMaterialManager::Get().ScanMaterialAssets();

	return SkeletalMesh;
}

void FFBXManager::ReleaseAllGPU()
{
	for (auto& [Key, Mesh] : SkeletalMeshCache)
	{
		if (!Mesh) continue;

		// Skeletal RenderBuffer
		if (FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset())
		{
			if (Asset->RenderBuffer)
			{
				Asset->RenderBuffer->Release();
				Asset->RenderBuffer.reset();
			}
		}

		// Embedded static RenderBuffer (hybrid FBX)
		if (UStaticMesh* Embedded = Mesh->GetEmbeddedStaticMesh())
		{
			if (FStaticMesh* StaticAsset = Embedded->GetStaticMeshAsset())
			{
				if (StaticAsset->RenderBuffer)
				{
					StaticAsset->RenderBuffer->Release();
					StaticAsset->RenderBuffer.reset();
				}
			}
		}
	}
	SkeletalMeshCache.clear();
}
