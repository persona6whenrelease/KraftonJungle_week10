#include "LuaWorldLibrary.h"

#include "Core/Log.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Object/UClass.h"
#include "Runtime/Engine.h"

#include "GameFramework/World.h"
#include "GameFramework/StaticMeshActor.h"

#include "Component/SceneComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/SphereComponent.h"
#include "Component/Collision/CapsuleComponent.h"
#include "Component/Movement/MovementComponent.h"
#include "Component/Movement/ProjectileMovementComponent.h"
#include "Component/Movement/InterpToMovementComponent.h"
#include "Component/Movement/PendulumMovementComponent.h"
#include "Component/Movement/RotatingMovementComponent.h"
#include "Component/Movement/PawnMovementComponent.h"
#include "Component/ControllerInputComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SpringArmComponent.h"
#include "Component/TextRenderComponent.h"

#include "Mesh/ObjManager.h"
#include "Mesh/StaticMesh.h"

#include "Materials/Material.h"
#include "Materials/MaterialManager.h"

#include <d3d11.h>
#include <cctype>
#include <algorithm>

namespace
{
	struct FLuaAllowedComponentClass;

	FString NormalizeLuaTypeName(FString Name);
	FString NormalizeLuaScriptIdentifier(FString Value);
	bool HasSuffix(const FString& Value, const FString& Suffix);
	bool HasLuaExtension(const FString& Value);
	FString GetPathBaseName(const FString& Value);
	FString GetPathStem(const FString& Value);
	bool LuaScriptMatchesIdentifier(const ULuaScriptComponent* Component, const FString& ScriptIdentifier);
	bool IsAllowedLuaActorClassName(const FString& ClassName);
	UClass* FindRegisteredClassByName(const FString& ClassName);
	const FLuaAllowedComponentClass* FindAllowedLuaComponentClass(const FString& TypeName);
}

UWorld* FLuaWorldLibrary::GetActiveWorld()
{
	if (!GEngine)
	{
		UE_LOG("[Lua] Active World lookup failed: GEngine is null.");
		return nullptr;
	}

	UWorld* World = GEngine->GetWorld();

	if (!World)
	{
		UE_LOG("[Lua] Active World lookup failed: World is null.");
		return nullptr;
	}

	return World;
}

AActor* FLuaWorldLibrary::SpawnActorByClassName(const FString& ClassName, const FVector& Location)
{
	if (!IsAllowedLuaActorClassName(ClassName))
	{
		UE_LOG("[LuaSecurity] SpawnActor blocked: class is not allowed from Lua. ClassName = %s", ClassName.c_str());
		return nullptr;
	}

	UWorld* World = GetActiveWorld();

	if (!World)
	{
		return nullptr;
	}

	UObject* Object = FObjectFactory::Get().Create(ClassName, World);

	if (!Object)
	{
		UE_LOG("[Lua] SpawnActor failed: unknown class name = %s", ClassName.c_str());
		return nullptr;
	}

	AActor* Actor = Cast<AActor>(Object);

	if (!Actor)
	{
		UE_LOG("[Lua] SpawnActor failed: class is not AActor. ClassName = %s", ClassName.c_str());
		UObjectManager::Get().DestroyObject(Object);
		return nullptr;
	}

	Actor->InitDefaultComponents();
	EnsureRootComponent(Actor);
	Actor->SetActorLocation(Location);
	World->AddActor(Actor);

	return Actor;
}

AActor* FLuaWorldLibrary::SpawnStaticMeshActor(const FString& StaticMeshPath, const FVector& Location)
{
	UWorld* World = GetActiveWorld();

	if (!World)
	{
		return nullptr;
	}

	AStaticMeshActor* Actor = UObjectManager::Get().CreateObject<AStaticMeshActor>(World);

	if (!Actor)
	{
		UE_LOG("[Lua] SpawnStaticMeshActor failed.");
		return nullptr;
	}

	Actor->InitDefaultComponents(StaticMeshPath);
	Actor->SetActorLocation(Location);
	World->AddActor(Actor);

	return Actor;
}

bool FLuaWorldLibrary::DestroyActor(AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	UWorld* World = Actor->GetWorld();

	if (!World)
	{
		UE_LOG("[Lua] DestroyActor failed: Actor has no World.");
		return false;
	}

	const uint32 ActorUUID = Actor->GetUUID();

	// TODO: FLuaDelegateManager::Get().ClearActor(ActorUUID);
	(void)ActorUUID;

	World->DestroyActor(Actor);

	return true;
}

bool FLuaWorldLibrary::RemoveComponent(AActor* Actor, UActorComponent* Component)
{
	if (!Actor || !Component)
	{
		return false;
	}

	if (Actor->GetRootComponent() == Component)
	{
		UE_LOG("[Lua] RemoveComponent failed: cannot remove RootComponent from Lua.");
		return false;
	}

	Actor->RemoveComponent(Component);
	return true;
}

bool FLuaWorldLibrary::RemoveShape(AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	bool bRemoved = false;

	TArray<UActorComponent*> Components = Actor->GetComponents();

	for (UActorComponent* Component : Components)
	{
		if (UShapeComponent* Shape = Cast<UShapeComponent>(Component))
		{
			if (RemoveComponent(Actor, Shape))
			{
				bRemoved = true;
			}
		}
	}

	return bRemoved;
}

UStaticMeshComponent* FLuaWorldLibrary::GetOrAddStaticMeshComponent(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	if (UStaticMeshComponent* Existing = FindComponent<UStaticMeshComponent>(Actor))
	{
		return Existing;
	}

	EnsureRootComponent(Actor);

	UStaticMeshComponent* MeshComponent = Actor->AddComponent<UStaticMeshComponent>();

	if (!MeshComponent)
	{
		UE_LOG("[Lua] Failed to add StaticMeshComponent.");
		return nullptr;
	}

	MeshComponent->AttachToComponent(Actor->GetRootComponent());

	PostComponentAdded(Actor, MeshComponent);

	return MeshComponent;
}

bool FLuaWorldLibrary::SetStaticMesh(UStaticMeshComponent* MeshComponent, const FString& StaticMeshPath)
{
	if (!MeshComponent)
	{
		UE_LOG("[Lua] SetStaticMesh failed: invalid StaticMeshComponent.");
		return false;
	}

	UWorld* World = MeshComponent->GetWorld();

	if (!World)
	{
		UE_LOG("[Lua] SetStaticMesh failed: StaticMeshComponent has no World.");
		return false;
	}

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();

	if (!Device)
	{
		UE_LOG("[Lua] SetStaticMesh failed: Failed to get D3D11 Device.");
		return false;
	}

	UStaticMesh* Mesh = FObjManager::LoadObjStaticMesh(StaticMeshPath, Device);

	if (!Mesh)
	{
		UE_LOG("[Lua] SetStaticMesh failed: cannot load static mesh = %s", StaticMeshPath.c_str());
		return false;
	}

	MeshComponent->SetStaticMesh(Mesh);
	return true;
}

bool FLuaWorldLibrary::SetMaterial(UStaticMeshComponent* MeshComponent, int32 ElementIndex, const FString& MaterialPath)
{
	if (!MeshComponent)
	{
		UE_LOG("[Lua] SetMaterial failed: invalid StaticMeshComponent.");
		return false;
	}

	UMaterial* Material = nullptr;

	if (!MaterialPath.empty() && MaterialPath != "None")
	{
		Material = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
	}

	MeshComponent->SetMaterial(ElementIndex, Material);
	return true;
}

bool FLuaWorldLibrary::SetMaterialScalarParameter(UStaticMeshComponent* MeshComponent, int32 ElementIndex, const FString& ParameterName, float Value)
{
	if (!MeshComponent)
	{
		return false;
	}

	UMaterial* Material = MeshComponent->GetMaterial(ElementIndex);

	if (!Material)
	{
		UE_LOG("[Lua] SetMaterialScalarParameter failed: invalid Material.");
		return false;
	}

	if (!Material->SetScalarParameter(ParameterName, Value))
	{
		UE_LOG("[Lua] SetMaterialScalarParameter failed: Material does not have parameter = %s", ParameterName.c_str());
		return false;
	}

	MeshComponent->SetMaterial(ElementIndex, Material);
	return true;
}

bool FLuaWorldLibrary::SetMaterialVectorParameter(UStaticMeshComponent* MeshComponent, int32 ElementIndex, const FString& ParameterName, const FVector& Value)
{
	if (!MeshComponent)
	{
		return false;
	}

	UMaterial* Material = MeshComponent->GetMaterial(ElementIndex);

	if (!Material)
	{
		UE_LOG("[Lua] SetMaterialVectorParameter failed: invalid Material.");
		return false;
	}

	if (!Material->SetVector3Parameter(ParameterName, Value))
	{
		UE_LOG("[Lua] SetMaterialVectorParameter failed: Material does not have parameter = %s", ParameterName.c_str());
		return false;
	}

	MeshComponent->SetMaterial(ElementIndex, Material);
	return true;
}

bool FLuaWorldLibrary::SetMaterialVector4Parameter(UStaticMeshComponent* MeshComponent, int32 ElementIndex, const FString& ParameterName, const FVector4& Value)
{
	if (!MeshComponent)
	{
		return false;
	}

	UMaterial* Material = MeshComponent->GetMaterial(ElementIndex);

	if (!Material)
	{
		UE_LOG("[Lua] SetMaterialVector4Parameter failed: invalid Material.");
		return false;
	}

	if (!Material->SetVector4Parameter(ParameterName, Value))
	{
		UE_LOG("[Lua] SetMaterialVector4Parameter failed: Material does not have parameter = %s", ParameterName.c_str());
		return false;
	}

	MeshComponent->SetMaterial(ElementIndex, Material);
	return true;
}

namespace
{
	struct FLuaAllowedComponentClass
	{
		FString NormalizedName;
		UClass* Class;
		bool bCanCreate;
	};

	TArray<FLuaAllowedComponentClass>& GetExtraAllowedComponentClasses()
	{
		static TArray<FLuaAllowedComponentClass> ExtraClasses;
		return ExtraClasses;
	}

	FString NormalizeLuaTypeName(FString Name)
	{
		Name.erase(
			std::remove_if(
				Name.begin(),
				Name.end(),
				[](unsigned char C)
				{
					return std::isspace(C) || C == '_' || C == '-';
				}
			),
			Name.end()
		);

		std::transform(
			Name.begin(),
			Name.end(),
			Name.begin(),
			[](unsigned char C)
			{
				return static_cast<char>(std::tolower(C));
			}
		);

		if (!Name.empty() && Name[0] == 'u')
		{
			Name.erase(Name.begin());
		}

		const FString ComponentSuffix = "component";
		if (Name.size() >= ComponentSuffix.size() &&
			Name.compare(Name.size() - ComponentSuffix.size(), ComponentSuffix.size(), ComponentSuffix) == 0)
		{
			Name.erase(Name.size() - ComponentSuffix.size());
		}

		return Name;
	}

	FString NormalizeLuaScriptIdentifier(FString Value)
	{
		std::replace(Value.begin(), Value.end(), '\\', '/');
		std::transform(
			Value.begin(),
			Value.end(),
			Value.begin(),
			[](unsigned char C)
			{
				return static_cast<char>(std::tolower(C));
			}
		);

		while (Value.rfind("./", 0) == 0)
		{
			Value.erase(0, 2);
		}

		return Value;
	}

	bool HasSuffix(const FString& Value, const FString& Suffix)
	{
		return Value.size() >= Suffix.size()
			&& Value.compare(Value.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
	}

	bool HasLuaExtension(const FString& Value)
	{
		return HasSuffix(Value, ".lua");
	}

	FString GetPathBaseName(const FString& Value)
	{
		const size_t Slash = Value.find_last_of('/');
		return Slash == FString::npos ? Value : Value.substr(Slash + 1);
	}

	FString GetPathStem(const FString& Value)
	{
		FString BaseName = GetPathBaseName(Value);
		const size_t Dot = BaseName.find_last_of('.');
		return Dot == FString::npos ? BaseName : BaseName.substr(0, Dot);
	}

	bool LuaScriptMatchesIdentifier(const ULuaScriptComponent* Component, const FString& ScriptIdentifier)
	{
		if (!Component)
		{
			return false;
		}

		if (ScriptIdentifier.empty())
		{
			return true;
		}

		const FString ScriptPath = NormalizeLuaScriptIdentifier(Component->GetScriptPath());
		FString Query = NormalizeLuaScriptIdentifier(ScriptIdentifier);
		FString QueryWithExtension = Query;
		if (!HasLuaExtension(QueryWithExtension))
		{
			QueryWithExtension += ".lua";
		}

		if (ScriptPath == Query || ScriptPath == QueryWithExtension)
		{
			return true;
		}

		if (HasSuffix(ScriptPath, "/" + Query) || HasSuffix(ScriptPath, "/" + QueryWithExtension))
		{
			return true;
		}

		return GetPathBaseName(ScriptPath) == Query
			|| GetPathBaseName(ScriptPath) == QueryWithExtension
			|| GetPathStem(ScriptPath) == Query;
	}

	bool IsAllowedLuaActorClassName(const FString& ClassName)
	{
		const FString NormalizedClassName = NormalizeLuaTypeName(ClassName);

		static const char* AllowedActorClassNames[] =
		{
			"actor",
			"aactor",
			"staticmeshactor",
			"astaticmeshactor",
			"cameraactor",
			"acameraactor",
			"pawn",
			"apawn",
			"playercontroller",
			"aplayercontroller",
			"luaactor",
			"aluaactor"
		};

		for (const char* AllowedName : AllowedActorClassNames)
		{
			if (NormalizedClassName == AllowedName)
			{
				return true;
			}
		}

		return false;
	}

	UClass* FindRegisteredClassByName(const FString& ClassName)
	{
		const FString NormalizedClassName = NormalizeLuaTypeName(ClassName);
		for (UClass* Class : UClass::GetAllClasses())
		{
			if (!Class)
			{
				continue;
			}

			if (ClassName == Class->GetName() || NormalizedClassName == NormalizeLuaTypeName(Class->GetName()))
			{
				return Class;
			}
		}

		return nullptr;
	}

	const FLuaAllowedComponentClass* FindAllowedLuaComponentClass(const FString& TypeName)
	{
		const FString NormalizedTypeName = NormalizeLuaTypeName(TypeName);

		static const FLuaAllowedComponentClass AllowedComponents[] =
		{
			{ "scene", USceneComponent::StaticClass(), true },
			{ "luascript", ULuaScriptComponent::StaticClass(), true },
			{ "staticmesh", UStaticMeshComponent::StaticClass(), true },
			{ "textrender", UTextRenderComponent::StaticClass(), true },
			{ "springarm", USpringArmComponent::StaticClass(), true },
			{ "shape", UShapeComponent::StaticClass(), false },
			{ "box", UBoxComponent::StaticClass(), true },
			{ "sphere", USphereComponent::StaticClass(), true },
			{ "capsule", UCapsuleComponent::StaticClass(), true },
			{ "movement", UMovementComponent::StaticClass(), false },
			{ "pawnmovement", UPawnMovementComponent::StaticClass(), true },
			{ "controllerinput", UControllerInputComponent::StaticClass(), true },
			{ "projectilemovement", UProjectileMovementComponent::StaticClass(), true },
			{ "interptomovement", UInterpToMovementComponent::StaticClass(), true },
			{ "pendulummovement", UPendulumMovementComponent::StaticClass(), true },
			{ "rotatingmovement",URotatingMovementComponent::StaticClass(),true },
		};

		for (const FLuaAllowedComponentClass& Entry : AllowedComponents)
		{
			if (NormalizedTypeName == Entry.NormalizedName)
			{
				return &Entry;
			}
		}

		for (const FLuaAllowedComponentClass& Entry : GetExtraAllowedComponentClasses())
		{
			if (NormalizedTypeName == Entry.NormalizedName)
			{
				return &Entry;
			}
		}

		return nullptr;
	}
}

UActorComponent* FLuaWorldLibrary::FindComponentByTypeName(AActor* Actor, const FString& TypeName)
{
	return FindComponentByTypeName(Actor, TypeName, 0);
}

UActorComponent* FLuaWorldLibrary::FindComponentByTypeName(AActor* Actor, const FString& TypeName, int32 ComponentIndex)
{
	if (ComponentIndex < 0)
	{
		return nullptr;
	}

	TArray<UActorComponent*> Components = FindComponentsByTypeName(Actor, TypeName);
	const size_t Index = static_cast<size_t>(ComponentIndex);
	return Index < Components.size() ? Components[Index] : nullptr;
}

void FLuaWorldLibrary::RegisterAllowedComponentClass(const FString& TypeName, UClass* Class, bool bCanCreateFromLua)
{
	if (!Class)
	{
		return;
	}

	const FString NormalizedTypeName = NormalizeLuaTypeName(TypeName);
	TArray<FLuaAllowedComponentClass>& ExtraClasses = GetExtraAllowedComponentClasses();
	for (FLuaAllowedComponentClass& Entry : ExtraClasses)
	{
		if (Entry.NormalizedName == NormalizedTypeName)
		{
			Entry.Class = Class;
			Entry.bCanCreate = bCanCreateFromLua;
			return;
		}
	}

	ExtraClasses.push_back({ NormalizedTypeName, Class, bCanCreateFromLua });
}

TArray<UActorComponent*> FLuaWorldLibrary::FindComponentsByTypeName(AActor* Actor, const FString& TypeName)
{
	TArray<UActorComponent*> Result;
	if (!Actor)
	{
		return Result;
	}

	const FLuaAllowedComponentClass* AllowedClass = FindAllowedLuaComponentClass(TypeName);
	if (!AllowedClass || !AllowedClass->Class)
	{
		UE_LOG("[LuaSecurity] FindComponent blocked: component type is not allowed from Lua. type = %s", TypeName.c_str());
		return Result;
	}

	UClass* TargetClass = AllowedClass->Class;

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (!Component || !Component->GetClass())
		{
			continue;
		}

		if (Component->GetClass()->IsA(TargetClass))
		{
			Result.push_back(Component);
		}
	}

	return Result;
}

ULuaScriptComponent* FLuaWorldLibrary::FindLuaScriptComponent(AActor* Actor, const FString& ScriptIdentifier)
{
	if (!Actor)
	{
		return nullptr;
	}

	for (UActorComponent* Component : Actor->GetComponents())
	{
		ULuaScriptComponent* ScriptComponent = Cast<ULuaScriptComponent>(Component);
		if (LuaScriptMatchesIdentifier(ScriptComponent, ScriptIdentifier))
		{
			return ScriptComponent;
		}
	}

	return nullptr;
}

UActorComponent* FLuaWorldLibrary::GetOrAddComponentByTypeName(AActor* Actor, const FString& TypeName)
{
	if (!Actor)
	{
		return nullptr;
	}

	if (UActorComponent* Existing = FindComponentByTypeName(Actor, TypeName))
	{
		return Existing;
	}

	const FLuaAllowedComponentClass* AllowedClass = FindAllowedLuaComponentClass(TypeName);
	if (!AllowedClass || !AllowedClass->Class || !AllowedClass->bCanCreate)
	{
		UE_LOG("[LuaSecurity] GetOrAddComponent blocked: component type is not creatable from Lua. type = %s", TypeName.c_str());
		return nullptr;
	}

	UClass* ComponentClass = AllowedClass->Class;

	EnsureRootComponent(Actor);

	UActorComponent* Component = Actor->AddComponentByClass(ComponentClass);
	if (!Component)
	{
		UE_LOG("[Lua] GetOrAddComponent failed: add failed. type = %s", TypeName.c_str());
		return nullptr;
	}

	PostComponentAdded(Actor, Component);
	return Component;
}

bool FLuaWorldLibrary::RemoveComponentByTypeName(AActor* Actor, const FString& TypeName)
{
	if (!Actor)
	{
		return false;
	}

	UActorComponent* Component = FindComponentByTypeName(Actor, TypeName);
	if (!Component)
	{
		return false;
	}

	return RemoveComponent(Actor, Component);
}

void FLuaWorldLibrary::PostComponentAdded(AActor* Actor, UActorComponent* Component)
{
	if (!Actor || !Component)
	{
		return;
	}

	if (USceneComponent* Scene = Cast<USceneComponent>(Component))
	{
		USceneComponent* Root = Actor->GetRootComponent();
		if (Root && Scene != Root && !Scene->GetParent())
		{
			Scene->AttachToComponent(Root);
		}
	}

	if (Actor->HasActorBegunPlay())
	{
		Component->BeginPlay();
	}

	if (UMovementComponent* Movement = Cast<UMovementComponent>(Component))
	{
		Movement->ResolveUpdatedComponent();
	}
}

void FLuaWorldLibrary::EnsureRootComponent(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (Actor->GetRootComponent())
	{
		return;
	}

	USceneComponent* Root = Actor->AddComponent<USceneComponent>();

	if (!Root)
	{
		UE_LOG("[Lua] EnsureRootComponent failed.");
		return;
	}

	Actor->SetRootComponent(Root);
}
