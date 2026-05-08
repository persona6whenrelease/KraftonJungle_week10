#pragma once

#include "Component/ActorComponent.h"
#include "Math/Vector.h"

class APlayerController;
class UMovementComponent;
class FArchive;

enum class EPawnFacingMode : int32
{
	None = 0,
	ControlRotationYaw = 1,
	MovementInputDirection = 2,
	MovementVelocityDirection = 3,
	MovementDirectionWithControlFallback = 4,
	CustomWorldDirection = 5,
};

class UPawnOrientationComponent : public UActorComponent
{
public:
	DECLARE_CLASS(UPawnOrientationComponent, UActorComponent)

	UPawnOrientationComponent() = default;
	~UPawnOrientationComponent() override = default;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	bool RefreshFacing(float DeltaTime);
	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	EPawnFacingMode GetFacingMode() const { return static_cast<EPawnFacingMode>(FacingMode); }
	void SetFacingMode(EPawnFacingMode InMode);

	float GetRotationSpeed() const { return RotationSpeed; }
	void SetRotationSpeed(float InSpeed);

	bool IsYawOnly() const { return bYawOnly; }
	void SetYawOnly(bool bInYawOnly) { bYawOnly = bInYawOnly; }

	void SetCustomFacingDirection(const FVector& InDirection);
	const FVector& GetCustomFacingDirection() const { return CustomFacingDirection; }

private:
	void NormalizeOptions();
	APlayerController* FindOwningPlayerController() const;
	UMovementComponent* FindBestMovementComponent() const;
	bool ComputeDesiredFacingYaw(float& OutYaw) const;
	void ApplyFacingYaw(float TargetYaw, float DeltaTime);

private:
	int32 FacingMode = static_cast<int32>(EPawnFacingMode::ControlRotationYaw);
	float RotationSpeed = 720.0f;
	bool bYawOnly = true;
	float MinFacingInputSize = 0.01f;
	float MinFacingSpeed = 1.0f;
	FVector CustomFacingDirection = FVector::ForwardVector;
};
