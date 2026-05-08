#pragma once

#include "SolInclude.h"

#include "Core/CoreTypes.h"
#include "Core/PropertyTypes.h"
#include "Component/ActorComponent.h"

class FLuaPropertyBridge
{
public:
	static sol::table ListProperties(sol::this_state State, UActorComponent* Component);

	static bool HasProperty(UActorComponent* Component, const FString& PropertyName);

	static sol::object GetProperty(sol::this_state State, UActorComponent* Component, const FString& PropertyName);

	static bool SetProperty(UActorComponent* Component, const FString& PropertyName, const sol::object& Value);

private:
	static FString NormalizePropertyName(FString Name);

	static bool IsSamePropertyName(const FString& A, const FString& B);

	static bool TryGetDescriptors(UActorComponent* Component, TArray<FPropertyDescriptor>& OutProps);

	static const FPropertyDescriptor* FindDescriptor(const TArray<FPropertyDescriptor>& Props, const FString& PropertyName);

	static const char* ToLuaTypeName(EPropertyType Type);
};
