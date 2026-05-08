#pragma once

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"
#include "GameFramework/AActor.h"

#include <type_traits>

class UClass;
class UWorld;

struct FPooledClassBucket
{
	TArray<AActor*> InactiveActors;
	int32 MaxPoolSize = 1000;
};

struct FPoolKey
{
	UClass* ClassKey = nullptr;
	FString PrefabKey;

	static FPoolKey FromClass(UClass* Class)
	{
		FPoolKey Key;
		Key.ClassKey = Class;
		return Key;
	}

	static FPoolKey FromPrefab(const FString& PrefabPath)
	{
		FPoolKey Key;
		Key.PrefabKey = PrefabPath;
		return Key;
	}

	bool IsPrefab() const { return !PrefabKey.empty(); }

	bool operator==(const FPoolKey& Other) const
	{
		return ClassKey == Other.ClassKey && PrefabKey == Other.PrefabKey;
	}
};

namespace std
{
	template<>
	struct hash<FPoolKey>
	{
		size_t operator()(const FPoolKey& Key) const noexcept
		{
			const size_t ClassHash = std::hash<UClass*>{}(Key.ClassKey);
			const size_t PrefabHash = std::hash<FString>{}(Key.PrefabKey);
			return ClassHash ^ (PrefabHash + 0x9e3779b97f4a7c15ull + (ClassHash << 6) + (ClassHash >> 2));
		}
	};
}

class FActorPoolSystem : public TSingleton<FActorPoolSystem>
{
	friend class TSingleton<FActorPoolSystem>;

public:
	void Initialize() {}
	void Shutdown();

	template<typename T>
	T* AcquireActor(UWorld* World, UClass* Class, const FVector& Location, const FRotator& Rotation)
	{
		static_assert(std::is_base_of_v<AActor, T>, "AcquireActor<T>: T must derive from AActor");
		return Cast<T>(AcquireActor(World, Class ? Class : T::StaticClass(), Location, Rotation));
	}

	template<typename T>
	T* AcquireActor(UWorld* World, const FVector& Location, const FRotator& Rotation)
	{
		return AcquireActor<T>(World, T::StaticClass(), Location, Rotation);
	}

	AActor* AcquireActor(UWorld* World, UClass* Class, const FVector& Location, const FRotator& Rotation);
	AActor* AcquirePrefab(UWorld* World, const FString& PrefabPath, const FVector& Location, const FRotator& Rotation);
	bool ReleaseActor(AActor* Actor);
	int32 WarmUp(UWorld* World, UClass* Class, int32 Count);
	int32 WarmUpPrefab(UWorld* World, const FString& PrefabPath, int32 Count);

	void SetMaxPoolSize(UClass* Class, int32 MaxPoolSize);
	void SetMaxPoolSize(const FString& PrefabPath, int32 MaxPoolSize);
	int32 GetMaxPoolSize(UClass* Class) const;
	int32 GetMaxPoolSize(const FString& PrefabPath) const;
	int32 GetInactiveCount(UClass* Class) const;
	int32 GetInactiveCount(const FString& PrefabPath) const;

	void ClearWorld(UWorld* World);
	void ForgetActor(AActor* Actor);

private:
	FActorPoolSystem() = default;

	AActor* AcquireActorByKey(UWorld* World, const FPoolKey& Key, const FVector& Location, const FRotator& Rotation);
	int32 WarmUpKey(UWorld* World, const FPoolKey& Key, int32 Count);
	AActor* SpawnActorForPool(UWorld* World, const FPoolKey& Key, bool bStartInactive);
	void ActivateActor(AActor* Actor, const FVector& Location, const FRotator& Rotation);
	void DeactivateActor(AActor* Actor);
	void DispatchSpawnCallbacks(AActor* Actor);
	void DispatchReturnCallbacks(AActor* Actor);
	void DestroyActor(AActor* Actor);

	FPooledClassBucket* FindBucket(const FPoolKey& Key);
	const FPooledClassBucket* FindBucket(const FPoolKey& Key) const;
	FPooledClassBucket& GetOrCreateBucket(const FPoolKey& Key);
	FPoolKey ResolveActorKey(AActor* Actor) const;

	TMap<FPoolKey, FPooledClassBucket> PoolMap;
	TMap<AActor*, FPoolKey> ActorPoolKeys;
};
