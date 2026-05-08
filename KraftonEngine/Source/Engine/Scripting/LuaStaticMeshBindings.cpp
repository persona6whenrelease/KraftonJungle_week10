// LuaStaticMeshBindings.cpp

#include "LuaBindings.h"
#include "SolInclude.h"

#include "LuaBindingHelper.h"
#include "LuaHandles.h"
#include "LuaWorldLibrary.h"
#include "LuaPropertyBridge.h"

#include "Core/Log.h"
#include "Component/StaticMeshComponent.h"

void RegisterStaticMeshComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaStaticMeshComponentHandle>(
		"StaticMeshComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaStaticMeshComponentHandle),

		"MeshPath",
		sol::property(
			[](const FLuaStaticMeshComponentHandle& Self)
			{
				UStaticMeshComponent* Mesh = Self.Resolve();

				if (!Mesh)
				{
					return FString("None");
				}

				return Mesh->GetStaticMeshPath();
			}
		),

		"Visible",
		sol::property(
			[](const FLuaStaticMeshComponentHandle& Self)
			{
				UStaticMeshComponent* Mesh = Self.Resolve();
				return Mesh ? Mesh->IsVisible() : false;
			},
			[](const FLuaStaticMeshComponentHandle& Self, bool bVisible)
			{
				UStaticMeshComponent* Mesh = Self.Resolve();
				if (!Mesh)
				{
					UE_LOG("[Lua] Invalid StaticMeshComponent.Visible Access.");
					return;
				}
				Mesh->SetVisibility(bVisible);
			}
		),

		"SetMesh",
		[](const FLuaStaticMeshComponentHandle& Self, const FString& MeshPath)
		{
			UStaticMeshComponent* Mesh = Self.Resolve();

			if (!Mesh)
			{
				UE_LOG("[Lua] Invalid StaticMeshComponent.SetMesh Call.");
				return false;
			}

			return FLuaWorldLibrary::SetStaticMesh(Mesh, MeshPath);
		},

		"SetMaterial",
		[](const FLuaStaticMeshComponentHandle& Self, int32 ElementIndex, const FString& MaterialPath)
		{
			UStaticMeshComponent* Mesh = Self.Resolve();

			if (!Mesh)
			{
				UE_LOG("[Lua] Invalid StaticMeshComponent.SetMaterial Call.");
				return false;
			}

			return FLuaWorldLibrary::SetMaterial(Mesh, ElementIndex, MaterialPath);
		},

		"SetScalarParameter",
		[](const FLuaStaticMeshComponentHandle& Self, int32 ElementIndex, const FString& ParameterName, float Value)
		{
			UStaticMeshComponent* Mesh = Self.Resolve();

			if (!Mesh)
			{
				UE_LOG("[Lua] Invalid StaticMeshComponent.SetScalarParameter Call.");
				return false;
			}

			return FLuaWorldLibrary::SetMaterialScalarParameter(Mesh, ElementIndex, ParameterName, Value);
		},

		"SetVectorParameter",
		[](const FLuaStaticMeshComponentHandle& Self, int32 ElementIndex, const FString& ParameterName, const FVector& Value)
		{
			UStaticMeshComponent* Mesh = Self.Resolve();

			if (!Mesh)
			{
				UE_LOG("[Lua] Invalid StaticMeshComponent.SetVectorParameter Call.");
				return false;
			}

			return FLuaWorldLibrary::SetMaterialVectorParameter(Mesh, ElementIndex, ParameterName, Value);
		},

		"SetVector4Parameter",
		[](const FLuaStaticMeshComponentHandle& Self, int32 ElementIndex, const FString& ParameterName, const FVector4& Value)
		{
			UStaticMeshComponent* Mesh = Self.Resolve();

			if (!Mesh)
			{
				UE_LOG("[Lua] Invalid StaticMeshComponent.SetVector4Parameter Call.");
				return false;
			}

			return FLuaWorldLibrary::SetMaterialVector4Parameter(Mesh, ElementIndex, ParameterName, Value);
		},

		"ListProperties",
		[](const FLuaStaticMeshComponentHandle& Self, sol::this_state State)
		{
			return FLuaPropertyBridge::ListProperties(State, Self.Resolve());
		},

		"HasProperty",
		[](const FLuaStaticMeshComponentHandle& Self, const FString& Name)
		{
			return FLuaPropertyBridge::HasProperty(Self.Resolve(), Name);
		},

		"GetProperty",
		[](const FLuaStaticMeshComponentHandle& Self, const FString& Name, sol::this_state State)
		{
			return FLuaPropertyBridge::GetProperty(State, Self.Resolve(), Name);
		},

		"SetProperty",
		[](const FLuaStaticMeshComponentHandle& Self, const FString& Name, sol::object Value)
		{
			return FLuaPropertyBridge::SetProperty(Self.Resolve(), Name, Value);
		},

		"SetColor",
		[](const FLuaStaticMeshComponentHandle& Self, const FVector4& Color)
		{
			UStaticMeshComponent* Mesh = Self.Resolve();

			if (!Mesh)
			{
				UE_LOG("[Lua] Invalid StaticMeshComponent.SetColor Call.");
				return false;
			}

			return FLuaWorldLibrary::SetMaterialVector4Parameter(Mesh, 0, "SectionColor", Color);
		}
	);
}
