#include "Runtime/RuntimeModuleManager.h"

#include "Core/Log.h"

namespace
{
	TMap<FString, FRuntimeModuleFactory>& GetRuntimeModuleFactories()
	{
		static TMap<FString, FRuntimeModuleFactory> Factories;
		return Factories;
	}
}

bool FRuntimeModuleRegistry::RegisterFactory(const FString& Name, FRuntimeModuleFactory Factory)
{
	if (Name.empty() || !Factory)
	{
		return false;
	}

	GetRuntimeModuleFactories()[Name] = Factory;
	return true;
}

std::unique_ptr<IRuntimeModule> FRuntimeModuleRegistry::Create(const FString& Name)
{
	auto& Factories = GetRuntimeModuleFactories();
	auto It = Factories.find(Name);
	if (It == Factories.end())
	{
		UE_LOG("[RuntimeModule] Factory not found: %s", Name.c_str());
		return nullptr;
	}
	return It->second();
}

bool FRuntimeModuleManager::LoadModules(const TArray<FString>& ModuleNames)
{
	bool bAllLoaded = true;
	for (const FString& Name : ModuleNames)
	{
		if (Name.empty())
		{
			continue;
		}

		std::unique_ptr<IRuntimeModule> Module = FRuntimeModuleRegistry::Create(Name);
		if (!Module)
		{
			bAllLoaded = false;
			continue;
		}

		Module->OnRegister();
		UE_LOG("[RuntimeModule] Loaded: %s", Module->GetName());
		Modules.push_back(std::move(Module));
	}

	bShutdownCalled = false;
	return bAllLoaded;
}

void FRuntimeModuleManager::UnloadModules()
{
	OnShutdown();
	Modules.clear();
	bShutdownCalled = false;
}

void FRuntimeModuleManager::OnEngineInit(FEngineModuleContext& Context)
{
	for (std::unique_ptr<IRuntimeModule>& Module : Modules)
	{
		Module->OnEngineInit(Context);
	}
}

void FRuntimeModuleManager::OnViewportCreated(FViewportModuleContext& Context)
{
	for (std::unique_ptr<IRuntimeModule>& Module : Modules)
	{
		Module->OnViewportCreated(Context);
	}
}

void FRuntimeModuleManager::OnWorldCreated(UWorld* World)
{
	for (std::unique_ptr<IRuntimeModule>& Module : Modules)
	{
		Module->OnWorldCreated(World);
	}
}

void FRuntimeModuleManager::OnBeginPlay(UWorld* World)
{
	for (std::unique_ptr<IRuntimeModule>& Module : Modules)
	{
		Module->OnBeginPlay(World);
	}
}

void FRuntimeModuleManager::OnTick(float DeltaTime)
{
	for (std::unique_ptr<IRuntimeModule>& Module : Modules)
	{
		Module->OnTick(DeltaTime);
	}
}

void FRuntimeModuleManager::OnPreWorldReset(UWorld* World)
{
	for (std::unique_ptr<IRuntimeModule>& Module : Modules)
	{
		Module->OnPreWorldReset(World);
	}
}

void FRuntimeModuleManager::OnPostWorldReset(UWorld* World)
{
	for (std::unique_ptr<IRuntimeModule>& Module : Modules)
	{
		Module->OnPostWorldReset(World);
	}
}

void FRuntimeModuleManager::OnShutdown()
{
	if (bShutdownCalled)
	{
		return;
	}

	for (auto It = Modules.rbegin(); It != Modules.rend(); ++It)
	{
		(*It)->OnShutdown();
	}
	bShutdownCalled = true;
}
