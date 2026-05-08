#include "Games/Crossy/Scripting/CrossyLuaBindings.h"

#include "Scripting/SolInclude.h"
#include "Scripting/LuaBindingHelper.h"
#include "Games/Crossy/Scripting/CrossyLuaHandles.h"
#include "Games/Crossy/Components/HopMovementComponent.h"
#include "Games/Crossy/Components/ParryComponent.h"
#include "Core/Log.h"
#include "Scripting/LuaWorldLibrary.h"

void RegisterCrossyHopMovementComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaHopMovementComponentHandle>(
		"HopMovementComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaHopMovementComponentHandle),

		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"MovementInput",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			FVector,
			FVector::ZeroVector,
			GetMovementInput(),
			SetMovementInput(Value)
		),



		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"Velocity",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			FVector,
			FVector::ZeroVector,
			GetVelocity(),
			SetVelocity(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"InitialSpeed",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			float,
			0.0f,
			GetInitialSpeed(),
			SetInitialSpeed(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"MaxSpeed",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			float,
			0.0f,
			GetMaxSpeed(),
			SetMaxSpeed(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"HopCoefficient",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			float,
			1.0f,
			GetHopCoefficient(),
			SetHopCoefficient(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"Acceleration",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			float,
			0.0f,
			GetAcceleration(),
			SetAcceleration(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"BrakingDeceleration",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			float,
			0.0f,
			GetBrakingDeceleration(),
			SetBrakingDeceleration(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"HopHeight",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			float,
			0.0f,
			GetHopHeight(),
			SetHopHeight(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"HopFrequency",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			float,
			1.0f,
			GetHopFrequency(),
			SetHopFrequency(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"HopOnlyWhenMoving",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			bool,
			true,
			IsHopOnlyWhenMoving(),
			SetHopOnlyWhenMoving(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"ResetHopWhenIdle",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			bool,
			true,
			ShouldResetHopWhenIdle(),
			SetResetHopWhenIdle(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"Simulating",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			bool,
			false,
			IsSimulating(),
			SetSimulating(Value)
		),





		LUA_COMPONENT_METHOD(
			"HopMovementComponent",
			"ClearMovementInput",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			ClearMovementInput()
		),

		LUA_COMPONENT_METHOD(
			"HopMovementComponent",
			"StopMovementImmediately",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			StopMovementImmediately()
		),


		LUA_COMPONENT_METHOD(
        "HopMovementComponent",
        "Dash",
        FLuaHopMovementComponentHandle,
        UHopMovementComponent,
        Dash()
		)
	);
}

void RegisterCrossyGameObjectComponentBindings(sol::state& Lua)
{
	sol::usertype<FLuaGameObjectHandle> GameObject = Lua["GameObject"];

	GameObject.set("HopMovement", sol::property(
		[](const FLuaGameObjectHandle& Self)
		{
			FLuaHopMovementComponentHandle Handle;
			AActor* Actor = Self.Resolve();
			UHopMovementComponent* Component = FLuaWorldLibrary::FindComponent<UHopMovementComponent>(Actor);
			if (Component)
			{
				Handle.UUID = Component->GetUUID();
			}
			return Handle;
		}));

	GameObject.set("Parry", sol::property(
		[](const FLuaGameObjectHandle& Self)
		{
			FLuaParryComponentHandle Handle;
			AActor* Actor = Self.Resolve();
			UParryComponent* Component = FLuaWorldLibrary::FindComponent<UParryComponent>(Actor);
			if (Component)
			{
				Handle.UUID = Component->GetUUID();
			}
			return Handle;
		}));

	GameObject.set_function("GetOrAddHopMovement",
		[](const FLuaGameObjectHandle& Self)
		{
			FLuaHopMovementComponentHandle Handle;
			AActor* Actor = Self.Resolve();
			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.GetOrAddHopMovement Call.");
				return Handle;
			}
			UHopMovementComponent* Component = FLuaWorldLibrary::GetOrAddComponent<UHopMovementComponent>(Actor);
			if (Component)
			{
				Handle.UUID = Component->GetUUID();
			}
			return Handle;
		});

	GameObject.set_function("GetOrAddParry",
		[](const FLuaGameObjectHandle& Self)
		{
			FLuaParryComponentHandle Handle;
			AActor* Actor = Self.Resolve();
			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.GetOrAddParry Call.");
				return Handle;
			}
			UParryComponent* Component = FLuaWorldLibrary::GetOrAddComponent<UParryComponent>(Actor);
			if (Component)
			{
				Handle.UUID = Component->GetUUID();
			}
			return Handle;
		});

	GameObject.set_function("RemoveHopMovement",
		[](const FLuaGameObjectHandle& Self)
		{
			AActor* Actor = Self.Resolve();
			return Actor ? FLuaWorldLibrary::RemoveComponent<UHopMovementComponent>(Actor) : false;
		});
}
