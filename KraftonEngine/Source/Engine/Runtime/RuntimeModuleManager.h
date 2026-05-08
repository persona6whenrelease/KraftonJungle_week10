#pragma once

#include "Runtime/IRuntimeModule.h"

class FRuntimeModuleManager
{
public:
	bool LoadModules(const TArray<FString>& ModuleNames);
	void UnloadModules();

	void OnEngineInit(FEngineModuleContext& Context);
	void OnViewportCreated(FViewportModuleContext& Context);
	void OnWorldCreated(UWorld* World);
	void OnBeginPlay(UWorld* World);
	void OnTick(float DeltaTime);
	void OnPreWorldReset(UWorld* World);
	void OnPostWorldReset(UWorld* World);
	void OnShutdown();

	bool HasLoadedModules() const { return !Modules.empty(); }

private:
	TArray<std::unique_ptr<IRuntimeModule>> Modules;
	bool bShutdownCalled = false;
};
