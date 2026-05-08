#include "GameFramework/PlayerController.h"

#include "Component/ActorComponent.h"
#include "Component/CameraComponent.h"
#include "Component/ControllerInputComponent.h"
#include "Component/PawnOrientationComponent.h"
#include "Component/Movement/MovementComponent.h"
#include "Component/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/World.h"
#include "Serialization/Archive.h"

IMPLEMENT_CLASS(APlayerController, AActor)

namespace
{
	UPawnOrientationComponent* FindPawnOrientationOnActor(AActor* Target)
	{
		if (!Target)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Target->GetComponents())
		{
			if (UPawnOrientationComponent* Orientation = Cast<UPawnOrientationComponent>(Component))
			{
				return Orientation;
			}
		}
		return nullptr;
	}

	UMovementComponent* FindControllerDrivenMovementComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		UMovementComponent* BestMovement = nullptr;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			UMovementComponent* Movement = Cast<UMovementComponent>(Component);
			if (!Movement || !Movement->CanReceiveControllerInput())
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

	FRotator MakeControlRotationFromCamera(const UCameraComponent* Camera)
	{
		FRotator Rotation = Camera ? Camera->GetWorldRotation() : FRotator();
		Rotation.Roll = 0.0f;
		return Rotation;
	}
}

void APlayerController::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);
	Ar << ControlRotation;
	Ar << PossessedActorUUID;
	CameraManager.Serialize(Ar);

	if (Ar.IsLoading())
	{
		PossessedActor = nullptr;
		CameraManager.Initialize(this);
	}
}

void APlayerController::RemapActorReferences(const TMap<uint32, uint32>& ActorUUIDRemap)
{
	AActor::RemapActorReferences(ActorUUIDRemap);

	auto RemapUUID = [&ActorUUIDRemap](uint32& UUID)
	{
		if (UUID == 0)
		{
			return;
		}

		auto It = ActorUUIDRemap.find(UUID);
		if (It != ActorUUIDRemap.end())
		{
			UUID = It->second;
		}
		else
		{
			UUID = 0;
		}
	};

	RemapUUID(PossessedActorUUID);
	CameraManager.RemapActorReferences(ActorUUIDRemap);
	PossessedActor = nullptr;
}

void APlayerController::InitDefaultComponents()
{
	if (!GetRootComponent())
	{
		if (UStaticMeshComponent* Root = AddComponent<UStaticMeshComponent>())
		{
			SetRootComponent(Root);
		}
	}
	if (!FindControllerInputComponent())
	{
		AddComponent<UControllerInputComponent>();
	}
	CameraManager.Initialize(this);
}

void APlayerController::EndPlay()
{
	UnPossess();
	CameraManager.ClearActiveCamera();
	AActor::EndPlay();
}

void APlayerController::Possess(AActor* InActor)
{
	if (InActor)
	{
		UWorld* ControllerWorld = GetWorld();
		UWorld* TargetWorld = InActor->GetWorld();
		if (ControllerWorld && TargetWorld && ControllerWorld != TargetWorld)
		{
			return;
		}
	}

	AActor* PreviousPossessedActor = GetPossessedActor();
	UCameraComponent* ExistingActiveCamera = GetActiveCamera();
	const bool bUsePossessedActorCamera =
		!ExistingActiveCamera
		|| ExistingActiveCamera->GetOwner() == PreviousPossessedActor
		|| ExistingActiveCamera->GetOwner() == InActor;

	if (PossessedActor == InActor)
	{
		if (bUsePossessedActorCamera && InActor)
		{
			SetActiveCameraFromPossessedPawn();
		}
		return;
	}

	UnPossess();

	PossessedActor = InActor;
	PossessedActorUUID = InActor ? InActor->GetUUID() : 0;

	if (PossessedActor)
	{
		UPawnOrientationComponent* Orientation = FindPawnOrientationOnActor(PossessedActor);
		if (!Orientation)
		{
			Orientation = PossessedActor->AddComponent<UPawnOrientationComponent>();
		}

		if (APawn* Pawn = Cast<APawn>(PossessedActor))
		{
			Pawn->SetController(this);
		}
	}

	if (bUsePossessedActorCamera)
	{
		SetActiveCameraFromPossessedPawn();
	}
}

void APlayerController::UnPossess()
{
	AActor* Actor = GetPossessedActor();
	if (UCameraComponent* ActiveCamera = GetActiveCamera())
	{
		if (ActiveCamera->GetOwner() == Actor)
		{
			ClearActiveCamera();
		}
	}

	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		if (Pawn->GetController() == this)
		{
			Pawn->SetController(nullptr);
		}
	}
	PossessedActor = nullptr;
	PossessedActorUUID = 0;
}

AActor* APlayerController::GetPossessedActor() const
{
	AActor* Actor = PossessedActor;
	if (!Actor && PossessedActorUUID != 0)
	{
		Actor = const_cast<APlayerController*>(this)->ResolveActorUUID(PossessedActorUUID);
	}

	if (!Actor || !IsAliveObject(Actor))
	{
		return nullptr;
	}
	if (UWorld* World = GetWorld())
	{
		return World->IsActorInWorld(Actor) ? Actor : nullptr;
	}
	return Actor;
}

void APlayerController::SetActiveCamera(UCameraComponent* Camera)
{
	if (!Camera)
	{
		ClearActiveCamera();
		return;
	}

	if (APawn* OwnerPawn = Cast<APawn>(Camera->GetOwner()))
	{
		if (GetPossessedActor() != OwnerPawn)
		{
			Possess(OwnerPawn);
		}
	}

	CameraManager.Initialize(this);
	CameraManager.SetActiveCamera(Camera, false);
	ControlRotation = MakeControlRotationFromCamera(Camera);
}

void APlayerController::SetActiveCameraWithBlend(UCameraComponent* Camera)
{
	if (!Camera)
	{
		ClearActiveCamera();
		return;
	}

	if (APawn* OwnerPawn = Cast<APawn>(Camera->GetOwner()))
	{
		if (GetPossessedActor() != OwnerPawn)
		{
			Possess(OwnerPawn);
		}
	}

	CameraManager.Initialize(this);
	CameraManager.SetActiveCamera(Camera, true);
}

bool APlayerController::SetActiveCameraFromPossessedPawn()
{
	UCameraComponent* Camera = FindCameraOnActor(GetPossessedActor());
	if (!Camera)
	{
		return false;
	}
	SetActiveCamera(Camera);
	return true;
}

void APlayerController::ClearActiveCamera()
{
	CameraManager.ClearActiveCamera();
}

UCameraComponent* APlayerController::GetActiveCamera() const
{
	return CameraManager.GetActiveCamera();
}

UCameraComponent* APlayerController::ResolveViewCamera() const
{
	if (UCameraComponent* OutputCamera = CameraManager.GetOutputCameraIfValid())
	{
		return OutputCamera;
	}
	if (UCameraComponent* ActiveCamera = CameraManager.GetActiveCamera())
	{
		return ActiveCamera;
	}
	if (UCameraComponent* Camera = FindCameraOnActor(GetPossessedActor()))
	{
		return Camera;
	}
	return nullptr;
}

void APlayerController::ClearCameraReferencesForActor(const AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	CameraManager.ClearCameraReferencesForActor(Actor);
	if (PossessedActor == Actor || PossessedActorUUID == Actor->GetUUID())
	{
		UnPossess();
	}
}

void APlayerController::ClearCameraReferencesForComponent(const UActorComponent* Component)
{
	CameraManager.ClearCameraReferencesForComponent(Component);
}

void APlayerController::SetControlRotation(const FRotator& InRotation)
{
	ControlRotation = InRotation;
	ControlRotation.Roll = 0.0f;
}

void APlayerController::AddYawInput(float Value)
{
	FRotator Rotation = GetControlRotation();
	Rotation.Yaw += Value;
	SetControlRotation(Rotation);
}

void APlayerController::AddPitchInput(float Value)
{
	FRotator Rotation = GetControlRotation();
	Rotation.Pitch += Value;
	SetControlRotation(Rotation);
}

bool APlayerController::AddMovementInput(const FVector& WorldDirection, float Scale, float DeltaTime)
{
	AActor* Actor = GetPossessedActor();
	if (!Actor || WorldDirection.IsNearlyZero() || Scale == 0.0f)
	{
		return false;
	}

	FControllerMovementInput Input;
	Input.WorldDirection = WorldDirection.Normalized();
	Input.LocalInput = Input.WorldDirection;
	Input.WorldDelta = Input.WorldDirection * Scale;
	Input.MovementForward = Input.WorldDirection;
	Input.MovementRight = FVector(-Input.WorldDirection.Y, Input.WorldDirection.X, 0.0f);
	Input.SpeedMultiplier = Scale;
	Input.DeltaTime = DeltaTime;
	return ApplyControllerMovementInput(Input);
}

bool APlayerController::ApplyControllerMovementInput(const FControllerMovementInput& Input)
{
	AActor* Actor = GetPossessedActor();
	if (!Actor || Input.WorldDirection.IsNearlyZero() || Input.WorldDelta.IsNearlyZero())
	{
		return false;
	}

	if (UMovementComponent* Movement = FindControllerDrivenMovementComponent(Actor))
	{
		return Movement->ApplyControllerMovementInput(Input);
	}

	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		Pawn->AddMovementInput(Input.WorldDirection, Input.WorldDelta.Length());
		return true;
	}

	return false;
}

UControllerInputComponent* APlayerController::FindControllerInputComponent() const
{
	for (UActorComponent* Component : GetComponents())
	{
		if (UControllerInputComponent* Input = Cast<UControllerInputComponent>(Component))
		{
			return Input;
		}
	}
	return nullptr;
}

UCameraComponent* APlayerController::FindCameraOnActor(AActor* Target) const
{
	if (!Target)
	{
		return nullptr;
	}

	for (UActorComponent* Component : Target->GetComponents())
	{
		if (!Component || Component->IsHiddenInComponentTree())
		{
			continue;
		}

		if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
		{
			return Camera;
		}
	}
	return nullptr;
}

AActor* APlayerController::ResolveActorUUID(uint32 ActorUUID) const
{
	if (ActorUUID == 0)
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	return World ? World->FindActorByUUIDInWorld(ActorUUID) : nullptr;
}
