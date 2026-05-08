#pragma once

#include <string>
#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

class AActor;
class UWorld;

namespace json
{
	class JSON;
}

class FPrefabSaveManager
{
public:
	static constexpr const wchar_t* PrefabExtension = L".Prefab";

	static bool SaveActorAsPrefab(AActor* Actor, const FString& PrefabName);
	static bool LoadPrefabRootActorJson(const FString& PrefabNameOrPath, json::JSON& OutRootActorJson);
	static AActor* LoadPrefabActor(UWorld* World, const FString& PrefabNameOrPath);
	static AActor* LoadPrefabActor(UWorld* World, const FString& PrefabNameOrPath, bool bRenewActorUUID);
	static bool ApplyPrefabToActor(AActor* Actor, const FString& PrefabNameOrPath);
	static AActor* SpawnPrefab(UWorld* World, const FString& PrefabNameOrPath, const FVector& SpawnLocation);
	static AActor* SpawnPrefab(UWorld* World, const FString& PrefabNameOrPath, const FVector& SpawnLocation, bool bRenewActorUUID);
	static AActor* SpawnPrefab(UWorld* World, const FString& PrefabNameOrPath, const FVector& SpawnLocation, const FRotator& SpawnRotation);
	static AActor* SpawnPrefab(UWorld* World, const FString& PrefabNameOrPath, const FVector& SpawnLocation, const FRotator& SpawnRotation, bool bRenewActorUUID);
	static bool DoesPrefabExist(const FString& PrefabNameOrPath);
	static TArray<FString> GetPrefabList();
};
