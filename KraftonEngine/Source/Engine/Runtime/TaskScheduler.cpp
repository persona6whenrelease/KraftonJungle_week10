#include "Runtime/TaskScheduler.h"

#include "Scripting/LuaScriptSubsystem.h"

#include <algorithm>

void FTaskScheduler::Schedule(float Delay, const std::function<void()>& Callback, uint32 OwnerUUID, bool bRequiresLiveLuaBinding)
{
	if (!Callback)
	{
		return;
	}

	FDelayedTask Task;
	Task.RemainingTime = (std::max)(Delay, 0.0f);
	Task.OwnerUUID = OwnerUUID;
	Task.bRequiresLiveLuaBinding = bRequiresLiveLuaBinding;
	Task.OnExpired.Add(Callback, OwnerUUID);
	Tasks.push_back(std::move(Task));
}

void FTaskScheduler::Tick(float DeltaTime)
{
	if (Tasks.empty())
	{
		return;
	}

	TArray<FDelayedTask> ExpiredTasks;
	for (size_t Index = 0; Index < Tasks.size();)
	{
		FDelayedTask& Task = Tasks[Index];
		Task.RemainingTime -= DeltaTime;

		if (Task.RemainingTime <= 0.0f)
		{
			ExpiredTasks.push_back(std::move(Task));
			Tasks.erase(Tasks.begin() + Index);
			continue;
		}

		++Index;
	}

	for (FDelayedTask& Task : ExpiredTasks)
	{
		if (Task.bRequiresLiveLuaBinding &&
			Task.OwnerUUID != 0 &&
			!FLuaScriptSubsystem::Get().IsComponentBound(Task.OwnerUUID))
		{
			continue;
		}

		Task.OnExpired.BroadCast();
	}
}

void FTaskScheduler::CancelTasks(uint32 OwnerUUID)
{
	for (FDelayedTask& Task : Tasks)
	{
		if (Task.OwnerUUID == OwnerUUID)
		{
			Task.OnExpired.RemoveAllByInstance(OwnerUUID);
		}
	}

	std::erase_if(Tasks, [OwnerUUID](const FDelayedTask& Task)
		{
			return Task.OwnerUUID == OwnerUUID;
		});
}

void FTaskScheduler::Clear()
{
	Tasks.clear();
}
