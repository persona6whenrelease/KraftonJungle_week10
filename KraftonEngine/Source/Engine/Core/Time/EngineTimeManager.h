#pragma once

#include "Core/CoreTypes.h"
#include <algorithm>

class FEngineTimeManager
{
public:
	FEngineTimeManager() = default;
	~FEngineTimeManager() = default;

	void Reset();
	void Update(float InRawDeltaTime);

	void SetGlobalTimeDilation(float InDilation);
	float GetGlobalTimeDilation() const { return GlobalTimeDilation; }

	void StartHitStop(float Duration);
	void StopHitStop();
	bool IsHitStopActive() const { return bHitStopActive; }
	float GetHitStopTimeRemaining() const { return HitStopTimeRemaining; }

	void StartSlomo(float TimeDilation, float Duration);
	void StopSlomo();
	bool IsSlomoActive() const { return bSlomoActive; }
	float GetSlomoTimeRemaining() const { return SlomoTimeRemaining; }
	float GetSlomoTimeDilation() const { return SlomoTimeDilation; }

	float GetRawDeltaTime() const { return RawDeltaTime; }
	float GetGameDeltaTime() const { return GameDeltaTime; }
	float GetUnscaledTime() const { return UnscaledTime; }
	float GetGameTime() const { return GameTime; }
	float GetEffectiveTimeDilation() const;

private:
	float RawDeltaTime = 0.0f;
	float GameDeltaTime = 0.0f;
	float UnscaledTime = 0.0f;
	float GameTime = 0.0f;

	float GlobalTimeDilation = 1.0f;
	float SlomoTimeDilation = 1.0f;
	
	float SlomoTimeRemaining = 0.0f;
	float HitStopTimeRemaining = 0.0f;

	bool bSlomoActive = false;
	bool bHitStopActive = false;
};