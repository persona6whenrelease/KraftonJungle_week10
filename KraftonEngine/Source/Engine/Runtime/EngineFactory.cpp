#include "Runtime/EngineFactory.h"

#include "Core/Log.h"

namespace
{
	TMap<FString, FEngineFactory>& GetEngineFactories()
	{
		static TMap<FString, FEngineFactory> Factories;
		return Factories;
	}
}

bool FEngineFactoryRegistry::RegisterFactory(const FString& Name, FEngineFactory Factory)
{
	if (Name.empty() || !Factory)
	{
		return false;
	}

	GetEngineFactories()[Name] = Factory;
	return true;
}

UEngine* FEngineFactoryRegistry::Create(const FString& Name)
{
	auto& Factories = GetEngineFactories();
	auto It = Factories.find(Name);
	if (It == Factories.end())
	{
		UE_LOG("[EngineFactory] Factory not found: %s", Name.c_str());
		return nullptr;
	}

	return It->second();
}
