#include "Component/CameraComponent.h"

#include "Component/ActorComponent.h"
#include "Component/Movement/MovementComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>
#include <cstring>

IMPLEMENT_CLASS(UCameraComponent, USceneComponent)

namespace
{
	constexpr int32 ViewModeCount = 5;
	constexpr int32 BlendFunctionCount = 4;
	constexpr int32 ProjectionSwitchModeCount = 3;

	float ExpAlpha(float Speed, float DeltaTime)
	{
		if (Speed <= 0.0f || DeltaTime <= 0.0f)
		{
			return 1.0f;
		}
		return 1.0f - std::exp(-Speed * DeltaTime);
	}

	float LerpFloat(float A, float B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}

	int32 NormalizeViewModeValue(int32 Value)
	{
		switch (Value)
		{
		case static_cast<int32>(ECameraViewMode::Static):
		case static_cast<int32>(ECameraViewMode::FirstPerson):
		case static_cast<int32>(ECameraViewMode::ThirdPerson):
		case static_cast<int32>(ECameraViewMode::OrthographicFollow):
		case static_cast<int32>(ECameraViewMode::Custom):
			return Value;
		default:
			return static_cast<int32>(ECameraViewMode::Static);
		}
	}

	int32 NormalizeBlendFunctionValue(int32 Value)
	{
		switch (Value)
		{
		case static_cast<int32>(ECameraBlendFunction::Linear):
		case static_cast<int32>(ECameraBlendFunction::EaseIn):
		case static_cast<int32>(ECameraBlendFunction::EaseOut):
		case static_cast<int32>(ECameraBlendFunction::EaseInOut):
			return Value;
		default:
			return static_cast<int32>(ECameraBlendFunction::EaseInOut);
		}
	}

	int32 NormalizeProjectionSwitchModeValue(int32 Value)
	{
		switch (Value)
		{
		case static_cast<int32>(ECameraProjectionSwitchMode::SwitchAtStart):
		case static_cast<int32>(ECameraProjectionSwitchMode::SwitchAtHalf):
		case static_cast<int32>(ECameraProjectionSwitchMode::SwitchAtEnd):
			return Value;
		default:
			return static_cast<int32>(ECameraProjectionSwitchMode::SwitchAtHalf);
		}
	}

	UMovementComponent* FindBestMovementComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		UMovementComponent* BestMovement = nullptr;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			UMovementComponent* Movement = Cast<UMovementComponent>(Component);
			if (!Movement)
			{
				continue;
			}

			if (!BestMovement || Movement->GetControllerInputPriority() > BestMovement->GetControllerInputPriority())
			{
				BestMovement = Movement;
			}
		}
		return BestMovement;
	}
}

void UCameraComponent::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);

	Ar << CameraState.FOV;
	Ar << CameraState.AspectRatio;
	Ar << CameraState.NearZ;
	Ar << CameraState.FarZ;
	Ar << CameraState.OrthoWidth;
	Ar << CameraState.bIsOrthogonal;

	Ar << ViewMode;
	Ar << bUseOwnerAsTarget;
	Ar << TargetActorUUID;
	Ar << TargetOffset;

	Ar << EyeHeight;
	Ar << bFirstPersonUseControlRotation;

	Ar << BackDistance;
	Ar << Height;
	Ar << SideOffset;
	Ar << ViewOffset;
	Ar << bUseTargetForward;
	Ar << bUseControlRotationYaw;

	Ar << bEnableLookAhead;
	Ar << LookAheadDistance;
	Ar << LookAheadLagSpeed;

	Ar << bStabilizeVerticalInOrthographic;
	Ar << VerticalDeadZone;
	Ar << VerticalFollowStrength;
	Ar << VerticalLagSpeed;

	Ar << SmoothingSettings.bEnableSmoothing;
	Ar << SmoothingSettings.LocationLagSpeed;
	Ar << SmoothingSettings.RotationLagSpeed;
	Ar << SmoothingSettings.FOVLagSpeed;
	Ar << SmoothingSettings.OrthoWidthLagSpeed;

	Ar << TransitionSettings.BlendTime;

	int32 BlendFunctionValue = static_cast<int32>(TransitionSettings.Function);
	Ar << BlendFunctionValue;
	if (Ar.IsLoading())
	{
		TransitionSettings.Function = static_cast<ECameraBlendFunction>(BlendFunctionValue);
	}

	int32 ProjectionSwitchValue = static_cast<int32>(TransitionSettings.ProjectionSwitchMode);
	Ar << ProjectionSwitchValue;
	if (Ar.IsLoading())
	{
		TransitionSettings.ProjectionSwitchMode = static_cast<ECameraProjectionSwitchMode>(ProjectionSwitchValue);
	}

	Ar << TransitionSettings.bBlendLocation;
	Ar << TransitionSettings.bBlendRotation;
	Ar << TransitionSettings.bBlendFOV;
	Ar << TransitionSettings.bBlendOrthoWidth;
	Ar << TransitionSettings.bBlendNearFar;

	if (Ar.IsLoading())
	{
		NormalizeOptions();
		ResetRuntimeCameraState();
	}
}

FMatrix UCameraComponent::GetViewMatrix() const
{
	UpdateWorldMatrix();
	return FMatrix::MakeViewMatrix(GetRightVector(), GetUpVector(), GetForwardVector(), GetWorldLocation());
}

FMatrix UCameraComponent::GetProjectionMatrix() const
{
	if (!CameraState.bIsOrthogonal) {
		return FMatrix::PerspectiveFovLH(CameraState.FOV, CameraState.AspectRatio, CameraState.NearZ, CameraState.FarZ);
	}
	else {
		float HalfW = CameraState.OrthoWidth * 0.5f;
		float HalfH = HalfW / CameraState.AspectRatio;
		return FMatrix::OrthoLH(HalfW * 2.0f, HalfH * 2.0f, CameraState.NearZ, CameraState.FarZ);
	}
}

FMatrix UCameraComponent::GetViewProjectionMatrix() const
{
	return GetViewMatrix() * GetProjectionMatrix();
}

FConvexVolume UCameraComponent::GetConvexVolume() const
{
	FConvexVolume ConvexVolume;
	ConvexVolume.UpdateFromMatrix(GetViewMatrix() * GetProjectionMatrix());
	return ConvexVolume;
}

void UCameraComponent::LookAt(const FVector& Target)
{
	FVector Position = GetWorldLocation();
	FVector Diff = (Target - Position).Normalized();

	FRotator LookRotation = GetRelativeRotation();
	LookRotation.Pitch = -asinf(Diff.Z) * RAD_TO_DEG;

	if (fabsf(Diff.Z) < 0.999f) {
		LookRotation.Yaw = atan2f(Diff.Y, Diff.X) * RAD_TO_DEG;
	}

	SetRelativeRotation(LookRotation);
}

void UCameraComponent::OnResize(int32 Width, int32 Height)
{
	if (Width <= 0 || Height <= 0)
	{
		return;
	}
	CameraState.AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
}

void UCameraComponent::SetCameraState(const FCameraState& NewState)
{
	CameraState = NewState;
	NormalizeOptions();
}

FCameraView UCameraComponent::GetCameraView() const
{
	FCameraView View;
	View.Location = GetWorldLocation();
	View.Rotation = GetWorldRotationQuat();
	View.State = CameraState;
	View.bValid = true;
	return View;
}

void UCameraComponent::ApplyCameraView(const FCameraView& View)
{
	if (!View.bValid)
	{
		return;
	}

	SetWorldLocation(View.Location);
	SetWorldRotation(View.Rotation);
	SetCameraState(View.State);
}

bool UCameraComponent::CalcCameraView(APlayerController* Controller, float DeltaTime, FCameraView& OutView)
{
	NormalizeOptions();

	switch (GetViewMode())
	{
	case ECameraViewMode::FirstPerson:
		return CalcFirstPersonView(Controller, DeltaTime, OutView);
	case ECameraViewMode::ThirdPerson:
		return CalcThirdPersonView(Controller, DeltaTime, OutView);
	case ECameraViewMode::OrthographicFollow:
		return CalcOrthographicFollowView(Controller, DeltaTime, OutView);
	case ECameraViewMode::Static:
	case ECameraViewMode::Custom:
	default:
		return CalcStaticView(Controller, DeltaTime, OutView);
	}
}

void UCameraComponent::SetActiveCamera()
{
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* Controller = World->FindOrCreatePlayerController())
		{
			if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
			{
				Controller->Possess(OwnerPawn);
			}

			Controller->SetActiveCamera(this);
			Controller->GetCameraManager().SnapToActiveCamera();

			if (UCameraComponent* ViewCamera = Controller->ResolveViewCamera())
			{
				World->SetViewCamera(ViewCamera);
				World->SetActiveCamera(ViewCamera);
			}
		}
	}
}

void UCameraComponent::SetActiveCameraWithBlend()
{
	SetActiveCamera();
}

void UCameraComponent::SetTargetActor(AActor* Target)
{
	TargetActorUUID = Target ? Target->GetUUID() : 0;
	bUseOwnerAsTarget = Target == nullptr;
	if (Target && GetViewMode() == ECameraViewMode::Static)
	{
		SetViewMode(ECameraViewMode::ThirdPerson);
	}
	ResetRuntimeCameraState();
}

AActor* UCameraComponent::ResolveTargetActor() const
{
	UWorld* World = GetWorld();
	if (TargetActorUUID != 0 && World)
	{
		if (AActor* Target = World->FindActorByUUIDInWorld(TargetActorUUID))
		{
			return Target;
		}
	}

	return bUseOwnerAsTarget ? GetOwner() : nullptr;
}

AActor* UCameraComponent::ResolveSubjectActor(APlayerController* Controller) const
{
	UWorld* World = GetWorld();
	if (TargetActorUUID != 0 && World)
	{
		if (AActor* Target = World->FindActorByUUIDInWorld(TargetActorUUID))
		{
			return Target;
		}
	}

	AActor* OwnerActor = GetOwner();
	if (OwnerActor && Cast<APawn>(OwnerActor))
	{
		return OwnerActor;
	}

	if (Controller)
	{
		if (AActor* Possessed = Controller->GetPossessedActor())
		{
			return Possessed;
		}
	}

	return bUseOwnerAsTarget ? OwnerActor : nullptr;
}

void UCameraComponent::ClearTargetActor()
{
	TargetActorUUID = 0;
	bUseOwnerAsTarget = true;
	ResetRuntimeCameraState();
}

void UCameraComponent::ClearTargetActorIfMatches(const AActor* Actor)
{
	if (Actor && TargetActorUUID == Actor->GetUUID())
	{
		TargetActorUUID = 0;
	}
}

void UCameraComponent::SetViewMode(ECameraViewMode InMode)
{
	ViewMode = NormalizeViewModeValue(static_cast<int32>(InMode));
	ResetRuntimeCameraState();
}

void UCameraComponent::SetViewModeIndex(int32 InMode)
{
	ViewMode = NormalizeViewModeValue(InMode);
	ResetRuntimeCameraState();
}

void UCameraComponent::SetEyeHeight(float InEyeHeight)
{
	EyeHeight = (std::max)(InEyeHeight, 0.0f);
}

void UCameraComponent::SetBackDistance(float InBackDistance)
{
	BackDistance = (std::max)(InBackDistance, 0.0f);
}

void UCameraComponent::SetLookAheadDistance(float InDistance)
{
	LookAheadDistance = (std::max)(InDistance, 0.0f);
}

void UCameraComponent::SetLookAheadLagSpeed(float InLagSpeed)
{
	LookAheadLagSpeed = (std::max)(InLagSpeed, 0.0f);
}

void UCameraComponent::SetVerticalDeadZone(float InDeadZone)
{
	VerticalDeadZone = (std::max)(InDeadZone, 0.0f);
}

void UCameraComponent::SetVerticalFollowStrength(float InStrength)
{
	VerticalFollowStrength = Clamp(InStrength, 0.0f, 1.0f);
}

void UCameraComponent::SetVerticalLagSpeed(float InLagSpeed)
{
	VerticalLagSpeed = (std::max)(InLagSpeed, 0.0f);
}

void UCameraComponent::ResetRuntimeCameraState()
{
	SmoothedLookAheadWorld = FVector::ZeroVector;
	StableFocusPoint = FVector::ZeroVector;
	StableFocusZ = 0.0f;
	bHasInitializedStableFocus = false;
}

void UCameraComponent::RemapActorReferences(const TMap<uint32, uint32>& ActorUUIDRemap)
{
	USceneComponent::RemapActorReferences(ActorUUIDRemap);

	if (TargetActorUUID == 0)
	{
		return;
	}

	auto It = ActorUUIDRemap.find(TargetActorUUID);
	if (It != ActorUUIDRemap.end())
	{
		TargetActorUUID = It->second;
	}
	else
	{
		TargetActorUUID = 0;
	}
}

FRay UCameraComponent::DeprojectScreenToWorld(float MouseX, float MouseY, float ScreenWidth, float ScreenHeight) {
	float NdcX = (2.0f * MouseX) / ScreenWidth - 1.0f;
	float NdcY = 1.0f - (2.0f * MouseY) / ScreenHeight;

	FVector NdcNear(NdcX, NdcY, 1.0f);
	FVector NdcFar(NdcX, NdcY, 0.0f);

	FMatrix ViewProj = GetViewMatrix() * GetProjectionMatrix();
	FMatrix InverseViewProjection = ViewProj.GetInverse();

	FVector WorldNear = InverseViewProjection.TransformPositionWithW(NdcNear);
	FVector WorldFar = InverseViewProjection.TransformPositionWithW(NdcFar);

	FRay Ray;
	Ray.Origin = WorldNear;

	FVector Dir = WorldFar - WorldNear;
	float Length = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y + Dir.Z * Dir.Z);
	Ray.Direction = (Length > 1e-4f) ? Dir / Length : FVector(1, 0, 0);

	return Ray;
}

void UCameraComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	USceneComponent::GetEditableProperties(OutProps);

	static const char* ViewModeNames[] = { "Static", "First Person", "Third Person", "Orthographic Follow", "Custom" };
	static const char* BlendFunctionNames[] = { "Linear", "Ease In", "Ease Out", "Ease In Out" };
	static const char* ProjectionSwitchNames[] = { "Switch At Start", "Switch At Half", "Switch At End" };

	OutProps.push_back({ "FOV", EPropertyType::Float, &CameraState.FOV, 0.1f, 3.14f, 0.01f });
	OutProps.push_back({ "Aspect Ratio", EPropertyType::Float, &CameraState.AspectRatio, 0.01f, 10.0f, 0.01f });
	OutProps.push_back({ "Near Z", EPropertyType::Float, &CameraState.NearZ, 0.001f, 1000.0f, 0.01f });
	OutProps.push_back({ "Far Z", EPropertyType::Float, &CameraState.FarZ, 1.0f, 100000.0f, 10.0f });
	OutProps.push_back({ "Orthographic", EPropertyType::Bool, &CameraState.bIsOrthogonal });
	OutProps.push_back({ "Ortho Width", EPropertyType::Float, &CameraState.OrthoWidth, 0.1f, 10000.0f, 0.5f });

	OutProps.push_back({ "View Mode", EPropertyType::Enum, &ViewMode, 0.0f, 0.0f, 0.1f, ViewModeNames, ViewModeCount });
	OutProps.push_back({ "Follow Subject Auto", EPropertyType::Bool, &bUseOwnerAsTarget });
	OutProps.push_back({ "Follow Target", EPropertyType::ActorRef, &TargetActorUUID });
	OutProps.push_back({ "Follow Offset", EPropertyType::Vec3, &TargetOffset, 0.0f, 0.0f, 0.1f });

	OutProps.push_back({ "Eye Height", EPropertyType::Float, &EyeHeight, 0.0f, 1000.0f, 0.01f });
	OutProps.push_back({ "First Person Use Control Rotation", EPropertyType::Bool, &bFirstPersonUseControlRotation });

	OutProps.push_back({ "Back Distance", EPropertyType::Float, &BackDistance, 0.0f, 10000.0f, 0.1f });
	OutProps.push_back({ "Height", EPropertyType::Float, &Height, -10000.0f, 10000.0f, 0.1f });
	OutProps.push_back({ "Side Offset", EPropertyType::Float, &SideOffset, -10000.0f, 10000.0f, 0.1f });
	OutProps.push_back({ "View Offset", EPropertyType::Vec3, &ViewOffset, 0.0f, 0.0f, 0.1f });
	OutProps.push_back({ "Use Target Forward", EPropertyType::Bool, &bUseTargetForward });
	OutProps.push_back({ "Use Control Rotation Yaw", EPropertyType::Bool, &bUseControlRotationYaw });

	OutProps.push_back({ "Enable Look Ahead", EPropertyType::Bool, &bEnableLookAhead });
	OutProps.push_back({ "Look Ahead Distance", EPropertyType::Float, &LookAheadDistance, 0.0f, 10000.0f, 0.1f });
	OutProps.push_back({ "Look Ahead Lag Speed", EPropertyType::Float, &LookAheadLagSpeed, 0.0f, 100.0f, 0.1f });

	OutProps.push_back({ "Stabilize Vertical In Orthographic", EPropertyType::Bool, &bStabilizeVerticalInOrthographic });
	OutProps.push_back({ "Vertical Dead Zone", EPropertyType::Float, &VerticalDeadZone, 0.0f, 10000.0f, 0.01f });
	OutProps.push_back({ "Vertical Follow Strength", EPropertyType::Float, &VerticalFollowStrength, 0.0f, 1.0f, 0.01f });
	OutProps.push_back({ "Vertical Lag Speed", EPropertyType::Float, &VerticalLagSpeed, 0.0f, 100.0f, 0.1f });

	OutProps.push_back({ "Enable Smoothing", EPropertyType::Bool, &SmoothingSettings.bEnableSmoothing });
	OutProps.push_back({ "Location Lag Speed", EPropertyType::Float, &SmoothingSettings.LocationLagSpeed, 0.0f, 100.0f, 0.1f });
	OutProps.push_back({ "Rotation Lag Speed", EPropertyType::Float, &SmoothingSettings.RotationLagSpeed, 0.0f, 100.0f, 0.1f });
	OutProps.push_back({ "FOV Lag Speed", EPropertyType::Float, &SmoothingSettings.FOVLagSpeed, 0.0f, 100.0f, 0.1f });
	OutProps.push_back({ "Ortho Width Lag Speed", EPropertyType::Float, &SmoothingSettings.OrthoWidthLagSpeed, 0.0f, 100.0f, 0.1f });

	OutProps.push_back({ "Blend Time", EPropertyType::Float, &TransitionSettings.BlendTime, 0.0f, 10.0f, 0.01f });
	OutProps.push_back({ "Blend Function", EPropertyType::Enum, &TransitionSettings.Function, 0.0f, 0.0f, 0.1f, BlendFunctionNames, BlendFunctionCount });
	OutProps.push_back({ "Projection Switch Mode", EPropertyType::Enum, &TransitionSettings.ProjectionSwitchMode, 0.0f, 0.0f, 0.1f, ProjectionSwitchNames, ProjectionSwitchModeCount });
	OutProps.push_back({ "Blend Location", EPropertyType::Bool, &TransitionSettings.bBlendLocation });
	OutProps.push_back({ "Blend Rotation", EPropertyType::Bool, &TransitionSettings.bBlendRotation });
	OutProps.push_back({ "Blend FOV", EPropertyType::Bool, &TransitionSettings.bBlendFOV });
	OutProps.push_back({ "Blend Ortho Width", EPropertyType::Bool, &TransitionSettings.bBlendOrthoWidth });
	OutProps.push_back({ "Blend Near/Far", EPropertyType::Bool, &TransitionSettings.bBlendNearFar });
}

void UCameraComponent::PostEditProperty(const char* PropertyName)
{
	USceneComponent::PostEditProperty(PropertyName);
	NormalizeOptions();

	if (std::strcmp(PropertyName, "Follow Target") == 0)
	{
		bUseOwnerAsTarget = (TargetActorUUID == 0);
		if (TargetActorUUID != 0 && GetViewMode() == ECameraViewMode::Static)
		{
			SetViewMode(ECameraViewMode::ThirdPerson);
		}
		ResetRuntimeCameraState();
	}
	else if (std::strcmp(PropertyName, "Follow Subject Auto") == 0)
	{
		if (bUseOwnerAsTarget)
		{
			TargetActorUUID = 0;
		}
		ResetRuntimeCameraState();
	}
	else if (std::strcmp(PropertyName, "View Mode") == 0 ||
		std::strcmp(PropertyName, "Orthographic") == 0)
	{
		ResetRuntimeCameraState();
	}
}

void UCameraComponent::NormalizeOptions()
{
	ViewMode = NormalizeViewModeValue(ViewMode);
	CameraState.FOV = Clamp(CameraState.FOV, 0.01f, 3.13f);
	CameraState.AspectRatio = (std::max)(CameraState.AspectRatio, 0.01f);
	CameraState.NearZ = (std::max)(CameraState.NearZ, 0.001f);
	CameraState.FarZ = (std::max)(CameraState.FarZ, CameraState.NearZ + 0.001f);
	CameraState.OrthoWidth = (std::max)(CameraState.OrthoWidth, 0.001f);

	EyeHeight = (std::max)(EyeHeight, 0.0f);
	BackDistance = (std::max)(BackDistance, 0.0f);
	LookAheadDistance = (std::max)(LookAheadDistance, 0.0f);
	LookAheadLagSpeed = (std::max)(LookAheadLagSpeed, 0.0f);

	VerticalDeadZone = (std::max)(VerticalDeadZone, 0.0f);
	VerticalFollowStrength = Clamp(VerticalFollowStrength, 0.0f, 1.0f);
	VerticalLagSpeed = (std::max)(VerticalLagSpeed, 0.0f);

	SmoothingSettings.LocationLagSpeed = (std::max)(SmoothingSettings.LocationLagSpeed, 0.0f);
	SmoothingSettings.RotationLagSpeed = (std::max)(SmoothingSettings.RotationLagSpeed, 0.0f);
	SmoothingSettings.FOVLagSpeed = (std::max)(SmoothingSettings.FOVLagSpeed, 0.0f);
	SmoothingSettings.OrthoWidthLagSpeed = (std::max)(SmoothingSettings.OrthoWidthLagSpeed, 0.0f);

	TransitionSettings.BlendTime = (std::max)(TransitionSettings.BlendTime, 0.0f);
	TransitionSettings.Function = static_cast<ECameraBlendFunction>(
		NormalizeBlendFunctionValue(static_cast<int32>(TransitionSettings.Function)));
	TransitionSettings.ProjectionSwitchMode = static_cast<ECameraProjectionSwitchMode>(
		NormalizeProjectionSwitchModeValue(static_cast<int32>(TransitionSettings.ProjectionSwitchMode)));
}

bool UCameraComponent::CalcStaticView(APlayerController* Controller, float DeltaTime, FCameraView& OutView)
{
	(void)Controller;
	(void)DeltaTime;
	OutView = GetCameraView();
	return OutView.bValid;
}

bool UCameraComponent::CalcFirstPersonView(APlayerController* Controller, float DeltaTime, FCameraView& OutView)
{
	(void)DeltaTime;
	AActor* Target = ResolveSubjectActor(Controller);
	if (!Target)
	{
		return CalcStaticView(Controller, DeltaTime, OutView);
	}

	FVector Location = Target->GetActorLocation();
	Location.Z += EyeHeight;
	Location += TargetOffset;

	FQuat Rotation = GetWorldRotationQuat();
	if (bFirstPersonUseControlRotation && Controller)
	{
		Rotation = Controller->GetControlRotation().ToQuaternion();
	}

	OutView.Location = Location;
	OutView.Rotation = Rotation;
	OutView.State = CameraState;
	OutView.State.bIsOrthogonal = false;
	OutView.bValid = true;
	return true;
}

bool UCameraComponent::CalcThirdPersonView(APlayerController* Controller, float DeltaTime, FCameraView& OutView)
{
	AActor* Target = ResolveSubjectActor(Controller);
	if (!Target)
	{
		return CalcStaticView(Controller, DeltaTime, OutView);
	}

	FVector FocusPoint = ComputeTargetFocusPoint(Target, DeltaTime);
	FocusPoint += ComputeLookAheadWorld(Controller, DeltaTime);

	FVector Forward = Target->GetActorForward();
	FVector Right = FVector::Cross(FVector::UpVector, Forward);

	if (bUseControlRotationYaw && Controller)
	{
		FRotator ControlRot = Controller->GetControlRotation();
		ControlRot.Pitch = 0.0f;
		ControlRot.Roll = 0.0f;
		Forward = ControlRot.GetForwardVector();
		Right = ControlRot.GetRightVector();
	}

	if (!bUseTargetForward)
	{
		Forward = FVector::ForwardVector;
		Right = FVector::RightVector;
	}

	Forward = Forward.IsNearlyZero() ? FVector::ForwardVector : Forward.Normalized();
	Right = Right.IsNearlyZero() ? FVector::RightVector : Right.Normalized();

	FVector CameraLocation = FocusPoint - Forward * BackDistance + FVector::UpVector * Height + Right * SideOffset;

	OutView.Location = CameraLocation;
	OutView.Rotation = MakeLookAtRotationQuat(CameraLocation, FocusPoint);
	OutView.State = CameraState;
	OutView.State.bIsOrthogonal = false;
	OutView.bValid = true;
	return true;
}

bool UCameraComponent::CalcOrthographicFollowView(APlayerController* Controller, float DeltaTime, FCameraView& OutView)
{
	AActor* Target = ResolveSubjectActor(Controller);
	if (!Target)
	{
		return CalcStaticView(Controller, DeltaTime, OutView);
	}

	FVector FocusPoint = ComputeTargetFocusPoint(Target, DeltaTime);
	FocusPoint += ComputeLookAheadWorld(Controller, DeltaTime);

	FVector DesiredFocus = FocusPoint;
	if (bStabilizeVerticalInOrthographic)
	{
		if (!bHasInitializedStableFocus)
		{
			StableFocusPoint = FocusPoint;
			StableFocusZ = FocusPoint.Z;
			bHasInitializedStableFocus = true;
		}

		const float DeltaZ = FocusPoint.Z - StableFocusZ;
		if (std::fabs(DeltaZ) > VerticalDeadZone)
		{
			const float Sign = DeltaZ > 0.0f ? 1.0f : -1.0f;
			const float TargetZ = StableFocusZ + (DeltaZ - VerticalDeadZone * Sign) * VerticalFollowStrength;
			StableFocusZ = LerpFloat(StableFocusZ, TargetZ, ExpAlpha(VerticalLagSpeed, DeltaTime));
		}

		DesiredFocus.Z = StableFocusZ;
	}

	FVector CameraLocation = DesiredFocus + ViewOffset;

	OutView.Location = CameraLocation;
	OutView.Rotation = MakeLookAtRotationQuat(CameraLocation, DesiredFocus);
	OutView.State = CameraState;
	OutView.State.bIsOrthogonal = true;
	OutView.bValid = true;
	return true;
}

FVector UCameraComponent::ComputeTargetFocusPoint(AActor* Target, float DeltaTime)
{
	(void)DeltaTime;
	if (!Target)
	{
		return GetWorldLocation();
	}

	return Target->GetActorLocation() + TargetOffset;
}

FVector UCameraComponent::ComputeLookAheadWorld(APlayerController* Controller, float DeltaTime)
{
	FVector Desired = FVector::ZeroVector;

	if (bEnableLookAhead && Controller)
	{
		AActor* Target = Controller->GetPossessedActor();
		UMovementComponent* Movement = FindBestMovementComponent(Target);
		if (Movement && Movement->HasMovementInput())
		{
			FVector Direction = Movement->GetLastMovementInput();
			Direction.Z = 0.0f;
			if (!Direction.IsNearlyZero())
			{
				Desired = Direction.Normalized() * LookAheadDistance;
			}
		}
	}

	const float Alpha = ExpAlpha(LookAheadLagSpeed, DeltaTime);
	SmoothedLookAheadWorld = SmoothedLookAheadWorld + (Desired - SmoothedLookAheadWorld) * Alpha;
	return SmoothedLookAheadWorld;
}

FQuat UCameraComponent::MakeLookAtRotationQuat(const FVector& Location, const FVector& Target) const
{
	FVector Diff = Target - Location;
	if (Diff.IsNearlyZero())
	{
		return GetWorldRotationQuat();
	}

	Diff = Diff.Normalized();
	FRotator LookRotation;
	LookRotation.Pitch = -asinf(Diff.Z) * RAD_TO_DEG;
	LookRotation.Yaw = (fabsf(Diff.Z) < 0.999f) ? atan2f(Diff.Y, Diff.X) * RAD_TO_DEG : 0.0f;
	LookRotation.Roll = 0.0f;
	return LookRotation.ToQuaternion();
}
