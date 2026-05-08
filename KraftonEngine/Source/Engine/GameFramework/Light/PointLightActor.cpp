#include "PointLightActor.h"
#include "Component/BillboardComponent.h"
#include "Component/Light/PointLightComponent.h"
#include "Materials/MaterialManager.h"

IMPLEMENT_CLASS(APointLightActor, AActor)

void APointLightActor::InitDefaultComponents()
{
	if (LightComponent && GetRootComponent() == LightComponent)
	{
		BillboardComponent = LightComponent->EnsureEditorBillboard();
		return;
	}

	if (UPointLightComponent* RootLight = Cast<UPointLightComponent>(GetRootComponent()))
	{
		LightComponent = RootLight;
		BillboardComponent = LightComponent->EnsureEditorBillboard();
		return;
	}

	for (UActorComponent* Component : GetComponents())
	{
		if (UPointLightComponent* Light = Cast<UPointLightComponent>(Component))
		{
			LightComponent = Light;
			BillboardComponent = LightComponent->EnsureEditorBillboard();
			return;
		}
	}

	if (!GetRootComponent())
	{
		LightComponent = AddComponent<UPointLightComponent>();
		SetRootComponent(LightComponent);
		BillboardComponent = LightComponent->EnsureEditorBillboard();
	}
}
