#include "GameClient/GameClientSettings.h"

#include "Engine/Platform/Paths.h"
#include "SimpleJSON/json.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
FString ReadString(json::JSON& Object, const char* Key, const FString& Fallback)
{
	return Object.hasKey(Key) ? Object[Key].ToString() : Fallback;
}

int32 ReadInt(json::JSON& Object, const char* Key, int32 Fallback)
{
	return Object.hasKey(Key) ? static_cast<int32>(Object[Key].ToInt()) : Fallback;
}

bool ReadBool(json::JSON& Object, const char* Key, bool Fallback)
{
	return Object.hasKey(Key) ? Object[Key].ToBool() : Fallback;
}

EViewMode ReadViewMode(json::JSON& Object, const char* Key, EViewMode Fallback)
{
	if (!Object.hasKey(Key))
	{
		return Fallback;
	}

	const FString ViewMode = Object[Key].ToString();
	if (ViewMode == "Unlit") return EViewMode::Unlit;
	if (ViewMode == "Lit_Gouraud") return EViewMode::Lit_Gouraud;
	if (ViewMode == "Lit_Lambert") return EViewMode::Lit_Lambert;
	if (ViewMode == "Wireframe") return EViewMode::Wireframe;
	if (ViewMode == "SceneDepth") return EViewMode::SceneDepth;
	if (ViewMode == "WorldNormal") return EViewMode::WorldNormal;
	if (ViewMode == "LightCulling") return EViewMode::LightCulling;
	return EViewMode::Lit_Phong;
}

void ApplyRuntimeDefaults(FGameClientSettings& Settings)
{
	Settings.RenderOptions = FViewportRenderOptions();
	Settings.RenderOptions.ViewMode = EViewMode::Lit_Phong;
	Settings.RenderOptions.ShowFlags.bPrimitives = true;
	Settings.RenderOptions.ShowFlags.bGrid = false;
	Settings.RenderOptions.ShowFlags.bWorldAxis = false;
	Settings.RenderOptions.ShowFlags.bGizmo = false;
	Settings.RenderOptions.ShowFlags.bBillboardText = false;
	Settings.RenderOptions.ShowFlags.bBoundingVolume = false;
	Settings.RenderOptions.ShowFlags.bCollisionShapes = false;
	Settings.RenderOptions.ShowFlags.bDebugDraw = Settings.bEnableDebugDraw;
	Settings.RenderOptions.ShowFlags.bOctree = false;
	Settings.RenderOptions.ShowFlags.bPickingBVH = false;
	Settings.RenderOptions.ShowFlags.bCollisionBVH = false;
	Settings.RenderOptions.ShowFlags.bFog = true;
	Settings.RenderOptions.ShowFlags.bFXAA = true;
	Settings.RenderOptions.ShowFlags.bViewLightCulling = false;
	Settings.RenderOptions.ShowFlags.bVisualize25DCulling = false;
	Settings.RenderOptions.ShowFlags.bShowShadowFrustum = false;
}
}

void FGameClientSettings::Load()
{
	ApplyRuntimeDefaults(*this);

	std::ifstream File(std::filesystem::path(FPaths::GameSettingsFilePath()), std::ios::binary);
	if (!File.is_open())
	{
		return;
	}

	const FString Text((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
	json::JSON Root = json::JSON::Load(Text);

	if (Root.hasKey("Window"))
	{
		json::JSON& Window = Root["Window"];
		WindowTitle = ReadString(Window, "Title", WindowTitle);
		WindowWidth = ReadInt(Window, "Width", WindowWidth);
		WindowHeight = ReadInt(Window, "Height", WindowHeight);
		bFullscreen = ReadBool(Window, "Fullscreen", bFullscreen);
	}

	if (Root.hasKey("Paths"))
	{
		json::JSON& Paths = Root["Paths"];
		StartupScenePackagePath = ReadString(Paths, "StartupScene", StartupScenePackagePath);
	}

	if (Root.hasKey("Runtime"))
	{
		json::JSON& Runtime = Root["Runtime"];
		bRequireStartupScene = ReadBool(Runtime, "RequireStartupScene", bRequireStartupScene);
		bEnableOverlay = ReadBool(Runtime, "EnableOverlay", bEnableOverlay);
		bEnableDebugDraw = ReadBool(Runtime, "EnableDebugDraw", bEnableDebugDraw);
		bEnableLuaHotReload = ReadBool(Runtime, "EnableLuaHotReload", bEnableLuaHotReload);
	}

	ApplyRuntimeDefaults(*this);

	if (Root.hasKey("Render"))
	{
		json::JSON& Render = Root["Render"];
		RenderOptions.ViewMode = ReadViewMode(Render, "ViewMode", RenderOptions.ViewMode);
		RenderOptions.ShowFlags.bFXAA = ReadBool(Render, "FXAA", RenderOptions.ShowFlags.bFXAA);
		RenderOptions.ShowFlags.bFog = ReadBool(Render, "Fog", RenderOptions.ShowFlags.bFog);
	}
}
