#include "Engine/UI/Game/GameUiSystem.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "Engine/UI/Rml/RmlD3D11RenderInterface.h"
#include "Engine/UI/Rml/RmlWin32SystemInterface.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Render/Pipeline/Renderer.h"
#include "Viewport/GameViewportClient.h"
#include "Core/Log.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Engine/UI/Rml/RmlUiConfig.h"
#if WITH_RMLUI
#include "Engine/UI/Rml/RmlUiWindowsMacroGuard.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>
#endif

namespace
{
	int GetSignedMouseX(LPARAM Param)
	{
		return static_cast<int>(static_cast<short>(LOWORD(Param)));
	}

	int GetSignedMouseY(LPARAM Param)
	{
		return static_cast<int>(static_cast<short>(HIWORD(Param)));
	}

	int GetSignedWheelDelta(WPARAM Param)
	{
		return static_cast<int>(static_cast<short>(HIWORD(Param)));
	}

	int ToRmlMouseButton(UINT Msg)
	{
		switch (Msg)
		{
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
			return 0;
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
			return 1;
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
			return 2;
		default:
			return 0;
		}
	}

#if WITH_RMLUI
	int GetRmlModifierState()
	{
		int Modifiers = 0;
		if (::GetKeyState(VK_CONTROL) & 0x8000) Modifiers |= Rml::Input::KM_CTRL;
		if (::GetKeyState(VK_SHIFT) & 0x8000) Modifiers |= Rml::Input::KM_SHIFT;
		if (::GetKeyState(VK_MENU) & 0x8000) Modifiers |= Rml::Input::KM_ALT;
		if (::GetKeyState(VK_CAPITAL) & 0x0001) Modifiers |= Rml::Input::KM_CAPSLOCK;
		if (::GetKeyState(VK_NUMLOCK) & 0x0001) Modifiers |= Rml::Input::KM_NUMLOCK;
		if (::GetKeyState(VK_SCROLL) & 0x0001) Modifiers |= Rml::Input::KM_SCROLLLOCK;
		return Modifiers;
	}

	Rml::Input::KeyIdentifier ToRmlKey(int VirtualKey)
	{
		if (VirtualKey >= 'A' && VirtualKey <= 'Z')
		{
			return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + (VirtualKey - 'A'));
		}
		if (VirtualKey >= '0' && VirtualKey <= '9')
		{
			return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + (VirtualKey - '0'));
		}
		if (VirtualKey >= VK_F1 && VirtualKey <= VK_F12)
		{
			return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_F1 + (VirtualKey - VK_F1));
		}
		if (VirtualKey >= VK_NUMPAD0 && VirtualKey <= VK_NUMPAD9)
		{
			return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_NUMPAD0 + (VirtualKey - VK_NUMPAD0));
		}

		switch (VirtualKey)
		{
		case VK_SPACE: return Rml::Input::KI_SPACE;
		case VK_BACK: return Rml::Input::KI_BACK;
		case VK_TAB: return Rml::Input::KI_TAB;
		case VK_RETURN: return Rml::Input::KI_RETURN;
		case VK_ESCAPE: return Rml::Input::KI_ESCAPE;
		case VK_PRIOR: return Rml::Input::KI_PRIOR;
		case VK_NEXT: return Rml::Input::KI_NEXT;
		case VK_END: return Rml::Input::KI_END;
		case VK_HOME: return Rml::Input::KI_HOME;
		case VK_LEFT: return Rml::Input::KI_LEFT;
		case VK_UP: return Rml::Input::KI_UP;
		case VK_RIGHT: return Rml::Input::KI_RIGHT;
		case VK_DOWN: return Rml::Input::KI_DOWN;
		case VK_INSERT: return Rml::Input::KI_INSERT;
		case VK_DELETE: return Rml::Input::KI_DELETE;
		case VK_SHIFT: return Rml::Input::KI_LSHIFT;
		case VK_LSHIFT: return Rml::Input::KI_LSHIFT;
		case VK_RSHIFT: return Rml::Input::KI_RSHIFT;
		case VK_CONTROL: return Rml::Input::KI_LCONTROL;
		case VK_LCONTROL: return Rml::Input::KI_LCONTROL;
		case VK_RCONTROL: return Rml::Input::KI_RCONTROL;
		case VK_MENU: return Rml::Input::KI_LMENU;
		case VK_LMENU: return Rml::Input::KI_LMENU;
		case VK_RMENU: return Rml::Input::KI_RMENU;
		case VK_OEM_PLUS: return Rml::Input::KI_OEM_PLUS;
		case VK_OEM_COMMA: return Rml::Input::KI_OEM_COMMA;
		case VK_OEM_MINUS: return Rml::Input::KI_OEM_MINUS;
		case VK_OEM_PERIOD: return Rml::Input::KI_OEM_PERIOD;
		case VK_OEM_1: return Rml::Input::KI_OEM_1;
		case VK_OEM_2: return Rml::Input::KI_OEM_2;
		case VK_OEM_3: return Rml::Input::KI_OEM_3;
		case VK_OEM_4: return Rml::Input::KI_OEM_4;
		case VK_OEM_5: return Rml::Input::KI_OEM_5;
		case VK_OEM_6: return Rml::Input::KI_OEM_6;
		case VK_OEM_7: return Rml::Input::KI_OEM_7;
		default: break;
		}
		return Rml::Input::KI_UNKNOWN;
	}
#endif
}

#if WITH_RMLUI
class FGameUiEventListener final : public Rml::EventListener
{
public:
	explicit FGameUiEventListener(FGameUiSystem* InOwner)
		: Owner(InOwner)
	{
	}

	void ProcessEvent(Rml::Event& Event) override
	{
		if (!Owner)
		{
			return;
		}

		Rml::Element* Element = Event.GetCurrentElement();
		if (!Element)
		{
			Element = Event.GetTargetElement();
		}
		if (!Element)
		{
			return;
		}

		Owner->HandleClick(Element->GetId());
	}

private:
	FGameUiSystem* Owner = nullptr;
};
#else
// WITH_RMLUI=0 빌드에서도 FGameUiSystem.h의
// std::unique_ptr<FGameUiEventListener>가 안전하게 소멸될 수 있도록
// 같은 번역 단위에서 완전한 타입을 제공한다.
class FGameUiEventListener final
{
public:
	explicit FGameUiEventListener(FGameUiSystem*) {}
};
#endif

FGameUiSystem::FGameUiSystem() = default;
FGameUiSystem::~FGameUiSystem()
{
	Shutdown();
}

bool FGameUiSystem::Initialize(FWindowsWindow* Window, FRenderer& Renderer, UGameViewportClient* InViewportClient)
{
	Shutdown();

	OwnerWindow = Window;
	ViewportClient = InViewportClient;
	if (!OwnerWindow || !ViewportClient)
	{
		return false;
	}

	PresentationRect = ViewportClient->GetPresentationRect();
	if (!PresentationRect.IsValid())
	{
		PresentationRect = FViewportPresentationRect(
			0.0f,
			0.0f,
			OwnerWindow->GetWidth(),
			OwnerWindow->GetHeight());
	}

#if WITH_RMLUI
	RenderInterface = std::make_unique<FRmlD3D11RenderInterface>();
	if (!RenderInterface->Initialize(
		Renderer.GetFD3DDevice().GetDevice(),
		Renderer.GetFD3DDevice().GetDeviceContext()))
	{
		Shutdown();
		return false;
	}
	RenderInterface->SetPresentationRect(PresentationRect);
	RenderInterface->SetRenderTargetSize(OwnerWindow->GetWidth(), OwnerWindow->GetHeight());

	SystemInterface = std::make_unique<FRmlWin32SystemInterface>();
	Rml::SetRenderInterface(RenderInterface.get());
	Rml::SetSystemInterface(SystemInterface.get());

	if (!Rml::Initialise())
	{
		Shutdown();
		return false;
	}

	Context = Rml::CreateContext(
		"GameUI",
		Rml::Vector2i(
			static_cast<int>(PresentationRect.Width),
			static_cast<int>(PresentationRect.Height)));
	if (!Context)
	{
		Rml::Shutdown();
		Shutdown();
		return false;
	}
	Context->EnableMouseCursor(true);

	Rml::LoadFontFace("C:/Windows/Fonts/malgun.ttf");
	Rml::LoadFontFace("C:/Windows/Fonts/malgunbd.ttf");
	LoadDocuments();

	bAvailable = Context != nullptr;
#else
	(void)Renderer;
	bAvailable = false;
#endif

	bInitialized = true;
	return true;
}

void FGameUiSystem::Shutdown()
{
#if WITH_RMLUI
	UnbindPauseMenuEvents();

	IntroDocument = nullptr;
	HudDocument = nullptr;
	PauseMenuDocument = nullptr;
	GameOverDocument = nullptr;

	if (Context)
	{
		const Rml::String ContextName = Context->GetName();
		Rml::RemoveContext(ContextName);
		Context = nullptr;
	}

	EventListener.reset();

	if (bAvailable)
	{
		Rml::Shutdown();
	}

	if (RenderInterface)
	{
		RenderInterface->Shutdown();
	}
#endif

	RenderInterface.reset();
	SystemInterface.reset();

	ScriptEventHandler = nullptr;
	PendingScriptEvents.clear();
	bFlushingScriptEvents = false;
	bStartEventQueuedOrDispatched = false;
	bStartTransitionActive = false;
	bStartTransitionResetDispatched = false;
	StartTransitionTime = 0.0f;
	Callbacks = {};

	OwnerWindow = nullptr;
	ViewportClient = nullptr;

	bInitialized = false;
	bAvailable = false;
	bIntroVisible = false;
	bHudVisible = false;
	bPauseMenuVisible = false;
	bGameOverVisible = false;
	bShowingOptions = false;
	bShowingIntroOptions = false;
}

void FGameUiSystem::SetCallbacks(FGameUiCallbacks InCallbacks)
{
	Callbacks = std::move(InCallbacks);
	RefreshOptionLabels();
}

void FGameUiSystem::SetScriptEventHandler(std::function<void(const FString&)> InHandler)
{
	ScriptEventHandler = std::move(InHandler);
}

void FGameUiSystem::ClearScriptEventHandler()
{
	ScriptEventHandler = nullptr;
	PendingScriptEvents.clear();
	bFlushingScriptEvents = false;
}

void FGameUiSystem::SetPresentationRect(const FViewportPresentationRect& InRect)
{
	if (!InRect.IsValid())
	{
		return;
	}
	PresentationRect = InRect;
	SyncContextDimensions();
}

void FGameUiSystem::SyncContextDimensions()
{
#if WITH_RMLUI
	if (Context)
	{
		Context->SetDimensions(Rml::Vector2i(
			static_cast<int>(PresentationRect.Width),
			static_cast<int>(PresentationRect.Height)));
	}
	if (RenderInterface)
	{
		RenderInterface->SetPresentationRect(PresentationRect);
		if (OwnerWindow)
		{
			RenderInterface->SetRenderTargetSize(OwnerWindow->GetWidth(), OwnerWindow->GetHeight());
		}
	}
#endif
}

void FGameUiSystem::Update(float DeltaTime)
{
	if (!bInitialized)
	{
		return;
	}

	if (ViewportClient && ViewportClient->HasValidPresentationRect())
	{
		SetPresentationRect(ViewportClient->GetPresentationRect());
	}

	UpdateStartTransition(DeltaTime);

#if WITH_RMLUI
	RefreshOptionLabels();
	if (Context)
	{
		Context->Update();
	}
#endif

	FlushQueuedScriptEvents();
}

void FGameUiSystem::Render()
{
#if WITH_RMLUI
	if (!bInitialized || !Context || !RenderInterface)
	{
		return;
	}

	SyncContextDimensions();
	Context->Render();
#endif
}

void FGameUiSystem::SetIntroVisible(bool bVisible)
{
	bIntroVisible = bVisible;
	if (bVisible)
	{
		bStartEventQueuedOrDispatched = false;
		bStartTransitionActive = false;
		bStartTransitionResetDispatched = false;
		StartTransitionTime = 0.0f;
		bPauseMenuVisible = false;
		bGameOverVisible = false;
		bHudVisible = false;
		bShowingIntroOptions = false;
	}

#if WITH_RMLUI
	if (IntroDocument)
	{
		bVisible ? IntroDocument->Show() : IntroDocument->Hide();
	}
	if (!bVisible)
	{
		ClearDocumentFocus(IntroDocument);
	}
	if (bVisible)
	{
		SetElementDisplay(IntroDocument, "intro-panel", true);
		SetElementDisplay(IntroDocument, "intro-soft-top", true);
		SetElementDisplay(IntroDocument, "intro-soft-bottom", true);
		ApplyIntroIdleBoxVisual();
		SetIntroOptionsVisible(false);
		if (PauseMenuDocument) PauseMenuDocument->Hide();
		if (GameOverDocument) GameOverDocument->Hide();
		if (HudDocument) HudDocument->Hide();
	}
#endif
}

void FGameUiSystem::SetHudVisible(bool bVisible)
{
	bHudVisible = bVisible;
#if WITH_RMLUI
	if (HudDocument)
	{
		bVisible ? HudDocument->Show() : HudDocument->Hide();
	}
#endif
}

void FGameUiSystem::SetPauseMenuVisible(bool bVisible)
{
	bPauseMenuVisible = bVisible;
	if (!bVisible)
	{
		bShowingOptions = false;
		bShowingIntroOptions = false;
		ClearDocumentFocus(PauseMenuDocument);
	}

#if WITH_RMLUI
	if (!PauseMenuDocument && !IntroDocument)
	{
		return;
	}

	if (bVisible)
	{
		PauseMenuDocument->Show();
		SetOptionsVisible(bShowingOptions);
	}
	else
	{
		PauseMenuDocument->Hide();
	}
#endif
}

void FGameUiSystem::SetGameOverVisible(bool bVisible)
{
	bGameOverVisible = bVisible;
	if (bVisible)
	{
		bIntroVisible = false;
		bPauseMenuVisible = false;
		bHudVisible = false;
		bShowingOptions = false;
		bShowingIntroOptions = false;
	}

#if WITH_RMLUI
	if (GameOverDocument)
	{
		bVisible ? GameOverDocument->Show() : GameOverDocument->Hide();
	}
	if (!bVisible)
	{
		ClearDocumentFocus(GameOverDocument);
	}
	if (bVisible)
	{
		if (IntroDocument) IntroDocument->Hide();
		if (PauseMenuDocument) PauseMenuDocument->Hide();
		if (HudDocument) HudDocument->Hide();
	}
#endif
}

void FGameUiSystem::SetScore(int32 Score)
{
	SetElementTextAny("score-value", std::to_string(std::max(0, Score)).c_str());
}

void FGameUiSystem::SetBestScore(int32 BestScore)
{
	const FString Text = std::to_string(std::max(0, BestScore));

	// HUD.rml uses best-score-value. Keep best-value as a compatibility fallback
	// for older UI documents.
	SetElementTextAny("best-score-value", Text.c_str());
	SetElementTextAny("best-value", Text.c_str());
	SetElementTextAny("final-best-value", Text.c_str());
}

void FGameUiSystem::SetCoins(int32 Coins)
{
	SetElementTextAny("coin-value", std::to_string(std::max(0, Coins)).c_str());
}

void FGameUiSystem::SetLane(int32 Lane)
{
	SetElementTextAny("lane-value", std::to_string(std::max(1, Lane)).c_str());
}

void FGameUiSystem::SetCombo(int32 Combo)
{
	const FString Text = "x" + std::to_string(std::max(1, Combo));
	SetElementTextAny("combo-value", Text.c_str());
}

void FGameUiSystem::SetStatusText(const FString& Text)
{
	SetElementTextAny("status-text", Text.c_str());
}

void FGameUiSystem::SetTopScoresText(const FString& Text)
{
	SetElementTextAny("top-scores-value", Text.c_str());
}

void FGameUiSystem::ShowGameOver(int32 FinalScore, int32 BestScore)
{
	const int32 SafeFinalScore = std::max(0, FinalScore);
	const int32 SafeBestScore = std::max(SafeFinalScore, BestScore);
	SetElementTextAny("final-score-value", std::to_string(SafeFinalScore).c_str());
	SetElementTextAny("final-best-value", std::to_string(SafeBestScore).c_str());
	SetGameOverVisible(true);
}

void FGameUiSystem::HideGameOver()
{
	SetGameOverVisible(false);
}

void FGameUiSystem::ResetRunUi()
{
	SetScore(0);
	SetCoins(0);
	SetLane(1);
	SetCombo(1);
	SetStatusText("READY");
	SetIntroOptionsVisible(false);
	SetIntroVisible(false);
	SetPauseMenuVisible(false);
	SetGameOverVisible(false);
	SetHudVisible(true);
	ClearInteractiveFocus();
}

bool FGameUiSystem::LoadDocuments()
{
#if WITH_RMLUI
	if (!Context)
	{
		return false;
	}

	IntroDocument = Context->LoadDocument("Asset/UI/Intro.rml");
	HudDocument = Context->LoadDocument("Asset/UI/HUD.rml");
	PauseMenuDocument = Context->LoadDocument("Asset/UI/PauseMenu.rml");
	GameOverDocument = Context->LoadDocument("Asset/UI/GameOver.rml");

	if (IntroDocument) IntroDocument->Show();
	if (HudDocument) HudDocument->Hide();
	if (PauseMenuDocument) PauseMenuDocument->Hide();
	if (GameOverDocument) GameOverDocument->Hide();

	bIntroVisible = IntroDocument != nullptr;
	bHudVisible = false;
	bPauseMenuVisible = false;
	bGameOverVisible = false;

	BindUiEvents();
	RefreshOptionLabels();
	ResetRunUi();
	SetIntroVisible(true);

	return IntroDocument != nullptr || HudDocument != nullptr || PauseMenuDocument != nullptr || GameOverDocument != nullptr;
#else
	return false;
#endif
}

void FGameUiSystem::BindUiEvents()
{
#if WITH_RMLUI
	if (!EventListener)
	{
		EventListener = std::make_unique<FGameUiEventListener>(this);
	}

	UnbindPauseMenuEvents();

	static const char* IntroIds[] = {
		"ui-start",
		"ui-settings",
		"ui-exit",
		"ui-intro-back",
		"ui-intro-toggle-fullscreen",
		"ui-intro-toggle-fxaa"
	};
	static const char* PauseIds[] = {
		"continue",
		"options",
		"restart",
		"exit",
		"back",
		"toggle-fullscreen",
		"toggle-fxaa"
	};
	static const char* GameOverIds[] = {
		"ui-restart",
		"ui-main-menu",
		"ui-gameover-exit"
	};

	BindDocumentClickEvents(IntroDocument, IntroIds, static_cast<int32>(sizeof(IntroIds) / sizeof(IntroIds[0])));
	BindDocumentClickEvents(PauseMenuDocument, PauseIds, static_cast<int32>(sizeof(PauseIds) / sizeof(PauseIds[0])));
	BindDocumentClickEvents(GameOverDocument, GameOverIds, static_cast<int32>(sizeof(GameOverIds) / sizeof(GameOverIds[0])));
#endif
}

void FGameUiSystem::BindDocumentClickEvents(Rml::ElementDocument* Document, const char* const* ElementIds, int32 ElementCount)
{
#if WITH_RMLUI
	if (!Document || !EventListener || !ElementIds)
	{
		return;
	}

	for (int32 Index = 0; Index < ElementCount; ++Index)
	{
		if (Rml::Element* Element = Document->GetElementById(ElementIds[Index]))
		{
			Element->AddEventListener("click", EventListener.get(), false);
		}
	}
#else
	(void)Document;
	(void)ElementIds;
	(void)ElementCount;
#endif
}

void FGameUiSystem::UnbindDocumentClickEvents(Rml::ElementDocument* Document, const char* const* ElementIds, int32 ElementCount)
{
#if WITH_RMLUI
	if (!Document || !EventListener || !ElementIds)
	{
		return;
	}

	for (int32 Index = 0; Index < ElementCount; ++Index)
	{
		if (Rml::Element* Element = Document->GetElementById(ElementIds[Index]))
		{
			Element->RemoveEventListener("click", EventListener.get(), false);
		}
	}
#else
	(void)Document;
	(void)ElementIds;
	(void)ElementCount;
#endif
}

void FGameUiSystem::HandleClick(const FString& ElementId)
{
	if (bStartTransitionActive)
	{
		return;
	}

	if (ElementId == "ui-start")
	{
		if (bStartEventQueuedOrDispatched)
		{
			UE_LOG("[GameUI] Duplicate ui-start ignored.");
			return;
		}

		BeginStartTransition();
		return;
	}
	if (ElementId == "ui-settings")
	{
		SetIntroOptionsVisible(true);
		return;
	}
	if (ElementId == "ui-intro-back")
	{
		SetIntroOptionsVisible(false);
		return;
	}
	if (ElementId == "ui-intro-toggle-fullscreen")
	{
		if (Callbacks.OnToggleFullscreen)
		{
			Callbacks.OnToggleFullscreen();
		}
		RefreshOptionLabels();
		return;
	}
	if (ElementId == "ui-intro-toggle-fxaa")
	{
		bool bNext = true;
		if (Callbacks.IsFxaaEnabled)
		{
			bNext = !Callbacks.IsFxaaEnabled();
		}
		if (Callbacks.OnFxaaChanged)
		{
			Callbacks.OnFxaaChanged(bNext);
		}
		RefreshOptionLabels();
		return;
	}
	if (ElementId == "ui-exit" || ElementId == "ui-gameover-exit")
	{
		QueueScriptEvent("exit");
		if (Callbacks.OnExit)
		{
			Callbacks.OnExit();
		}
		return;
	}
	if (ElementId == "continue")
	{
		QueueScriptEvent("continue");
		if (Callbacks.OnContinue)
		{
			Callbacks.OnContinue();
		}
		return;
	}
	if (ElementId == "options")
	{
		SetOptionsVisible(true);
		return;
	}
	if (ElementId == "restart" || ElementId == "ui-restart")
	{
		// 인게임 재시작은 PIE/GameClient 세션 재시작이 아니라
		// 현재 월드 안에서 런타임 맵과 플레이 상태만 다시 시작합니다.
		if (ElementId == "restart" && Callbacks.OnClosePauseMenu)
		{
			Callbacks.OnClosePauseMenu();
		}
		ResetRunUi();
		SetPauseMenuVisible(false);
		SetGameOverVisible(false);
		SetIntroVisible(false);
		SetHudVisible(true);

		QueueScriptEvent("restart");
		return;
	}
	if (ElementId == "exit")
	{
		QueueScriptEvent("exit");
		if (Callbacks.OnExit)
		{
			Callbacks.OnExit();
		}
		return;
	}
	if (ElementId == "ui-main-menu")
	{
		SetGameOverVisible(false);
		SetHudVisible(false);
		SetIntroVisible(true);
		QueueScriptEvent("main_menu");
		return;
	}
	if (ElementId == "back")
	{
		SetOptionsVisible(false);
		return;
	}
	if (ElementId == "toggle-fullscreen")
	{
		if (Callbacks.OnToggleFullscreen)
		{
			Callbacks.OnToggleFullscreen();
		}
		RefreshOptionLabels();
		return;
	}
	if (ElementId == "toggle-fxaa")
	{
		bool bNext = true;
		if (Callbacks.IsFxaaEnabled)
		{
			bNext = !Callbacks.IsFxaaEnabled();
		}
		if (Callbacks.OnFxaaChanged)
		{
			Callbacks.OnFxaaChanged(bNext);
		}
		RefreshOptionLabels();
		return;
	}
}

void FGameUiSystem::QueueScriptEvent(const FString& EventName)
{
	if (EventName.empty())
	{
		return;
	}

	for (const FString& PendingEvent : PendingScriptEvents)
	{
		if (PendingEvent == EventName)
		{
			UE_LOG("[GameUI] Duplicate pending script event ignored: %s", EventName.c_str());
			return;
		}
	}

	PendingScriptEvents.push_back(EventName);
}

void FGameUiSystem::FlushQueuedScriptEvents()
{
	if (bFlushingScriptEvents || PendingScriptEvents.empty())
	{
		return;
	}

	bFlushingScriptEvents = true;
	TArray<FString> Events = std::move(PendingScriptEvents);
	PendingScriptEvents.clear();

	for (const FString& EventName : Events)
	{
		DispatchScriptEvent(EventName);
	}

	bFlushingScriptEvents = false;
}

void FGameUiSystem::DispatchScriptEvent(const FString& EventName)
{
	if (ScriptEventHandler)
	{
		ScriptEventHandler(EventName);
	}
}

void FGameUiSystem::BeginStartTransition()
{
	if (bStartTransitionActive || bStartEventQueuedOrDispatched)
	{
		return;
	}

	bStartEventQueuedOrDispatched = true;
	bStartTransitionActive = true;
	bStartTransitionResetDispatched = false;
	StartTransitionTime = 0.0f;

	SetIntroOptionsVisible(false);
	SetElementDisplay(IntroDocument, "intro-panel", true);
	SetElementDisplay(IntroDocument, "intro-soft-top", true);
	SetElementDisplay(IntroDocument, "intro-soft-bottom", true);
	ApplyIntroIdleBoxVisual();
}

void FGameUiSystem::UpdateStartTransition(float DeltaTime)
{
	if (!bStartTransitionActive)
	{
		return;
	}

	StartTransitionTime += std::max(0.0f, DeltaTime);

	float Height = PresentationRect.Height;
	if (Height <= 1.0f && OwnerWindow)
	{
		Height = static_cast<float>(OwnerWindow->GetHeight());
	}
	if (Height <= 1.0f)
	{
		Height = 720.0f;
	}

	const float BaseTop = std::max(96.0f, std::min(180.0f, Height * 0.22f));
	const float BaseBottom = std::max(112.0f, std::min(220.0f, Height * 0.27f));
	const float OpenTop = std::max(52.0f, BaseTop - 58.0f);
	const float OpenBottom = std::max(58.0f, BaseBottom - 66.0f);
	const float CoveredTop = Height * 0.54f;
	const float CoveredBottom = Height * 0.54f;

	// 멈춤 구간 없이 "가운데가 넓어짐 -> 바로 닫힘"으로 이어지는 타이밍.
	const float GapWideTime = 0.24f;
	const float CoveredTime = 0.88f;
	const float FinishTime = 1.64f;

	float TopHeight = BaseTop;
	float BottomHeight = BaseBottom;
	int32 Alpha = 72;

	if (StartTransitionTime < GapWideTime)
	{
		// 원래 위치에서 중앙 틈이 한 번 넓어지는 구간. 정지 없이 다음 닫힘으로 이어진다.
		const float T = EaseInOutCubic(StartTransitionTime / GapWideTime);
		TopHeight = LerpFloat(BaseTop, OpenTop, T);
		BottomHeight = LerpFloat(BaseBottom, OpenBottom, T);
		Alpha = static_cast<int32>(LerpFloat(72.0f, 92.0f, T));
	}
	else if (StartTransitionTime < CoveredTime)
	{
		// 넓어진 순간 곧바로 닫힌다. 중간에 base 위치에서 멈추거나 눌리는 구간을 두지 않는다.
		const float T = EaseInOutCubic((StartTransitionTime - GapWideTime) / (CoveredTime - GapWideTime));
		TopHeight = LerpFloat(OpenTop, CoveredTop, T);
		BottomHeight = LerpFloat(OpenBottom, CoveredBottom, T);
		Alpha = static_cast<int32>(LerpFloat(92.0f, 240.0f, T));
	}
	else
	{
		if (!bStartTransitionResetDispatched)
		{
			bStartTransitionResetDispatched = true;

			SetScore(0);
			SetCoins(0);
			SetLane(1);
			SetCombo(1);
			SetStatusText("READY");
			SetIntroOptionsVisible(false);
			SetElementDisplay(IntroDocument, "intro-panel", false);
			SetElementDisplay(IntroDocument, "intro-soft-top", true);
			SetElementDisplay(IntroDocument, "intro-soft-bottom", true);
			SetGameOverVisible(false);
			QueueScriptEvent("start");
		}

		// 초기화 후에는 카메라 전환이 보이도록 바로 열린다.
		const float T = EaseInOutCubic(std::min(1.0f, (StartTransitionTime - CoveredTime) / (FinishTime - CoveredTime)));
		TopHeight = LerpFloat(CoveredTop, 0.0f, T);
		BottomHeight = LerpFloat(CoveredBottom, 0.0f, T);
		Alpha = static_cast<int32>(LerpFloat(240.0f, 0.0f, T));
	}

	ApplyStartTransitionVisual(TopHeight, BottomHeight, Alpha);

	if (StartTransitionTime >= FinishTime)
	{
		CompleteStartTransition();
	}
}

void FGameUiSystem::CompleteStartTransition()
{
	bStartTransitionActive = false;
	bStartTransitionResetDispatched = false;
	StartTransitionTime = 0.0f;
	ApplyStartTransitionVisual(0.0f, 0.0f, 0);
	SetElementDisplay(IntroDocument, "intro-soft-top", false);
	SetElementDisplay(IntroDocument, "intro-soft-bottom", false);
	SetHudVisible(true);

#if WITH_RMLUI
	if (IntroDocument)
	{
		IntroDocument->Hide();
	}
#endif
	bIntroVisible = false;
	ClearDocumentFocus(IntroDocument);
}

void FGameUiSystem::ApplyStartTransitionVisual(float TopHeightPx, float BottomHeightPx, int32 Alpha)
{
	const int32 SafeTopHeight = static_cast<int32>(std::max(0.0f, TopHeightPx));
	const int32 SafeBottomHeight = static_cast<int32>(std::max(0.0f, BottomHeightPx));
	const int32 SafeAlpha = std::max(0, std::min(255, Alpha));
	const FString TopHeightText = std::to_string(SafeTopHeight) + "px";
	const FString BottomHeightText = std::to_string(SafeBottomHeight) + "px";
	const FString ColorText = "rgba(2, 4, 7, " + std::to_string(SafeAlpha) + ")";

	SetElementProperty(IntroDocument, "intro-soft-top", "height", TopHeightText.c_str());
	SetElementProperty(IntroDocument, "intro-soft-bottom", "height", BottomHeightText.c_str());
	SetElementProperty(IntroDocument, "intro-soft-top", "background-color", ColorText.c_str());
	SetElementProperty(IntroDocument, "intro-soft-bottom", "background-color", ColorText.c_str());
}

void FGameUiSystem::ApplyIntroIdleBoxVisual()
{
	float Height = PresentationRect.Height;
	if (Height <= 1.0f && OwnerWindow)
	{
		Height = static_cast<float>(OwnerWindow->GetHeight());
	}
	if (Height <= 1.0f)
	{
		Height = 720.0f;
	}

	const float BaseTop = std::max(96.0f, std::min(180.0f, Height * 0.22f));
	const float BaseBottom = std::max(112.0f, std::min(220.0f, Height * 0.27f));
	ApplyStartTransitionVisual(BaseTop, BaseBottom, 72);
}

float FGameUiSystem::EaseInOutCubic(float T) const
{
	T = std::max(0.0f, std::min(1.0f, T));
	if (T < 0.5f)
	{
		return 4.0f * T * T * T;
	}
	const float K = -2.0f * T + 2.0f;
	return 1.0f - (K * K * K) * 0.5f;
}

float FGameUiSystem::LerpFloat(float A, float B, float T) const
{
	return A + (B - A) * std::max(0.0f, std::min(1.0f, T));
}

void FGameUiSystem::SetOptionsVisible(bool bVisible)
{
	bShowingOptions = bVisible;
	SetElementDisplay(PauseMenuDocument, "main-panel", !bShowingOptions);
	SetElementDisplay(PauseMenuDocument, "options-panel", bShowingOptions);
	RefreshOptionLabels();
}

void FGameUiSystem::SetIntroOptionsVisible(bool bVisible)
{
	bShowingIntroOptions = bVisible;
	SetElementDisplay(IntroDocument, "intro-main-panel", !bShowingIntroOptions);
	SetElementDisplay(IntroDocument, "intro-options-panel", bShowingIntroOptions);
	RefreshOptionLabels();
}

void FGameUiSystem::RefreshOptionLabels()
{
#if WITH_RMLUI
	if (!PauseMenuDocument && !IntroDocument)
	{
		return;
	}

	const bool bFullscreen = Callbacks.IsFullscreen ? Callbacks.IsFullscreen() : false;
	const char* FullscreenText = bFullscreen ? "전체화면 해제" : "전체화면";
	SetElementText(PauseMenuDocument, "toggle-fullscreen", FullscreenText);
	SetElementText(IntroDocument, "ui-intro-toggle-fullscreen", FullscreenText);

	const bool bFxaa = Callbacks.IsFxaaEnabled ? Callbacks.IsFxaaEnabled() : false;
	const char* FxaaText = bFxaa ? "FXAA: 켜짐" : "FXAA: 꺼짐";
	SetElementText(PauseMenuDocument, "toggle-fxaa", FxaaText);
	SetElementText(IntroDocument, "ui-intro-toggle-fxaa", FxaaText);
#endif
}

void FGameUiSystem::SetElementProperty(Rml::ElementDocument* Document, const char* ElementId, const char* PropertyName, const char* Value)
{
#if WITH_RMLUI
	if (!Document || !ElementId || !PropertyName || !Value)
	{
		return;
	}
	if (Rml::Element* Element = Document->GetElementById(ElementId))
	{
		Element->SetProperty(PropertyName, Value);
	}
#else
	(void)Document;
	(void)ElementId;
	(void)PropertyName;
	(void)Value;
#endif
}

void FGameUiSystem::SetElementDisplay(Rml::ElementDocument* Document, const char* ElementId, bool bVisible)
{
#if WITH_RMLUI
	if (!Document || !ElementId)
	{
		return;
	}
	if (Rml::Element* Element = Document->GetElementById(ElementId))
	{
		Element->SetProperty("display", bVisible ? "block" : "none");
	}
#else
	(void)Document;
	(void)ElementId;
	(void)bVisible;
#endif
}

void FGameUiSystem::SetElementText(Rml::ElementDocument* Document, const char* ElementId, const char* Text)
{
#if WITH_RMLUI
	if (!Document || !ElementId || !Text)
	{
		return;
	}
	if (Rml::Element* Element = Document->GetElementById(ElementId))
	{
		Element->SetInnerRML(Text);
	}
#else
	(void)Document;
	(void)ElementId;
	(void)Text;
#endif
}

void FGameUiSystem::SetElementTextAny(const char* ElementId, const char* Text)
{
#if WITH_RMLUI
	SetElementText(IntroDocument, ElementId, Text);
	SetElementText(HudDocument, ElementId, Text);
	SetElementText(PauseMenuDocument, ElementId, Text);
	SetElementText(GameOverDocument, ElementId, Text);
#else
	(void)ElementId;
	(void)Text;
#endif
}

bool FGameUiSystem::IsInteractiveUiVisible() const
{
	return bStartTransitionActive || bIntroVisible || bPauseMenuVisible || bGameOverVisible;
}

bool FGameUiSystem::ShouldCaptureKeyboard() const
{
	return bIntroVisible || bPauseMenuVisible || bGameOverVisible;
}

void FGameUiSystem::ClearDocumentFocus(Rml::ElementDocument* Document)
{
#if WITH_RMLUI
	if (!Context || !Document)
	{
		return;
	}

	if (Rml::Element* FocusElement = Context->GetFocusElement())
	{
		FocusElement->Blur();
	}
	Context->UnfocusDocument(Document);
#else
	(void)Document;
#endif
}

void FGameUiSystem::ClearInteractiveFocus()
{
#if WITH_RMLUI
	ClearDocumentFocus(IntroDocument);
	ClearDocumentFocus(PauseMenuDocument);
	ClearDocumentFocus(GameOverDocument);
#endif
}

bool FGameUiSystem::ProcessWin32Message(void* hWnd, uint32 Msg, std::uintptr_t wParam, std::intptr_t lParam)
{
	if (!bInitialized || !bAvailable)
	{
		return false;
	}

	HWND NativeHWnd = static_cast<HWND>(hWnd);
	WPARAM NativeWParam = static_cast<WPARAM>(wParam);
	LPARAM NativeLParam = static_cast<LPARAM>(lParam);

	switch (Msg)
	{
	case WM_MOUSEMOVE:
		return ProcessMouseMove(static_cast<float>(GetSignedMouseX(NativeLParam)), static_cast<float>(GetSignedMouseY(NativeLParam)));
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
		return ProcessMouseButtonDown(ToRmlMouseButton(Msg), static_cast<float>(GetSignedMouseX(NativeLParam)), static_cast<float>(GetSignedMouseY(NativeLParam)));
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
		return ProcessMouseButtonUp(ToRmlMouseButton(Msg), static_cast<float>(GetSignedMouseX(NativeLParam)), static_cast<float>(GetSignedMouseY(NativeLParam)));
	case WM_MOUSEWHEEL:
	{
		POINT Point = { GetSignedMouseX(NativeLParam), GetSignedMouseY(NativeLParam) };
		if (NativeHWnd)
		{
			::ScreenToClient(NativeHWnd, &Point);
		}
		const float WheelDelta = static_cast<float>(GetSignedWheelDelta(NativeWParam)) / static_cast<float>(WHEEL_DELTA);
		return ProcessMouseWheel(-WheelDelta, static_cast<float>(Point.x), static_cast<float>(Point.y));
	}
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		return ProcessKeyDown(static_cast<int>(NativeWParam));
	case WM_KEYUP:
	case WM_SYSKEYUP:
		return ProcessKeyUp(static_cast<int>(NativeWParam));
	case WM_CHAR:
		return ProcessTextInput(static_cast<uint32>(NativeWParam));
	default:
		break;
	}

	return false;
}

bool FGameUiSystem::ProcessMouseMove(float ScreenX, float ScreenY)
{
#if WITH_RMLUI
	if (!Context)
	{
		return false;
	}

	float LocalX = 0.0f;
	float LocalY = 0.0f;
	if (!PresentationRect.ScreenToLocal(ScreenX, ScreenY, LocalX, LocalY))
	{
		Context->ProcessMouseLeave();
		return false;
	}

	const bool bNotConsumed = Context->ProcessMouseMove(static_cast<int>(LocalX), static_cast<int>(LocalY), GetRmlModifierState());
	return !bNotConsumed || WantsMouse();
#else
	(void)ScreenX;
	(void)ScreenY;
	return false;
#endif
}

bool FGameUiSystem::ProcessMouseButtonDown(int Button, float ScreenX, float ScreenY)
{
#if WITH_RMLUI
	if (!Context || !IsInteractiveUiVisible())
	{
		return false;
	}

	float LocalX = 0.0f;
	float LocalY = 0.0f;
	if (!PresentationRect.ScreenToLocal(ScreenX, ScreenY, LocalX, LocalY))
	{
		return false;
	}

	Context->ProcessMouseMove(static_cast<int>(LocalX), static_cast<int>(LocalY), GetRmlModifierState());
	const bool bNotConsumed = Context->ProcessMouseButtonDown(Button, GetRmlModifierState());
	return !bNotConsumed || WantsMouse();
#else
	(void)Button;
	(void)ScreenX;
	(void)ScreenY;
	return false;
#endif
}

bool FGameUiSystem::ProcessMouseButtonUp(int Button, float ScreenX, float ScreenY)
{
#if WITH_RMLUI
	if (!Context || !IsInteractiveUiVisible())
	{
		return false;
	}

	float LocalX = 0.0f;
	float LocalY = 0.0f;
	if (PresentationRect.ScreenToLocal(ScreenX, ScreenY, LocalX, LocalY))
	{
		Context->ProcessMouseMove(static_cast<int>(LocalX), static_cast<int>(LocalY), GetRmlModifierState());
	}
	const bool bNotConsumed = Context->ProcessMouseButtonUp(Button, GetRmlModifierState());
	return !bNotConsumed || WantsMouse();
#else
	(void)Button;
	(void)ScreenX;
	(void)ScreenY;
	return false;
#endif
}

bool FGameUiSystem::ProcessMouseWheel(float WheelDelta, float ScreenX, float ScreenY)
{
#if WITH_RMLUI
	if (!Context || !IsInteractiveUiVisible())
	{
		return false;
	}

	float LocalX = 0.0f;
	float LocalY = 0.0f;
	if (!PresentationRect.ScreenToLocal(ScreenX, ScreenY, LocalX, LocalY))
	{
		return false;
	}

	Context->ProcessMouseMove(static_cast<int>(LocalX), static_cast<int>(LocalY), GetRmlModifierState());
	const bool bNotConsumed = Context->ProcessMouseWheel(Rml::Vector2f(0.0f, WheelDelta), GetRmlModifierState());
	return !bNotConsumed || WantsMouse();
#else
	(void)WheelDelta;
	(void)ScreenX;
	(void)ScreenY;
	return false;
#endif
}

bool FGameUiSystem::ProcessKeyDown(int VirtualKey)
{
#if WITH_RMLUI
	if (!Context || !IsInteractiveUiVisible())
	{
		return false;
	}

	const Rml::Input::KeyIdentifier Key = ToRmlKey(VirtualKey);
	if (Key == Rml::Input::KI_UNKNOWN)
	{
		return false;
	}
	const bool bNotConsumed = Context->ProcessKeyDown(Key, GetRmlModifierState());
	return !bNotConsumed || WantsKeyboard();
#else
	(void)VirtualKey;
	return false;
#endif
}

bool FGameUiSystem::ProcessKeyUp(int VirtualKey)
{
#if WITH_RMLUI
	if (!Context || !IsInteractiveUiVisible())
	{
		return false;
	}

	const Rml::Input::KeyIdentifier Key = ToRmlKey(VirtualKey);
	if (Key == Rml::Input::KI_UNKNOWN)
	{
		return false;
	}
	const bool bNotConsumed = Context->ProcessKeyUp(Key, GetRmlModifierState());
	return !bNotConsumed || WantsKeyboard();
#else
	(void)VirtualKey;
	return false;
#endif
}

bool FGameUiSystem::ProcessTextInput(uint32 Codepoint)
{
#if WITH_RMLUI
	if (!Context || !IsInteractiveUiVisible() || Codepoint < 32)
	{
		return false;
	}

	const bool bNotConsumed = Context->ProcessTextInput(static_cast<Rml::Character>(Codepoint));
	return !bNotConsumed || WantsKeyboard();
#else
	(void)Codepoint;
	return false;
#endif
}

bool FGameUiSystem::WantsMouse() const
{
#if WITH_RMLUI
	return Context && IsInteractiveUiVisible();
#else
	return false;
#endif
}

bool FGameUiSystem::WantsKeyboard() const
{
#if WITH_RMLUI
	return Context && ShouldCaptureKeyboard();
#else
	return false;
#endif
}

void FGameUiSystem::UnbindPauseMenuEvents()
{
#if WITH_RMLUI
	if (!EventListener)
	{
		return;
	}

	static const char* IntroIds[] = {
		"ui-start",
		"ui-settings",
		"ui-exit",
		"ui-intro-back",
		"ui-intro-toggle-fullscreen",
		"ui-intro-toggle-fxaa"
	};
	static const char* PauseIds[] = {
		"continue",
		"options",
		"restart",
		"exit",
		"back",
		"toggle-fullscreen",
		"toggle-fxaa"
	};
	static const char* GameOverIds[] = {
		"ui-restart",
		"ui-main-menu",
		"ui-gameover-exit"
	};

	UnbindDocumentClickEvents(IntroDocument, IntroIds, static_cast<int32>(sizeof(IntroIds) / sizeof(IntroIds[0])));
	UnbindDocumentClickEvents(PauseMenuDocument, PauseIds, static_cast<int32>(sizeof(PauseIds) / sizeof(PauseIds[0])));
	UnbindDocumentClickEvents(GameOverDocument, GameOverIds, static_cast<int32>(sizeof(GameOverIds) / sizeof(GameOverIds[0])));
#endif
}
