#include "LuaBindings.h"
#include "SolInclude.h"

#include "Core/Log.h"
#include "Engine/Runtime/Engine.h"
#include "Scripting/LuaScriptSubsystem.h"
#include "Engine/UI/Game/GameUiSystem.h"
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

	FGameUiSystem* GetGameUiSystem()
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

		FGameUiSystem& GameUi = ViewportClient->GetGameUiSystem();
		if (GPendingUiEventHandler)
		{
			GameUi.SetScriptEventHandler(GPendingUiEventHandler);
		}
		return &GameUi;
	}

	void WithGameUi(const char* FunctionName, const std::function<void(FGameUiSystem&)>& Callback)
	{
		FGameUiSystem* GameUi = GetGameUiSystem();
		if (!GameUi)
		{
			UE_LOG("[Lua UI] %s: Game UI system is not available yet.", FunctionName ? FunctionName : "UI");
			return;
		}

		Callback(*GameUi);
	}
}

void InstallLuaUiEventRouter()
{
	// This handler is intentionally native-only. It never captures sol::function,
	// so it remains safe when the active Lua state is replaced during hot reload.
	GPendingUiEventHandler = [](const FString& EventName)
	{
		RouteUiEventToCurrentLua(EventName);
	};

	if (FGameUiSystem* GameUi = GetGameUiSystem())
	{
		GameUi->SetScriptEventHandler(GPendingUiEventHandler);
	}
}

void ClearLuaUiEventHandler()
{
	GPendingUiEventHandler = nullptr;
	if (FGameUiSystem* GameUi = GetGameUiSystem())
	{
		GameUi->ClearScriptEventHandler();
	}
}

void RegisterUiBinding(sol::state& Lua)
{
	sol::table UI = Lua.get_or("UI", Lua.create_table());
	Lua["UI"] = UI;

	UI.set_function("SetEventHandler",
		[](sol::protected_function Handler, sol::this_state State)
		{
			sol::state_view LuaView(State);
			sol::table UiTable = LuaView["UI"];
			if (Handler.valid())
			{
				UiTable["_EventHandler"] = Handler;
				InstallLuaUiEventRouter();
			}
			else
			{
				UiTable["_EventHandler"] = sol::nil;
			}
		});

	UI.set_function("ClearEventHandler",
		[](sol::this_state State)
		{
			sol::state_view LuaView(State);
			sol::object UiObject = LuaView["UI"];
			if (UiObject.get_type() == sol::type::table)
			{
				sol::table UiTable = UiObject.as<sol::table>();
				UiTable["_EventHandler"] = sol::nil;
			}
			ClearLuaUiEventHandler();
		});

	UI.set_function("ShowIntro",
		[](sol::optional<bool> bVisible)
		{
			WithGameUi("UI.ShowIntro", [bVisible](FGameUiSystem& GameUi)
			{
				GameUi.SetIntroVisible(bVisible.value_or(true));
			});
		});

	UI.set_function("ShowHUD",
		[](sol::optional<bool> bVisible)
		{
			WithGameUi("UI.ShowHUD", [bVisible](FGameUiSystem& GameUi)
			{
				GameUi.SetHudVisible(bVisible.value_or(true));
			});
		});

	UI.set_function("ShowPause",
		[](sol::optional<bool> bVisible)
		{
			WithGameUi("UI.ShowPause", [bVisible](FGameUiSystem& GameUi)
			{
				GameUi.SetPauseMenuVisible(bVisible.value_or(true));
			});
		});

	UI.set_function("ShowGameOver",
		[](sol::optional<int32> FinalScore, sol::optional<int32> BestScore)
		{
			WithGameUi("UI.ShowGameOver", [FinalScore, BestScore](FGameUiSystem& GameUi)
			{
				const int32 Final = FinalScore.value_or(0);
				const int32 Best = BestScore.value_or(Final);
				GameUi.ShowGameOver(Final, Best);
			});
		});

	UI.set_function("HideGameOver",
		[]()
		{
			WithGameUi("UI.HideGameOver", [](FGameUiSystem& GameUi)
			{
				GameUi.HideGameOver();
			});
		});

	UI.set_function("ResetRun",
		[]()
		{
			WithGameUi("UI.ResetRun", [](FGameUiSystem& GameUi)
			{
				GameUi.ResetRunUi();
			});
		});

	UI.set_function("SetScore",
		[](int32 Score)
		{
			WithGameUi("UI.SetScore", [Score](FGameUiSystem& GameUi)
			{
				GameUi.SetScore(Score);
			});
		});

	UI.set_function("SetBestScore",
		[](int32 BestScore)
		{
			WithGameUi("UI.SetBestScore", [BestScore](FGameUiSystem& GameUi)
			{
				GameUi.SetBestScore(BestScore);
			});
		});

	UI.set_function("SetCoins",
		[](int32 Coins)
		{
			WithGameUi("UI.SetCoins", [Coins](FGameUiSystem& GameUi)
			{
				GameUi.SetCoins(Coins);
			});
		});

	UI.set_function("SetLane",
		[](int32 Lane)
		{
			WithGameUi("UI.SetLane", [Lane](FGameUiSystem& GameUi)
			{
				GameUi.SetLane(Lane);
			});
		});

	UI.set_function("SetCombo",
		[](int32 Combo)
		{
			WithGameUi("UI.SetCombo", [Combo](FGameUiSystem& GameUi)
			{
				GameUi.SetCombo(Combo);
			});
		});

	UI.set_function("SetStatus",
		[](const std::string& Text)
		{
			WithGameUi("UI.SetStatus", [&Text](FGameUiSystem& GameUi)
			{
				GameUi.SetStatusText(FString(Text));
			});
		});

	UI.set_function("SetTopScoresText",
		[](const std::string& Text)
		{
			WithGameUi("UI.SetTopScoresText", [&Text](FGameUiSystem& GameUi)
			{
				GameUi.SetTopScoresText(FString(Text));
			});
		});
}
