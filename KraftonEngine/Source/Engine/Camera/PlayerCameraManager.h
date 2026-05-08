#pragma once

#include "Camera/CameraTypes.h"
#include "Core/EngineTypes.h"
#include "GameFramework/AActor.h"
#include "Math/Vector.h"
#include "Object/FName.h"

class APlayerController;
class FArchive;
class UActorComponent;
class UCameraComponent;
class UCameraModifier;
class UCameraShakeModifier;
struct FCameraShakeParams;
class UCameraFadeModifier;
class UVignetteModifier;

struct FViewTarget
{
	AActor* Target = nullptr;
	UCameraComponent* Camera = nullptr;
	FCameraView POV;
};

class APlayerCameraManager : public AActor
{
public:
	DECLARE_CLASS(APlayerCameraManager, AActor)

	APlayerCameraManager();
	~APlayerCameraManager() override;

	void InitDefaultComponents() override;
	void EndPlay() override;

	// AActor로 직렬화될 때 쓰는 함수
	void Serialize(FArchive& Ar) override;

	// PlayerController 내부 상태로 저장할 때 쓰는 함수
	void SerializeCameraState(FArchive& Ar);

	void Initialize(APlayerController* InOwner);

	void SetActiveCamera(UCameraComponent* Camera, bool bBlend);
	void ClearActiveCamera();

	UCameraComponent* GetActiveCamera() const;
	UCameraComponent* GetOutputCamera() const { return OutputCameraComponent; }
	bool HasValidOutputCamera() const;
	UCameraComponent* GetOutputCameraIfValid() const;

	void UpdateCamera(float GameDeltaTime, float RawDeltaTime);
	void SnapToActiveCamera();

	void AddCameraModifier(UCameraModifier* Modifier);
	void RemoveCameraModifier(UCameraModifier* Modifier, bool bImmediate = false);
	void ClearCameraModifiers();
	const TArray<UCameraModifier*>& GetCameraModifiers() const { return ModifierList; }

	void StartFadeIn(float Duration, float TargetAlpha, const FVector& Color);
	void StartFadeOut(float Duration);

	void StartVignette(float Intensity, const FVector& Color, float Duration, float Smoothness = 0.5f);
	void StopVignette(float Duration);

	void RemapActorReferences(const TMap<uint32, uint32>& ActorUUIDRemap);
	void ClearCameraReferencesForActor(const AActor* Actor);
	void ClearCameraReferencesForComponent(const UActorComponent* Component);

	UCameraShakeModifier* StartCameraShake(const FCameraShakeParams& Params);
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

	void ApplyCameraModifiers(float RawDeltaTime, FCameraView& InOutView);
	void CleanupCameraModifiers();
	void SortCameraModifiers();
	UCameraFadeModifier* EnsureFadeModifier();
	UVignetteModifier* EnsureVignetteModifier();

	// Pawn(SubjectActor) 월드 좌표를 OutputCamera의 ViewProjection으로 투영해 PostProcess.VignetteCenter를 UV로 갱신.
	// Subject가 없거나 화면 밖이면 (0.5, 0.5)로 폴백.
	void UpdateVignetteCenter(UCameraComponent* TargetCamera);

private:
	APlayerController* OwnerController = nullptr;

	FName CameraStyle;
	FViewTarget ViewTarget;

	// 기존 카메라 상태
	FCameraComponentReference ActiveCameraRef; // uint32 OwnerActorUUID = 0; FString ComponentPath;
	FCameraComponentReference PendingCameraRef;

	UCameraComponent* ActiveCameraCached = nullptr;
	UCameraComponent* PendingCameraCached = nullptr;
	UCameraComponent* OutputCameraComponent = nullptr;

	FCameraView CurrentView;
	FCameraView BlendFromView;

	TArray<UCameraModifier*> ModifierList;
	UCameraFadeModifier* FadeModifier = nullptr;
	UVignetteModifier* VignetteModifier = nullptr;

	float BlendElapsedTime = 0.0f;
	bool bIsBlending = false;
};