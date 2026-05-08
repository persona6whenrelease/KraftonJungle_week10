#include "TickFunction.h"

#include "Component/ActorComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <algorithm>

namespace
{
	bool ShouldDispatchActorTick(const AActor* Actor, ELevelTick TickType)
	{
		if (!Actor || !IsAliveObject(Actor))
		{
			return false;
		}

		if (Actor->IsPooledActorInactive())
		{
			return false;
		}

		switch (TickType)
		{
		case LEVELTICK_ViewportsOnly:
			return Actor->bTickInEditor;

		case LEVELTICK_All:
		case LEVELTICK_TimeOnly:
		case LEVELTICK_PauseTick:
			return Actor->bNeedsTick && Actor->HasActorBegunPlay();

		default:
			return false;
		}
	}
}

void FTickFunction::RegisterTickFunction()
{
	bRegistered = true;
	TickAccumulator = 0.0f;
}

void FTickFunction::UnRegisterTickFunction()
{
	bRegistered = false;
	bTickEnabled = false;
	TickAccumulator = 0.0f;
}

void FTickManager::Tick(UWorld* World, float DeltaTime, ELevelTick TickType)
{
	GatherTickFunctions(World, TickType);

	for (int GroupIndex = 0; GroupIndex < TG_MAX; ++GroupIndex)
	{
		const ETickingGroup CurrentGroup = static_cast<ETickingGroup>(GroupIndex);

		for (size_t Index = 0; Index < TickFunctions.size();)
		{
			FTickFunction* TickFunction = TickFunctions[Index];

			if (!TickFunction)
			{
				TickFunctions.erase(TickFunctions.begin() + Index);
				continue;
			}

			// 중요:
			// 앞선 TickFunction 실행 중 Actor가 Destroy되면 뒤쪽 TickFunction 포인터가
			// stale pointer가 될 수 있습니다. 실행 전에 owner 생존 여부를 검사합니다.
			if (!TickFunction->IsTargetValid())
			{
				TickFunctions.erase(TickFunctions.begin() + Index);
				continue;
			}

			if (TickFunction->GetTickGroup() != CurrentGroup)
			{
				++Index;
				continue;
			}

			if (!TickFunction->CanTick(TickType))
			{
				++Index;
				continue;
			}

			if (!TickFunction->ConsumeInterval(DeltaTime))
			{
				++Index;
				continue;
			}

			TickFunction->ExecuteTick(DeltaTime, TickType);

			// ExecuteTick 안에서 runtime reset/DestroyActor가 호출되면
			// RemoveTickFunction()이 현재 배열을 수정할 수 있습니다.
			// 따라서 단순 range-for 대신 index 기반으로 순회합니다.
			++Index;
		}
	}
}

void FTickManager::Reset()
{
	TickFunctions.clear();
}

void FTickManager::RemoveTickFunction(FTickFunction* TickFunction)
{
	if (!TickFunction)
	{
		return;
	}

	TickFunctions.erase(
		std::remove(TickFunctions.begin(), TickFunctions.end(), TickFunction),
		TickFunctions.end()
	);
}

void FTickManager::GatherTickFunctions(UWorld* World, ELevelTick TickType)
{
	TickFunctions.clear();

	if (!World)
	{
		return;
	}

	for (AActor* Actor : World->GetActors())
	{
		if (!ShouldDispatchActorTick(Actor, TickType))
		{
			continue;
		}

		QueueTickFunction(Actor->PrimaryActorTick);

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component || !IsAliveObject(Component))
			{
				continue;
			}

			QueueTickFunction(Component->PrimaryComponentTick);
		}
	}
}

void FTickManager::QueueTickFunction(FTickFunction& TickFunction)
{
	if (!TickFunction.IsTargetValid())
	{
		return;
	}

	if (!TickFunction.bRegistered)
	{
		TickFunction.RegisterTickFunction();
	}

	TickFunctions.push_back(&TickFunction);
}

void FActorTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType)
{
	if (Target && IsAliveObject(Target))
	{
		Target->TickActor(DeltaTime, TickType, *this);
	}
}

const char* FActorTickFunction::GetDebugName() const
{
	return Target ? Target->GetClass()->GetName() : "FActorTickFunction";
}

bool FActorTickFunction::IsTargetValid() const
{
	return Target && IsAliveObject(Target);
}

void FActorComponentTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType)
{
	if (Target && IsAliveObject(Target))
	{
		Target->TickComponent(DeltaTime, TickType, *this);
	}
}

const char* FActorComponentTickFunction::GetDebugName() const
{
	return Target ? Target->GetClass()->GetName() : "FActorComponentTickFunction";
}

bool FActorComponentTickFunction::IsTargetValid() const
{
	return Target && IsAliveObject(Target);
}