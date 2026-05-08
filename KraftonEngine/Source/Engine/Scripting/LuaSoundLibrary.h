#pragma once

namespace sol
{
	class state;
}

class FLuaSoundLibrary
{
public:
	static void RegisterSoundBinding(sol::state& Lua);
};
