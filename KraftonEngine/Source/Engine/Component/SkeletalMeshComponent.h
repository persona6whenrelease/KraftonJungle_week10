#pragma once
#include "SkinnedMeshComponent.h"

class FPrimitiveSceneProxy;


class USkeletalMeshComponent :
    public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	// 구체 프록시 생성 (FSkeletalMeshSceneProxy)
	FPrimitiveSceneProxy* CreateSceneProxy() override;

private:


};

