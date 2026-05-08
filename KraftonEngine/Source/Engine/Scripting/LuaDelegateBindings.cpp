// LuaDelegateBindings.cpp

#include "LuaBindings.h"
#include "SolInclude.h"

#include "LuaBindingHelper.h"
#include "LuaHandles.h"

#include "Core/Log.h"
#include "GameFramework/AActor.h"

// DelegateManager를 연결했다면 추가
// #include "LuaDelegateManager.h"

void RegisterDelegateBinding(sol::state& Lua)
{
	LUA_REGISTER_ACTOR_DELEGATE(
		Lua,
		"RegisterOnTick",
		[](){}
	);

	// TODO: DelegateManager 연결 후에는 위 [](){} 대신 아래처럼 변경
	/*
	LUA_REGISTER_ACTOR_DELEGATE(
		Lua,
		"RegisterOnTick",
		FLuaDelegateManager::Get().RegisterActorTick(
			Actor->GetUUID(),
			Function
		)
	);
	*/
}
