#pragma once

#include "Component/ActorComponent.h"
#include "Core/PropertyTypes.h"
#include "Runtime/PooledObjectInterface.h"

class ULuaScriptComponent : public UActorComponent, public IPooledObjectInterface
{
public:
	DECLARE_CLASS(ULuaScriptComponent, UActorComponent)

	virtual void BeginPlay() override;
	virtual void EndPlay() override;
	void OnSpawnFromPool() override;
	void OnReturnToPool() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	virtual void PostEditProperty(const char* PropertyName) override;

	const FString& GetScriptPath() const { return ScriptPath; }
	void SetScriptPath(const FString& InScriptPath);
	bool ReloadScript();

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	FString ScriptPath;
};
