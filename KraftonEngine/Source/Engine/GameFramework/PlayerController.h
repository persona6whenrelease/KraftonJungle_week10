#pragma once

#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/AActor.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

class APawn;
class FArchive;
class UActorComponent;
class UCameraComponent;
class UControllerInputComponent;
struct FControllerMovementInput;

class APlayerController : public AActor
{
public:
	DECLARE_CLASS(APlayerController, AActor)

	APlayerController() = default;
	~APlayerController() override = default;

	void Serialize(FArchive& Ar) override;
	void RemapActorReferences(const TMap<uint32, uint32>& ActorUUIDRemap) override;
	void InitDefaultComponents() override;
	void EndPlay() override;

	void Possess(AActor* InActor);
	void UnPossess();
	AActor* GetPossessedActor() const;


	void SetActiveCamera(UCameraComponent* Camera);
	void SetActiveCameraWithBlend(UCameraComponent* Camera);
	bool SetActiveCameraFromPossessedPawn();
	void ClearActiveCamera();
	UCameraComponent* GetActiveCamera() const;
	UCameraComponent* ResolveViewCamera() const;

	void ClearCameraReferencesForActor(const AActor* Actor);
	void ClearCameraReferencesForComponent(const UActorComponent* Component);

	UControllerInputComponent* FindControllerInputComponent() const;
	FPlayerCameraManager& GetCameraManager() { return CameraManager; }
	const FPlayerCameraManager& GetCameraManager() const { return CameraManager; }

	FRotator GetControlRotation() const { return ControlRotation; }
	void SetControlRotation(const FRotator& InRotation);
	void AddYawInput(float Value);
	void AddPitchInput(float Value);
	bool AddMovementInput(const FVector& WorldDirection, float Scale = 1.0f, float DeltaTime = 0.0f);
	bool ApplyControllerMovementInput(const FControllerMovementInput& Input);

private:
	UCameraComponent* FindCameraOnActor(AActor* Target) const;
	AActor* ResolveActorUUID(uint32 ActorUUID) const;

private:
	AActor* PossessedActor = nullptr;
	uint32 PossessedActorUUID = 0;
	FRotator ControlRotation;
	FPlayerCameraManager CameraManager;
};
