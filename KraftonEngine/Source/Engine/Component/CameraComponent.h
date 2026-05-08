#pragma once

#include "Camera/CameraTypes.h"
#include "Engine/Core/RayTypes.h"
#include "Object/ObjectFactory.h"
#include "Component/SceneComponent.h"
#include "Math/Matrix.h"
#include "Math/MathUtils.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Collision/ConvexVolume.h"

class AActor;
class APlayerController;
class FArchive;

class UCameraComponent : public USceneComponent
{
public:
	DECLARE_CLASS(UCameraComponent, USceneComponent)

	UCameraComponent() = default;

	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	void RemapActorReferences(const TMap<uint32, uint32>& ActorUUIDRemap) override;

	void LookAt(const FVector& Target);
	void SetCameraState(const FCameraState& NewState);
	const FCameraState& GetCameraState() const { return CameraState; }

	FCameraView GetCameraView() const;
	void ApplyCameraView(const FCameraView& View);
	bool CalcCameraView(APlayerController* Controller, float DeltaTime, FCameraView& OutView);

	void SetActiveCamera();
	void SetActiveCameraWithBlend();

	void SetFOV(float InFOV) { CameraState.FOV = InFOV; }
	void SetAspectRatio(float InAspectRatio) { if (InAspectRatio > 0.0f) CameraState.AspectRatio = InAspectRatio; }
	void SetNearPlane(float InNearZ) { CameraState.NearZ = InNearZ; }
	void SetFarPlane(float InFarZ) { CameraState.FarZ = InFarZ; }
	void SetOrthoWidth(float InWidth) { CameraState.OrthoWidth = InWidth; }
	void SetOrthographic(bool bOrtho) { CameraState.bIsOrthogonal = bOrtho; }

	void OnResize(int32 Width, int32 Height);

	FMatrix GetViewMatrix() const;
	FMatrix GetProjectionMatrix() const;
	FMatrix GetViewProjectionMatrix() const;
	FConvexVolume GetConvexVolume() const;

	float GetFOV() const { return CameraState.FOV; }
	float GetAspectRatio() const { return CameraState.AspectRatio; }
	float GetNearPlane() const { return CameraState.NearZ; }
	float GetFarPlane() const { return CameraState.FarZ; }
	float GetOrthoWidth() const { return CameraState.OrthoWidth; }
	bool IsOrthogonal() const { return CameraState.bIsOrthogonal; }

	void SetTargetActor(AActor* Target);
	AActor* ResolveTargetActor() const;
	void ClearTargetActor();
	void ClearTargetActorIfMatches(const AActor* Actor);
	uint32 GetTargetActorUUID() const { return TargetActorUUID; }

	ECameraViewMode GetViewMode() const { return static_cast<ECameraViewMode>(ViewMode); }
	int32 GetViewModeIndex() const { return ViewMode; }
	void SetViewMode(ECameraViewMode InMode);
	void SetViewModeIndex(int32 InMode);

	bool UsesOwnerAsTarget() const { return bUseOwnerAsTarget; }
	void SetUseOwnerAsTarget(bool bInUseOwnerAsTarget) { bUseOwnerAsTarget = bInUseOwnerAsTarget; }
	const FVector& GetTargetOffset() const { return TargetOffset; }
	void SetTargetOffset(const FVector& InOffset) { TargetOffset = InOffset; }

	float GetEyeHeight() const { return EyeHeight; }
	void SetEyeHeight(float InEyeHeight);
	bool FirstPersonUsesControlRotation() const { return bFirstPersonUseControlRotation; }
	void SetFirstPersonUseControlRotation(bool bInUseControlRotation) { bFirstPersonUseControlRotation = bInUseControlRotation; }

	float GetBackDistance() const { return BackDistance; }
	void SetBackDistance(float InBackDistance);
	float GetHeight() const { return Height; }
	void SetHeight(float InHeight) { Height = InHeight; }
	float GetSideOffset() const { return SideOffset; }
	void SetSideOffset(float InSideOffset) { SideOffset = InSideOffset; }
	const FVector& GetViewOffset() const { return ViewOffset; }
	void SetViewOffset(const FVector& InViewOffset) { ViewOffset = InViewOffset; }
	bool UsesTargetForward() const { return bUseTargetForward; }
	void SetUseTargetForward(bool bInUseTargetForward) { bUseTargetForward = bInUseTargetForward; }
	bool UsesControlRotationYaw() const { return bUseControlRotationYaw; }
	void SetUseControlRotationYaw(bool bInUseControlRotationYaw) { bUseControlRotationYaw = bInUseControlRotationYaw; }

	bool IsLookAheadEnabled() const { return bEnableLookAhead; }
	void SetEnableLookAhead(bool bInEnableLookAhead) { bEnableLookAhead = bInEnableLookAhead; }
	float GetLookAheadDistance() const { return LookAheadDistance; }
	void SetLookAheadDistance(float InDistance);
	float GetLookAheadLagSpeed() const { return LookAheadLagSpeed; }
	void SetLookAheadLagSpeed(float InLagSpeed);

	bool StabilizesVerticalInOrthographic() const { return bStabilizeVerticalInOrthographic; }
	void SetStabilizeVerticalInOrthographic(bool bInStabilize) { bStabilizeVerticalInOrthographic = bInStabilize; }
	float GetVerticalDeadZone() const { return VerticalDeadZone; }
	void SetVerticalDeadZone(float InDeadZone);
	float GetVerticalFollowStrength() const { return VerticalFollowStrength; }
	void SetVerticalFollowStrength(float InStrength);
	float GetVerticalLagSpeed() const { return VerticalLagSpeed; }
	void SetVerticalLagSpeed(float InLagSpeed);

	const FCameraTransitionSettings& GetTransitionSettings() const { return TransitionSettings; }
	FCameraTransitionSettings& GetMutableTransitionSettings() { return TransitionSettings; }
	const FCameraSmoothingSettings& GetSmoothingSettings() const { return SmoothingSettings; }
	FCameraSmoothingSettings& GetMutableSmoothingSettings() { return SmoothingSettings; }

	void ResetRuntimeCameraState();

	FRay DeprojectScreenToWorld(float MouseX, float MouseY, float ScreenWidth, float ScreenHeight);

private:
	void NormalizeOptions();
	bool CalcStaticView(APlayerController* Controller, float DeltaTime, FCameraView& OutView);
	bool CalcFirstPersonView(APlayerController* Controller, float DeltaTime, FCameraView& OutView);
	bool CalcThirdPersonView(APlayerController* Controller, float DeltaTime, FCameraView& OutView);
	bool CalcOrthographicFollowView(APlayerController* Controller, float DeltaTime, FCameraView& OutView);

	AActor* ResolveSubjectActor(APlayerController* Controller) const;
	FVector ComputeTargetFocusPoint(AActor* Target, float DeltaTime);
	FVector ComputeLookAheadWorld(APlayerController* Controller, float DeltaTime);
	FQuat MakeLookAtRotationQuat(const FVector& Location, const FVector& Target) const;

private:
	FCameraState CameraState;
	int32 ViewMode = static_cast<int32>(ECameraViewMode::Static);
	bool bUseOwnerAsTarget = true;
	uint32 TargetActorUUID = 0;
	FVector TargetOffset = FVector::ZeroVector;

	float EyeHeight = 1.6f;
	bool bFirstPersonUseControlRotation = true;

	float BackDistance = 5.0f;
	float Height = 2.0f;
	float SideOffset = 0.0f;
	FVector ViewOffset = FVector(-5.0f, 5.0f, 5.0f);
	bool bUseTargetForward = true;
	bool bUseControlRotationYaw = true;

	bool bEnableLookAhead = false;
	float LookAheadDistance = 1.0f;
	float LookAheadLagSpeed = 8.0f;

	bool bStabilizeVerticalInOrthographic = true;
	float VerticalDeadZone = 0.4f;
	float VerticalFollowStrength = 0.15f;
	float VerticalLagSpeed = 2.0f;

	FCameraSmoothingSettings SmoothingSettings;
	FCameraTransitionSettings TransitionSettings;

	FVector SmoothedLookAheadWorld = FVector::ZeroVector;
	FVector StableFocusPoint = FVector::ZeroVector;
	float StableFocusZ = 0.0f;
	bool bHasInitializedStableFocus = false;
};
