#include "HeightFogActor.h"
#include "Component/HeightFogComponent.h"
#include "Component/BillboardComponent.h"
#include "Materials/MaterialManager.h"

IMPLEMENT_CLASS(AHeightFogActor, AActor)

AHeightFogActor::AHeightFogActor()
{
}

void AHeightFogActor::InitDefaultComponents()
{
	if (FogComponent && GetRootComponent() == FogComponent)
	{
		BillboardComponent = FogComponent->EnsureEditorBillboard();
		return;
	}

	if (UHeightFogComponent* RootFog = Cast<UHeightFogComponent>(GetRootComponent()))
	{
		FogComponent = RootFog;
		BillboardComponent = FogComponent->EnsureEditorBillboard();
		return;
	}

	for (UActorComponent* Component : GetComponents())
	{
		if (UHeightFogComponent* Fog = Cast<UHeightFogComponent>(Component))
		{
			FogComponent = Fog;
			BillboardComponent = FogComponent->EnsureEditorBillboard();
			return;
		}
	}

	if (!GetRootComponent())
	{
		FogComponent = AddComponent<UHeightFogComponent>();
		SetRootComponent(FogComponent);

		BillboardComponent = FogComponent->EnsureEditorBillboard();
	}
}
