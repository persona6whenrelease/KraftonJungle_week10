#pragma once

#include "Core/Log.h"
#include "LuaWorldLibrary.h"

#ifndef LUA_ENABLE_DEBUG_HANDLE_UUID
#define LUA_ENABLE_DEBUG_HANDLE_UUID 1
#endif

#if LUA_ENABLE_DEBUG_HANDLE_UUID
#define LUA_HANDLE_DEBUG_UUID(HANDLE_TYPE) \
	, "UUID", \
	sol::property( \
		[](const HANDLE_TYPE& Self) \
		{ \
			return Self.UUID; \
		} \
	)
#else
#define LUA_HANDLE_DEBUG_UUID(HANDLE_TYPE)
#endif

// Handle 타입에 공통으로 필요한 Lua 멤버를 등록
// UUID는 기본 Lua API에 노출하지 않는다. 디버깅이 필요할 때만 LUA_ENABLE_DEBUG_HANDLE_UUID를 켠다.
#define LUA_HANDLE_COMMON(HANDLE_TYPE) \
	"IsValid", \
	[](const HANDLE_TYPE& Handle) \
	{ \
		return Handle.IsValid(); \
	} \
	LUA_HANDLE_DEBUG_UUID(HANDLE_TYPE)

// Component의 읽기 전용 property를 Lua에 등록
// TYPE_NAME: 로그용 타입 이름, PROPERTY_NAME: Lua 속성 이름, HANDLE_TYPE: Lua Handle 타입, COMPONENT_TYPE: 실제 C++ 컴포넌트 타입, VALUE_TYPE: 반환 타입, DEFAULT_VALUE: 실패 시 기본값, GETTER_CALL: Component-> 뒤에 붙을 getter 호출식
#define LUA_COMPONENT_RO_PROPERTY(TYPE_NAME, PROPERTY_NAME, HANDLE_TYPE, COMPONENT_TYPE, VALUE_TYPE, DEFAULT_VALUE, GETTER_CALL) \
	PROPERTY_NAME, \
	sol::property( \
		[](const HANDLE_TYPE& Self) -> VALUE_TYPE \
		{ \
			COMPONENT_TYPE* Component = Self.Resolve(); \
			return Component ? Component->GETTER_CALL : DEFAULT_VALUE; \
		} \
	)

// Component의 읽기/쓰기 property를 Lua에 등록
// TYPE_NAME: 로그용 타입 이름, PROPERTY_NAME: Lua 속성 이름, HANDLE_TYPE: Lua Handle 타입, COMPONENT_TYPE: 실제 C++ 컴포넌트 타입, VALUE_TYPE: 속성 타입, DEFAULT_VALUE: 실패 시 기본값, GETTER_CALL: getter 호출식, SETTER_CALL: Value를 사용하는 setter 호출식
#define LUA_COMPONENT_RW_PROPERTY(TYPE_NAME, PROPERTY_NAME, HANDLE_TYPE, COMPONENT_TYPE, VALUE_TYPE, DEFAULT_VALUE, GETTER_CALL, SETTER_CALL) \
	PROPERTY_NAME, \
	sol::property( \
		[](const HANDLE_TYPE& Self) -> VALUE_TYPE \
		{ \
			COMPONENT_TYPE* Component = Self.Resolve(); \
			return Component ? Component->GETTER_CALL : DEFAULT_VALUE; \
		}, \
		[](const HANDLE_TYPE& Self, const VALUE_TYPE& Value) \
		{ \
			COMPONENT_TYPE* Component = Self.Resolve(); \
			if (!Component) \
			{ \
				UE_LOG("[Lua] Invalid " TYPE_NAME "." PROPERTY_NAME " Access."); \
				return; \
			} \
			Component->SETTER_CALL; \
		} \
	)

// Component의 멤버 함수를 Lua 메서드로 등록
// TYPE_NAME: 로그용 타입 이름, METHOD_NAME: Lua 메서드 이름, HANDLE_TYPE: Lua Handle 타입, COMPONENT_TYPE: 실제 C++ 컴포넌트 타입, METHOD_CALL: Component-> 뒤에 붙을 함수 호출식
#define LUA_COMPONENT_METHOD(TYPE_NAME, METHOD_NAME, HANDLE_TYPE, COMPONENT_TYPE, METHOD_CALL) \
	METHOD_NAME, \
	[](const HANDLE_TYPE& Self) \
	{ \
		COMPONENT_TYPE* Component = Self.Resolve(); \
		if (!Component) \
		{ \
			UE_LOG("[Lua] Invalid " TYPE_NAME "." METHOD_NAME " Call."); \
			return; \
		} \
		Component->METHOD_CALL; \
	}

// GameObject에서 특정 컴포넌트를 찾아 Lua Handle로 반환하는 property를 등록
// PROPERTY_NAME: GameObject에 노출할 Lua 속성 이름, HANDLE_TYPE: 반환할 Lua Handle 타입, COMPONENT_TYPE: Actor에서 찾을 실제 C++ 컴포넌트 타입
#define LUA_GAMEOBJECT_COMPONENT_PROPERTY(PROPERTY_NAME, HANDLE_TYPE, COMPONENT_TYPE) \
	PROPERTY_NAME, \
	sol::property( \
		[](const FLuaGameObjectHandle& Self) \
		{ \
			HANDLE_TYPE Handle; \
			AActor* Actor = Self.Resolve(); \
			COMPONENT_TYPE* Component = FLuaWorldLibrary::FindComponent<COMPONENT_TYPE>(Actor); \
			if (Component) \
			{ \
				Handle.UUID = Component->GetUUID(); \
			} \
			return Handle; \
		} \
	)

// 부모 Handle에서 자식 타입 Handle로 다운캐스팅하는 Lua 메서드를 등록
// METHOD_NAME: Lua 다운캐스팅 메서드 이름, SOURCE_HANDLE_TYPE: 원본 Handle 타입, SOURCE_COMPONENT_TYPE: 원본 C++ 타입, TARGET_HANDLE_TYPE: 반환할 Handle 타입, TARGET_COMPONENT_TYPE: 캐스팅할 자식 C++ 타입
#define LUA_HANDLE_DOWNCAST_METHOD(METHOD_NAME, SOURCE_HANDLE_TYPE, SOURCE_COMPONENT_TYPE, TARGET_HANDLE_TYPE, TARGET_COMPONENT_TYPE) \
	METHOD_NAME, \
	[](const SOURCE_HANDLE_TYPE& Self, sol::this_state State) -> sol::object \
	{ \
		sol::state_view LuaView(State); \
		SOURCE_COMPONENT_TYPE* Source = Self.Resolve(); \
		TARGET_COMPONENT_TYPE* Target = Cast<TARGET_COMPONENT_TYPE>(Source); \
		if (!Target) \
		{ \
			return sol::nil; \
		} \
		TARGET_HANDLE_TYPE Handle; \
		Handle.UUID = Target->GetUUID(); \
		return sol::make_object(LuaView, Handle); \
	}

// 일반 getter 호출로 처리하기 어려운 읽기 전용 property를 등록
// TYPE_NAME: 로그용 타입 이름, PROPERTY_NAME: Lua 속성 이름, HANDLE_TYPE: Lua Handle 타입, COMPONENT_TYPE: 실제 C++ 타입, VALUE_TYPE: 반환 타입, DEFAULT_VALUE: 실패 시 기본값, EXPR: Component 변수를 사용하는 반환 표현식
#define LUA_COMPONENT_RO_PROPERTY_EXPR(TYPE_NAME, PROPERTY_NAME, HANDLE_TYPE, COMPONENT_TYPE, VALUE_TYPE, DEFAULT_VALUE, EXPR) \
	PROPERTY_NAME, \
	sol::property( \
		[](const HANDLE_TYPE& Self) -> VALUE_TYPE \
		{ \
			COMPONENT_TYPE* Component = Self.Resolve(); \
			if (!Component) \
			{ \
				return DEFAULT_VALUE; \
			} \
			return EXPR; \
		} \
	)

// Actor 기반 Lua Delegate 등록 함수를 생성
// LUA_STATE: sol::state 변수, LUA_FUNCTION_NAME: Lua에 노출할 함수 이름, REGISTER_CALL: Actor와 Function을 사용해 실제 DelegateManager에 등록하는 호출식
#define LUA_REGISTER_ACTOR_DELEGATE(LUA_STATE, LUA_FUNCTION_NAME, REGISTER_CALL) \
	(LUA_STATE).set_function( \
		LUA_FUNCTION_NAME, \
		[](const FLuaGameObjectHandle& Object, sol::protected_function Function) \
		{ \
			AActor* Actor = Object.Resolve(); \
			if (!Actor) \
			{ \
				UE_LOG("[Lua] " LUA_FUNCTION_NAME " Failed: Invalid GameObject."); \
				return false; \
			} \
			REGISTER_CALL; \
			UE_LOG("[Lua] " LUA_FUNCTION_NAME " registered for Actor UUID = %u", Actor->GetUUID()); \
			return true; \
		} \
	)

// GameObject에서 컴포넌트를 찾고, 없으면 추가하는 메서드 등록
// METHOD_NAME: Lua 메서드 이름, HANDLE_TYPE: 반환할 Lua Handle, COMPONENT_TYPE: 실제 C++ 컴포넌트 타입
#define LUA_GAMEOBJECT_GET_OR_ADD_COMPONENT_METHOD(METHOD_NAME, HANDLE_TYPE, COMPONENT_TYPE) \
	METHOD_NAME, \
	[](const FLuaGameObjectHandle& Self) \
	{ \
		HANDLE_TYPE Handle; \
		AActor* Actor = Self.Resolve(); \
		if (!Actor) \
		{ \
			UE_LOG("[Lua] Invalid GameObject." METHOD_NAME " Call."); \
			return Handle; \
		} \
		COMPONENT_TYPE* Component = FLuaWorldLibrary::GetOrAddComponent<COMPONENT_TYPE>(Actor); \
		if (Component) \
		{ \
			Handle.UUID = Component->GetUUID(); \
		} \
		return Handle; \
	}

// GameObject에서 특정 컴포넌트를 제거하는 메서드 등록
// METHOD_NAME: Lua 메서드 이름, COMPONENT_TYPE: 제거할 실제 C++ 컴포넌트 타입
#define LUA_GAMEOBJECT_REMOVE_COMPONENT_METHOD(METHOD_NAME, COMPONENT_TYPE) \
	METHOD_NAME, \
	[](const FLuaGameObjectHandle& Self) \
	{ \
		AActor* Actor = Self.Resolve(); \
		if (!Actor) \
		{ \
			UE_LOG("[Lua] Invalid GameObject." METHOD_NAME " Call."); \
			return false; \
		} \
		return FLuaWorldLibrary::RemoveComponent<COMPONENT_TYPE>(Actor); \
	}

// GameObject의 대표 Shape를 특정 Shape 타입으로 교체하는 메서드 등록
// METHOD_NAME: Lua 메서드 이름, HANDLE_TYPE: 반환할 Shape Handle, SHAPE_TYPE: 생성할 Shape 컴포넌트 타입
#define LUA_GAMEOBJECT_SET_SHAPE_METHOD(METHOD_NAME, HANDLE_TYPE, SHAPE_TYPE) \
	METHOD_NAME, \
	[](const FLuaGameObjectHandle& Self) \
	{ \
		HANDLE_TYPE Handle; \
		AActor* Actor = Self.Resolve(); \
		if (!Actor) \
		{ \
			UE_LOG("[Lua] Invalid GameObject." METHOD_NAME " Call."); \
			return Handle; \
		} \
		SHAPE_TYPE* Shape = FLuaWorldLibrary::SetExclusiveShape<SHAPE_TYPE>(Actor); \
		if (Shape) \
		{ \
			Handle.UUID = Shape->GetUUID(); \
		} \
		return Handle; \
	}
