#pragma once

#include "Camera/CameraTypes.h"

class AActor;
class APlayerController;
class FArchive;
class UActorComponent;
class UCameraComponent;

class FPlayerCameraManager
{
public:
	void Initialize(APlayerController* InOwner);
	void Serialize(FArchive& Ar);

	void SetActiveCamera(UCameraComponent* Camera, bool bBlend);
	void ClearActiveCamera();

	UCameraComponent* GetActiveCamera() const;
	UCameraComponent* GetOutputCamera() const { return OutputCameraComponent; }
	bool HasValidOutputCamera() const;
	UCameraComponent* GetOutputCameraIfValid() const;

	void UpdateCamera(float DeltaTime);
	void SnapToActiveCamera();

	void RemapActorReferences(const TMap<uint32, uint32>& ActorUUIDRemap);
	void ClearCameraReferencesForActor(const AActor* Actor);
	void ClearCameraReferencesForComponent(const UActorComponent* Component);

private:
	UCameraComponent* ResolveCameraReference(const FCameraComponentReference& Ref) const;
	FCameraComponentReference MakeCameraReference(UCameraComponent* Camera) const;

	FCameraView BlendViews(
		const FCameraView& From,
		const FCameraView& To,
		float Alpha,
		const FCameraTransitionSettings& Params) const;
	float EvaluateBlendAlpha(float RawAlpha, ECameraBlendFunction Function) const;
	void EnsureOutputCamera();

private:
	APlayerController* OwnerController = nullptr;

	FCameraComponentReference ActiveCameraRef;
	FCameraComponentReference PendingCameraRef;

	UCameraComponent* ActiveCameraCached = nullptr;
	UCameraComponent* PendingCameraCached = nullptr;
	UCameraComponent* OutputCameraComponent = nullptr;

	FCameraView CurrentView;
	FCameraView BlendFromView;

	float BlendElapsedTime = 0.0f;
	bool bIsBlending = false;
};
