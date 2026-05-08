#include "GameFramework/PlayerController.h"

#include "Object/ObjectFactory.h"
#include "Component/ActorComponent.h"
#include "Component/CameraComponent.h"
#include "Component/ControllerInputComponent.h"
#include "Component/PawnOrientationComponent.h"
#include "Component/Movement/MovementComponent.h"
#include "Component/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/World.h"
#include "Serialization/Archive.h"
#include "Camera\CameraShakeModifier.h"

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

APlayerController::~APlayerController()
{
	DestroyCameraManager();
}

void APlayerController::Serialize(FArchive& Ar)
{
	AActor::Serialize(Ar);
	Ar << ControlRotation;
	Ar << PossessedActorUUID;
	EnsureCameraManager()->SerializeCameraState(Ar);

	if (Ar.IsLoading())
	{
		PossessedActor = nullptr;
		EnsureCameraManager()->Initialize(this);
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
	EnsureCameraManager()->RemapActorReferences(ActorUUIDRemap);
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
	EnsureCameraManager();
}

void APlayerController::EndPlay()
{
	UnPossess();
	if (CameraManager && IsAliveObject(CameraManager))
	{
		CameraManager->ClearActiveCamera();
	}
	DestroyCameraManager();
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

	EnsureCameraManager()->SetActiveCamera(Camera, false);
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

	EnsureCameraManager()->SetActiveCamera(Camera, true);
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
	if (CameraManager && IsAliveObject(CameraManager))
	{
		CameraManager->ClearActiveCamera();
	}
}

UCameraComponent* APlayerController::GetActiveCamera() const
{
	return const_cast<APlayerController*>(this)->EnsureCameraManager()->GetActiveCamera();
}

UCameraComponent* APlayerController::ResolveViewCamera() const
{
	APlayerCameraManager* Manager = const_cast<APlayerController*>(this)->EnsureCameraManager();

	if (UCameraComponent* OutputCamera = Manager->GetOutputCameraIfValid())
	{
		return OutputCamera;
	}
	if (UCameraComponent* ActiveCamera = Manager->GetActiveCamera())
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

	EnsureCameraManager()->ClearCameraReferencesForActor(Actor);
	if (PossessedActor == Actor || PossessedActorUUID == Actor->GetUUID())
	{
		UnPossess();
	}
}

void APlayerController::ClearCameraReferencesForComponent(const UActorComponent* Component)
{
	EnsureCameraManager()->ClearCameraReferencesForComponent(Component);
}

void APlayerController::StartCameraShake(
	float Duration,
	float LocationAmplitude,
	float RotationAmplitude,
	float Frequency,
	float FOVAmplitude,
	bool bSingleInstance)
{
	if (Duration <= 0.0f)
	{
		return;
	}

	const float SafeLocAmp = LocationAmplitude < 0.0f ? -LocationAmplitude : LocationAmplitude;
	const float SafeRotAmp = RotationAmplitude < 0.0f ? -RotationAmplitude : RotationAmplitude;
	const float SafeFreq = Frequency < 0.0f ? 0.0f : Frequency;

	FCameraShakeParams Params;
	Params.Duration = Duration;
	Params.LocationAmplitude = FVector(SafeLocAmp, SafeLocAmp, SafeLocAmp);
	Params.RotationAmplitude = FRotator(SafeRotAmp, SafeRotAmp, SafeRotAmp);
	Params.Frequency = SafeFreq;
	Params.FOVAmplitude = FOVAmplitude;
	Params.bSingleInstance = bSingleInstance;

	GetCameraManager().StartCameraShake(Params);
}

void APlayerController::SetCameraVignette(float Intensity, float Smoothness, const FVector& Color)
{
	UCameraComponent* TargetCamera = GetActiveCamera();
	if (!TargetCamera)
	{
		return;
	}

	FCameraPostProcess& PP = TargetCamera->GetMutablePostProcess();
	PP.VignetteIntensity = Intensity < 0.0f ? 0.0f : Intensity;
	PP.VignetteSmoothness = Smoothness < 0.0f ? 0.0f : Smoothness;
	PP.VignetteColor = Color;

	if (UCameraComponent* OutputCamera = GetCameraManagerPtr() ? GetCameraManagerPtr()->GetOutputCameraIfValid() : nullptr)
	{
		FCameraPostProcess& OutputPP = OutputCamera->GetMutablePostProcess();
		OutputPP.VignetteIntensity = PP.VignetteIntensity;
		OutputPP.VignetteSmoothness = PP.VignetteSmoothness;
		OutputPP.VignetteColor = PP.VignetteColor;
	}
}

void APlayerController::SetCameraFade(float Alpha, const FVector& Color)
{
	UCameraComponent* TargetCamera = GetActiveCamera();
	if (!TargetCamera)
	{
		return;
	}

	FCameraPostProcess& PP = TargetCamera->GetMutablePostProcess();
	PP.FadeAlpha = Alpha < 0.0f ? 0.0f : (Alpha > 1.0f ? 1.0f : Alpha);
	PP.FadeColor = Color;

	if (UCameraComponent* OutputCamera = GetCameraManagerPtr() ? GetCameraManagerPtr()->GetOutputCameraIfValid() : nullptr)
	{
		FCameraPostProcess& OutputPP = OutputCamera->GetMutablePostProcess();
		OutputPP.FadeAlpha = PP.FadeAlpha;
		OutputPP.FadeColor = PP.FadeColor;
	}
}

void APlayerController::ResetCameraPostProcess()
{
	UCameraComponent* TargetCamera = GetActiveCamera();
	if (!TargetCamera)
	{
		return;
	}

	TargetCamera->SetPostProcess(FCameraPostProcess());

	if (UCameraComponent* OutputCamera = GetCameraManagerPtr() ? GetCameraManagerPtr()->GetOutputCameraIfValid() : nullptr)
	{
		OutputCamera->SetPostProcess(FCameraPostProcess());
	}
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

APlayerCameraManager* APlayerController::EnsureCameraManager()
{
	if (CameraManager && IsAliveObject(CameraManager))
	{
		return CameraManager;
	}

	CameraManager = UObjectManager::Get().CreateObject<APlayerCameraManager>(this);
	if (CameraManager)
	{
		CameraManager->SetFName(FName("PlayerCameraManager"));
		CameraManager->Initialize(this);
	}

	return CameraManager;
}

void APlayerController::DestroyCameraManager()
{
	if (CameraManager && IsAliveObject(CameraManager))
	{
		CameraManager->EndPlay();
		UObjectManager::Get().DestroyObject(CameraManager);
	}

	CameraManager = nullptr;
}

APlayerCameraManager& APlayerController::GetCameraManager()
{
	return *EnsureCameraManager();
}

const APlayerCameraManager& APlayerController::GetCameraManager() const
{
	return *const_cast<APlayerController*>(this)->EnsureCameraManager();
}

APlayerCameraManager* APlayerController::GetCameraManagerPtr() const
{
	return CameraManager && IsAliveObject(CameraManager) ? CameraManager : nullptr;
}