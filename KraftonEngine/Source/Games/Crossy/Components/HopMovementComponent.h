#pragma once

#include "Component/Movement/MovementComponent.h"
#include "Runtime/Delegate.h"
#include "Math/Vector.h"

class FArchive;
class FScene;

/**
 * 입력 기반으로 수평 이동을 수행하면서, 선택적으로 위아래 Hop 오프셋을 더하는 이동 컴포넌트.
 *
 * 이 컴포넌트는 특정 키보드/마우스/패드 입력 시스템에 직접 의존하지 않는다.
 * 상위 입력 계층은 매 프레임 SetMovementInput 또는 AddMovementInput만 호출하면 된다.
 */
class UHopMovementComponent : public UMovementComponent
{
public:
	DECLARE_CLASS(UHopMovementComponent, UMovementComponent)
	DECLARE_DELEGATE(DashDelegate)

	UHopMovementComponent();
	~UHopMovementComponent() override = default;

	void BeginPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void Serialize(FArchive& Ar) override;
	void PostEditProperty(const char* PropertyName) override;
	void ContributeSelectedVisuals(FScene& Scene) const override;

	// ---------------------------------------------------------------------
	// Input-facing API
	// ---------------------------------------------------------------------
	// 지속형 입력. 입력 시스템이 현재 축 값을 계산해 매 프레임 세팅하거나, 키 해제 시 ClearMovementInput을 호출한다.
	void SetMovementInput(const FVector& InWorldInput);
	const FVector& GetMovementInput() const { return MovementInput; }
	void ClearMovementInput();

	// 누적형 입력. 여러 입력 소스(WASD, 패드, AI, 스크립트)가 한 프레임에 더할 수 있으며 Tick 후 자동으로 비워진다.
	void AddMovementInput(const FVector& WorldDirection, float Scale = 1.0f);

	bool ApplyControllerMovementInput(const FControllerMovementInput& Input) override;

	const FVector& GetLastMovementInput() const { return LastMovementInput; }

	// ---------------------------------------------------------------------
	// Movement settings / state
	// ---------------------------------------------------------------------
	void SetVelocity(const FVector& InVelocity) { Velocity = InVelocity; }
	const FVector& GetVelocity() const override { return Velocity; }
	FVector GetMovementVelocity() const override { return Velocity; }

	// 기존 Lua/에디터 호환용 이름. 플레이어 이동에서는 기본 이동 속도처럼 사용한다.
	void SetInitialSpeed(float InInitialSpeed) { InitialSpeed = InInitialSpeed; }
	float GetInitialSpeed() const { return InitialSpeed; }

	float GetMaxSpeed() const { return MaxSpeed; }
	void SetMaxSpeed(float InMaxSpeed) { MaxSpeed = InMaxSpeed; }

	// 기존 호환용 이름. 최종 속도 배율로 사용한다.
	void SetHopCoefficient(float InHopCoefficient) { HopCoefficient = InHopCoefficient; }
	float GetHopCoefficient() const { return HopCoefficient; }

	void SetAcceleration(float InAcceleration) { Acceleration = InAcceleration; }
	float GetAcceleration() const { return Acceleration; }

	void SetBrakingDeceleration(float InBrakingDeceleration) { BrakingDeceleration = InBrakingDeceleration; }
	float GetBrakingDeceleration() const { return BrakingDeceleration; }

	void SetHopHeight(float InHopHeight) { HopHeight = InHopHeight; }
	float GetHopHeight() const { return HopHeight; }

	void SetHopFrequency(float InHopFrequency) { HopFrequency = InHopFrequency; }
	float GetHopFrequency() const { return HopFrequency; }

	void SetHopOnlyWhenMoving(bool bInHopOnlyWhenMoving) { bHopOnlyWhenMoving = bInHopOnlyWhenMoving; }
	bool IsHopOnlyWhenMoving() const { return bHopOnlyWhenMoving; }

	void SetResetHopWhenIdle(bool bInResetHopWhenIdle) { bResetHopWhenIdle = bInResetHopWhenIdle; }
	bool ShouldResetHopWhenIdle() const { return bResetHopWhenIdle; }

	void SetSimulating(bool bInSimulating) { if (bInSimulating) StartSimulating(); else StopSimulating(); }
	bool IsSimulating() const { return bSimulating; }
	void StartSimulating() { bSimulating = true; }
	void StopSimulating();
	void StopMovementImmediately();


	void Dash();
	bool IsDashing() const { return DashTimeRemaining > 0.0f; }

protected:
	FVector ConsumeFrameMovementInput();
	float GetEffectiveMoveSpeed() const;
	void RemoveAppliedHopOffset();
	void CaptureGameplayPlane();
	void LockUpdatedComponentToGameplayPlane();
	bool ResolveVisualHopComponent(bool bResetBaseLocation = false);
	USceneComponent* FindFallbackVisualHopComponent() const;
	void ApplyVisualHopOffset(float NewHopOffset);

	// Persistent/direct input. Input layer may set this every frame and clear it on release.
	FVector MovementInput = FVector(0.0f, 0.0f, 0.0f);

	// One-frame accumulated input. Cleared after Tick.
	FVector PendingMovementInput = FVector(0.0f, 0.0f, 0.0f);
	FVector LastMovementInput = FVector(0.0f, 0.0f, 0.0f);

	// Runtime horizontal velocity in world space.
	FVector Velocity = FVector(0.0f, 0.0f, 0.0f);

	// Legacy-compatible movement settings.
	float InitialSpeed = 10.0f;
	float MaxSpeed = 15.0f;
	float HopCoefficient = 1.0f;

	float Acceleration = 2048.0f;
	float BrakingDeceleration = 4096.0f;

	float HopHeight = 0.3f;
	float HopFrequency = 4.0f; // cycles per second. High value + low height gives a vibration-like motion.
	bool bHopOnlyWhenMoving = true;
	bool bResetHopWhenIdle = true;

	float HopElapsedTime = 0.0f;
	float AppliedHopOffset = 0.0f;
	bool bSimulating = true;
	float LockedGameplayPlaneZ = 0.0f;
	bool bHasLockedGameplayPlaneZ = false;
	USceneComponent* PlaneLockedComponent = nullptr;

	FString VisualHopComponentPath;
	USceneComponent* VisualHopComponent = nullptr;
	FVector VisualHopBaseRelativeLocation = FVector::ZeroVector;
	bool bHasVisualHopBaseRelativeLocation = false;

	// Dash 설정. 에디터에서 튜닝.
	float DashSpeed = 150.0f;
	float DashDuration = 0.15f;

	// Dash 런타임 상태. 0보다 크면 Dash 중.
	float DashTimeRemaining = 0.0f;
};
