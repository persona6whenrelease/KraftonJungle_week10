#pragma once

namespace sol
{
	class state;
}

void RegisterCrossyLuaBindings(sol::state& Lua);
void RegisterCrossyHopMovementComponentBinding(sol::state& Lua);
void RegisterCrossyParryComponentBinding(sol::state& Lua);
void RegisterCrossyGameObjectComponentBindings(sol::state& Lua);
void RegisterCrossyRowManagerBinding(sol::state& Lua);
void RegisterCrossyUiBinding(sol::state& Lua);
void RegisterCrossySaveGameBinding(sol::state& Lua);
void ClearCrossyLuaUiEventHandler();
