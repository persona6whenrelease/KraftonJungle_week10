#include "Runtime/ActorPoolSystem.h"

#include "Component/ActorComponent.h"
#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Object/UClass.h"
#include "Runtime/PooledObjectInterface.h"
#include "Serialization/PrefabSaveManager.h"

#include <algorithm>

namespace
{
	bool IsReusablePooledActor(AActor* Actor, UWorld* World)
	{
		return Actor && IsAliveObject(Actor) && Actor->GetWorld() == World;
	}

	int32 CountReusablePooledActors(const FPooledClassBucket& Bucket, UWorld* World)
	{
		int32 Count = 0;
		for (AActor* Actor : Bucket.InactiveActors)
		{
			if (IsReusablePooledActor(Actor, World))
			{
				++Count;
			}
		}
		return Count;
	}

	FString NormalizePrefabPoolPath(FString Path)
	{
		std::replace(Path.begin(), Path.end(), '\\', '/');
		return Path;
	}

	bool IsValidPoolKey(const FPoolKey& Key)
	{
		if (Key.IsPrefab())
		{
			return true;
		}
		return Key.ClassKey && Key.ClassKey->IsA(AActor::StaticClass());
	}
}

void FActorPoolSystem::Shutdown()
{
	TArray<AActor*> ActorsToDestroy;
	for (auto& Pair : PoolMap)
	{
		ActorsToDestroy.insert(
			ActorsToDestroy.end(),
			Pair.second.InactiveActors.begin(),
			Pair.second.InactiveActors.end()
		);
		Pair.second.InactiveActors.clear();
	}
	PoolMap.clear();
	ActorPoolKeys.clear();

	for (AActor* Actor : ActorsToDestroy)
	{
		DestroyActor(Actor);
	}
}

AActor* FActorPoolSystem::AcquireActor(UWorld* World, UClass* Class, const FVector& Location, const FRotator& Rotation)
{
	if (!World || !Class || !Class->IsA(AActor::StaticClass()))
	{
		return nullptr;
	}

	return AcquireActorByKey(World, FPoolKey::FromClass(Class), Location, Rotation);
}

AActor* FActorPoolSystem::AcquirePrefab(UWorld* World, const FString& PrefabPath, const FVector& Location, const FRotator& Rotation)
{
	const FString NormalizedPath = NormalizePrefabPoolPath(PrefabPath);
	if (!World || NormalizedPath.empty())
	{
		return nullptr;
	}

	return AcquireActorByKey(World, FPoolKey::FromPrefab(NormalizedPath), Location, Rotation);
}

AActor* FActorPoolSystem::AcquireActorByKey(UWorld* World, const FPoolKey& Key, const FVector& Location, const FRotator& Rotation)
{
	if (!World || !IsValidPoolKey(Key))
	{
		return nullptr;
	}

	FPooledClassBucket& Bucket = GetOrCreateBucket(Key);
	AActor* Actor = nullptr;
	TArray<AActor*> SkippedActors;

	while (!Bucket.InactiveActors.empty())
	{
		AActor* Candidate = Bucket.InactiveActors.back();
		Bucket.InactiveActors.pop_back();

		if (IsReusablePooledActor(Candidate, World))
		{
			Actor = Candidate;
			break;
		}

		if (Candidate && IsAliveObject(Candidate))
		{
			SkippedActors.push_back(Candidate);
		}
	}

	Bucket.InactiveActors.insert(Bucket.InactiveActors.end(), SkippedActors.begin(), SkippedActors.end());

	if (!Actor)
	{
		Actor = SpawnActorForPool(World, Key, false);
		if (!Actor)
		{
			return nullptr;
		}
	}

	ActorPoolKeys[Actor] = Key;
	ActivateActor(Actor, Location, Rotation);
	return Actor;
}

bool FActorPoolSystem::ReleaseActor(AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	auto It = ActorPoolKeys.find(Actor);
	if (It == ActorPoolKeys.end())
	{
		return false; // 이미 소멸되었거나 풀에서 관리하지 않는 객체
	}

	if (Actor->IsPooledActorInactive())
	{
		return false;
	}

	FPoolKey Key = It->second; // ResolveActorKey() 호출 대신 찾은 Key를 바로 사용
	if (!IsValidPoolKey(Key))
	{
		return false;
	}

	FPooledClassBucket& Bucket = GetOrCreateBucket(Key);
	ActorPoolKeys[Actor] = Key;
	DispatchReturnCallbacks(Actor);

	DeactivateActor(Actor);
	Bucket.InactiveActors.push_back(Actor);
	return true;
}

int32 FActorPoolSystem::WarmUp(UWorld* World, UClass* Class, int32 Count)
{
	if (!World || !Class || !Class->IsA(AActor::StaticClass()) || Count <= 0)
	{
		return 0;
	}

	return WarmUpKey(World, FPoolKey::FromClass(Class), Count);
}

int32 FActorPoolSystem::WarmUpPrefab(UWorld* World, const FString& PrefabPath, int32 Count)
{
	const FString NormalizedPath = NormalizePrefabPoolPath(PrefabPath);
	if (!World || NormalizedPath.empty() || Count <= 0)
	{
		return 0;
	}

	return WarmUpKey(World, FPoolKey::FromPrefab(NormalizedPath), Count);
}

int32 FActorPoolSystem::WarmUpKey(UWorld* World, const FPoolKey& Key, int32 Count)
{
	if (!World || !IsValidPoolKey(Key) || Count <= 0)
	{
		return 0;
	}

	FPooledClassBucket& Bucket = GetOrCreateBucket(Key);
	const int32 ExistingWorldActors = CountReusablePooledActors(Bucket, World);
	const int32 ActorsToCreate = Count - ExistingWorldActors;

	if (ActorsToCreate <= 0)
	{
		return 0;
	}

	const int32 RequiredTotalCapacity = static_cast<int32>(Bucket.InactiveActors.size()) + ActorsToCreate;
	if (Bucket.MaxPoolSize < RequiredTotalCapacity)
	{
		Bucket.MaxPoolSize = RequiredTotalCapacity;
	}

	int32 CreatedCount = 0;
	while (CreatedCount < ActorsToCreate)
	{
		AActor* Actor = SpawnActorForPool(World, Key, true);
		if (!Actor)
		{
			break;
		}

		DeactivateActor(Actor);
		Bucket.InactiveActors.push_back(Actor);
		++CreatedCount;
	}

	return CreatedCount;
}

void FActorPoolSystem::SetMaxPoolSize(UClass* Class, int32 MaxPoolSize)
{
	if (!Class)
	{
		return;
	}

	FPoolKey Key = FPoolKey::FromClass(Class);
	if (!IsValidPoolKey(Key))
	{
		return;
	}

	FPooledClassBucket& Bucket = GetOrCreateBucket(Key);
	Bucket.MaxPoolSize = (std::max)(0, MaxPoolSize);

	while (static_cast<int32>(Bucket.InactiveActors.size()) > Bucket.MaxPoolSize)
	{
		AActor* Actor = Bucket.InactiveActors.back();
		Bucket.InactiveActors.pop_back();
		DestroyActor(Actor);
	}
}

void FActorPoolSystem::SetMaxPoolSize(const FString& PrefabPath, int32 MaxPoolSize)
{
	const FString NormalizedPath = NormalizePrefabPoolPath(PrefabPath);
	if (NormalizedPath.empty())
	{
		return;
	}

	FPooledClassBucket& Bucket = GetOrCreateBucket(FPoolKey::FromPrefab(NormalizedPath));
	Bucket.MaxPoolSize = (std::max)(0, MaxPoolSize);

	while (static_cast<int32>(Bucket.InactiveActors.size()) > Bucket.MaxPoolSize)
	{
		AActor* Actor = Bucket.InactiveActors.back();
		Bucket.InactiveActors.pop_back();
		DestroyActor(Actor);
	}
}

int32 FActorPoolSystem::GetMaxPoolSize(UClass* Class) const
{
	const FPooledClassBucket* Bucket = FindBucket(FPoolKey::FromClass(Class));
	return Bucket ? Bucket->MaxPoolSize : 0;
}

int32 FActorPoolSystem::GetMaxPoolSize(const FString& PrefabPath) const
{
	const FPooledClassBucket* Bucket = FindBucket(FPoolKey::FromPrefab(NormalizePrefabPoolPath(PrefabPath)));
	return Bucket ? Bucket->MaxPoolSize : 0;
}

int32 FActorPoolSystem::GetInactiveCount(UClass* Class) const
{
	const FPooledClassBucket* Bucket = FindBucket(FPoolKey::FromClass(Class));
	return Bucket ? static_cast<int32>(Bucket->InactiveActors.size()) : 0;
}

int32 FActorPoolSystem::GetInactiveCount(const FString& PrefabPath) const
{
	const FPooledClassBucket* Bucket = FindBucket(FPoolKey::FromPrefab(NormalizePrefabPoolPath(PrefabPath)));
	return Bucket ? static_cast<int32>(Bucket->InactiveActors.size()) : 0;
}

void FActorPoolSystem::ClearWorld(UWorld* World)
{
	if (!World)
	{
		return;
	}

	TArray<AActor*> ActorsToForget;

	for (auto& Pair : PoolMap)
	{
		TArray<AActor*>& InactiveActors = Pair.second.InactiveActors;
		InactiveActors.erase(
			std::remove_if(
				InactiveActors.begin(),
				InactiveActors.end(),
				[World, &ActorsToForget](AActor* Actor)
				{
					if (!Actor || !IsAliveObject(Actor) || Actor->GetWorld() == World)
					{
						if (Actor)
						{
							ActorsToForget.push_back(Actor);
						}
						if (Actor && IsAliveObject(Actor))
						{
							Actor->SetPooledActorState(false, false);
						}
						return true;
					}
					return false;
				}
			),
			InactiveActors.end()
		);
	}

	for (AActor* Actor : ActorsToForget)
	{
		ActorPoolKeys.erase(Actor);
	}
}

void FActorPoolSystem::ForgetActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	for (auto& Pair : PoolMap)
	{
		TArray<AActor*>& InactiveActors = Pair.second.InactiveActors;
		InactiveActors.erase(
			std::remove(InactiveActors.begin(), InactiveActors.end(), Actor),
			InactiveActors.end()
		);
	}

	ActorPoolKeys.erase(Actor);
}

AActor* FActorPoolSystem::SpawnActorForPool(UWorld* World, const FPoolKey& Key, bool bStartInactive)
{
	if (!World || !IsValidPoolKey(Key))
	{
		return nullptr;
	}

	AActor* Actor = nullptr;

	if (Key.IsPrefab())
	{
		Actor = FPrefabSaveManager::LoadPrefabActor(World, Key.PrefabKey);
	}
	else
	{
		UObject* Object = FObjectFactory::Get().Create(Key.ClassKey->GetName(), World);
		if (!Object)
		{
			return nullptr;
		}

		Actor = Cast<AActor>(Object);
		if (!Actor)
		{
			UObjectManager::Get().DestroyObject(Object);
			return nullptr;
		}
	}

	if (!Actor)
	{
		return nullptr;
	}

	if (bStartInactive)
	{
		Actor->SetPooledActorState(true, true);
	}

	ActorPoolKeys[Actor] = Key;
	World->AddActor(Actor);
	return Actor;
}

void FActorPoolSystem::ActivateActor(AActor* Actor, const FVector& Location, const FRotator& Rotation)
{
	if (!Actor)
	{
		return;
	}

	Actor->SetPooledActorState(true, false);
	Actor->SetActorLocation(Location);
	Actor->SetActorRotation(Rotation);
	Actor->SetActorHiddenInGame(false);
	Actor->SetActorEnableCollision(true);
	Actor->SetActorTickEnabled(true);

	if (UWorld* World = Actor->GetWorld())
	{
		if (World->HasBegunPlay() && !Actor->HasActorBegunPlay())
		{
			Actor->BeginPlay();
		}
	}

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (Component)
		{
			Component->Activate();
		}
	}

	DispatchSpawnCallbacks(Actor);
}

void FActorPoolSystem::DeactivateActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	Actor->SetPooledActorState(true, true);
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	Actor->SetActorTickEnabled(false);

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (!Component)
		{
			continue;
		}

		Component->Deactivate();
	}
}

void FActorPoolSystem::DispatchSpawnCallbacks(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (IPooledObjectInterface* PooledActor = dynamic_cast<IPooledObjectInterface*>(Actor))
	{
		PooledActor->OnSpawnFromPool();
	}

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (IPooledObjectInterface* PooledComponent = dynamic_cast<IPooledObjectInterface*>(Component))
		{
			PooledComponent->OnSpawnFromPool();
		}
	}
}

void FActorPoolSystem::DispatchReturnCallbacks(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (IPooledObjectInterface* PooledActor = dynamic_cast<IPooledObjectInterface*>(Actor))
	{
		PooledActor->OnReturnToPool();
	}

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (IPooledObjectInterface* PooledComponent = dynamic_cast<IPooledObjectInterface*>(Component))
		{
			PooledComponent->OnReturnToPool();
		}
	}
}

void FActorPoolSystem::DestroyActor(AActor* Actor)
{
	if (!Actor || !IsAliveObject(Actor))
	{
		return;
	}

	ForgetActor(Actor);
	Actor->SetPooledActorState(false, false);

	if (UWorld* World = Actor->GetWorld())
	{
		World->DestroyActor(Actor);
	}
	else
	{
		UObjectManager::Get().DestroyObject(Actor);
	}
}

FPooledClassBucket* FActorPoolSystem::FindBucket(const FPoolKey& Key)
{
	auto It = PoolMap.find(Key);
	return It != PoolMap.end() ? &It->second : nullptr;
}

const FPooledClassBucket* FActorPoolSystem::FindBucket(const FPoolKey& Key) const
{
	auto It = PoolMap.find(Key);
	return It != PoolMap.end() ? &It->second : nullptr;
}

FPooledClassBucket& FActorPoolSystem::GetOrCreateBucket(const FPoolKey& Key)
{
	return PoolMap[Key];
}

FPoolKey FActorPoolSystem::ResolveActorKey(AActor* Actor) const
{
	auto It = ActorPoolKeys.find(Actor);
	if (It != ActorPoolKeys.end())
	{
		return It->second;
	}

	return FPoolKey::FromClass(Actor ? Actor->GetClass() : nullptr);
}
