#pragma once

#define SOL_ALL_SAFETIES_ON 1
#define SOL_LUAJIT 1

#include "SolInclude.h"

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"
#include "Platform/DirectoryWatcher.h"

class AActor;
class ULuaScriptComponent;
class UWorld;
struct FLuaGameObjectHandle;

class FLuaScriptSubsystem : public TSingleton<FLuaScriptSubsystem>
{
	friend class TSingleton<FLuaScriptSubsystem>;

public:
	using FLuaBindingRegistrar = void(*)(sol::state&);

	void Initialize();
	void Shutdown();

	sol::state& GetState()
	{
		return Lua;
	}

	void AddBindingRegistrar(FLuaBindingRegistrar Registrar);
	void ClearBindingRegistrars();
	void RegisterScriptDirectoryWatcher(const FString& ScriptSubDirectory);
	
	bool DispatchUiEvent(const FString& EventName);
	bool DispatchGameEvent(const FString& EventName, AActor* Instigator = nullptr);

	bool ExecuteString(const FString& Code);
	bool ExecuteFile(const FString& Path);

	bool BindComponent(ULuaScriptComponent* Component, const FString& ScriptPath);
	void UnbindComponent(const ULuaScriptComponent* Component);
	void CallComponentBeginPlay(ULuaScriptComponent* Component);
	void CallComponentHotReload(ULuaScriptComponent* Component);
	void CallInput(UWorld* World, float DeltaTime);
	void CallComponentTick(ULuaScriptComponent* Component, float DeltaTime);
	void CallComponentEndPlay(ULuaScriptComponent* Component);
	void CallComponentSpawnFromPool(ULuaScriptComponent* Component);
	void CallComponentReturnToPool(ULuaScriptComponent* Component);
	void CallComponentOverlap(ULuaScriptComponent* Component, AActor* OtherActor);
	bool IsComponentBound(uint32 ComponentUUID) const;

private:
	FLuaScriptSubsystem() = default;

	void OnScriptsChanged(const TSet<FString>& ChangedFiles);
	void ConfigureLuaState();
	void ConfigureLuaState(sol::state& TargetLua);
	void RegisterRuntimeFunctions(sol::state& TargetLua);

	bool ExecuteFileInternal(const FString& Path, bool bTrackAsEntry);
	bool ExecuteEntryScript(sol::state_view LuaView, const FString& NormalizedPath, TMap<FString, TSet<FString>>& OutScriptIncludes, TMap<FString, FString>& OutModulePaths);
	bool ExecuteScriptFile(sol::state_view LuaView, const FString& NormalizedPath);
	bool ExecuteScriptFile(sol::state_view LuaView, const FString& NormalizedPath, sol::environment* Environment, sol::object* OutResult = nullptr);
	bool CompileFile(sol::state_view LuaView, const FString& NormalizedPath);
	bool ReloadScriptsAtomically(const TSet<FString>& ReloadTargets);

	FString ResolveScriptPath(const FString& Path) const;
	FString ResolveModulePath(const FString& ModuleName) const;
	FString MakeAbsoluteScriptPath(const FString& NormalizedPath) const;
	void RecordIncludeDependency(const FString& IncludePath);
	void RebuildIncludeDependents();
	TMap<FString, FString>& GetActiveModulePaths();

	sol::object IncludeFile(const FString& Path, sol::this_environment ThisEnv, sol::this_state State);
	sol::object RequireModule(const FString& ModuleName, sol::this_environment ThisEnv, sol::this_state State);

	struct FLuaDependencyContext
	{
		FString EntryScript;
		TSet<FString>* Includes = nullptr;
		TMap<FString, FString>* ModulePaths = nullptr;
	};

	struct FLuaComponentBinding
	{
		uint32 ComponentUUID = 0;
		uint32 OwnerActorUUID = 0;
		FString ScriptPath;
		sol::environment Environment;
		sol::function BeginPlay;
		sol::function OnInput;
		sol::function Tick;
		sol::function EndPlay;
		sol::function OnHotReload;
		sol::function OnSpawnFromPool;
		sol::function OnReturnToPool;
		sol::function OnOverlap;
		TSet<FString> ActiveCoroutineFunctions;
		bool bDisabledByError = false;
		FString LastError;
	};

	FLuaComponentBinding* FindComponentBinding(uint32 ComponentUUID);
	const FLuaComponentBinding* FindComponentBinding(uint32 ComponentUUID) const;
	void CancelAllComponentTasks();
	void DisableComponentBindingByError(uint32 ComponentUUID, const FString& Context, const char* Error);
	bool TryBeginCoroutine(const FString& FunctionName, uint32 OwnerUUID);
	void FinishCoroutine(const FString& FunctionName, uint32 OwnerUUID);
	void StartCoroutine(const char* FunctionName, const sol::function& Function, uint32 OwnerUUID);
	void StartCoroutine(const char* FunctionName, const sol::function& Function, uint32 OwnerUUID, float DeltaTime);
	void StartCoroutine(const char* FunctionName, const sol::function& Function, uint32 OwnerUUID, const FLuaGameObjectHandle& OtherActor);
	sol::thread CreateCoroutineThread(const sol::function& Function);
	void ResumeCoroutine(sol::thread Thread, uint32 OwnerUUID, const FString& FunctionName, int ArgCount);
	void HandleCoroutineResult(int Status, sol::thread Thread, lua_State* ThreadState, uint32 OwnerUUID, const FString& FunctionName);
	void ScheduleCoroutineResume(float Delay, sol::thread Thread, uint32 OwnerUUID, const FString& FunctionName);
	float ExtractYieldDelay(lua_State* ThreadState) const;

private:
	sol::state Lua;
	bool bInitialized = false;
	TArray<FSubscriptionID> WatchSubs;
	TSet<FString> LoadedScripts;
	TArray<FString> LoadedScriptOrder;
	TMap<FString, TSet<FString>> ScriptIncludes;
	TMap<FString, TSet<FString>> IncludeDependents;
	TMap<FString, FString> ModulePaths;
	TArray<FLuaDependencyContext> DependencyContextStack;
	TMap<uint32, FLuaComponentBinding> ComponentBindings;
	TArray<FLuaBindingRegistrar> ExtraBindingRegistrars;
	uint64 LuaGeneration = 0;
};
