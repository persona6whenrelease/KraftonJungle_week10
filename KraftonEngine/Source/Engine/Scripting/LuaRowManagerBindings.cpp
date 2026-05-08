#include "LuaBindings.h"
#include "SolInclude.h"

#include "Runtime/RowManager.h"
#include "Runtime/ObjectPoolSystem.h"
#include "Scripting/LuaHandles.h"
#include "Scripting/LuaWorldLibrary.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Component/StaticMeshComponent.h"
#include "Core/Log.h"

namespace
{
    bool ContainsToken(const FString& Source, const char* Token)
    {
        return !Source.empty() && Token && Source.find(Token) != FString::npos;
    }

    bool IsRuntimeMapActor(AActor* Actor)
    {
        if (!Actor || !IsAliveObject(Actor))
        {
            return false;
        }

        // RowManager/ObjectPool에서 생성한 런타임 맵 액터.
        if (Actor->HasTag("__RuntimeMap") ||
            Actor->HasTag("__RuntimeSpawned") ||
            Actor->HasTag("__RowManaged") ||
            Actor->HasTag("__RuntimeVehicle") ||
            Actor->HasTag("Vehicle") ||
            Actor->IsPooledActor())
        {
            return true;
        }

        // 이전 빌드에서 태그가 덜 붙은 차량이 남아 있는 경우를 위한 호환성 필터.
        // 새로 배치한 플레이어/카메라/매니저류는 보통 이 이름을 갖지 않습니다.
        const FString ActorName = Actor->GetFName().ToString();
        if (ContainsToken(ActorName, "Car") ||
            ContainsToken(ActorName, "Bus") ||
            ContainsToken(ActorName, "Vehicle") ||
            ContainsToken(ActorName, "RacingCar") ||
            ContainsToken(ActorName, "MiniBus"))
        {
            return true;
        }

        // 프리팹/씬에 이미 저장되어 들어온 차량은 ActorName이 AActor_x 형태일 수 있습니다.
        // 이 경우 StaticMesh 경로에 차량명이 남아 있으므로 컴포넌트까지 검사합니다.
        for (UActorComponent* Component : Actor->GetComponents())
        {
            UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
            if (!StaticMeshComponent)
            {
                continue;
            }

            const FString& MeshPath = StaticMeshComponent->GetStaticMeshPath();
            if (ContainsToken(MeshPath, "Car") ||
                ContainsToken(MeshPath, "Bus") ||
                ContainsToken(MeshPath, "Vehicle") ||
                ContainsToken(MeshPath, "RacingCar") ||
                ContainsToken(MeshPath, "MiniBus"))
            {
                return true;
            }
        }

        return false;
    }

    int32 DestroyRuntimeMapActorsInWorld(UWorld* World)
    {
        if (!World)
        {
            return 0;
        }

        TArray<AActor*> ActorsToDestroy;
        const TArray<AActor*> ActorSnapshot = World->GetActors();

        for (AActor* Actor : ActorSnapshot)
        {
            if (!IsRuntimeMapActor(Actor))
            {
                continue;
            }

            ActorsToDestroy.push_back(Actor);
        }

        int32 DestroyedCount = 0;
        for (AActor* Actor : ActorsToDestroy)
        {
            if (!Actor || !IsAliveObject(Actor))
            {
                continue;
            }

            // ObjectPool이 들고 있는 active/inactive 참조를 먼저 끊습니다.
            FObjectPoolSystem::Get().ForgetActor(Actor);
            Actor->SetPooledActorState(false, false);

            World->DestroyActor(Actor);
            ++DestroyedCount;
        }

        return DestroyedCount;
    }

    void ResetRuntimeMap()
    {
        UWorld* World = FLuaWorldLibrary::GetActiveWorld();

        // RowManager가 알고 있는 행/장애물/차량을 먼저 정상 경로로 삭제합니다.
        FRowManager::Get().Shutdown(true);

        // RowManager가 더 이상 참조하지 못하는 차량/풀 액터/이전 빌드 잔여 액터까지 월드에서 직접 제거합니다.
        const int32 SweptActors = DestroyRuntimeMapActorsInWorld(World);

        if (World)
        {
            FObjectPoolSystem::Get().ClearWorld(World);
        }

        FRowManager::Get().Initialize();

        UE_LOG("[ResetMap] Runtime map reset. SweptActors=%d World=%p", SweptActors, World);
    }
}

void RegisterRowManagerBinding(sol::state& Lua)
{
    Lua.set_function("SetRowSize",
        [](int32 SlotCount, float SlotSize, float RowDepth)
        {
            FRowManager::Get().SetRowSize(SlotCount, SlotSize, RowDepth);
        });

    Lua.set_function("SetRowBufferCounts",
        [](int32 KeepRowsBehind, int32 KeepRowsAhead)
        {
            FRowManager::Get().SetRowBufferCounts(KeepRowsBehind, KeepRowsAhead);
        });

    Lua.set_function("SetRowBiome",
        [](int32 RowIndex, int32 BiomeType)
        {
            FRowManager::Get().SetRowBiome(RowIndex, BiomeType);
        });

    Lua.set_function("SpawnStaticObstacle",
        [](int32 RowIndex, int32 SlotIndex, const FString& PrefabPath, sol::optional<float> OffsetX, sol::optional<float> OffsetY, sol::optional<float> YawDegrees)
        {
            FRowManager::Get().SpawnStaticObstacle(
                RowIndex,
                SlotIndex,
                PrefabPath,
                OffsetX.value_or(0.0f),
                OffsetY.value_or(0.0f),
                YawDegrees.value_or(0.0f));
        });

    Lua.set_function("SpawnDynamicVehicle",
        [](int32 RowIndex, const FString& PrefabPath, float Speed, int32 DirectionX, sol::this_state State) -> sol::object
        {
            AActor* Spawned = FRowManager::Get().SpawnDynamicVehicle(RowIndex, PrefabPath, Speed, DirectionX);
            if (!Spawned)
            {
                return sol::nil;
            }

            FLuaGameObjectHandle Handle;
            Handle.UUID = Spawned->GetUUID();
            return sol::make_object(sol::state_view(State), Handle);
        });

	Lua.set_function("MoveForward",
		[](int32 NewCurrentRowIndex)
		{
			FRowManager::Get().MoveForward(NewCurrentRowIndex);
		});

	Lua.set_function("ResetMap",
		[]()
		{
            ResetRuntimeMap();
		});

    // 디버깅/비상용: Lua에서 직접 호출해도 같은 강제 스윕을 수행합니다.
    Lua.set_function("ResetRuntimeMap",
        []()
        {
            ResetRuntimeMap();
        });
}
