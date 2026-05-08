#include "PrefabSaveManager.h"
#include "SceneSaveManager.h"
#include "SimpleJSON/json.hpp"
#include "Platform/Paths.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Component/ActorComponent.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
	std::filesystem::path ResolvePrefabFilePath(const FString& PrefabNameOrPath)
	{
		std::wstring WideInput = FPaths::ToWide(PrefabNameOrPath);
		std::replace(WideInput.begin(), WideInput.end(), L'\\', L'/');

		std::filesystem::path InputPath(WideInput);
		if (InputPath.has_parent_path())
		{
			if (!InputPath.has_extension())
			{
				InputPath += FPrefabSaveManager::PrefabExtension;
			}
			return InputPath;
		}

		std::wstring FileName = WideInput;
		std::filesystem::path FilePath(FileName);
		if (!FilePath.has_extension())
		{
			FileName += FPrefabSaveManager::PrefabExtension;
		}

		return std::filesystem::path(FPaths::PrefabDir()) / FileName;
	}

	bool ReadTextFile(const std::filesystem::path& FilePath, std::string& OutContent)
	{
		std::ifstream File(FilePath);
		if (!File.is_open())
		{
			return false;
		}

		OutContent.assign(
			std::istreambuf_iterator<char>(File),
			std::istreambuf_iterator<char>());
		return true;
	}
}

bool FPrefabSaveManager::SaveActorAsPrefab(AActor* Actor, const FString& PrefabName)
{
	if (!Actor || PrefabName.empty())
	{
		std::cerr << "[Prefab] SaveActorAsPrefab failed: Actor is null or PrefabName is empty\n";
		return false;
	}

	std::wstring PrefabDir = FPaths::PrefabDir();
	FPaths::CreateDir(PrefabDir);
	std::filesystem::path FileDestination = ResolvePrefabFilePath(PrefabName);

	using namespace json;
	JSON Root = json::Object();
	Root["Version"] = 1;
	Root["Type"] = "ActorPrefab";
	Root["Name"] = FPaths::ToUtf8(FileDestination.stem().wstring()).c_str();
	Root["RootActor"] = FSceneSaveManager::SerializeActor(Actor);
	if (FileDestination.has_parent_path())
	{
		std::filesystem::create_directories(FileDestination.parent_path());
	}

	std::ofstream File(FileDestination);
	if (File.is_open())
	{
		File << Root.dump();
		File.flush();
		File.close();
		std::cout << "[Prefab] Saved: " << FPaths::ToUtf8(FileDestination.wstring()) << "\n";
		return true;
	}

	std::cerr << "[Prefab] SaveActorAsPrefab failed: Could not open file for writing\n";
	return false;
}

bool FPrefabSaveManager::LoadPrefabRootActorJson(const FString& PrefabNameOrPath, json::JSON& OutRootActorJson)
{
	if (PrefabNameOrPath.empty())
	{
		std::cerr << "[Prefab] Load failed: PrefabName is empty\n";
		return false;
	}

	std::filesystem::path FileDestination = ResolvePrefabFilePath(PrefabNameOrPath);
	if (!std::filesystem::exists(FileDestination))
	{
		std::cerr << "[Prefab] Load failed: file not found - " << FPaths::ToUtf8(FileDestination.wstring()) << "\n";
		return false;
	}

	std::string FileContent;
	if (!ReadTextFile(FileDestination, FileContent))
	{
		std::cerr << "[Prefab] Load failed: Could not open file - " << FPaths::ToUtf8(FileDestination.wstring()) << "\n";
		return false;
	}

	json::JSON Root = json::JSON::Load(FileContent);
	if (Root.hasKey("RootActor"))
	{
		OutRootActorJson = Root["RootActor"];
		return true;
	}

	// Compatibility path for raw actor JSON prefabs made by the other branch.
	if (Root.hasKey("ClassName"))
	{
		OutRootActorJson = Root;
		return true;
	}

	std::cerr << "[Prefab] Load failed: Missing RootActor/ClassName - " << FPaths::ToUtf8(FileDestination.wstring()) << "\n";
	return false;
}

AActor* FPrefabSaveManager::LoadPrefabActor(UWorld* World, const FString& PrefabNameOrPath)
{
	return LoadPrefabActor(World, PrefabNameOrPath, true);
}

AActor* FPrefabSaveManager::LoadPrefabActor(UWorld* World, const FString& PrefabNameOrPath, bool bRenewActorUUID)
{
	if (!World || PrefabNameOrPath.empty())
	{
		return nullptr;
	}

	json::JSON RootActorJson;
	if (!LoadPrefabRootActorJson(PrefabNameOrPath, RootActorJson))
	{
		return nullptr;
	}

	TMap<uint32, uint32> ActorUUIDRemap;

	FActorDeserializeOptions Options;
	Options.bAddToWorld = false;
	Options.bInitDefaultComponentsIfMissing = false;
	Options.bRestoreActorUUID = !bRenewActorUUID;
	Options.ActorUUIDRemap = bRenewActorUUID ? &ActorUUIDRemap : nullptr;

	AActor* Actor = FSceneSaveManager::DeserializeActor(World, RootActorJson, nullptr, Options);
	if (Actor && bRenewActorUUID)
	{
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component)
			{
				Component->RemapActorReferences(ActorUUIDRemap);
			}
		}
	}

	return Actor;
}

bool FPrefabSaveManager::ApplyPrefabToActor(AActor* Actor, const FString& PrefabNameOrPath)
{
	if (!Actor || PrefabNameOrPath.empty())
	{
		return false;
	}

	json::JSON RootActorJson;
	if (!LoadPrefabRootActorJson(PrefabNameOrPath, RootActorJson))
	{
		return false;
	}

	return FSceneSaveManager::ApplyPrefabDataToActor(Actor, RootActorJson);
}

AActor* FPrefabSaveManager::SpawnPrefab(UWorld* World, const FString& PrefabNameOrPath, const FVector& SpawnLocation)
{
	return SpawnPrefab(World, PrefabNameOrPath, SpawnLocation, FRotator(), true);
}

AActor* FPrefabSaveManager::SpawnPrefab(UWorld* World, const FString& PrefabNameOrPath, const FVector& SpawnLocation, bool bRenewActorUUID)
{
	return SpawnPrefab(World, PrefabNameOrPath, SpawnLocation, FRotator(), bRenewActorUUID);
}

AActor* FPrefabSaveManager::SpawnPrefab(UWorld* World, const FString& PrefabNameOrPath, const FVector& SpawnLocation, const FRotator& SpawnRotation)
{
	return SpawnPrefab(World, PrefabNameOrPath, SpawnLocation, SpawnRotation, true);
}

AActor* FPrefabSaveManager::SpawnPrefab(UWorld* World, const FString& PrefabNameOrPath, const FVector& SpawnLocation, const FRotator& SpawnRotation, bool bRenewActorUUID)
{
	if (!World)
	{
		return nullptr;
	}

	AActor* Actor = LoadPrefabActor(World, PrefabNameOrPath, bRenewActorUUID);
	if (!Actor)
	{
		return nullptr;
	}

	// The actor is added to the world only after all prefab components/properties are restored.
	// This prevents LuaScriptComponent::BeginPlay from running before the prefab data is ready.
	Actor->SetActorLocation(SpawnLocation);
	Actor->SetActorRotation(SpawnRotation);
	World->AddActor(Actor);

	std::cout << "[Prefab] Spawned: " << PrefabNameOrPath << " at ("
		<< SpawnLocation.X << ", " << SpawnLocation.Y << ", " << SpawnLocation.Z << ")\n";

	return Actor;
}

bool FPrefabSaveManager::DoesPrefabExist(const FString& PrefabNameOrPath)
{
	return !PrefabNameOrPath.empty() && std::filesystem::exists(ResolvePrefabFilePath(PrefabNameOrPath));
}

TArray<FString> FPrefabSaveManager::GetPrefabList()
{
	TArray<FString> Result;
	std::wstring PrefabDir = FPaths::PrefabDir();
	if (!std::filesystem::exists(PrefabDir))
	{
		return Result;
	}

	for (auto& Entry : std::filesystem::directory_iterator(PrefabDir))
	{
		if (Entry.is_regular_file() && Entry.path().extension() == PrefabExtension)
		{
			Result.push_back(FPaths::ToUtf8(Entry.path().stem().wstring()));
		}
	}
	return Result;
}
