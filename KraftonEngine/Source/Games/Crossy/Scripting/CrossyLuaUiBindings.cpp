#include "Games/Crossy/Scripting/CrossyLuaBindings.h"
#include "Scripting/SolInclude.h"

#include "Core/Log.h"
#include "Engine/Runtime/Engine.h"
#include "Scripting/LuaScriptSubsystem.h"
#include "Games/Crossy/UI/CrossyGameUiSystem.h"
#include "Viewport/GameViewportClient.h"

#include <algorithm>
#include <functional>
#include <string>

namespace
{
	std::function<void(const FString&)> GPendingUiEventHandler;

	void RouteUiEventToCurrentLua(const FString& EventName)
	{
		FLuaScriptSubsystem::Get().DispatchUiEvent(EventName);
	}

	FCrossyGameUiSystem* GetCrossyGameUiSystem()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		UGameViewportClient* ViewportClient = GEngine->GetGameViewportClient();
		if (!ViewportClient)
		{
			return nullptr;
		}

		IViewportUiLayer* UiLayer = ViewportClient->GetUiLayer();
		FCrossyGameUiSystem* GameUi = UiLayer ? static_cast<FCrossyGameUiSystem*>(UiLayer) : nullptr;
		if (GameUi && GPendingUiEventHandler)
		{
			GameUi->SetScriptEventHandler(GPendingUiEventHandler);
		}
		return GameUi;
	}

	void WithGameUi(const char* FunctionName, const std::function<void(FCrossyGameUiSystem&)>& Callback)
	{
		FCrossyGameUiSystem* GameUi = GetCrossyGameUiSystem();
		if (!GameUi)
		{
			UE_LOG("[Lua UI] %s: Game UI system is not available yet.", FunctionName ? FunctionName : "UI");
			return;
		}

		Callback(*GameUi);
	}
}

void InstallCrossyLuaUiEventRouter()
{
	// This handler is intentionally native-only. It never captures sol::function,
	// so it remains safe when the active Lua state is replaced during hot reload.
	GPendingUiEventHandler = [](const FString& EventName)
	{
		RouteUiEventToCurrentLua(EventName);
	};

	if (FCrossyGameUiSystem* GameUi = GetCrossyGameUiSystem())
	{
		GameUi->SetScriptEventHandler(GPendingUiEventHandler);
	}
}

void ClearCrossyLuaUiEventHandler()
{
	GPendingUiEventHandler = nullptr;
	if (FCrossyGameUiSystem* GameUi = GetCrossyGameUiSystem())
	{
		GameUi->ClearScriptEventHandler();
	}
}

void RegisterCrossyUiBinding(sol::state& Lua)
{
	sol::table Game = Lua.get_or("Game", Lua.create_table());
	Lua["Game"] = Game;
	sol::table GameUi = Game.get_or("UI", Lua.create_table());
	Game["UI"] = GameUi;

	GameUi.set_function("SetEventHandler",
		[](sol::protected_function Handler, sol::this_state State)
		{
			sol::state_view LuaView(State);
			sol::table GameTable = LuaView["Game"];
			sol::table UiTable = GameTable["UI"];
			if (Handler.valid())
			{
				UiTable["_EventHandler"] = Handler;
				InstallCrossyLuaUiEventRouter();
			}
			else
			{
				UiTable["_EventHandler"] = sol::nil;
			}
		});

	GameUi.set_function("ClearEventHandler",
		[](sol::this_state State)
		{
			sol::state_view LuaView(State);
			sol::object GameObject = LuaView["Game"];
			if (GameObject.get_type() == sol::type::table)
			{
				sol::table GameTable = GameObject.as<sol::table>();
				sol::object UiObject = GameTable["UI"];
				if (UiObject.get_type() == sol::type::table)
				{
					sol::table UiTable = UiObject.as<sol::table>();
					UiTable["_EventHandler"] = sol::nil;
				}
			}
			ClearCrossyLuaUiEventHandler();
		});

	GameUi.set_function("ShowIntro",
		[](sol::optional<bool> bVisible)
		{
			WithGameUi("Game.UI.ShowIntro", [bVisible](FCrossyGameUiSystem& GameUi)
			{
				GameUi.SetIntroVisible(bVisible.value_or(true));
			});
		});

	GameUi.set_function("ShowHUD",
		[](sol::optional<bool> bVisible)
		{
			WithGameUi("Game.UI.ShowHUD", [bVisible](FCrossyGameUiSystem& GameUi)
			{
				GameUi.SetHudVisible(bVisible.value_or(true));
			});
		});

	GameUi.set_function("ShowPause",
		[](sol::optional<bool> bVisible)
		{
			WithGameUi("Game.UI.ShowPause", [bVisible](FCrossyGameUiSystem& GameUi)
			{
				GameUi.SetPauseMenuVisible(bVisible.value_or(true));
			});
		});

	GameUi.set_function("ShowGameOver",
		[](sol::optional<int32> FinalScore, sol::optional<int32> BestScore)
		{
			WithGameUi("Game.UI.ShowGameOver", [FinalScore, BestScore](FCrossyGameUiSystem& GameUi)
			{
				const int32 Final = FinalScore.value_or(0);
				const int32 Best = BestScore.value_or(Final);
				GameUi.ShowGameOver(Final, Best);
			});
		});

	GameUi.set_function("HideGameOver",
		[]()
		{
			WithGameUi("Game.UI.HideGameOver", [](FCrossyGameUiSystem& GameUi)
			{
				GameUi.HideGameOver();
			});
		});

	GameUi.set_function("ResetRun",
		[]()
		{
			WithGameUi("Game.UI.ResetRun", [](FCrossyGameUiSystem& GameUi)
			{
				GameUi.ResetRunUi();
			});
		});

	GameUi.set_function("SetScore",
		[](int32 Score)
		{
			WithGameUi("Game.UI.SetScore", [Score](FCrossyGameUiSystem& GameUi)
			{
				GameUi.SetScore(Score);
			});
		});

	GameUi.set_function("SetBestScore",
		[](int32 BestScore)
		{
			WithGameUi("Game.UI.SetBestScore", [BestScore](FCrossyGameUiSystem& GameUi)
			{
				GameUi.SetBestScore(BestScore);
			});
		});

	GameUi.set_function("SetCoins",
		[](int32 Coins)
		{
			WithGameUi("Game.UI.SetCoins", [Coins](FCrossyGameUiSystem& GameUi)
			{
				GameUi.SetCoins(Coins);
			});
		});

	GameUi.set_function("SetLane",
		[](int32 Lane)
		{
			WithGameUi("Game.UI.SetLane", [Lane](FCrossyGameUiSystem& GameUi)
			{
				GameUi.SetLane(Lane);
			});
		});

	GameUi.set_function("SetCombo",
		[](int32 Combo)
		{
			WithGameUi("Game.UI.SetCombo", [Combo](FCrossyGameUiSystem& GameUi)
			{
				GameUi.SetCombo(Combo);
			});
		});

	GameUi.set_function("SetStatus",
		[](const std::string& Text)
		{
			WithGameUi("Game.UI.SetStatus", [&Text](FCrossyGameUiSystem& GameUi)
			{
				GameUi.SetStatusText(FString(Text));
			});
		});

	GameUi.set_function("SetTopScoresText",
		[](const std::string& Text)
		{
			WithGameUi("Game.UI.SetTopScoresText", [&Text](FCrossyGameUiSystem& GameUi)
			{
				GameUi.SetTopScoresText(FString(Text));
			});
		});
}
