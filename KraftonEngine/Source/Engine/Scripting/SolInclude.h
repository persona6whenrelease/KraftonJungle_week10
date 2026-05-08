// SolInclude.h
#pragma once

#ifndef SOL_ALL_SAFETIES_ON
#define SOL_ALL_SAFETIES_ON 1
#endif

#ifndef SOL_LUAJIT
#define SOL_LUAJIT 1
#endif


#ifdef check
	#pragma push_macro("check")
	#undef check
	#define KRAFTON_RESTORE_CHECK_MACRO
#endif

#ifdef checkf
	#pragma push_macro("checkf")
	#undef checkf
	#define KRAFTON_RESTORE_CHECKF_MACRO
#endif

#include <sol/sol.hpp>

#ifdef KRAFTON_RESTORE_CHECKF_MACRO
	#pragma pop_macro("checkf")
	#undef KRAFTON_RESTORE_CHECKF_MACRO
#endif

#ifdef KRAFTON_RESTORE_CHECK_MACRO
	#pragma pop_macro("check")
	#undef KRAFTON_RESTORE_CHECK_MACRO
#endif

/*
================================================================================
 새 Lua 컴포넌트 추가 가이드
 예: UHopMovementComponent를 Lua에서 obj.HopMovement로 쓰고 싶을 때
================================================================================

 1. LuaHandles.h
    - Lua용 Handle을 추가한다.

      struct FLuaHopMovementComponentHandle
      {
          uint32 UUID = 0;

          UHopMovementComponent* Resolve() const
          {
              UObject* Object = UObjectManager::Get().FindByUUID(UUID);
              return Cast<UHopMovementComponent>(Object);
          }

          bool IsValid() const
          {
              return Resolve() != nullptr;
          }
      };

 2. LuaBindings.h
    - 등록 함수 선언을 추가한다.

      void RegisterHopMovementComponentBinding(sol::state& Lua);

 3. LuaBindings.cpp
    - RegisterLuaBindings() 안에서 호출한다.

      RegisterHopMovementComponentBinding(Lua);

 4. LuaMovementBindings.cpp
    - Lua 타입을 등록한다.
    - 여기서 Lua에 보여줄 property와 method를 정한다.

      void RegisterHopMovementComponentBinding(sol::state& Lua)
      {
          Lua.new_usertype<FLuaHopMovementComponentHandle>(
              "HopMovementComponent",

              sol::no_constructor,

              LUA_HANDLE_COMMON(FLuaHopMovementComponentHandle),

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

              LUA_COMPONENT_METHOD(
                  "HopMovementComponent",
                  "StopSimulating",
                  FLuaHopMovementComponentHandle,
                  UHopMovementComponent,
                  StopSimulating()
              )
          );
      }

 5. LuaGameObjectBindings.cpp
    - GameObject에서 바로 꺼내 쓸 수 있게 연결한다.
    - 이 부분이 있어야 Lua에서 obj.HopMovement,
      obj:GetOrAddHopMovement(), obj:RemoveHopMovement()를 사용할 수 있다.

      LUA_GAMEOBJECT_COMPONENT_PROPERTY(
          "HopMovement",
          FLuaHopMovementComponentHandle,
          UHopMovementComponent
      ),

      LUA_GAMEOBJECT_GET_OR_ADD_COMPONENT_METHOD(
          "GetOrAddHopMovement",
          FLuaHopMovementComponentHandle,
          UHopMovementComponent
      ),

      LUA_GAMEOBJECT_REMOVE_COMPONENT_METHOD(
          "RemoveHopMovement",
          UHopMovementComponent
      ),

 6. LuaWorldLibrary.cpp
    - 문자열 방식도 지원하려면 허용 목록에 추가한다.
    - 이걸 넣으면 obj:GetOrAddComponent("HopMovement")도 가능해진다.

      { "hopmovement", UHopMovementComponent::StaticClass(), true },

 7. LuaComponentBindings.cpp  // 선택
    - Component로 받은 뒤 HopMovement로 바꿔 쓰고 싶을 때만 추가한다.

      comp:AsHopMovement()

 Lua 사용 예:

      local hop = obj.HopMovement

      if not hop:IsValid() then
          hop = obj:GetOrAddHopMovement()
      end

      hop.HopHeight = 40
      hop:StopSimulating()

================================================================================
*/
