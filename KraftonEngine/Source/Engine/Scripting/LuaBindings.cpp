#include "LuaBindings.h"

#define SOL_ALL_SAFETIES_ON 1
#define SOL_LUAJIT 1

// Lua에 함수를 바인딩함
void RegisterLuaBindings(sol::state& Lua)
{
	RegisterFVectorBinding(Lua);
	RegisterFVector4Binding(Lua);
	RegisterFRotatorBinding(Lua);

	RegisterActorLifecycleBinding(Lua);

	RegisterActorComponentBinding(Lua);
	RegisterLuaScriptComponentBinding(Lua);
	RegisterSceneComponentBinding(Lua);
	RegisterPrimitiveComponentBinding(Lua);
	
	RegisterShapeComponentBinding(Lua);
	RegisterSphereComponentBinding(Lua);
	RegisterBoxComponentBinding(Lua);
	RegisterCapsuleComponentBinding(Lua);

	RegisterStaticMeshComponentBinding(Lua);
	RegisterCameraComponentBinding(Lua);
	RegisterPawnOrientationComponentBinding(Lua);

	RegisterMovementComponentBinding(Lua);
	RegisterProjectileMovementComponentBinding(Lua);
	RegisterInterpToMovementComponentBinding(Lua);
	RegisterPendulumMovementComponentBinding(Lua);
	RegisterRotatingMovementComponentBinding(Lua);
	RegisterHopMovementComponentBinding(Lua);
	RegisterParryComponentBinding(Lua);

	RegisterPawnBinding(Lua);
	RegisterPlayerControllerBinding(Lua);

	RegisterGameObjectBinding(Lua);
	RegisterWorldExtendedBinding(Lua);
	RegisterInputBinding(Lua);
	RegisterDelegateBinding(Lua);
	RegisterRowManagerBinding(Lua);
	RegisterUiBinding(Lua);
	RegisterSaveGameBinding(Lua);
}
