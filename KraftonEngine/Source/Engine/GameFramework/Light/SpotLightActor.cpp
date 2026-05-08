#include "SpotLightActor.h"
#include "Component/BillboardComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Materials/MaterialManager.h"

IMPLEMENT_CLASS(ASpotLightActor, AActor)

void ASpotLightActor::InitDefaultComponents()
{
	if (LightComponent && GetRootComponent() == LightComponent)
	{
		BillboardComponent = LightComponent->EnsureEditorBillboard();
		return;
	}

	if (USpotLightComponent* RootLight = Cast<USpotLightComponent>(GetRootComponent()))
	{
		LightComponent = RootLight;
		BillboardComponent = LightComponent->EnsureEditorBillboard();
		return;
	}

	for (UActorComponent* Component : GetComponents())
	{
		if (USpotLightComponent* Light = Cast<USpotLightComponent>(Component))
		{
			LightComponent = Light;
			BillboardComponent = LightComponent->EnsureEditorBillboard();
			return;
		}
	}

	if (!GetRootComponent())
	{
		LightComponent = AddComponent<USpotLightComponent>();
		SetRootComponent(LightComponent);
		BillboardComponent = LightComponent->EnsureEditorBillboard();
	}
}
