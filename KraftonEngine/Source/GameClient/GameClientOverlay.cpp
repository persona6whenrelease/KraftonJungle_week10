#include "GameClient/GameClientOverlay.h"

#include "GameClient/GameClientEngine.h"
#include "GameClient/GameClientViewport.h"
#include "GameClient/GameCameraManager.h"
#include "Component/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "GameFramework/World.h"
#include "Render/Pipeline/Renderer.h"
#include "Object/Object.h"
#include "SimpleJSON/json.hpp"
#include "Viewport/Viewport.h"
#include "Viewport/GameViewportClient.h"
#include "Viewport/ViewportPresentationTypes.h"
#include "Engine/UI/ImGui/ImGuiViewportPresenter.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
	constexpr const char* ContinueLabel = "\xEA\xB3\x84\xEC\x86\x8D";
	constexpr const char* OptionsLabel = "\xEC\x98\xB5\xEC\x85\x98";
	constexpr const char* RestartLabel = "\xEB\x8B\xA4\xEC\x8B\x9C\xEC\x8B\x9C\xEC\x9E\x91";
	constexpr const char* ExitLabel = "\xEC\xA2\x85\xEB\xA3\x8C";
	constexpr const char* BackLabel = "\xEB\x92\xA4\xEB\xA1\x9C";

	FString GetImGuiStyleSettingsPath()
	{
		return FPaths::ToUtf8(FPaths::SettingsDir() + L"ImGuiStyle.ini");
	}

	void ApplyFallbackStyle()
	{
		ImGui::StyleColorsDark();

		ImGuiStyle& Style = ImGui::GetStyle();
		Style.WindowRounding = 4.0f;
		Style.FrameRounding = 3.0f;
		Style.GrabRounding = 3.0f;
		Style.ScrollbarRounding = 3.0f;
		Style.WindowBorderSize = 1.0f;
		Style.FrameBorderSize = 0.0f;
		Style.WindowPadding = ImVec2(10.0f, 8.0f);
		Style.ItemSpacing = ImVec2(8.0f, 8.0f);
	}

	void LoadEditorImGuiStyle()
	{
		ApplyFallbackStyle();

		const FString Path = GetImGuiStyleSettingsPath();
		const std::filesystem::path FilePath(FPaths::ToWide(Path));
		std::ifstream File(FilePath);
		if (!File.is_open())
		{
			return;
		}

		const FString Content(
			(std::istreambuf_iterator<char>(File)),
			std::istreambuf_iterator<char>());

		json::JSON Root = json::JSON::Load(Content);
		if (Root.IsNull())
		{
			return;
		}

		ImGuiStyle& Style = ImGui::GetStyle();

		if (Root.hasKey("Colors"))
		{
			json::JSON Colors = Root["Colors"];
			int32 Count = static_cast<int32>(Colors.length());
			if (Count > ImGuiCol_COUNT)
			{
				Count = ImGuiCol_COUNT;
			}

			for (int32 Index = 0; Index < Count; ++Index)
			{
				json::JSON Color = Colors[Index];
				Style.Colors[Index] = ImVec4(
					static_cast<float>(Color[0].ToFloat()),
					static_cast<float>(Color[1].ToFloat()),
					static_cast<float>(Color[2].ToFloat()),
					static_cast<float>(Color[3].ToFloat()));
			}
		}

		if (Root.hasKey("Alpha")) Style.Alpha = static_cast<float>(Root["Alpha"].ToFloat());
		if (Root.hasKey("WindowRounding")) Style.WindowRounding = static_cast<float>(Root["WindowRounding"].ToFloat());
		if (Root.hasKey("FrameRounding")) Style.FrameRounding = static_cast<float>(Root["FrameRounding"].ToFloat());
		if (Root.hasKey("GrabRounding")) Style.GrabRounding = static_cast<float>(Root["GrabRounding"].ToFloat());
		if (Root.hasKey("ScrollbarRounding")) Style.ScrollbarRounding = static_cast<float>(Root["ScrollbarRounding"].ToFloat());
		if (Root.hasKey("WindowBorderSize")) Style.WindowBorderSize = static_cast<float>(Root["WindowBorderSize"].ToFloat());
		if (Root.hasKey("FrameBorderSize")) Style.FrameBorderSize = static_cast<float>(Root["FrameBorderSize"].ToFloat());
	}

	bool DrawMenuButton(const char* Label, float Width, float Height)
	{
		ImGui::SetCursorPosX((ImGui::GetWindowSize().x - Width) * 0.5f);
		return ImGui::Button(Label, ImVec2(Width, Height));
	}
}

bool FGameClientOverlay::Initialize(
	FWindowsWindow* Window,
	FRenderer& Renderer,
	UGameClientEngine* InEngine)
{
	if (!Window)
	{
		return false;
	}

	Engine = InEngine;
	this->Window = Window;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	LoadEditorImGuiStyle();

	ImGuiIO& IO = ImGui::GetIO();
	IO.IniFilename = "Settings/imgui_gameclient.ini";
	IO.Fonts->AddFontFromFileTTF(
		"C:\\Windows\\Fonts\\malgun.ttf",
		16.0f,
		nullptr,
		IO.Fonts->GetGlyphRangesKorean());

	ImGui_ImplWin32_Init(reinterpret_cast<void*>(Window->GetHWND()));
	ImGui_ImplDX11_Init(
		Renderer.GetFD3DDevice().GetDevice(),
		Renderer.GetFD3DDevice().GetDeviceContext());

	bUseD3D11ViewportPresenter = D3D11ViewportPresenter.Initialize(
		Renderer.GetFD3DDevice().GetDevice(),
		Renderer.GetFD3DDevice().GetDeviceContext());

	bInitialized = true;
	return true;
}

void FGameClientOverlay::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	D3D11ViewportPresenter.Release();
	bUseD3D11ViewportPresenter = false;

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	bInitialized = false;
	Engine = nullptr;
}

void FGameClientOverlay::Update(float DeltaTime)
{
	(void)DeltaTime;

	if (!bInitialized)
	{
		return;
	}

	ImGuiIO& IO = ImGui::GetIO();
	const bool bMenuOpen = Engine && Engine->IsPauseMenuOpen();
	if (!bMenuOpen)
	{
		bShowingOptions = false;
	}

	bool bGameUiWantsMouse = false;
	bool bGameUiWantsKeyboard = false;
	if (Engine)
	{
		if (UGameViewportClient* ViewportClient = Engine->GetGameViewport().GetViewportClient())
		{
			if (IViewportUiLayer* UiLayer = ViewportClient->GetUiLayer())
			{
				UiLayer->SetLayerVisible("PauseMenu", bMenuOpen);
				bGameUiWantsMouse = UiLayer->WantsMouse();
				bGameUiWantsKeyboard = UiLayer->WantsKeyboard();
			}
		}
	}

	const bool bImGuiWantsMouse = bMenuOpen && IO.WantCaptureMouse;
	const bool bImGuiWantsKeyboard = bMenuOpen && IO.WantCaptureKeyboard;
	const bool bImGuiWantsTextInput = bMenuOpen && IO.WantTextInput;
	InputSystem::Get().SetGuiMouseCapture(bMenuOpen || bGameUiWantsMouse || bImGuiWantsMouse);
	InputSystem::Get().SetGuiKeyboardCapture(bMenuOpen || bGameUiWantsKeyboard || bImGuiWantsKeyboard);
	InputSystem::Get().SetGuiTextInputCapture(bGameUiWantsKeyboard || bImGuiWantsTextInput);
}

void FGameClientOverlay::Render(float DeltaTime)
{
	if (!bInitialized)
	{
		return;
	}

	bool bGameUiAvailable = false;
	auto RenderGameUi = [&]()
	{
		if (!Engine)
		{
			return;
		}
		UGameViewportClient* ViewportClient = Engine->GetGameViewport().GetViewportClient();
		if (!ViewportClient)
		{
			return;
		}
		if (IViewportUiLayer* UiLayer = ViewportClient->GetUiLayer())
		{
			UiLayer->SetLayerVisible("PauseMenu", Engine->IsPauseMenuOpen());
			UiLayer->Update(DeltaTime);
			UiLayer->Render();
			bGameUiAvailable = UiLayer->IsAvailable();
		}
	};

	if (bUseD3D11ViewportPresenter)
	{
		DrawViewport();
		RenderGameUi();
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	if (!bUseD3D11ViewportPresenter)
	{
		DrawViewport();
	}

	DrawOverlay(DeltaTime);
	if (!bGameUiAvailable)
	{
		DrawPauseMenu();
	}

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	if (!bUseD3D11ViewportPresenter)
	{
		RenderGameUi();
	}
}

void FGameClientOverlay::DrawViewport()
{
	if (!Engine)
	{
		return;
	}

	FViewport* Viewport = Engine->GetGameViewport().GetViewport();
	if (!Viewport || !Viewport->GetSRV())
	{
		return;
	}

	const FViewportPresentationRect PresentationRect(
		0.0f,
		0.0f,
		Window ? static_cast<float>(Window->GetWidth()) : static_cast<float>(Viewport->GetWidth()),
		Window ? static_cast<float>(Window->GetHeight()) : static_cast<float>(Viewport->GetHeight()));

	if (UGameViewportClient* ViewportClient = Engine->GetGameViewport().GetViewportClient())
	{
		ViewportClient->SetPresentationRect(PresentationRect);
		ViewportClient->SetCursorClipRect(PresentationRect);
	}

	if (bUseD3D11ViewportPresenter)
	{
		D3D11ViewportPresenter.SetRenderTargetSize(PresentationRect.Width, PresentationRect.Height);
		D3D11ViewportPresenter.DrawViewport(Viewport, PresentationRect);
		return;
	}

	FImGuiViewportPresenter::DrawInMainBackground(Viewport, PresentationRect);
}

void FGameClientOverlay::DrawOverlay(float DeltaTime)
{
	if (!Engine || !Engine->GetSettings().bEnableOverlay)
	{
		return;
	}

	ImGui::SetNextWindowBgAlpha(0.65f);
	ImGui::Begin(
		"GameClient",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);

	ImGui::Text("Krafton GameClient");
	ImGui::Text("DeltaTime: %.4f", DeltaTime);

	if (UWorld* World = Engine->GetWorld())
	{
		ImGui::Text("World: Game");
		ImGui::Text("Actors: %d", static_cast<int>(World->GetActors().size()));

		FGameCameraManager& CameraManager = Engine->GetCameraManager();
		APlayerController* Controller = CameraManager.GetPlayerController();
		if (!IsAliveObject(Controller) || !World->IsActorInWorld(Controller))
		{
			Controller = World->GetPlayerController(0);
		}
		AActor* Pawn = Controller ? Controller->GetPossessedActor() : nullptr;
		UCameraComponent* ActiveCamera = World->GetActiveCamera();
		UCameraComponent* ViewCamera = World->GetViewCamera();
		UCameraComponent* DebugCamera = CameraManager.GetDebugCamera();
		if (!IsAliveObject(DebugCamera))
		{
			DebugCamera = nullptr;
		}


		ImGui::Separator();
		ImGui::Text("Controller: %s", Controller ? Controller->GetFName().ToString().c_str() : "(none)");
		ImGui::Text("Pawn: %s", Pawn ? Pawn->GetFName().ToString().c_str() : "(none)");
		ImGui::Text("ActiveCamera: %s", ActiveCamera ? ActiveCamera->GetFName().ToString().c_str() : "(none)");
		ImGui::Text("ViewCamera: %s", ViewCamera ? ViewCamera->GetFName().ToString().c_str() : "(none)");
		ImGui::Text("DebugCamera: %s", DebugCamera ? DebugCamera->GetFName().ToString().c_str() : "(none)");
	}

	ImGui::End();
}

void FGameClientOverlay::DrawPauseMenu()
{
	if (!Engine || !Engine->IsPauseMenuOpen())
	{
		return;
	}

	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	const ImVec2 ViewportMin = MainViewport->Pos;
	const ImVec2 ViewportMax(
		MainViewport->Pos.x + MainViewport->Size.x,
		MainViewport->Pos.y + MainViewport->Size.y);
	ImGui::GetBackgroundDrawList(MainViewport)->AddRectFilled(
		ViewportMin,
		ViewportMax,
		IM_COL32(0, 0, 0, 145));

	const ImVec2 MenuSize = bShowingOptions ? ImVec2(320.0f, 216.0f) : ImVec2(320.0f, 292.0f);
	const ImVec2 MenuCenter(
		MainViewport->Pos.x + MainViewport->Size.x * 0.5f,
		MainViewport->Pos.y + MainViewport->Size.y * 0.5f);

	ImGui::SetNextWindowPos(MenuCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(MenuSize, ImGuiCond_Always);
	ImGui::SetNextWindowFocus();

	const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoCollapse;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 22.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 12.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.11f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.25f, 0.30f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.23f, 0.30f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.50f, 0.80f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.42f, 0.72f, 1.0f));

	if (ImGui::Begin("##GameClientPauseMenu", nullptr, Flags))
	{
		if (bShowingOptions)
		{
			DrawPauseOptionsMenu();
		}
		else
		{
			DrawPauseMainMenu();
		}
	}
	ImGui::End();

	ImGui::PopStyleColor(5);
	ImGui::PopStyleVar(5);
}

void FGameClientOverlay::DrawPauseMainMenu()
{
	const float ButtonWidth = 220.0f;
	const float ButtonHeight = 38.0f;
	const char* Title = "MENU";
	const float TitleWidth = ImGui::CalcTextSize(Title).x;

	ImGui::SetCursorPosX((ImGui::GetWindowSize().x - TitleWidth) * 0.5f);
	ImGui::TextUnformatted(Title);
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0.0f, 6.0f));

	if (DrawMenuButton(ContinueLabel, ButtonWidth, ButtonHeight))
	{
		Engine->SetPauseMenuOpen(false);
	}
	if (DrawMenuButton(OptionsLabel, ButtonWidth, ButtonHeight))
	{
		bShowingOptions = true;
	}
	if (DrawMenuButton(RestartLabel, ButtonWidth, ButtonHeight))
	{
		Engine->RequestRestart();
	}
	if (DrawMenuButton(ExitLabel, ButtonWidth, ButtonHeight))
	{
		Engine->RequestExit();
	}
}

void FGameClientOverlay::DrawPauseOptionsMenu()
{
	FViewportRenderOptions& Options = Engine->GetSettings().RenderOptions;
	const char* Title = "GRAPHICS";
	const float TitleWidth = ImGui::CalcTextSize(Title).x;
	ImGui::SetCursorPosX((ImGui::GetWindowSize().x - TitleWidth) * 0.5f);
	ImGui::TextUnformatted(Title);
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0.0f, 4.0f));

	ImGui::Checkbox("FXAA", &Options.ShowFlags.bFXAA);

	const bool bFullscreen = Window && Window->IsFullscreen();
	const char* FullscreenLabel = bFullscreen ? "전체화면 해제" : "전체화면";
	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	if (DrawMenuButton(FullscreenLabel, 220.0f, 38.0f))
	{
		if (Window)
		{
			Window->ToggleFullscreen();
		}
	}

	ImGui::Dummy(ImVec2(0.0f, 8.0f));
	if (DrawMenuButton(BackLabel, 180.0f, 34.0f))
	{
		bShowingOptions = false;
	}
}
