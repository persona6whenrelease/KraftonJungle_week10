#pragma once

#include "GameFramework/AActor.h"

class USkeletalMeshComponent;

class ASkeletalMeshActor : public AActor
{
public:
	DECLARE_CLASS(ASkeletalMeshActor, AActor)
	ASkeletalMeshActor() {}

	void InitDefaultComponents(const FString& SkeletalMeshFileName);

private:
	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
};
