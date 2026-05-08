#pragma once

#include "Editor/UI/EditorConsoleWidget.h"
#include "Editor/UI/EditorControlWidget.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/UI/EditorPropertyWidget.h"
#include "Editor/UI/EditorSceneWidget.h"
#include "Editor/UI/EditorStatWidget.h"
#include "Editor/UI/EditorShadowMapDebugWidget.h"
#include "Editor/UI/EditorProjectSettingsWidget.h"
#include "Editor/UI/EditorCurveWidget.h"
#include "Editor/Packaging/EditorPackageSettings.h"
#include "Editor/UI/ContentBrowser/ContentBrowser.h"
#include "Math/Vector.h"

#include <mutex>
#include <thread>

class AActor;
class FRenderer;
class UEditorEngine;
class FWindowsWindow;

class FEditorMainPanel
{
public:
	void Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine);
	void Release();
	void Render(float DeltaTime);
	void Update();
	void SaveToSettings() const;
	void HideEditorWindows();
	void ShowEditorWindows();
	void SetShowEditorOnlyComponents(bool bEnable) { PropertyWidget.SetShowEditorOnlyComponents(bEnable); }
	bool IsShowingEditorOnlyComponents() const { return PropertyWidget.IsShowingEditorOnlyComponents(); }
	void HideEditorWindowsForPIE();
	void RestoreEditorWindowsAfterPIE();
	void RefreshContentBrowser() { ContentBrowserWidget.Refresh(); }
	void SetContentBrowserIconSize(float Size) { ContentBrowserWidget.SetIconSize(Size); }
	float GetContentBrowserIconSize() const { return ContentBrowserWidget.GetIconSize(); }
	bool OpenCurveAsset(const FString& CurvePath);

private:
	void RenderMainMenuBar();
	void RenderShortcutOverlay();
	void RenderEditorDebugPanel();
	void RenderPackageSettingsWindow();
	void RenderConsoleDrawer(float DeltaTime);
	void RenderFooterOverlay(float DeltaTime);
	void HandleGlobalShortcuts();
	void ToggleConsoleDrawer(bool bFocusInput);
	void ProcessPendingDebugActions();
	void OpenPackageSettingsWindow();
	void BuildGamePackageFromSettings();
	void ConsumePackageBuildCompletion();
	void RefreshPackageGameDefinitions();
	void ApplySelectedPackageGame(int32 GameIndex);
	void CopyPackageSettingsToTextBuffers();
	void CopyTextBuffersToPackageSettings();

	FWindowsWindow* Window = nullptr;
	UEditorEngine* EditorEngine = nullptr;
	FEditorConsoleWidget ConsoleWidget;
	FEditorControlWidget ControlWidget;
	FEditorPropertyWidget PropertyWidget;
	FEditorSceneWidget SceneWidget;
	FEditorStatWidget StatWidget;
	FEditorCurveWidget CurveWidget;
	FEditorContentBrowserWidget ContentBrowserWidget;
	EditorShadowMapDebugWidget ShadowMapDebugWidget;
	EditorProjectSettingsWidget ProjectSettingsWidget;
	FEditorPackageSettings PackageSettings;
	bool bPackageSettingsOpen = false;
	char PackageProjectName[128] = {};
	char PackageOutputDirectory[260] = {};
	char PackageClientExecutablePath[260] = {};
	char PackageBuildToolPath[260] = {};
	char PackageBuildSolutionPath[260] = {};
	char PackageGameProjectPath[260] = {};
	char PackageBuildPlatform[64] = {};
	char PackageStartSceneName[128] = {};
	char PackageStartScenePackagePath[260] = {};
	char PackageBuildConfiguration[128] = {};
	char PackageRuntimeModules[260] = {};
	char PackageIncludePaths[4096] = {};
	char PackageExcludePaths[4096] = {};
	TArray<FEditorPackageGameDefinition> PackageGameDefinitions;
	std::thread PackageBuildThread;
	mutable std::mutex PackageBuildMutex;
	bool bPackageBuildRunning = false;
	bool bPackageBuildCompleted = false;
	float PackageBuildProgressPercent = 0.0f;
	FString PackageBuildStage;
	FGamePackageBuildResult PackageBuildResult;
	int32 PackageSelectedGameIndex = -1;
	bool bShowWidgetList = false;
	bool bShowShortcutOverlay = false;
	bool bHideEditorWindows = false;
	bool bHasSavedUIVisibility = false;
	bool bSavedShowWidgetList = false;
	bool bConsoleDrawerVisible = false;
	bool bBringConsoleDrawerToFrontNextFrame = false;
	bool bFocusConsoleInputNextFrame = false;
	bool bFocusConsoleButtonNextFrame = false;
	int32 ConsoleBacktickCycleState = 0;
	float ConsoleDrawerAnim = 0.0f;
	int32 DebugPlaceActorTypeIndex = 0;
	int32 DebugGridRows = 10;
	int32 DebugGridCols = 10;
	int32 DebugGridLayers = 1;
	float DebugGridSpacing = 2.0f;
	bool bDebugGridCenter = true;
	bool bDebugUseCameraOrigin = true;
	float DebugCameraForwardDistance = 30.0f;
	FVector DebugManualGridOrigin = FVector(0.0f, 0.0f, 0.0f);
	bool bDebugRandomYaw = false;
	float DebugRandomYawRange = 180.0f;
	bool bDebugApplyJitter = false;
	float DebugJitterXY = 0.0f;
	float DebugJitterZ = 0.0f;
	TArray<AActor*> DebugLastSpawnedActors;
	bool bPendingClearLastBatch = false;
	FEditorSettings::FUIVisibility SavedUIVisibility{};
};
