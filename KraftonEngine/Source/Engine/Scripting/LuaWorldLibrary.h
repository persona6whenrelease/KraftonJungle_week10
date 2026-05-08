#pragma once

#include "Core/CoreTypes.h"
#include "GameFramework/AActor.h"
#include "Component/ActorComponent.h"
#include "Component/StaticMeshComponent.h"

class UWorld;
class USceneComponent;
class UShapeComponent;
class ULuaScriptComponent;
class UClass;

class FLuaWorldLibrary
{
public:
	static UWorld* GetActiveWorld();

	static AActor* SpawnActorByClassName(const FString& ClassName, const FVector& Location);

	static AActor* SpawnStaticMeshActor(
		const FString& StaticMeshPath,
		const FVector& Location
		);

	static bool DestroyActor(AActor* Actor);

	template<typename TComponent>
	static TComponent* FindComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (TComponent* Typed = Cast<TComponent>(Component))
			{
				return Typed;
			}
		}
		return nullptr;
	}

	template<typename TComponent>
	static TArray<TComponent*> FindComponents(AActor* Actor)
	{
		TArray<TComponent*> Result;
		if (!Actor)
		{
			return Result;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (TComponent* Typed = Cast<TComponent>(Component))
			{
				Result.push_back(Typed);
			}
		}
		return Result;
	}

	template<typename TComponent>
	static TComponent* FindComponentAt(AActor* Actor, int32 ComponentIndex)
	{
		if (ComponentIndex < 0)
		{
			return nullptr;
		}

		TArray<TComponent*> Components = FindComponents<TComponent>(Actor);
		const size_t Index = static_cast<size_t>(ComponentIndex);
		return Index < Components.size() ? Components[Index] : nullptr;
	}

	template<typename TComponent>
	static TComponent* GetOrAddComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		if (TComponent* Existing = FindComponent<TComponent>(Actor))
		{
			return Existing;
		}

		EnsureRootComponent(Actor);

		TComponent* Component = Actor->AddComponent<TComponent>();

		PostComponentAdded(Actor, Component);

		return Component;
	}

	template<typename TComponent>
	static bool RemoveComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}

		TComponent* Component = FindComponent<TComponent>(Actor);

		if (!Component)
		{
			return false;
		}

		return RemoveComponent(Actor, Component);
	}

	static bool RemoveComponent(AActor* Actor, UActorComponent* Component);

	static bool RemoveShape(AActor* Actor);

	template<typename TShapeComponent>
	static TShapeComponent* SetExclusiveShape(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		EnsureRootComponent(Actor);

		RemoveShape(Actor);

		TShapeComponent* Shape = Actor->AddComponent<TShapeComponent>();

		PostComponentAdded(Actor, Shape);

		return Shape;
	}

	static UStaticMeshComponent* GetOrAddStaticMeshComponent(AActor* Actor);

	static UActorComponent* FindComponentByTypeName(AActor* Actor, const FString& TypeName);
	static UActorComponent* FindComponentByTypeName(AActor* Actor, const FString& TypeName, int32 ComponentIndex);
	static TArray<UActorComponent*> FindComponentsByTypeName(AActor* Actor, const FString& TypeName);
	static ULuaScriptComponent* FindLuaScriptComponent(AActor* Actor, const FString& ScriptIdentifier);

	static UActorComponent* GetOrAddComponentByTypeName(AActor* Actor, const FString& TypeName);

	static bool RemoveComponentByTypeName(AActor* Actor, const FString& TypeName);
	static void RegisterAllowedComponentClass(const FString& TypeName, UClass* Class, bool bCanCreateFromLua);

	static bool SetStaticMesh(UStaticMeshComponent* MeshComponent, const FString& StaticMeshPath);

	static bool SetMaterial(UStaticMeshComponent* MeshComponent, int32 ElementIndex, const FString& MaterialPath);

	static bool SetMaterialScalarParameter(UStaticMeshComponent* MeshComponent, int32 ElementIndex, const FString& ParameterName, float Value);

	static bool SetMaterialVectorParameter(UStaticMeshComponent* MeshComponent, int32 ElementIndex, const FString& ParameterName, const FVector& Value);

	static bool SetMaterialVector4Parameter(UStaticMeshComponent* MeshComponent, int32 ElementIndex, const FString& ParameterName, const FVector4& Value);

private:
	static void PostComponentAdded(AActor* Actor, UActorComponent* Component);
	static void EnsureRootComponent(AActor* Actor);
};
