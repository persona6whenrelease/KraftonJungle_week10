#include "LuaSoundLibrary.h"
#include "SolInclude.h"
#include "Sound/SoundManager.h"

void RegisterSoundBinding(sol::state& Lua)
{
	FLuaSoundLibrary::RegisterSoundBinding(Lua);
}

void FLuaSoundLibrary::RegisterSoundBinding(sol::state& Lua)
{
	sol::table Sound = Lua.create_named_table("Sound");

	Sound.set_function("PlayEffect", sol::overload(
		[](const FString& ID)
		{
			FSoundManager::Get().PlayEffect(ID);
		},
		[](const FString& ID, float Volume)
		{
			FSoundManager::Get().PlayEffect(ID);
		}
	));

	Sound.set_function("StopEffect", [](const FString& ID)
	{
		FSoundManager::Get().StopEffect(ID);
	});

	Sound.set_function("IsEffectPlaying", [](const FString& ID) -> bool
	{
		return FSoundManager::Get().IsEffectPlaying(ID);
	});
}
