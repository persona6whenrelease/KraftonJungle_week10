// LuaShapeBindings.cpp

#include "LuaBindings.h"
#include "SolInclude.h"

#include "LuaBindingHelper.h"
#include "LuaHandles.h"

#include "Math/Vector.h"

#include "Component/Collision/ShapeComponent.h"
#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/SphereComponent.h"
#include "Component/Collision/CapsuleComponent.h"
#include "Component/Collision/CollisionTypes.h"

// Lua에 충돌용 Shape를 등록
// Handle을 통해 UUID로만 접근 가능
void RegisterShapeComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaShapeComponentHandle>(
		"ShapeComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaShapeComponentHandle),

		LUA_COMPONENT_RO_PROPERTY_EXPR(
			"ShapeComponent",
			"ShapeType",
			FLuaShapeComponentHandle,
			UShapeComponent,
			int,
			static_cast<int>(ECollisionShapeType::None),
			static_cast<int>(Component->GetCollisionShapeType())
		),

		LUA_HANDLE_DOWNCAST_METHOD(
			"AsBox",
			FLuaShapeComponentHandle,
			UShapeComponent,
			FLuaBoxComponentHandle,
			UBoxComponent
		),

		LUA_HANDLE_DOWNCAST_METHOD(
			"AsSphere",
			FLuaShapeComponentHandle,
			UShapeComponent,
			FLuaSphereComponentHandle,
			USphereComponent
		),

		LUA_HANDLE_DOWNCAST_METHOD(
			"AsCapsule",
			FLuaShapeComponentHandle,
			UShapeComponent,
			FLuaCapsuleComponentHandle,
			UCapsuleComponent
		)
	);

	Lua["ShapeType_None"] = static_cast<int>(ECollisionShapeType::None);
	Lua["ShapeType_Box"] = static_cast<int>(ECollisionShapeType::Box);
	Lua["ShapeType_Sphere"] = static_cast<int>(ECollisionShapeType::Sphere);
	Lua["ShapeType_Capsule"] = static_cast<int>(ECollisionShapeType::Capsule);
}

void RegisterBoxComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaBoxComponentHandle>(
		"BoxComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaBoxComponentHandle),

		LUA_COMPONENT_RW_PROPERTY(
			"BoxComponent",
			"Extent",
			FLuaBoxComponentHandle,
			UBoxComponent,
			FVector,
			FVector::ZeroVector,
			GetBoxExtent(),
			SetBoxExtent(Value)
		)
	);
}

void RegisterSphereComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaSphereComponentHandle>(
		"SphereComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaSphereComponentHandle),

		LUA_COMPONENT_RW_PROPERTY(
			"SphereComponent",
			"Radius",
			FLuaSphereComponentHandle,
			USphereComponent,
			float,
			0.0f,
			GetSphereRadius(),
			SetSphereRadius(Value)
		)
	);
}

void RegisterCapsuleComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaCapsuleComponentHandle>(
		"CapsuleComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaCapsuleComponentHandle),

		LUA_COMPONENT_RW_PROPERTY(
			"CapsuleComponent",
			"Radius",
			FLuaCapsuleComponentHandle,
			UCapsuleComponent,
			float,
			0.0f,
			GetCapsuleRadius(),
			SetCapsuleRadius(Value)
		),

		LUA_COMPONENT_RW_PROPERTY(
			"CapsuleComponent",
			"HalfHeight",
			FLuaCapsuleComponentHandle,
			UCapsuleComponent,
			float,
			0.0f,
			GetCapsuleHalfHeight(),
			SetCapsuleHalfHeight(Value)
		)
	);
}
