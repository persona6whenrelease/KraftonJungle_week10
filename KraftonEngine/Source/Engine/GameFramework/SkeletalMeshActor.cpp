#include "GameFramework/SkeletalMeshActor.h"
#include "Component/SkeletalMeshComponent.h"
#include "SkeletalMesh/FBXManager.h"
#include "Engine/Runtime/Engine.h"

IMPLEMENT_CLASS(ASkeletalMeshActor, AActor)

void ASkeletalMeshActor::InitDefaultComponents(const FString& FbxFileName)
{
	SkeletalMeshComponent = AddComponent<USkeletalMeshComponent>();
	SetRootComponent(SkeletalMeshComponent);

	if (FbxFileName.empty()) return;

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	USkeletalMesh* Asset = FFBXManager::LoadSkeletalMesh(FbxFileName, Device);
	SkeletalMeshComponent->SetSkeletalMesh(Asset);
}
