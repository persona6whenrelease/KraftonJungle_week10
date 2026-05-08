#include "Camera/PlayerCameraManager.h"

#include "Component/CameraComponent.h"
#include "Component/ComponentReferenceUtils.h"
#include "GameFramework/AActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/World.h"
#include "Math/MathUtils.h"
#include "Object/FName.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

namespace
{
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

	FVector LerpVector(const FVector& A, const FVector& B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}
}

void FPlayerCameraManager::Initialize(APlayerController* InOwner)
{
	OwnerController = InOwner;
	EnsureOutputCamera();
}

void FPlayerCameraManager::Serialize(FArchive& Ar)
{
	Ar << ActiveCameraRef.OwnerActorUUID;
	Ar << ActiveCameraRef.ComponentPath;
	Ar << PendingCameraRef.OwnerActorUUID;
	Ar << PendingCameraRef.ComponentPath;

	if (Ar.IsLoading())
	{
		ActiveCameraCached = nullptr;
		PendingCameraCached = nullptr;
		OutputCameraComponent = nullptr;
		CurrentView = FCameraView();
		BlendFromView = FCameraView();
		BlendElapsedTime = 0.0f;
		bIsBlending = false;
	}
}

void FPlayerCameraManager::SetActiveCamera(UCameraComponent* Camera, bool bBlend)
{
	if (!Camera)
	{
		ClearActiveCamera();
		return;
	}

	EnsureOutputCamera();

	if (!bBlend || !CurrentView.bValid)
	{
		ActiveCameraRef = MakeCameraReference(Camera);
		ActiveCameraCached = Camera;
		PendingCameraRef.Reset();
		PendingCameraCached = nullptr;
		bIsBlending = false;
		BlendElapsedTime = 0.0f;
		SnapToActiveCamera();
		return;
	}

	BlendFromView = CurrentView;
	PendingCameraRef = MakeCameraReference(Camera);
	PendingCameraCached = Camera;
	bIsBlending = true;
	BlendElapsedTime = 0.0f;
}

void FPlayerCameraManager::ClearActiveCamera()
{
	ActiveCameraRef.Reset();
	PendingCameraRef.Reset();
	ActiveCameraCached = nullptr;
	PendingCameraCached = nullptr;
	CurrentView = FCameraView();
	BlendFromView = FCameraView();
	bIsBlending = false;
	BlendElapsedTime = 0.0f;
}

UCameraComponent* FPlayerCameraManager::GetActiveCamera() const
{
	if (ActiveCameraCached && IsAliveObject(ActiveCameraCached))
	{
		return ActiveCameraCached;
	}
	return ResolveCameraReference(ActiveCameraRef);
}

bool FPlayerCameraManager::HasValidOutputCamera() const
{
	return OutputCameraComponent
		&& IsAliveObject(OutputCameraComponent)
		&& CurrentView.bValid
		&& (ActiveCameraRef.IsSet() || PendingCameraRef.IsSet());
}

UCameraComponent* FPlayerCameraManager::GetOutputCameraIfValid() const
{
	return HasValidOutputCamera() ? OutputCameraComponent : nullptr;
}

void FPlayerCameraManager::UpdateCamera(float DeltaTime)
{
	EnsureOutputCamera();
	if (!OutputCameraComponent)
	{
		return;
	}

	UCameraComponent* TargetCamera = bIsBlending
		? (PendingCameraCached && IsAliveObject(PendingCameraCached) ? PendingCameraCached : ResolveCameraReference(PendingCameraRef))
		: (ActiveCameraCached && IsAliveObject(ActiveCameraCached) ? ActiveCameraCached : ResolveCameraReference(ActiveCameraRef));

	if (!TargetCamera)
	{
		if (bIsBlending)
		{
			PendingCameraRef.Reset();
			PendingCameraCached = nullptr;
			bIsBlending = false;
			BlendElapsedTime = 0.0f;
			TargetCamera = ActiveCameraCached && IsAliveObject(ActiveCameraCached)
				? ActiveCameraCached
				: ResolveCameraReference(ActiveCameraRef);
		}

		if (!TargetCamera)
		{
			ClearActiveCamera();
			return;
		}
	}

	FCameraView DesiredView;
	if (!TargetCamera->CalcCameraView(OwnerController, DeltaTime, DesiredView))
	{
		return;
	}

	if (!CurrentView.bValid)
	{
		CurrentView = DesiredView;
		OutputCameraComponent->ApplyCameraView(CurrentView);
		return;
	}

	const FCameraTransitionSettings& Transition = TargetCamera->GetTransitionSettings();
	const FCameraSmoothingSettings& Smoothing = TargetCamera->GetSmoothingSettings();

	if (bIsBlending)
	{
		BlendElapsedTime += DeltaTime;
		const float Duration = Transition.BlendTime;
		const float RawAlpha = Duration > 0.0f ? Clamp(BlendElapsedTime / Duration, 0.0f, 1.0f) : 1.0f;
		const float Alpha = EvaluateBlendAlpha(RawAlpha, Transition.Function);
		CurrentView = BlendViews(BlendFromView, DesiredView, Alpha, Transition);

		if (RawAlpha >= 1.0f)
		{
			ActiveCameraRef = PendingCameraRef;
			ActiveCameraCached = PendingCameraCached;
			PendingCameraRef.Reset();
			PendingCameraCached = nullptr;
			bIsBlending = false;
			BlendElapsedTime = 0.0f;
		}
	}
	else if (Smoothing.bEnableSmoothing)
	{
		const float LocationAlpha = ExpAlpha(Smoothing.LocationLagSpeed, DeltaTime);
		const float RotationAlpha = ExpAlpha(Smoothing.RotationLagSpeed, DeltaTime);
		const float FOVAlpha = ExpAlpha(Smoothing.FOVLagSpeed, DeltaTime);
		const float OrthoAlpha = ExpAlpha(Smoothing.OrthoWidthLagSpeed, DeltaTime);

		CurrentView.Location = LerpVector(CurrentView.Location, DesiredView.Location, LocationAlpha);
		CurrentView.Rotation = FQuat::Slerp(CurrentView.Rotation, DesiredView.Rotation, RotationAlpha);

		FCameraState NewState = DesiredView.State;
		NewState.FOV = LerpFloat(CurrentView.State.FOV, DesiredView.State.FOV, FOVAlpha);
		NewState.OrthoWidth = LerpFloat(CurrentView.State.OrthoWidth, DesiredView.State.OrthoWidth, OrthoAlpha);
		CurrentView.State = NewState;
		CurrentView.bValid = true;
	}
	else
	{
		CurrentView = DesiredView;
	}

	OutputCameraComponent->ApplyCameraView(CurrentView);
}

void FPlayerCameraManager::SnapToActiveCamera()
{
	EnsureOutputCamera();
	UCameraComponent* ActiveCamera = GetActiveCamera();
	if (!ActiveCamera || !OutputCameraComponent)
	{
		return;
	}

	FCameraView DesiredView;
	if (ActiveCamera->CalcCameraView(OwnerController, 0.0f, DesiredView))
	{
		CurrentView = DesiredView;
		OutputCameraComponent->ApplyCameraView(CurrentView);
	}
}

void FPlayerCameraManager::RemapActorReferences(const TMap<uint32, uint32>& ActorUUIDRemap)
{
	auto RemapRef = [&ActorUUIDRemap](FCameraComponentReference& Ref)
	{
		if (Ref.OwnerActorUUID == 0)
		{
			return;
		}

		auto It = ActorUUIDRemap.find(Ref.OwnerActorUUID);
		if (It != ActorUUIDRemap.end())
		{
			Ref.OwnerActorUUID = It->second;
		}
		else
		{
			Ref.Reset();
		}
	};

	RemapRef(ActiveCameraRef);
	RemapRef(PendingCameraRef);
	ActiveCameraCached = nullptr;
	PendingCameraCached = nullptr;
	bIsBlending = false;
	BlendElapsedTime = 0.0f;
}

void FPlayerCameraManager::ClearCameraReferencesForActor(const AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	const uint32 ActorUUID = Actor->GetUUID();
	if (ActiveCameraRef.OwnerActorUUID == ActorUUID)
	{
		ActiveCameraRef.Reset();
		ActiveCameraCached = nullptr;
		CurrentView = FCameraView();
		BlendFromView = FCameraView();
	}
	if (PendingCameraRef.OwnerActorUUID == ActorUUID)
	{
		PendingCameraRef.Reset();
		PendingCameraCached = nullptr;
		bIsBlending = false;
	}
	if (OutputCameraComponent && OutputCameraComponent->GetOwner() == Actor)
	{
		OutputCameraComponent = nullptr;
	}
}

void FPlayerCameraManager::ClearCameraReferencesForComponent(const UActorComponent* Component)
{
	const UCameraComponent* Camera = Cast<UCameraComponent>(Component);
	if (!Camera)
	{
		return;
	}

	const FCameraComponentReference RemovedRef = MakeCameraReference(const_cast<UCameraComponent*>(Camera));
	const bool bMatchesActiveRef = RemovedRef.IsSet()
		&& ActiveCameraRef.OwnerActorUUID == RemovedRef.OwnerActorUUID
		&& ActiveCameraRef.ComponentPath == RemovedRef.ComponentPath;
	const bool bMatchesPendingRef = RemovedRef.IsSet()
		&& PendingCameraRef.OwnerActorUUID == RemovedRef.OwnerActorUUID
		&& PendingCameraRef.ComponentPath == RemovedRef.ComponentPath;

	if (ActiveCameraCached == Camera || bMatchesActiveRef)
	{
		ActiveCameraRef.Reset();
		ActiveCameraCached = nullptr;
		CurrentView = FCameraView();
		BlendFromView = FCameraView();
	}
	if (PendingCameraCached == Camera || bMatchesPendingRef)
	{
		PendingCameraRef.Reset();
		PendingCameraCached = nullptr;
		bIsBlending = false;
	}
	if (OutputCameraComponent == Camera)
	{
		OutputCameraComponent = nullptr;
	}
}

UCameraComponent* FPlayerCameraManager::ResolveCameraReference(const FCameraComponentReference& Ref) const
{
	if (!Ref.IsSet() || !OwnerController)
	{
		return nullptr;
	}

	UWorld* World = OwnerController->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AActor* OwnerActor = World->FindActorByUUIDInWorld(Ref.OwnerActorUUID);
	if (!OwnerActor)
	{
		return nullptr;
	}

	return Cast<UCameraComponent>(ComponentReferenceUtils::ResolveComponentPath(OwnerActor, Ref.ComponentPath));
}

FCameraComponentReference FPlayerCameraManager::MakeCameraReference(UCameraComponent* Camera) const
{
	FCameraComponentReference Ref;
	if (!Camera || !Camera->GetOwner())
	{
		return Ref;
	}

	Ref.OwnerActorUUID = Camera->GetOwner()->GetUUID();
	Ref.ComponentPath = ComponentReferenceUtils::MakeComponentPath(Camera);
	return Ref;
}

FCameraView FPlayerCameraManager::BlendViews(
	const FCameraView& From,
	const FCameraView& To,
	float Alpha,
	const FCameraTransitionSettings& Params) const
{
	if (!From.bValid || !To.bValid)
	{
		return To;
	}

	FCameraView Out = To;
	Out.bValid = true;

	if (Params.bBlendLocation)
	{
		Out.Location = LerpVector(From.Location, To.Location, Alpha);
	}
	if (Params.bBlendRotation)
	{
		Out.Rotation = FQuat::Slerp(From.Rotation, To.Rotation, Alpha);
	}
	if (Params.bBlendFOV)
	{
		Out.State.FOV = LerpFloat(From.State.FOV, To.State.FOV, Alpha);
	}
	if (Params.bBlendOrthoWidth)
	{
		Out.State.OrthoWidth = LerpFloat(From.State.OrthoWidth, To.State.OrthoWidth, Alpha);
	}
	if (Params.bBlendNearFar)
	{
		Out.State.NearZ = LerpFloat(From.State.NearZ, To.State.NearZ, Alpha);
		Out.State.FarZ = LerpFloat(From.State.FarZ, To.State.FarZ, Alpha);
	}
	else
	{
		Out.State.NearZ = To.State.NearZ;
		Out.State.FarZ = To.State.FarZ;
	}

	Out.State.AspectRatio = To.State.AspectRatio;

	switch (Params.ProjectionSwitchMode)
	{
	case ECameraProjectionSwitchMode::SwitchAtStart:
		Out.State.bIsOrthogonal = To.State.bIsOrthogonal;
		break;
	case ECameraProjectionSwitchMode::SwitchAtEnd:
		Out.State.bIsOrthogonal = Alpha >= 1.0f ? To.State.bIsOrthogonal : From.State.bIsOrthogonal;
		break;
	case ECameraProjectionSwitchMode::SwitchAtHalf:
	default:
		Out.State.bIsOrthogonal = Alpha >= 0.5f ? To.State.bIsOrthogonal : From.State.bIsOrthogonal;
		break;
	}

	return Out;
}

float FPlayerCameraManager::EvaluateBlendAlpha(float RawAlpha, ECameraBlendFunction Function) const
{
	const float A = Clamp(RawAlpha, 0.0f, 1.0f);
	switch (Function)
	{
	case ECameraBlendFunction::EaseIn:
		return A * A;
	case ECameraBlendFunction::EaseOut:
		return 1.0f - (1.0f - A) * (1.0f - A);
	case ECameraBlendFunction::EaseInOut:
		return A * A * (3.0f - 2.0f * A);
	case ECameraBlendFunction::Linear:
	default:
		return A;
	}
}

void FPlayerCameraManager::EnsureOutputCamera()
{
	if (OutputCameraComponent && IsAliveObject(OutputCameraComponent))
	{
		return;
	}

	if (!OwnerController)
	{
		return;
	}

	OutputCameraComponent = OwnerController->AddComponent<UCameraComponent>();
	if (OutputCameraComponent)
	{
		OutputCameraComponent->SetFName(FName("PlayerOutputCamera"));
		OutputCameraComponent->SetHiddenInComponentTree(true);
		OutputCameraComponent->SetAutoActivate(false);
	}
}
