#include "Games/Crossy/Map/RowManager.h"

#include "Runtime/ActorPoolSystem.h"
#include "Component/Movement/ProjectileMovementComponent.h"

#include <algorithm>

#include "GameFramework/World.h"

namespace
{
	struct FCrossyPoolWarmupDesc
	{
		const char* PrefabPath;
		int32 Count;
	};

	const FCrossyPoolWarmupDesc GCrossyDefaultPoolWarmups[] =
	{
		{ "Asset/Prefab/Grass.Prefab", 40 },
		{ "Asset/Prefab/Road.Prefab", 40 },
		{ "Asset/Prefab/TrafficBarrierB.Prefab", 30 },
		{ "Asset/Prefab/TrafficBarrierA.Prefab", 30 },
		{ "Asset/Prefab/InvisibleSideWall.Prefab", 90 },

		{ "Asset/Prefab/Rock.Prefab", 25 },
		{ "Asset/Prefab/TreeA.Prefab", 15 },
		{ "Asset/Prefab/TreeB.Prefab", 15 },
		{ "Asset/Prefab/TreeC.Prefab", 15 },
		{ "Asset/Prefab/TreeD.Prefab", 15 },

		{ "Asset/Prefab/CarA.Prefab", 10 },
		{ "Asset/Prefab/CarB.Prefab", 10 },
		{ "Asset/Prefab/CarC.Prefab", 10 },
		{ "Asset/Prefab/CarD.Prefab", 10 },
		{ "Asset/Prefab/MiniBus.Prefab", 10 },
		{ "Asset/Prefab/FireCar.Prefab", 10 },
		{ "Asset/Prefab/PoliceCar.Prefab", 10 },
		{ "Asset/Prefab/RacingCar.Prefab", 10 },
		{ "Asset/Prefab/BasicCube.Prefab", 4 },
	};
}

void FRowData::ClearActors(bool bDestroyActors)
{
	auto ClearOneActor = [bDestroyActors](AActor*& Actor)
	{
		if (!Actor || !IsAliveObject(Actor))
		{
			Actor = nullptr;
			return;
		}

		UWorld* World = Actor->GetWorld();

		if (bDestroyActors)
		{
			if (World)
			{
				World->DestroyActor(Actor);
			}
			else
			{
				UObjectManager::Get().DestroyObject(Actor);
			}
		}
		else
		{
			if (Actor->IsPooledActorInactive())
			{
				// 이미 풀로 반환된 액터입니다. Row 참조만 제거하고 풀은 유지합니다.
			}
			else if (!FActorPoolSystem::Get().ReleaseActor(Actor))
			{
				if (World)
				{
					FActorPoolSystem::Get().ForgetActor(Actor);
					Actor->SetPooledActorState(false, false);
					World->DestroyActor(Actor);
				}
				else
				{
					UObjectManager::Get().DestroyObject(Actor);
				}
			}
		}

		Actor = nullptr;
	};

	for (FStaticObstacleData& Obstacle : StaticObstacles)
	{
		ClearOneActor(Obstacle.SpawnedActor);
	}
	StaticObstacles.clear();

	for (AActor*& DynActor : DynamicActors)
	{
		ClearOneActor(DynActor);
	}
	DynamicActors.clear();
}

void FRowManager::Initialize(UWorld* World)
{
    ActiveWorld = World;
    ActiveRows.clear();
    WarmUpDefaultPools();
}

void FRowManager::WarmUpDefaultPools()
{
	if (!ActiveWorld)
	{
		return;
	}

	for (const FCrossyPoolWarmupDesc& Desc : GCrossyDefaultPoolWarmups)
	{
		FActorPoolSystem::Get().WarmUpPrefab(ActiveWorld, FString(Desc.PrefabPath), Desc.Count);
	}
}

void FRowManager::Shutdown(bool bDestroyActors)
{
	for (FRowData& Row : ActiveRows)
	{
		Row.ClearActors(bDestroyActors);
	}
	ActiveRows.clear();
	ActiveWorld = nullptr;
}

FRowData* FRowManager::GetRowData(int32 RowIndex)
{
    for (FRowData& Row : ActiveRows)
    {
        if (Row.RowIndex == RowIndex)
        {
            return &Row;
        }
    }
    return nullptr;
}

FRowData& FRowManager::PushEmptyRow(int32 RowIndex)
{
    if (FRowData* Existing = GetRowData(RowIndex))
    {
        return *Existing;
    }

    FRowData NewRow;
    NewRow.RowIndex = RowIndex;

    auto InsertIt = std::find_if(ActiveRows.begin(), ActiveRows.end(),
        [RowIndex](const FRowData& Row)
        {
            return Row.RowIndex > RowIndex;
        });

    return *ActiveRows.insert(InsertIt, NewRow);
}

void FRowManager::SetRowSize(int32 SlotCount, float SlotSize, float RowDepth)
{
    Config.SlotCount = SlotCount;
    Config.SlotSize = SlotSize;
    Config.RowDepth = RowDepth;
}

void FRowManager::SetRowBufferCounts(int32 KeepRowsBehind, int32 KeepRowsAhead)
{
    Config.KeepRowsBehind = KeepRowsBehind;
    Config.KeepRowsAhead = KeepRowsAhead;
}

void FRowManager::SetRowBiome(int32 RowIndex, int32 BiomeType)
{
    FRowData& Row = PushEmptyRow(RowIndex);
    Row.Biome = static_cast<ERowBiome>(BiomeType);
}

void FRowManager::SpawnStaticObstacle(int32 RowIndex, int32 SlotIndex, const FString& PrefabPath, float OffsetX, float OffsetY, float YawDegrees)
{
    FRowData& Row = PushEmptyRow(RowIndex);

    FStaticObstacleData Obstacle;
    Obstacle.SlotIndex = SlotIndex;
    Obstacle.PrefabPath = PrefabPath;

	const float CenterOffsetY = (static_cast<float>(Config.SlotCount) - 1.0f) * 0.5f;
	const float WorldY = (static_cast<float>(SlotIndex) - CenterOffsetY) * Config.SlotSize + OffsetY;
	const float WorldX = static_cast<float>(RowIndex) * Config.RowDepth + OffsetX;

	const FVector SpawnLocation(WorldX, WorldY, 0.0f);
    FRotator SpawnRotation = FRotator();
	SpawnRotation.Yaw = YawDegrees;

    if (ActiveWorld)
    {
        Obstacle.SpawnedActor = FActorPoolSystem::Get().AcquirePrefab(ActiveWorld, PrefabPath, SpawnLocation, SpawnRotation);

        if (Obstacle.SpawnedActor)
        {
            Obstacle.SpawnedActor->SetSerializeToScene(false);
            Obstacle.SpawnedActor->AddTag("__RuntimeSpawned");
            Obstacle.SpawnedActor->AddTag("__RuntimeMap");
            Obstacle.SpawnedActor->AddTag("__RowManaged");
        }
    }

    Row.StaticObstacles.push_back(Obstacle);
}

AActor* FRowManager::SpawnDynamicVehicle(int32 RowIndex, const FString& PrefabPath, float Speed, int32 DirectionX)
{
	FRowData& Row = PushEmptyRow(RowIndex);

	if (!ActiveWorld) return nullptr;

	const float OffsetY = (static_cast<float>(Config.SlotCount) - 1.0f) * 0.5f;
	const float ExtentY = OffsetY * Config.SlotSize;

	const float SpawnY = (DirectionX > 0) ? -ExtentY - Config.SlotSize : ExtentY + Config.SlotSize;
	const float WorldX = static_cast<float>(Row.RowIndex) * Config.RowDepth;

	const FVector SpawnLocation(WorldX, SpawnY, 0.0f);

	FRotator SpawnRotation = FRotator();
	if (DirectionX < 0)
	{
		SpawnRotation.Yaw = 180.0f; 
	}

	AActor* SpawnedActor = FActorPoolSystem::Get().AcquirePrefab(ActiveWorld, PrefabPath, SpawnLocation, SpawnRotation);

	if (SpawnedActor)
	{
		SpawnedActor->AddTag("__RuntimeSpawned");
		SpawnedActor->AddTag("__RuntimeMap");
		SpawnedActor->AddTag("__RuntimeVehicle");
		SpawnedActor->AddTag("__RowManaged");
		SpawnedActor->AddTag("Vehicle");
		
		Row.DynamicActors.push_back(SpawnedActor);
		for (UActorComponent* Comp : SpawnedActor->GetComponents())
		{
			if (UProjectileMovementComponent* ProjComp = Cast<UProjectileMovementComponent>(Comp))
			{
				ProjComp->SetVelocity(FVector(0.0f, static_cast<float>(DirectionX), 0.0f) * Speed);
				break;
			}
		}
	}

	return SpawnedActor;
}

bool FRowManager::ReleaseRuntimeActor(AActor* Actor)
{
	if (!Actor || !IsAliveObject(Actor))
	{
		return false;
	}

	for (FRowData& Row : ActiveRows)
	{
		Row.DynamicActors.erase(
			std::remove(Row.DynamicActors.begin(), Row.DynamicActors.end(), Actor),
			Row.DynamicActors.end());

		for (FStaticObstacleData& Obstacle : Row.StaticObstacles)
		{
			if (Obstacle.SpawnedActor == Actor)
			{
				Obstacle.SpawnedActor = nullptr;
			}
		}
	}

	if (Actor->IsPooledActorInactive())
	{
		return true;
	}

	return FActorPoolSystem::Get().ReleaseActor(Actor);
}

void FRowManager::MoveForward(int32 NewCurrentRowIndex)
{
    const int32 Threshold = NewCurrentRowIndex - Config.KeepRowsBehind;
    PopOldRows(Threshold);
}

void FRowManager::PopOldRows(int32 ThresholdRowIndex)
{
    while (!ActiveRows.empty() && ActiveRows.front().RowIndex < ThresholdRowIndex)
    {
        ActiveRows.front().ClearActors();
        ActiveRows.pop_front();
    }
}
