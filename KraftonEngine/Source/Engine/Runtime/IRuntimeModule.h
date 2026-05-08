#pragma once

#include "Core/CoreTypes.h"
#include <functional>
#include <memory>

class UEngine;
class UWorld;
class FWindowsWindow;
class FRenderer;
class UGameViewportClient;

struct FEngineModuleContext
{
	UEngine* Engine = nullptr;
	FWindowsWindow* Window = nullptr;
};

struct FViewportUiCommandContext
{
	std::function<void(const FString& CommandName)> ExecuteCommand;
	std::function<bool(const FString& StateName)> QueryBool;
	std::function<void(const FString& StateName, bool bValue)> SetBool;
};

struct FViewportModuleContext
{
	UEngine* Engine = nullptr;
	FWindowsWindow* Window = nullptr;
	FRenderer* Renderer = nullptr;
	UGameViewportClient* ViewportClient = nullptr;
	FViewportUiCommandContext UiCommands;
};

class IRuntimeModule
{
public:
	virtual ~IRuntimeModule() = default;
	virtual const char* GetName() const = 0;

	virtual void OnRegister() {}
	virtual void OnEngineInit(FEngineModuleContext& Context) { (void)Context; }
	virtual void OnViewportCreated(FViewportModuleContext& Context) { (void)Context; }
	virtual void OnWorldCreated(UWorld* World) { (void)World; }
	virtual void OnBeginPlay(UWorld* World) { (void)World; }
	virtual void OnTick(float DeltaTime) { (void)DeltaTime; }
	virtual void OnPreWorldReset(UWorld* World) { (void)World; }
	virtual void OnPostWorldReset(UWorld* World) { (void)World; }
	virtual void OnShutdown() {}
};

using FRuntimeModuleFactory = std::unique_ptr<IRuntimeModule>(*)();

class FRuntimeModuleRegistry
{
public:
	static bool RegisterFactory(const FString& Name, FRuntimeModuleFactory Factory);
	static std::unique_ptr<IRuntimeModule> Create(const FString& Name);
};

class FRuntimeModuleRegistrar
{
public:
	FRuntimeModuleRegistrar(const FString& Name, FRuntimeModuleFactory Factory)
	{
		FRuntimeModuleRegistry::RegisterFactory(Name, Factory);
	}
};
