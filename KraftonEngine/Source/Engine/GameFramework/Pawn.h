#pragma once
#include "GameFramework/AActor.h"

class APlayerController;
class UCameraComponent;
class UPawnMovementComponent;

class APawn : public AActor
{
public:
	DECLARE_CLASS(APawn, AActor)

	APawn() = default;
	~APawn() override = default;

	void InitDefaultComponents() override;
	void EndPlay() override;

	void SetController(APlayerController* InController) { Controller = InController; }
	APlayerController* GetController() const;
	bool IsPossessed() const { return GetController() != nullptr; }

	void AddMovementInput(const FVector& Direction, float Scale = 1.0f);
	FVector ConsumeMovementInputVector();
	FVector GetPendingMovementInputVector() const;

	UCameraComponent* FindPawnCamera() const;
	UPawnMovementComponent* FindPawnMovementComponent() const;

private:
	APlayerController* Controller = nullptr;
};
