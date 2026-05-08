// LuaMovementBindings.cpp

#include "LuaBindings.h"
#include "SolInclude.h"

#include "LuaBindingHelper.h"
#include "LuaHandles.h"

#include "Math/Vector.h"
#include "Math/Rotator.h"

#include "Component/Movement/MovementComponent.h"
#include "Component/Movement/ProjectileMovementComponent.h"
#include "Component/Movement/InterpToMovementComponent.h"
#include "Component/Movement/PendulumMovementComponent.h"
#include "Component/Movement/RotatingMovementComponent.h"

void RegisterMovementComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaMovementComponentHandle>(
		"MovementComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaMovementComponentHandle),

		LUA_COMPONENT_RO_PROPERTY(
			"MovementComponent",
			"HasValidUpdatedComponent",
			FLuaMovementComponentHandle,
			UMovementComponent,
			bool,
			false,
			HasValidUpdatedComponent()
		)
	);
}

void RegisterProjectileMovementComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaProjectileMovementComponentHandle>(
		"ProjectileMovementComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaProjectileMovementComponentHandle),

		LUA_COMPONENT_RW_PROPERTY(
			"ProjectileMovementComponent",
			"Velocity",
			FLuaProjectileMovementComponentHandle,
			UProjectileMovementComponent,
			FVector,
			FVector::ZeroVector,
			GetVelocity(),
			SetVelocity(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"ProjectileMovementComponent",
			"InitialSpeed",
			FLuaProjectileMovementComponentHandle,
			UProjectileMovementComponent,
			float,
			0.0f,
			GetInitialSpeed(),
			SetInitialSpeed(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"ProjectileMovementComponent",
			"MaxSpeed",
			FLuaProjectileMovementComponentHandle,
			UProjectileMovementComponent,
			float,
			0.0f,
			GetMaxSpeed(),
			SetMaxSpeed(Value)
		),

		LUA_COMPONENT_RO_PROPERTY(
			"ProjectileMovementComponent",
			"PreviewVelocity",
			FLuaProjectileMovementComponentHandle,
			UProjectileMovementComponent,
			FVector,
			FVector::ZeroVector,
			GetPreviewVelocity()
		),

		LUA_COMPONENT_METHOD(
			"ProjectileMovementComponent",
			"StopSimulating",
			FLuaProjectileMovementComponentHandle,
			UProjectileMovementComponent,
			StopSimulating()
		)
	);
}

void RegisterInterpToMovementComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaInterpToMoveComponentHandle>(
		"InterpToMovementComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaInterpToMoveComponentHandle),

		LUA_COMPONENT_RW_PROPERTY(
			"InterpToMovementComponent",
			"Duration",
			FLuaInterpToMoveComponentHandle,
			UInterpToMovementComponent,
			float,
			0.0f,
			GetInterpDuration(),
			SetInterpDuration(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"InterpToMovementComponent",
			"AutoActivate",
			FLuaInterpToMoveComponentHandle,
			UInterpToMovementComponent,
			bool,
			false,
			IsAutoActivating(),
			ShouldAutoActivate(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"InterpToMovementComponent",
			"FaceTargetDir",
			FLuaInterpToMoveComponentHandle,
			UInterpToMovementComponent,
			bool,
			false,
			IsFacingTargetDir(),
			ShouldFaceTargetDir(Value)
		),

		LUA_COMPONENT_METHOD(
			"InterpToMovementComponent",
			"Initiate",
			FLuaInterpToMoveComponentHandle,
			UInterpToMovementComponent,
			Initiate()
		),

		LUA_COMPONENT_METHOD(
			"InterpToMovementComponent",
			"Reset",
			FLuaInterpToMoveComponentHandle,
			UInterpToMovementComponent,
			Reset()
		),

		LUA_COMPONENT_METHOD(
			"InterpToMovementComponent",
			"ResetAndHalt",
			FLuaInterpToMoveComponentHandle,
			UInterpToMovementComponent,
			ResetAndHalt()
		)
	);
}

void RegisterPendulumMovementComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaPendulumMovementComponentHandle>(
		"PendulumMovementComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaPendulumMovementComponentHandle),

		LUA_COMPONENT_RW_PROPERTY(
			"PendulumMovementComponent",
			"Axis",
			FLuaPendulumMovementComponentHandle,
			UPendulumMovementComponent,
			FVector,
			FVector::UpVector,
			GetAxis(),
			SetAxis(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"PendulumMovementComponent",
			"Amplitude",
			FLuaPendulumMovementComponentHandle,
			UPendulumMovementComponent,
			float,
			0.0f,
			GetAmplitude(),
			SetAmplitude(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"PendulumMovementComponent",
			"Frequency",
			FLuaPendulumMovementComponentHandle,
			UPendulumMovementComponent,
			float,
			0.0f,
			GetFrequency(),
			SetFrequency(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"PendulumMovementComponent",
			"Phase",
			FLuaPendulumMovementComponentHandle,
			UPendulumMovementComponent,
			float,
			0.0f,
			GetPhase(),
			SetPhase(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"PendulumMovementComponent",
			"AngleOffset",
			FLuaPendulumMovementComponentHandle,
			UPendulumMovementComponent,
			float,
			0.0f,
			GetAngleOffset(),
			SetAngleOffset(Value)
		)
	);
}

void RegisterRotatingMovementComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaRotatingMovementComponentHandle>(
		"RotatingMovementComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaRotatingMovementComponentHandle),

		LUA_COMPONENT_RW_PROPERTY(
			"RotatingMovementComponent",
			"RotationRate",
			FLuaRotatingMovementComponentHandle,
			URotatingMovementComponent,
			FRotator,
			FRotator(),
			GetRotationRate(),
			SetRotationRate(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"RotatingMovementComponent",
			"RotationInLocalSpace",
			FLuaRotatingMovementComponentHandle,
			URotatingMovementComponent,
			bool,
			false,
			IsRotationInLocalSpace(),
			SetRotationInLocalSpace(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"RotatingMovementComponent",
			"PivotTranslation",
			FLuaRotatingMovementComponentHandle,
			URotatingMovementComponent,
			FVector,
			FVector::ZeroVector,
			GetPivotTranslation(),
			SetPivotTranslation(Value)
		),

		LUA_COMPONENT_METHOD(
			"RotatingMovementComponent",
			"ResetWorldPivotCache",
			FLuaRotatingMovementComponentHandle,
			URotatingMovementComponent,
			ResetWorldPivotCache()
		)
	);
}

void RegisterHopMovementComponentBinding(sol::state& Lua)
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

		// Backward/short alias for scripts that treat this as the input axis vector.
		LUA_COMPONENT_RW_PROPERTY(
			"HopMovementComponent",
			"InputVector",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			FVector,
			FVector::ZeroVector,
			GetMovementInput(),
			SetMovementInput(Value)
		),

		LUA_COMPONENT_RO_PROPERTY(
			"HopMovementComponent",
			"LastMovementInput",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			FVector,
			FVector::ZeroVector,
			GetLastMovementInput()
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

		LUA_COMPONENT_RO_PROPERTY(
			"HopMovementComponent",
			"PreviewVelocity",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			FVector,
			FVector::ZeroVector,
			GetPreviewVelocity()
		),

		"AddMovementInput",
		[](const FLuaHopMovementComponentHandle& Self, const FVector& Direction, float Scale)
		{
			UHopMovementComponent* Component = Self.Resolve();
			if (!Component)
			{
				UE_LOG("[Lua] Invalid HopMovementComponent.AddMovementInput Call.");
				return;
			}
			Component->AddMovementInput(Direction, Scale);
		},

		"AddLocalMovementInput",
		[](const FLuaHopMovementComponentHandle& Self, const FVector& Direction, float Scale)
		{
			UHopMovementComponent* Component = Self.Resolve();
			if (!Component)
			{
				UE_LOG("[Lua] Invalid HopMovementComponent.AddLocalMovementInput Call.");
				return;
			}
			Component->AddLocalMovementInput(Direction, Scale);
		},

		"SetLocalMovementInput",
		[](const FLuaHopMovementComponentHandle& Self, const FVector& Direction)
		{
			UHopMovementComponent* Component = Self.Resolve();
			if (!Component)
			{
				UE_LOG("[Lua] Invalid HopMovementComponent.SetLocalMovementInput Call.");
				return;
			}
			Component->SetLocalMovementInput(Direction);
		},

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
			"StartSimulating",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			StartSimulating()
		),

		LUA_COMPONENT_METHOD(
			"HopMovementComponent",
			"StopSimulating",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent,
			StopSimulating()
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