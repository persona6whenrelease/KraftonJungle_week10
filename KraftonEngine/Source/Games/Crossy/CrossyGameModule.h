#pragma once

#include "Runtime/IRuntimeModule.h"
#include "Games/Crossy/UI/CrossyGameUiSystem.h"

class FCrossyGameModule final : public IRuntimeModule
{
public:
	const char* GetName() const override { return "CrossyGame"; }

	void OnRegister() override;
	void OnEngineInit(FEngineModuleContext& Context) override;
	void OnViewportCreated(FViewportModuleContext& Context) override;
	void OnWorldCreated(UWorld* World) override;
	void OnPreWorldReset(UWorld* World) override;
	void OnPostWorldReset(UWorld* World) override;
	void OnShutdown() override;

private:
	void LoadCrossyAudio();

	FCrossyGameUiSystem GameUi;
	bool bRowManagerInitialized = false;
};

void RegisterCrossyGameModule();
