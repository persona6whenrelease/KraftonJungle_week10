#include "GameClient/LinkedRuntimeModules.h"

#ifndef WITH_CROSSY_GAME_MODULE
#define WITH_CROSSY_GAME_MODULE IS_GAME_CLIENT
#endif

#if WITH_CROSSY_GAME_MODULE
#include "Games/Crossy/CrossyGameModule.h"
#endif

void RegisterLinkedRuntimeModules()
{
#if WITH_CROSSY_GAME_MODULE
	RegisterCrossyGameModule();
#endif
}
