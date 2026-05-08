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
class APlayerCameraManager;
struct FControllerMovementInput;

class APlayerController : public AActor
{
public:
	DECLARE_CLASS(APlayerController, AActor)

	APlayerController() = default;
	~APlayerController() override;

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

	void StartCameraShake(
		float Duration,
		float LocationAmplitude,
		float RotationAmplitude,
		float Frequency,
		float FOVAmplitude = 0.0f,
		bool bSingleInstance = false
	);

	void SetCameraVignette(float Intensity, float Smoothness, const FVector& Color);
	void SetCameraFade(float Alpha, const FVector& Color);
	void ResetCameraPostProcess();

	UControllerInputComponent* FindControllerInputComponent() const;
	APlayerCameraManager& GetCameraManager();
	const APlayerCameraManager& GetCameraManager() const;
	APlayerCameraManager* GetCameraManagerPtr() const;

	FRotator GetControlRotation() const { return ControlRotation; }
	void SetControlRotation(const FRotator& InRotation);
	void AddYawInput(float Value);
	void AddPitchInput(float Value);
	bool AddMovementInput(const FVector& WorldDirection, float Scale = 1.0f, float DeltaTime = 0.0f);
	bool ApplyControllerMovementInput(const FControllerMovementInput& Input);

private:
	UCameraComponent* FindCameraOnActor(AActor* Target) const;
	AActor* ResolveActorUUID(uint32 ActorUUID) const;

	APlayerCameraManager* EnsureCameraManager();
	void DestroyCameraManager();

private:
	AActor* PossessedActor = nullptr;
	uint32 PossessedActorUUID = 0;
	FRotator ControlRotation;
	APlayerCameraManager* CameraManager = nullptr;
};
