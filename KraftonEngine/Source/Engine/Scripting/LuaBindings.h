#pragma once

namespace sol
{
	class state;
}

void RegisterLuaBindings(sol::state& Lua);

void RegisterActorLifecycleBinding(sol::state& Lua);
void RegisterActorComponentBinding(sol::state& Lua);
void RegisterLuaScriptComponentBinding(sol::state& Lua);
void RegisterSceneComponentBinding(sol::state& Lua);
void RegisterPrimitiveComponentBinding(sol::state& Lua);
void RegisterStaticMeshComponentBinding(sol::state& Lua);
void RegisterCameraComponentBinding(sol::state& Lua);
void RegisterPawnOrientationComponentBinding(sol::state& Lua);

void RegisterFVectorBinding(sol::state& Lua);
void RegisterFVector4Binding(sol::state& Lua);
void RegisterFRotatorBinding(sol::state& Lua);
void RegisterGameObjectBinding(sol::state& Lua);

void RegisterMovementComponentBinding(sol::state& Lua);
void RegisterProjectileMovementComponentBinding(sol::state& Lua);
void RegisterInterpToMovementComponentBinding(sol::state& Lua);
void RegisterPendulumMovementComponentBinding(sol::state& Lua);
void RegisterRotatingMovementComponentBinding(sol::state& Lua);
void RegisterHopMovementComponentBinding(sol::state& Lua);
void RegisterParryComponentBinding(sol::state& Lua);

void RegisterShapeComponentBinding(sol::state& Lua);
void RegisterBoxComponentBinding(sol::state& Lua);
void RegisterSphereComponentBinding(sol::state& Lua);
void RegisterCapsuleComponentBinding(sol::state& Lua);

void RegisterPawnBinding(sol::state& Lua);
void RegisterPlayerControllerBinding(sol::state& Lua);
void RegisterWorldExtendedBinding(sol::state& Lua);
void RegisterInputBinding(sol::state& Lua);

void RegisterDelegateBinding(sol::state& Lua);
void RegisterRowManagerBinding(sol::state& Lua);
void RegisterUiBinding(sol::state& Lua);
void RegisterSaveGameBinding(sol::state& Lua);
void InstallLuaUiEventRouter();
void ClearLuaUiEventHandler();
