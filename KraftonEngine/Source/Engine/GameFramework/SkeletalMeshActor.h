#pragma once

#include "GameFramework/AActor.h"

class USkeletalMeshComponent;

class ASkeletalMeshActor : public AActor
{
public:
	DECLARE_CLASS(ASkeletalMeshActor, AActor)
	ASkeletalMeshActor() {}

	void InitDefaultComponents(const FString& FbxFileName = "");

	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }

private:
	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
};
