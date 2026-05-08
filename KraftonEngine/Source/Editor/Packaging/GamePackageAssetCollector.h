#pragma once

#include "Core/CoreTypes.h"

struct FGamePackageAssetCollector
{
	static TArray<FString> GetDefaultContentDirectories()
	{
		return { "Asset", "LuaScripts", "Data", "Shaders" };
	}
};
