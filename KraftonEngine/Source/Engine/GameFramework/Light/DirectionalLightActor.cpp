#include "DirectionalLightActor.h"
#include "Component/BillboardComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Materials/MaterialManager.h"
IMPLEMENT_CLASS(ADirectionalLightActor, AActor)

void ADirectionalLightActor::InitDefaultComponents()
{
	if (LightComponent && GetRootComponent() == LightComponent)
	{
		BillboardComponent = LightComponent->EnsureEditorBillboard();
		return;
	}

	if (UDirectionalLightComponent* RootLight = Cast<UDirectionalLightComponent>(GetRootComponent()))
	{
		LightComponent = RootLight;
		BillboardComponent = LightComponent->EnsureEditorBillboard();
		return;
	}

	for (UActorComponent* Component : GetComponents())
	{
		if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Component))
		{
			LightComponent = Light;
			BillboardComponent = LightComponent->EnsureEditorBillboard();
			return;
		}
	}

	if (!GetRootComponent())
	{
		LightComponent = AddComponent<UDirectionalLightComponent>();
		SetRootComponent(LightComponent);
		BillboardComponent = LightComponent->EnsureEditorBillboard();
	}
}
