#include "Component/SpringArmComponent.h"

#include "Component/CameraComponent.h"
#include "Core/RayTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cstring>

IMPLEMENT_CLASS(USpringArmComponent, USceneComponent)

USpringArmComponent::USpringArmComponent()
{
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	CurrentArmLength = TargetArmLength;
}
void USpringArmComponent::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);

	Ar << TargetArmLength;
	Ar << TargetOffset;
	Ar << SocketOffset;

	Ar << bDoCollisionTest;
	Ar << ProbeSize;
	Ar << MinArmLength;

	Ar << bUsePawnControlRotation;
	Ar << bInheritPitch;
	Ar << bInheritYaw;
	Ar << bInheritRoll;

	Ar << bEnableCameraLag;
	Ar << CameraLagSpeed;
	Ar << CollisionPullInSpeed;
	Ar << CollisionRecoverSpeed;

	if (Ar.IsLoading())
	{
		NormalizeOption();
		CurrentArmLength = TargetArmLength;
		LaggedCameraLocation = FVector::ZeroVector;
		bHasLaggedCameraLocation = false;
		bCollisionFixApplied = false;
	}
}

void USpringArmComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	USceneComponent::GetEditableProperties(OutProps);

	OutProps.push_back({ "Target Arm Length",EPropertyType::Float,&TargetArmLength,0.0f,10000.0f,0.1f });
	OutProps.push_back({ "Target Offset",EPropertyType::Vec3,&TargetOffset,0.0f,0.0f,0.1f });
	OutProps.push_back({ "Socket Offset",EPropertyType::Vec3,&SocketOffset,0.0f,0.0f,0.1f });

	OutProps.push_back({ "Do Collision Test",EPropertyType::Bool,&bDoCollisionTest });
	OutProps.push_back({ "Probe Size",EPropertyType::Float,&ProbeSize,0.0f,1000.0f,0.01f });
	OutProps.push_back({ "Min Arm Length",EPropertyType::Float,&MinArmLength,0.0f,10000.0f,0.01f });

	OutProps.push_back({ "Use Pawn Control Rotation",EPropertyType::Bool,&bUsePawnControlRotation });
	OutProps.push_back({ "Inherit Pitch",EPropertyType::Bool,&bInheritPitch });
	OutProps.push_back({ "Inherit Yaw",EPropertyType::Bool,&bInheritYaw });
	OutProps.push_back({ "Inherit Roll",EPropertyType::Bool,&bInheritRoll });

	OutProps.push_back({ "Enable Camera Lag",EPropertyType::Bool,&bEnableCameraLag });
	OutProps.push_back({ "Camera Lag Speed",EPropertyType::Float,&CameraLagSpeed,0.0f,100.0f,0.1f });
	OutProps.push_back({ "Collision Pull In Speed",EPropertyType::Float,&CollisionPullInSpeed,0.0f,1000.0f,1.0f });
	OutProps.push_back({ "Collision Recover Speed",EPropertyType::Float,&CollisionRecoverSpeed,0.0f,1000.0f,0.1f });
}

void USpringArmComponent::PostEditProperty(const char* PropertyName)
{
	USceneComponent::PostEditProperty(PropertyName);
	NormalizeOption();

	if (std::strcmp(PropertyName, "Target Arm Length") == 0)
	{
		CurrentArmLength = TargetArmLength;
		bHasLaggedCameraLocation = false;
	}
}
void USpringArmComponent::SetTargetArmLength(float InLength)
{
	TargetArmLength = FMath::Max(InLength, 0.0f);
	CurrentArmLength = FMath::Clamp(CurrentArmLength, MinArmLength, TargetArmLength);
}
void USpringArmComponent::SetProbeSize(float InProbeSize)
{
	ProbeSize = FMath::Max(InProbeSize, 0.0f);
}
void USpringArmComponent::SetCameraLagSpeed(float InSpeed)
{
	CameraLagSpeed = FMath::Max(InSpeed, 0.0f);
}
void USpringArmComponent::RefreshCameraTransform(float DeltaTime)
{
	NormalizeOption();

	const FQuat DesiredRotation = GetDesiredRotation();
	const FVector Pivot = GetWorldLocation() + TargetOffset;
	const FVector DesiredCameraLocation = ComputeDesiredCameraLocation(Pivot, DesiredRotation);
	FVector ResolvedCameraLocation = ResolveCollision(Pivot, DesiredCameraLocation, DeltaTime);

	if (bEnableCameraLag && !bCollisionFixApplied)
	{
		if (!bHasLaggedCameraLocation)
		{
			LaggedCameraLocation = ResolvedCameraLocation;
			bHasLaggedCameraLocation = true;
		}
		else
		{
			LaggedCameraLocation = FMath::VInterpTo(LaggedCameraLocation, ResolvedCameraLocation, DeltaTime, CameraLagSpeed);
			ResolvedCameraLocation = LaggedCameraLocation;
		}
	}
	else
	{
		LaggedCameraLocation = ResolvedCameraLocation;
		bHasLaggedCameraLocation = true;
	}

	UpdateCameraChildren(ResolvedCameraLocation, DesiredRotation);
}

void USpringArmComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	USceneComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshCameraTransform(DeltaTime);
}

void USpringArmComponent::NormalizeOption()
{
	TargetArmLength = FMath::Max(TargetArmLength, 0.0f);
	ProbeSize = FMath::Max(ProbeSize, 0.0f);
	MinArmLength = FMath::Clamp(MinArmLength, 0.0f, TargetArmLength);

	CameraLagSpeed = FMath::Max(CameraLagSpeed, 0.0f);
	CollisionPullInSpeed = FMath::Max(CollisionPullInSpeed, 0.0f);
	CollisionRecoverSpeed = FMath::Max(CollisionRecoverSpeed, 0.0f);
}
FQuat USpringArmComponent::GetDesiredRotation() const
{
	FRotator DesireRot = GetWorldRotation();
	if (bUsePawnControlRotation)
	{
		if (const APawn* PawnOwner = Cast<APawn>(GetOwner()))
		{
			if (APlayerController* Controller = PawnOwner->GetController())
			{
				DesireRot = Controller->GetControlRotation();
			}
		}
	}

	if (!bInheritPitch)
	{
		DesireRot.Pitch = 0.0f;
	}
	if (!bInheritYaw)
	{
		DesireRot.Yaw = 0.0f;
	}
	if (!bInheritRoll)
	{
		DesireRot.Roll = 0.0f;
	}
	return DesireRot.ToQuaternion();
}

FVector USpringArmComponent::ComputeDesiredCameraLocation(const FVector& Pivot, const FQuat& DesiredRotation) const
{
	const FVector Forward = DesiredRotation.GetForwardVector();
	const FVector Right = DesiredRotation.GetRightVector();
	const FVector Up = DesiredRotation.GetUpVector();

	return Pivot - Forward * TargetArmLength + Forward * SocketOffset.X + Right * SocketOffset.Y + Up * SocketOffset.Z;
}

FVector USpringArmComponent::ResolveCollision(const FVector& Pivot, const FVector& DesiredCameraLocation, float DeltaTime)
{
	FVector ToCamera = DesiredCameraLocation - Pivot;
	const float DesiredDistance = ToCamera.Length();

	if (DesiredDistance <= FMath::Epsilon)
	{
		CurrentArmLength = 0.0f;
		bCollisionFixApplied = false;
		return Pivot;
	}

	const FVector Dir = ToCamera / DesiredDistance;
	float TargetDistance = DesiredDistance;
	bCollisionFixApplied = false;

	if (bDoCollisionTest)
	{
		FRay Ray;
		Ray.Origin = Pivot;
		Ray.Direction = Dir;

		FRaycastQueryParams Params;
		Params.IgnoreActor = GetOwner();
		Params.bIgnoreHidden = true;
		Params.bTraceOnlyBlocking = true;
		Params.MaxDistance = DesiredDistance;

		FHitResult Hit;
		AActor* HitActor = nullptr;

		if (UWorld* World = GetWorld())
		{
			if (World->RaycastPrimitives(Ray, Hit, HitActor, Params) && Hit.bHit)
			{
				TargetDistance = FMath::Clamp(Hit.Distance - ProbeSize, MinArmLength, DesiredDistance);
				bCollisionFixApplied = true;
			}
		}
	}

	const float Speed = bCollisionFixApplied ? CollisionPullInSpeed : CollisionRecoverSpeed;
	CurrentArmLength = FMath::FInterpTo(CurrentArmLength, TargetDistance, DeltaTime, Speed);
	CurrentArmLength = FMath::Clamp(CurrentArmLength, MinArmLength, DesiredDistance);
	return Pivot + Dir * CurrentArmLength;
}
void USpringArmComponent::UpdateCameraChildren(const FVector& NewCameraLocation, const FQuat& CameraRotation)
{
	for (USceneComponent* Child : GetChildren())
	{
		if (!Child)
		{
			continue;
		}

		if (UCameraComponent* Camera = Cast<UCameraComponent>(Child))
		{
			Camera->SetWorldLocation(NewCameraLocation);
			Camera->SetWorldRotation(CameraRotation);
		}
	}
}
