#include "SkeletalMeshComponent.h"
#include "Render/Proxy/SkinnedMeshSceneProxy.h"

IMPLEMENT_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)


FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
	return new FSkinnedMesScenehProxy(this);

}
