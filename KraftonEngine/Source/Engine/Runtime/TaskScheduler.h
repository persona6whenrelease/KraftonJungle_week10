#pragma once

#include "Core/CoreTypes.h"
#include "Runtime/Delegate.h"

#include <functional>

struct FDelayedTask
{
	TDelegate<> OnExpired;
	float RemainingTime = 0.0f;
	uint32 OwnerUUID = 0;
	bool bRequiresLiveLuaBinding = false;
};

class FTaskScheduler
{
public:
	void Schedule(float Delay, const std::function<void()>& Callback, uint32 OwnerUUID, bool bRequiresLiveLuaBinding = false);
	void Tick(float DeltaTime);
	void CancelTasks(uint32 OwnerUUID);
	void Clear();

private:
	TArray<FDelayedTask> Tasks;
};
