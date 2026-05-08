#pragma once

#include "Object/Object.h"
#include "Camera/CameraTypes.h"

class APlayerCameraManager;

class UCameraModifier : public UObject
{
public:
	DECLARE_CLASS(UCameraModifier, UObject)
	UCameraModifier() = default;
	~UCameraModifier() override = default;

	void Initialize(APlayerCameraManager* Owner);
	virtual void OnAddedToCameraManager() {}
	virtual void OnRemovedFromCameraManager() {}

	//per frame, update alpha, call modifyCamera
	//비활성화를 반영해주며 alpha 값에 따라 ModifyCamera 를 호출함
	bool UpdateCameraModifier(float DeltaTime, FCameraView& InOutView);

	// 파생 클래스(UCameraFadeModifier 등)가 InOutView를 변형. 반환값은 chain 계속 여부 (true=다음 modifier 적용).
	virtual bool ModifyCamera(float DeltaTime, FCameraView& InOutView);

	void EnableModifier();
	void DisableModifier(bool bRemoveAfterFadeOut);

	//제거 대기 상태로 전환.
	void MarkPendingRemove();

	// --- Get , Set ---
	void SetAlpha(float InAlpha) { Alpha = InAlpha < 0.0f ? 0.0f : (InAlpha > 1.0f ? 1.0f : InAlpha); }
	void SetAlphaInTime(float InTime) { AlphaInTime = InTime < 0.0f ? 0.0f : InTime; }
	void SetAlphaOutTime(float InTime) { AlphaOutTime = InTime < 0.0f ? 0.0f : InTime; }
	void SetDisabled(bool bInDisabled) { bDisabled = bInDisabled ? 1 : 0; }
	void SetPriority(uint8 InPriority) { Priority = InPriority; }

	float GetAlpha() const { return Alpha; }
	float GetAlphaInTime() const { return AlphaInTime; }
	float GetAlphaOutTime() const{return AlphaOutTime;}
	bool  IsDisabled() const { return bDisabled != 0; }
	uint8 GetPriority() const { return Priority; }

	bool IsPendingRemove() const { return bPendingRemove != 0; }
	bool IsShouldDestroy() const { return IsPendingRemove() && Alpha <= 0.0f; }

protected:
	APlayerCameraManager* CameraOwner = nullptr;
	float Alpha = 0.0f;
	float AlphaInTime = 0.0f;	//alpha 값이 올라가는 시간
	float AlphaOutTime = 0.0f;	//alpha 값이 내려가는 시간
	uint32 bDisabled = 0;
	uint32 bPendingRemove = 0;	//true라면 효과가 완전히 꺼졌을 때 순회 리스트에서 제거
	uint8 Priority = 0;		//적용 순서
};