#include "Component/Script/LuaScriptComponent.h"

#include "GameFramework/AActor.h"
#include "Scripting/LuaScriptSubsystem.h"
#include "Serialization/Archive.h"

#include <cstring>

IMPLEMENT_CLASS(ULuaScriptComponent, UActorComponent)

void ULuaScriptComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ScriptPath.empty())
	{
		return;
	}

	if (FLuaScriptSubsystem::Get().BindComponent(this, ScriptPath))
	{
		FLuaScriptSubsystem::Get().CallComponentBeginPlay(this);
	}
}

void ULuaScriptComponent::EndPlay()
{
	FLuaScriptSubsystem::Get().CallComponentEndPlay(this);
	FLuaScriptSubsystem::Get().UnbindComponent(this);
	Super::EndPlay();
}

void ULuaScriptComponent::OnSpawnFromPool()
{
	FLuaScriptSubsystem::Get().CallComponentSpawnFromPool(this);
}

void ULuaScriptComponent::OnReturnToPool()
{
	FLuaScriptSubsystem::Get().CallComponentReturnToPool(this);
}

void ULuaScriptComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << ScriptPath;
}

void ULuaScriptComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	Super::GetEditableProperties(OutProps);
	OutProps.push_back({ "ScriptPath", EPropertyType::String, &ScriptPath });
}

void ULuaScriptComponent::PostEditProperty(const char* PropertyName)
{
	Super::PostEditProperty(PropertyName);

	if (PropertyName && std::strcmp(PropertyName, "ScriptPath") == 0)
	{
		ReloadScript();
	}
}

void ULuaScriptComponent::SetScriptPath(const FString& InScriptPath)
{
	if (ScriptPath == InScriptPath)
	{
		return;
	}

	ScriptPath = InScriptPath;
	ReloadScript();
}

bool ULuaScriptComponent::ReloadScript()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasActorBegunPlay())
	{
		return false;
	}

	FLuaScriptSubsystem::Get().UnbindComponent(this);

	if (ScriptPath.empty())
	{
		return false;
	}

	if (!FLuaScriptSubsystem::Get().BindComponent(this, ScriptPath))
	{
		return false;
	}

	FLuaScriptSubsystem::Get().CallComponentBeginPlay(this);

	return true;
}

void ULuaScriptComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	(void)TickType;
	(void)ThisTickFunction;

	FLuaScriptSubsystem::Get().CallComponentTick(this, DeltaTime);
}
