#pragma once

#include "Component/SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override = default;

	bool IsDebugRandomBoneAnimEnabled() const { return bDebugRandomBoneAnimEnabled; }
	void SetDebugRandomBoneAnimEnabled(bool bEnabled) { bDebugRandomBoneAnimEnabled = bEnabled; }

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void ApplyDebugRandomBoneAnimation(float DeltaTime);

	float DebugBoneAnimTime = 0.0f;
	bool bDebugRandomBoneAnimEnabled = false;
};
