#pragma once

class IPooledObjectInterface
{
public:
	virtual ~IPooledObjectInterface() = default;

	virtual void OnSpawnFromPool() {}
	virtual void OnReturnToPool() {}
};
