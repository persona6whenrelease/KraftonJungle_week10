#include "GameFramework/SkeletalMeshActor.h"

#include "Component/SkeletalMeshComponent.h"
#include "Mesh/FBXManager.h"
#include "Mesh/SkeletalMesh.h"

IMPLEMENT_CLASS(ASkeletalMeshActor, AActor)

void ASkeletalMeshActor::InitDefaultComponents(const FString& SkeletalMeshFileName)
{
	SkeletalMeshComponent = AddComponent<USkeletalMeshComponent>();
	SetRootComponent(SkeletalMeshComponent);

	USkeletalMesh* Asset = FFBXManager::LoadSkeletalMesh(SkeletalMeshFileName);
	SkeletalMeshComponent->SetSkeletalMesh(Asset);
}
