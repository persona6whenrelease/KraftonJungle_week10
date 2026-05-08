#include "GameFramework/CameraActor.h"

#include "Component/ActorComponent.h"
#include "Component/CameraComponent.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(ACameraActor, AActor)

void ACameraActor::InitDefaultComponents()
{
	if (CameraComponent && GetRootComponent() == CameraComponent)
	{
		return;
	}
	if (UCameraComponent* RootCamera = Cast<UCameraComponent>(GetRootComponent()))
	{
		CameraComponent = RootCamera;
		return;
	}
	for (UActorComponent* Component : GetComponents())
	{
		if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
		{
			CameraComponent = Camera;
			return;
		}
	}
	if (!GetRootComponent())
	{
		CameraComponent = AddComponent<UCameraComponent>();
		if (CameraComponent)
		{
			SetRootComponent(CameraComponent);
		}
	}
}
