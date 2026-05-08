#include "EngineTimeManager.h"
#include "Core/Log.h"
#include <algorithm>

void FEngineTimeManager::Reset()
{
	RawDeltaTime = 0.0f;
	GameDeltaTime = 0.0f;
	UnscaledTime = 0.0f;
	GameTime = 0.0f;

	GlobalTimeDilation = 1.0f;
	SlomoTimeDilation = 1.0f;
	
	SlomoTimeRemaining = 0.0f;
	HitStopTimeRemaining = 0.0f;

	bSlomoActive = false;
	bHitStopActive = false;
}

void FEngineTimeManager::Update(float InRawDeltaTime)
{
	RawDeltaTime = std::max(InRawDeltaTime, 0.0f);
	UnscaledTime += RawDeltaTime;

	if (HitStopTimeRemaining > 0.0f)
	{
		HitStopTimeRemaining -= RawDeltaTime;
		if (HitStopTimeRemaining <= 0.0f)
		{
			HitStopTimeRemaining = 0.0f;
			bHitStopActive = false;
		}
	}

	if (SlomoTimeRemaining > 0.0f)
	{
		SlomoTimeRemaining -= RawDeltaTime;
		if (SlomoTimeRemaining <= 0.0f)
		{
			SlomoTimeRemaining = 0.0f;
			SlomoTimeDilation = 1.0f;
			bSlomoActive = false;
		}
	}

	float EffectiveDilation = GetEffectiveTimeDilation();

	if (bHitStopActive)
	{
		GameDeltaTime = 0.0f;
	}
	else
	{
		GameDeltaTime = RawDeltaTime * EffectiveDilation;
	}

	GameTime += GameDeltaTime;
}

void FEngineTimeManager::SetGlobalTimeDilation(float InDilation)
{
	GlobalTimeDilation = std::clamp(InDilation, 0.0f, 20.0f);
	UE_LOG("[TimeManager] SetGlobalTimeDilation: %f", GlobalTimeDilation);
}

void FEngineTimeManager::StartHitStop(float Duration)
{
	if (Duration <= 0.0f)
	{
		return;
	}
	HitStopTimeRemaining = Duration;
	bHitStopActive = true;
	UE_LOG("[TimeManager] StartHitStop: Duration=%f", Duration);
}

void FEngineTimeManager::StopHitStop()
{
	HitStopTimeRemaining = 0.0f;
	bHitStopActive = false;
}

void FEngineTimeManager::StartSlomo(float TimeDilation, float Duration)
{
	if (Duration <= 0.0f)
	{
		return;
	}
	SlomoTimeDilation = std::clamp(TimeDilation, 0.0f, 20.0f);
	SlomoTimeRemaining = Duration;
	bSlomoActive = true;
	UE_LOG("[TimeManager] StartSlomo: Scale=%f, Duration=%f", SlomoTimeDilation, Duration);
}

void FEngineTimeManager::StopSlomo()
{
	SlomoTimeRemaining = 0.0f;
	SlomoTimeDilation = 1.0f;
	bSlomoActive = false;
}

float FEngineTimeManager::GetEffectiveTimeDilation() const
{
	float Effective = GlobalTimeDilation;
	if (bSlomoActive)
	{
		// 아래 발제 코드에서 Slomo까지 곱해놓음
		// float T = DeltaTime * GlobalTimeDilation;
		Effective *= SlomoTimeDilation;
	}
	return Effective;
}