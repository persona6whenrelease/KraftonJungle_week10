#pragma once

#include "Component/ActorComponent.h"
#include "Runtime/PooledObjectInterface.h"

class UParryableProjectileComponent : public UActorComponent, public IPooledObjectInterface
{
public:
	DECLARE_CLASS(UParryableProjectileComponent, UActorComponent)

	bool IsParried() const { return bParried; }
	void SetParried(bool bInParried) { bParried = bInParried; }

	void OnSpawnFromPool() override { ResetParryState(); }
	void OnReturnToPool() override { ResetParryState(); }

private:
	void ResetParryState() { bParried = false; }

	bool bParried = false;
};
