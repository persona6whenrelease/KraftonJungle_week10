#include "Runtime/RowManager.h"

#include "Runtime/ObjectPoolSystem.h"
#include "Scripting/LuaWorldLibrary.h"
#include "Component/Movement/ProjectileMovementComponent.h"

#include <algorithm>

#include "GameFramework/World.h"

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
			FObjectPoolSystem::Get().ReleaseActor(Actor);
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

void FRowManager::Initialize()
{
    ActiveRows.clear();
}

void FRowManager::Shutdown(bool bDestroyActors)
{
	for (FRowData& Row : ActiveRows)
	{
		Row.ClearActors(bDestroyActors);
	}
	ActiveRows.clear();
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

    UWorld* World = FLuaWorldLibrary::GetActiveWorld();
    if (World)
    {
        Obstacle.SpawnedActor = FObjectPoolSystem::Get().AcquirePrefab(World, PrefabPath, SpawnLocation, SpawnRotation);

        if (Obstacle.SpawnedActor)
        {
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

	UWorld* World = FLuaWorldLibrary::GetActiveWorld();
	if (!World) return nullptr;

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

	AActor* SpawnedActor = FObjectPoolSystem::Get().AcquirePrefab(World, PrefabPath, SpawnLocation, SpawnRotation);

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
