#pragma once

#include "Object/FName.h"

class UGameClientEngine;
class UWorld;

class FGameClientSession
{
public:
	bool Initialize(UGameClientEngine* InEngine);
	bool Restart();
	void Shutdown();

	UWorld* GetWorld() const { return World; }

private:
	void DestroyWorld();

private:
	UGameClientEngine* Engine = nullptr;
	UWorld* World = nullptr;
	FName WorldHandle = FName("GameClient");
};
