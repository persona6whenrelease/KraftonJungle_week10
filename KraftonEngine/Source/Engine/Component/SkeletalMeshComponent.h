#pragma once

#include "Component/SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override = default;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void ApplyDebugRandomBoneAnimation(float DeltaTime);
	bool ApplyBakedAnimation(float DeltaTime);

	float DebugBoneAnimTime = 0.0f;
	float BakedAnimTime = 0.0f;
};
