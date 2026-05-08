#include "LuaBindings.h"

// External registration functions from various Bindings.cpp files
void RegisterActorLifecycleBinding(sol::state& Lua);
void RegisterCameraComponentBinding(sol::state& Lua);
void RegisterActorComponentBinding(sol::state& Lua);
void RegisterLuaScriptComponentBinding(sol::state& Lua);
void RegisterSceneComponentBinding(sol::state& Lua);
void RegisterPrimitiveComponentBinding(sol::state& Lua);
void RegisterDelegateBinding(sol::state& Lua);
void RegisterGameObjectBinding(sol::state& Lua);
void RegisterInputBinding(sol::state& Lua);
void RegisterFVector2Binding(sol::state& Lua);
void RegisterFVectorBinding(sol::state& Lua);
void RegisterFVector4Binding(sol::state& Lua);
void RegisterFRotatorBinding(sol::state& Lua);
void RegisterMovementComponentBinding(sol::state& Lua);
void RegisterProjectileMovementComponentBinding(sol::state& Lua);
void RegisterInterpToMovementComponentBinding(sol::state& Lua);
void RegisterPendulumMovementComponentBinding(sol::state& Lua);
void RegisterRotatingMovementComponentBinding(sol::state& Lua);
void RegisterPawnBinding(sol::state& Lua);
void RegisterPawnOrientationComponentBinding(sol::state& Lua);
void RegisterPlayerControllerBinding(sol::state& Lua);
void RegisterShapeComponentBinding(sol::state& Lua);
void RegisterBoxComponentBinding(sol::state& Lua);
void RegisterSphereComponentBinding(sol::state& Lua);
void RegisterCapsuleComponentBinding(sol::state& Lua);
void RegisterSoundBinding(sol::state& Lua);
void RegisterSpringArmComponentBinding(sol::state& Lua);
void RegisterStaticMeshComponentBinding(sol::state& Lua);
void RegisterWorldExtendedBinding(sol::state& Lua);

void RegisterLuaBindings(sol::state& Lua)
{
    RegisterFVector2Binding(Lua);
    RegisterFVectorBinding(Lua);
    RegisterFVector4Binding(Lua);
    RegisterFRotatorBinding(Lua);

    RegisterGameObjectBinding(Lua);
    RegisterActorLifecycleBinding(Lua);
    RegisterDelegateBinding(Lua);
    RegisterInputBinding(Lua);
    RegisterSoundBinding(Lua);

    RegisterActorComponentBinding(Lua);
    RegisterSceneComponentBinding(Lua);
    RegisterPrimitiveComponentBinding(Lua);
    RegisterStaticMeshComponentBinding(Lua);
    RegisterCameraComponentBinding(Lua);
    RegisterSpringArmComponentBinding(Lua);
    RegisterLuaScriptComponentBinding(Lua);

    RegisterShapeComponentBinding(Lua);
    RegisterBoxComponentBinding(Lua);
    RegisterSphereComponentBinding(Lua);
    RegisterCapsuleComponentBinding(Lua);

    RegisterMovementComponentBinding(Lua);
    RegisterProjectileMovementComponentBinding(Lua);
    RegisterInterpToMovementComponentBinding(Lua);
    RegisterPendulumMovementComponentBinding(Lua);
    RegisterRotatingMovementComponentBinding(Lua);

    RegisterPawnBinding(Lua);
    RegisterPlayerControllerBinding(Lua);
    RegisterPawnOrientationComponentBinding(Lua);

    RegisterWorldExtendedBinding(Lua);
}
