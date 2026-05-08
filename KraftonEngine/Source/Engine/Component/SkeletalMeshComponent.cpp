#include "Component/SkeletalMeshComponent.h"

#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	USkinnedMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	SkinVerticesToReferencePose();
	EnsureRuntimeResources();
}
