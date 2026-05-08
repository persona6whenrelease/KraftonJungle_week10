#include "GameFramework/Pawn.h"

#include "Component/ActorComponent.h"
#include "Component/CameraComponent.h"
#include "Component/Movement/PawnMovementComponent.h"
#include "Component/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/World.h"

IMPLEMENT_CLASS(APawn, AActor)


void APawn::InitDefaultComponents()
{
	if (!GetRootComponent())
	{
		if (UStaticMeshComponent* Root = AddComponent<UStaticMeshComponent>())
		{
			SetRootComponent(Root);
		}
	}
	if (!FindPawnMovementComponent())
	{
		AddComponent<UPawnMovementComponent>();
	}
}

void APawn::EndPlay()
{
	if (APlayerController* CurrentController = GetController())
	{
		if (CurrentController->GetPossessedActor() == this)
		{
			CurrentController->UnPossess();
		}
	}
	Controller = nullptr;
	AActor::EndPlay();
}

APlayerController* APawn::GetController() const
{
	if (!Controller || !IsAliveObject(Controller))
	{
		return nullptr;
	}
	if (UWorld* World = GetWorld())
	{
		return World->IsActorInWorld(Controller) ? Controller : nullptr;
	}
	return Controller;
}

void APawn::AddMovementInput(const FVector& Direction, float Scale)
{
	if (UPawnMovementComponent* Movement = FindPawnMovementComponent())
	{
		Movement->AddMovementInput(Direction, Scale);
	}
}

FVector APawn::ConsumeMovementInputVector()
{
	if (UPawnMovementComponent* Movement = FindPawnMovementComponent())
	{
		return Movement->ConsumeMovementInputVector();
	}
	return FVector::ZeroVector;
}

FVector APawn::GetPendingMovementInputVector() const
{
	if (UPawnMovementComponent* Movement = FindPawnMovementComponent())
	{
		return Movement->GetPendingMovementInputVector();
	}
	return FVector::ZeroVector;
}

UCameraComponent* APawn::FindPawnCamera() const
{
	for (UActorComponent* Component : GetComponents())
	{
		if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
		{
			return Camera;
		}
	}
	return nullptr;
}

UPawnMovementComponent* APawn::FindPawnMovementComponent() const
{
	for (UActorComponent* Component : GetComponents())
	{
		if (UPawnMovementComponent* Movement = Cast<UPawnMovementComponent>(Component))
		{
			return Movement;
		}
	}
	return nullptr;
}
