#pragma once

#include "Core/CoreTypes.h"

struct FGameClientSettings;

class FGameClientPackageValidator
{
public:
	static bool ValidateBeforeEngineInit(const FGameClientSettings& Settings, FString& OutErrors);
};
