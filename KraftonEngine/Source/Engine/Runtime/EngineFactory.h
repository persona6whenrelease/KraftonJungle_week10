#pragma once

#include "Core/CoreTypes.h"

class UEngine;

using FEngineFactory = UEngine* (*)();

class FEngineFactoryRegistry
{
public:
	static bool RegisterFactory(const FString& Name, FEngineFactory Factory);
	static UEngine* Create(const FString& Name);
};

class FEngineFactoryRegistrar
{
public:
	FEngineFactoryRegistrar(const FString& Name, FEngineFactory Factory)
	{
		FEngineFactoryRegistry::RegisterFactory(Name, Factory);
	}
};
