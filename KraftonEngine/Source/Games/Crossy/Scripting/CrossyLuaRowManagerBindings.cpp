#include "Games/Crossy/Scripting/CrossyLuaBindings.h"
#include "Scripting/SolInclude.h"

#include "Games/Crossy/Map/RowManager.h"
#include "Runtime/ActorPoolSystem.h"
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

        // RowManager/ActorPool에서 생성한 런타임 맵 액터.
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

            // ActorPool이 들고 있는 active/inactive 참조를 먼저 끊습니다.
            FActorPoolSystem::Get().ForgetActor(Actor);
            Actor->SetPooledActorState(false, false);

            World->DestroyActor(Actor);
            ++DestroyedCount;
        }

        return DestroyedCount;
    }

    int32 SweepRuntimeMapActorsForSoftReset(UWorld* World)
    {
        if (!World)
        {
            return 0;
        }

        int32 HandledCount = 0;
        const TArray<AActor*> ActorSnapshot = World->GetActors();

        for (AActor* Actor : ActorSnapshot)
        {
            if (!Actor || !IsAliveObject(Actor) || !IsRuntimeMapActor(Actor))
            {
                continue;
            }

            // 이미 비활성 풀 상태인 액터는 월드에 남아 있어도 게임플레이/충돌/Tick에서 제외됩니다.
            if (Actor->IsPooledActorInactive())
            {
                continue;
            }

            // 풀에서 나온 액터라면 Destroy하지 않고 비활성 풀로 되돌립니다.
            if (FActorPoolSystem::Get().ReleaseActor(Actor))
            {
                ++HandledCount;
                continue;
            }

            // 풀에서 관리하지 않는 이전 빌드 잔여 런타임 액터만 강제로 제거합니다.
            FActorPoolSystem::Get().ForgetActor(Actor);
            Actor->SetPooledActorState(false, false);
            World->DestroyActor(Actor);
            ++HandledCount;
        }

        return HandledCount;
    }

    void SoftResetRuntimeMap()
    {
        UWorld* World = FLuaWorldLibrary::GetActiveWorld();

        FRowManager::Get().Shutdown(false);

        const int32 SweptActors = SweepRuntimeMapActorsForSoftReset(World);
        FRowManager::Get().Initialize(World);

        UE_LOG("[ResetMapSoft] Runtime map soft reset. SweptActors=%d World=%p", SweptActors, World);
    }

    void HardResetRuntimeMap()
    {
        UWorld* World = FLuaWorldLibrary::GetActiveWorld();

        // 레벨 종료/완전 재로드용: 실제 Destroy와 풀 월드 참조 제거를 수행합니다.
        FRowManager::Get().Shutdown(true);

        const int32 SweptActors = DestroyRuntimeMapActorsInWorld(World);

        if (World)
        {
            FActorPoolSystem::Get().ClearWorld(World);
        }

        FRowManager::Get().Initialize(World);

        UE_LOG("[ResetMapHard] Runtime map hard reset. SweptActors=%d World=%p", SweptActors, World);
    }
}

void RegisterCrossyRowManagerBinding(sol::state& Lua)
{
	sol::table Game = Lua.get_or("Game", Lua.create_table());
	Lua["Game"] = Game;
	sol::table Map = Game.get_or("Map", Lua.create_table());
	Game["Map"] = Map;

	Map.set_function("SetRowSize", [](int32 SlotCount, float SlotSize, float RowDepth)
	{
		FRowManager::Get().SetRowSize(SlotCount, SlotSize, RowDepth);
	});
	Map.set_function("SetRowBufferCounts", [](int32 KeepRowsBehind, int32 KeepRowsAhead)
	{
		FRowManager::Get().SetRowBufferCounts(KeepRowsBehind, KeepRowsAhead);
	});
	Map.set_function("SetRowBiome", [](int32 RowIndex, int32 BiomeType)
	{
		FRowManager::Get().SetRowBiome(RowIndex, BiomeType);
	});
	Map.set_function("SpawnStaticObstacle", [](int32 RowIndex, int32 SlotIndex, const FString& PrefabPath, sol::optional<float> OffsetX, sol::optional<float> OffsetY, sol::optional<float> YawDegrees)
	{
		FRowManager::Get().SpawnStaticObstacle(RowIndex, SlotIndex, PrefabPath, OffsetX.value_or(0.0f), OffsetY.value_or(0.0f), YawDegrees.value_or(0.0f));
	});
	Map.set_function("SpawnDynamicVehicle", [](int32 RowIndex, const FString& PrefabPath, float Speed, int32 DirectionX, sol::this_state State) -> sol::object
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
	Map.set_function("MoveForward", [](int32 NewCurrentRowIndex)
	{
		FRowManager::Get().MoveForward(NewCurrentRowIndex);
	});
	Map.set_function("ReleaseRuntimeActor", [](const FLuaGameObjectHandle& Handle)
	{
		return FRowManager::Get().ReleaseRuntimeActor(Handle.Resolve());
	});
	Map.set_function("IsVehicle", [](const FLuaGameObjectHandle& Handle)
	{
		AActor* Actor = Handle.Resolve();
		if (!Actor || !IsAliveObject(Actor))
		{
			return false;
		}

		if (Actor->HasTag("Vehicle") || Actor->HasTag("__RuntimeVehicle"))
		{
			return true;
		}

		const FString ActorName = Actor->GetFName().ToString();
		if (ContainsToken(ActorName, "Car") ||
			ContainsToken(ActorName, "Bus") ||
			ContainsToken(ActorName, "Vehicle") ||
			ContainsToken(ActorName, "RacingCar") ||
			ContainsToken(ActorName, "MiniBus"))
		{
			return true;
		}

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
	});
	Map.set_function("Reset", []() { SoftResetRuntimeMap(); });
	Map.set_function("ResetSoft", []() { SoftResetRuntimeMap(); });
	Map.set_function("ResetHard", []() { HardResetRuntimeMap(); });

}
