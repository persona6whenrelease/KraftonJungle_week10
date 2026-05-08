#include "LuaScriptSubsystem.h"

#include "LuaBindings.h"
#include "LuaHandles.h"
#include "Core/Log.h"
#include "Core/Notification.h"
#include "Platform/Paths.h"
#include "Platform/DirectoryWatcher.h"
#include "Component/Script/LuaScriptComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/Object.h"
#include "Runtime/Engine.h"

#include <algorithm>
#include <utility>
#include <cwctype>
#include <filesystem>
#include <fstream>

namespace
{
	int ResumeLuaThread(lua_State* ThreadState, int ArgCount)
	{
#if SOL_LUA_VERSION_I_ >= 504
		int ResultCount = 0;
		return lua_resume(ThreadState, nullptr, ArgCount, &ResultCount);
#else
		return lua_resume(ThreadState, nullptr, ArgCount);
#endif
	}

	FString ToGenericUtf8(const std::filesystem::path& Path)
	{
		return FPaths::ToUtf8(Path.generic_wstring());
	}

	FString NormalizeScriptPath(const std::filesystem::path& InPath)
	{
		std::filesystem::path Path = InPath;
		if (!Path.is_absolute())
		{
			Path = std::filesystem::path(FPaths::RootDir()) / Path;
		}

		Path = Path.lexically_normal();
		return ToGenericUtf8(Path);
	}

	bool IsLuaFile(const FString& Path)
	{
		std::filesystem::path FilePath(FPaths::ToWide(Path));
		std::wstring Extension = FilePath.extension().wstring();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(),
			[](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
		return Extension == L".lua";
	}

	AActor* ResolveComponentOwner(const ULuaScriptComponent* Component)
	{
		if (!Component)
		{
			return nullptr;
		}

		if (AActor* Owner = Component->GetOwner())
		{
			return Owner;
		}

		// Some deserialization/editor paths can create a component with the Actor as Outer
		// before the Owner pointer is restored. Lua still needs a stable owner handle.
		return Component->GetTypedOuter<AActor>();
	}

	void AssignGameObjectHandle(sol::environment& Environment, const char* Name, const AActor* Actor)
	{
		if (!Actor)
		{
			Environment[Name] = sol::nil;
			return;
		}

		FLuaGameObjectHandle Handle;
		Handle.UUID = Actor->GetUUID();
		Environment[Name] = Handle;
	}

	void AssignScriptComponentHandle(sol::environment& Environment, const char* Name, const ULuaScriptComponent* Component)
	{
		if (!Component)
		{
			Environment[Name] = sol::nil;
			return;
		}

		FLuaScriptComponentHandle Handle;
		Handle.UUID = Component->GetUUID();
		Environment[Name] = Handle;
	}

	void AssignComponentBindingHandles(sol::environment& Environment, const ULuaScriptComponent* Component)
	{
		AssignScriptComponentHandle(Environment, "self", Component);

		const AActor* Owner = ResolveComponentOwner(Component);
		AssignGameObjectHandle(Environment, "owner", Owner);
		AssignGameObjectHandle(Environment, "obj", Owner);
		if (Owner)
		{
			Environment["_ownerUUID"] = Owner->GetUUID();
			Environment["_accessLevel"] = "ActorLocal";
		}
		else
		{
			Environment["_ownerUUID"] = 0;
			Environment["_accessLevel"] = sol::nil;
		}
	}

	void ConfigureScriptEnvironment(sol::state_view LuaView, sol::environment& Environment)
	{
		sol::table MetaTable = LuaView.create_table();
		MetaTable["__index"] = LuaView.globals();
		MetaTable.set_function("__newindex",
			[](sol::table Target, sol::object Key, sol::object Value)
			{
				Target.raw_set(Key, Value);
			});
		Environment[sol::metatable_key] = MetaTable;
	}

	sol::environment GetCallerEnvironment(sol::this_environment ThisEnv)
	{
		sol::environment Environment = ThisEnv;
		return Environment.valid() ? Environment : sol::environment();
	}
}

void FLuaScriptSubsystem::Initialize()
{
	if (bInitialized)
	{
		return;
	}

	ConfigureLuaState();

	RegisterScriptDirectoryWatcher("Config/");
	RegisterScriptDirectoryWatcher("Game/");

	bInitialized = true;
	UE_LOG("[Lua] Lua Scripting Initialized.");
}

void FLuaScriptSubsystem::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	for (FSubscriptionID WatchSub : WatchSubs)
	{
		if (WatchSub != 0)
		{
			FDirectoryWatcher::Get().Unsubscribe(WatchSub);
		}
	}
	WatchSubs.clear();

	ClearLuaUiEventHandler();
	CancelAllComponentTasks();
	ComponentBindings.clear();
	++LuaGeneration;
	Lua = sol::state();

	LoadedScripts.clear();
	LoadedScriptOrder.clear();
	ScriptIncludes.clear();
	IncludeDependents.clear();
	ModulePaths.clear();
	DependencyContextStack.clear();
	bInitialized = false;
	UE_LOG("[Lua] Lua Scripting Shutdown.");
}


bool FLuaScriptSubsystem::DispatchUiEvent(const FString& EventName)
{
	if (!bInitialized || !Lua.lua_state())
	{
		return false;
	}

	auto InvokeLuaFunction = [&EventName](const sol::object& FunctionObject, const char* FunctionName) -> bool
	{
		if (FunctionObject.get_type() != sol::type::function)
		{
			return false;
		}

		sol::protected_function Function = FunctionObject.as<sol::protected_function>();
		sol::protected_function_result Result = Function(EventName);
		if (!Result.valid())
		{
			sol::error Error = Result;
			UE_LOG("[Lua UI] %s failed: %s", FunctionName ? FunctionName : "OnUIEvent", Error.what());
		}
		return true;
	};

	// Component scripts run in their own sol::environment, so a script-level
	// function OnUIEvent(eventName) is not stored on Lua.globals(). Dispatch to
	// those environments first. This prevents UI.SetEventHandler users such as
	// UIExample.lua from stealing the single global UI handler slot.
	bool bHandledByComponent = false;
	for (auto& Pair : ComponentBindings)
	{
		FLuaComponentBinding& Binding = Pair.second;
		if (Binding.bDisabledByError || !Binding.Environment.valid())
		{
			continue;
		}

		const FString FunctionName = Binding.ScriptPath + "::OnUIEvent";
		sol::object OnUiEventObject = Binding.Environment["OnUIEvent"];
		if (InvokeLuaFunction(OnUiEventObject, FunctionName.c_str()))
		{
			bHandledByComponent = true;
		}
	}

	if (bHandledByComponent)
	{
		return true;
	}

	// Backward-compatible fallback for UI.SetEventHandler(function(eventName) ... end).
	// This is intentionally fallback-only. Only one script can occupy this slot.
	sol::object UiObject = Lua["UI"];
	if (UiObject.get_type() == sol::type::table)
	{
		sol::table UiTable = UiObject.as<sol::table>();
		sol::object HandlerObject = UiTable["_EventHandler"];
		if (InvokeLuaFunction(HandlerObject, "UI.SetEventHandler"))
		{
			return true;
		}
	}

	// Standalone/global script fallback.
	sol::object OnUiEventObject = Lua["OnUIEvent"];
	return InvokeLuaFunction(OnUiEventObject, "OnUIEvent");
}

bool FLuaScriptSubsystem::DispatchGameEvent(const FString& EventName, AActor* Instigator)
{
	if (!bInitialized || !Lua.lua_state())
	{
		return false;
	}

	sol::object OnGameEventObject = Lua["OnGameEvent"];
	if (OnGameEventObject.get_type() != sol::type::function)
	{
		return false;
	}

	FLuaGameObjectHandle InstigatorHandle;
	InstigatorHandle.UUID = Instigator ? Instigator->GetUUID() : 0;

	sol::protected_function Function = OnGameEventObject.as<sol::protected_function>();
	sol::protected_function_result Result = Function(EventName, InstigatorHandle);
	if (!Result.valid())
	{
		sol::error Error = Result;
		UE_LOG("[Lua Game] OnGameEvent failed: %s", Error.what());
	}

	return true;
}

// 문자열로 들어온 Lua 코드를 실행
bool FLuaScriptSubsystem::ExecuteString(const FString& Code)
{
	if (!bInitialized)
	{
		UE_LOG("[Lua] Lua Scripting Not Initialized.");
		return false;
	}
	
	sol::protected_function_result Result = Lua.safe_script(Code.c_str(), sol::script_pass_on_error);
	
	if (!Result.valid())
	{
		sol::error Error = Result;
		UE_LOG("[Lua] Lua Scripting Error: %s", Error.what());
		return false;
	}
	
	return true;
}

bool FLuaScriptSubsystem::ExecuteFile(const FString& Path)
{
	return ExecuteFileInternal(Path, true);
}

bool FLuaScriptSubsystem::BindComponent(ULuaScriptComponent* Component, const FString& ScriptPath)
{
	if (!bInitialized || !Component)
	{
		return false;
	}

	const FString NormalizedPath = ResolveScriptPath(ScriptPath);
	const FString AbsolutePath = MakeAbsoluteScriptPath(NormalizedPath);

	if (AbsolutePath.empty())
	{
		UE_LOG("[Lua] Invalid script path: %s", ScriptPath.c_str());
		return false;
	}

	if (!std::filesystem::exists(FPaths::ToWide(AbsolutePath)))
	{
		return false;
	}
	 
	sol::state_view LuaView(Lua);
	sol::environment Env(LuaView, sol::create, LuaView.globals());
	ConfigureScriptEnvironment(LuaView, Env);
	AssignComponentBindingHandles(Env, Component);

	std::ifstream File(FPaths::ToWide(AbsolutePath), std::ios::binary);
	if (!File)
	{
		UE_LOG("[Lua] Lua File Compile Error (%s): Cannot open file", NormalizedPath.c_str());
		return false;
	}

	const std::string Content((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
	sol::load_result LoadResult = LuaView.load_buffer(Content.data(), Content.size(), "@" + AbsolutePath);
	if (!LoadResult.valid())
	{
		sol::error Error = LoadResult;
		UE_LOG("[Lua] Lua File Compile Error (%s): %s", NormalizedPath.c_str(), Error.what());
		return false;
	}

	sol::protected_function ScriptFunction = LoadResult;
	sol::set_environment(Env, ScriptFunction);
	sol::protected_function_result ExecResult = ScriptFunction();
	if (!ExecResult.valid())
	{
		sol::error Error = ExecResult;
		UE_LOG("[Lua] Lua File Execution Error (%s): %s", NormalizedPath.c_str(), Error.what());
		return false;
	}

	FLuaComponentBinding Binding;
	Binding.ComponentUUID = Component->GetUUID();
	AActor* BindingOwner = ResolveComponentOwner(Component);
	Binding.OwnerActorUUID = BindingOwner ? BindingOwner->GetUUID() : 0;
	Binding.ScriptPath = NormalizedPath;
	Binding.Environment = std::move(Env);
	Binding.BeginPlay = Binding.Environment["BeginPlay"];
	Binding.OnInput = Binding.Environment["OnInput"];
	Binding.Tick = Binding.Environment["Tick"];
	Binding.EndPlay = Binding.Environment["EndPlay"];
	Binding.OnHotReload = Binding.Environment["OnHotReload"];
	Binding.OnSpawnFromPool = Binding.Environment["OnSpawnFromPool"];
	Binding.OnReturnToPool = Binding.Environment["OnReturnToPool"];
	Binding.OnOverlap = Binding.Environment["OnOverlap"];

	if (GEngine)
	{
		GEngine->GetTaskScheduler().CancelTasks(Binding.ComponentUUID);
	}
	ComponentBindings.erase(Binding.ComponentUUID);
	ComponentBindings.emplace(Binding.ComponentUUID, std::move(Binding));
	return true;
}

void FLuaScriptSubsystem::UnbindComponent(const ULuaScriptComponent* Component)
{
	if (!Component)
	{
		return;
	}

	if (GEngine)
	{
		GEngine->GetTaskScheduler().CancelTasks(Component->GetUUID());
	}
	ComponentBindings.erase(Component->GetUUID());
}

void FLuaScriptSubsystem::CallComponentBeginPlay(ULuaScriptComponent* Component)
{
	if (FLuaComponentBinding* Binding = FindComponentBinding(Component ? Component->GetUUID() : 0))
	{
		if (Binding->bDisabledByError)
		{
			return;
		}
		AssignComponentBindingHandles(Binding->Environment, Component);
		StartCoroutine("BeginPlay", Binding->BeginPlay, Binding->ComponentUUID);
	}
}

void FLuaScriptSubsystem::CallComponentHotReload(ULuaScriptComponent* Component)
{
	if (FLuaComponentBinding* Binding = FindComponentBinding(Component ? Component->GetUUID() : 0))
	{
		if (Binding->bDisabledByError)
		{
			return;
		}
		AssignComponentBindingHandles(Binding->Environment, Component);
		if (Binding->OnHotReload.valid())
		{
			StartCoroutine("OnHotReload", Binding->OnHotReload, Binding->ComponentUUID);
		}
		else
		{
			StartCoroutine("BeginPlay", Binding->BeginPlay, Binding->ComponentUUID);
		}
	}
}


void FLuaScriptSubsystem::CallInput(UWorld* World, float DeltaTime)
{
	if (!bInitialized || !World)
	{
		return;
	}

	TArray<AActor*> ActorSnapshot = World->GetActors();
	for (AActor* Actor : ActorSnapshot)
	{
		if (!Actor || !IsAliveObject(Actor) || !World->IsActorInWorld(Actor) || Actor->IsPooledActorInactive())
		{
			continue;
		}

		TArray<UActorComponent*> ComponentSnapshot = Actor->GetComponents();
		for (UActorComponent* ComponentBase : ComponentSnapshot)
		{
			ULuaScriptComponent* Component = Cast<ULuaScriptComponent>(ComponentBase);
			if (!Component || !IsAliveObject(Component))
			{
				continue;
			}

			FLuaComponentBinding* Binding = FindComponentBinding(Component->GetUUID());
			if (!Binding || Binding->bDisabledByError || !Binding->OnInput.valid())
			{
				continue;
			}

			AssignComponentBindingHandles(Binding->Environment, Component);

			sol::protected_function OnInput = Binding->OnInput;
			sol::protected_function_result Result = OnInput(DeltaTime);
			if (!Result.valid())
			{
				sol::error Error = Result;
				UE_LOG("[Lua] Lua Component OnInput Error (%s): %s", Binding->ScriptPath.c_str(), Error.what());
				DisableComponentBindingByError(Binding->ComponentUUID, "OnInput", Error.what());
			}
		}
	}
}

void FLuaScriptSubsystem::CallComponentTick(ULuaScriptComponent* Component, float DeltaTime)
{
	if (FLuaComponentBinding* Binding = FindComponentBinding(Component ? Component->GetUUID() : 0))
	{
		if (Binding->bDisabledByError)
		{
			return;
		}
		AssignComponentBindingHandles(Binding->Environment, Component);
		StartCoroutine("Tick", Binding->Tick, Binding->ComponentUUID, DeltaTime);
	}
}

void FLuaScriptSubsystem::CallComponentEndPlay(ULuaScriptComponent* Component)
{
	if (FLuaComponentBinding* Binding = FindComponentBinding(Component ? Component->GetUUID() : 0))
	{
		if (Binding->bDisabledByError)
		{
			return;
		}
		AssignComponentBindingHandles(Binding->Environment, Component);
		StartCoroutine("EndPlay", Binding->EndPlay, Binding->ComponentUUID);
	}
}

void FLuaScriptSubsystem::CallComponentSpawnFromPool(ULuaScriptComponent* Component)
{
	if (FLuaComponentBinding* Binding = FindComponentBinding(Component ? Component->GetUUID() : 0))
	{
		if (Binding->bDisabledByError)
		{
			return;
		}
		AssignComponentBindingHandles(Binding->Environment, Component);
		StartCoroutine("OnSpawnFromPool", Binding->OnSpawnFromPool, Binding->ComponentUUID);
	}
}

void FLuaScriptSubsystem::CallComponentReturnToPool(ULuaScriptComponent* Component)
{
	if (FLuaComponentBinding* Binding = FindComponentBinding(Component ? Component->GetUUID() : 0))
	{
		if (Binding->bDisabledByError)
		{
			return;
		}
		AssignComponentBindingHandles(Binding->Environment, Component);
		StartCoroutine("OnReturnToPool", Binding->OnReturnToPool, Binding->ComponentUUID);
	}
}

void FLuaScriptSubsystem::CallComponentOverlap(ULuaScriptComponent* Component, AActor* OtherActor)
{
	if (FLuaComponentBinding* Binding = FindComponentBinding(Component ? Component->GetUUID() : 0))
	{
		if (Binding->bDisabledByError)
		{
			return;
		}
		AssignComponentBindingHandles(Binding->Environment, Component);
		FLuaGameObjectHandle OtherHandle;
		OtherHandle.UUID = OtherActor ? OtherActor->GetUUID() : 0;
		StartCoroutine("OnOverlap", Binding->OnOverlap, Binding->ComponentUUID, OtherHandle);
	}
}

void FLuaScriptSubsystem::RegisterScriptDirectoryWatcher(const FString& ScriptSubDirectory)
{
	const std::wstring WatchDirectory = FPaths::Combine(FPaths::ScriptDir(), FPaths::ToWide(ScriptSubDirectory));
	FWatchID WatchID = FDirectoryWatcher::Get().Watch(WatchDirectory, ScriptSubDirectory);
	if (WatchID != 0)
	{
		WatchSubs.push_back(FDirectoryWatcher::Get().Subscribe(WatchID,
			[this](const TSet<FString>& Files) { OnScriptsChanged(Files); }));
	}

}

FLuaScriptSubsystem::FLuaComponentBinding* FLuaScriptSubsystem::FindComponentBinding(uint32 ComponentUUID)
{
	auto It = ComponentBindings.find(ComponentUUID);
	return It != ComponentBindings.end() ? &It->second : nullptr;
}

const FLuaScriptSubsystem::FLuaComponentBinding* FLuaScriptSubsystem::FindComponentBinding(uint32 ComponentUUID) const
{
	auto It = ComponentBindings.find(ComponentUUID);
	return It != ComponentBindings.end() ? &It->second : nullptr;
}

bool FLuaScriptSubsystem::IsComponentBound(uint32 ComponentUUID) const
{
	return FindComponentBinding(ComponentUUID) != nullptr;
}

void FLuaScriptSubsystem::CancelAllComponentTasks()
{
	if (!GEngine)
	{
		return;
	}

	for (const auto& [ComponentUUID, Binding] : ComponentBindings)
	{
		(void)Binding;
		GEngine->GetTaskScheduler().CancelTasks(ComponentUUID);
	}
}

void FLuaScriptSubsystem::DisableComponentBindingByError(uint32 ComponentUUID, const FString& Context, const char* Error)
{
	FLuaComponentBinding* Binding = FindComponentBinding(ComponentUUID);
	if (!Binding)
	{
		return;
	}

	Binding->bDisabledByError = true;
	Binding->LastError = Error ? Error : "unknown error";
	Binding->ActiveCoroutineFunctions.clear();

	if (GEngine)
	{
		GEngine->GetTaskScheduler().CancelTasks(ComponentUUID);
	}

	UE_LOG("[Lua] Component script disabled after error (%s, %s): %s",
		Binding->ScriptPath.c_str(),
		Context.c_str(),
		Binding->LastError.c_str());
}

namespace
{
	bool IsSingleInstanceComponentCoroutine(const FString& FunctionName)
	{
		return FunctionName == "Tick";
	}
}

bool FLuaScriptSubsystem::TryBeginCoroutine(const FString& FunctionName, uint32 OwnerUUID)
{
	if (!IsSingleInstanceComponentCoroutine(FunctionName) || OwnerUUID == 0)
	{
		return true;
	}

	FLuaComponentBinding* Binding = FindComponentBinding(OwnerUUID);
	if (!Binding)
	{
		return false;
	}

	if (Binding->ActiveCoroutineFunctions.find(FunctionName) != Binding->ActiveCoroutineFunctions.end())
	{
		return false;
	}

	Binding->ActiveCoroutineFunctions.insert(FunctionName);
	return true;
}

void FLuaScriptSubsystem::FinishCoroutine(const FString& FunctionName, uint32 OwnerUUID)
{
	if (!IsSingleInstanceComponentCoroutine(FunctionName) || OwnerUUID == 0)
	{
		return;
	}

	if (FLuaComponentBinding* Binding = FindComponentBinding(OwnerUUID))
	{
		Binding->ActiveCoroutineFunctions.erase(FunctionName);
	}
}

void FLuaScriptSubsystem::StartCoroutine(const char* FunctionName, const sol::function& Function, uint32 OwnerUUID)
{
	if (!Function.valid())
	{
		return;
	}

	if (!TryBeginCoroutine(FunctionName ? FString(FunctionName) : FString(), OwnerUUID))
	{
		return;
	}

	sol::thread Thread = CreateCoroutineThread(Function);
	ResumeCoroutine(std::move(Thread), OwnerUUID, FunctionName, 0);
}

void FLuaScriptSubsystem::StartCoroutine(const char* FunctionName, const sol::function& Function, uint32 OwnerUUID, float DeltaTime)
{
	if (!Function.valid())
	{
		return;
	}

	if (!TryBeginCoroutine(FunctionName ? FString(FunctionName) : FString(), OwnerUUID))
	{
		return;
	}

	sol::thread Thread = CreateCoroutineThread(Function);
	lua_pushnumber(Thread.thread_state(), static_cast<lua_Number>(DeltaTime));
	ResumeCoroutine(std::move(Thread), OwnerUUID, FunctionName, 1);
}

void FLuaScriptSubsystem::StartCoroutine(const char* FunctionName, const sol::function& Function, uint32 OwnerUUID, const FLuaGameObjectHandle& OtherActor)
{
	if (!Function.valid())
	{
		return;
	}

	if (!TryBeginCoroutine(FunctionName ? FString(FunctionName) : FString(), OwnerUUID))
	{
		return;
	}

	sol::thread Thread = CreateCoroutineThread(Function);
	sol::stack::push(Thread.thread_state(), OtherActor);
	ResumeCoroutine(std::move(Thread), OwnerUUID, FunctionName, 1);
}

sol::thread FLuaScriptSubsystem::CreateCoroutineThread(const sol::function& Function)
{
	sol::thread Thread = sol::thread::create(Lua.lua_state());
	lua_State* FunctionState = Function.lua_state();
	lua_State* ThreadState = Thread.thread_state();

	Function.push();
	lua_xmove(FunctionState, ThreadState, 1);

	return Thread;
}

void FLuaScriptSubsystem::ResumeCoroutine(sol::thread Thread, uint32 OwnerUUID, const FString& FunctionName, int ArgCount)
{
	lua_State* ThreadState = Thread.thread_state();
	const int Status = ResumeLuaThread(ThreadState, ArgCount);
	HandleCoroutineResult(Status, std::move(Thread), ThreadState, OwnerUUID, FunctionName);
}

void FLuaScriptSubsystem::HandleCoroutineResult(int Status, sol::thread Thread, lua_State* ThreadState, uint32 OwnerUUID, const FString& FunctionName)
{
	if (Status == LUA_YIELD)
	{
		const float Delay = ExtractYieldDelay(ThreadState);
		lua_settop(ThreadState, 0);
		ScheduleCoroutineResume(Delay, std::move(Thread), OwnerUUID, FunctionName);
		return;
	}

	if (Status != LUA_OK)
	{
		const char* Error = lua_tostring(ThreadState, -1);
		UE_LOG("[Lua] Lua Component Coroutine Error (%s): %s", FunctionName.c_str(), Error ? Error : "unknown error");
		DisableComponentBindingByError(OwnerUUID, FunctionName, Error);
	}

	lua_settop(ThreadState, 0);
	FinishCoroutine(FunctionName, OwnerUUID);
}

void FLuaScriptSubsystem::ScheduleCoroutineResume(float Delay, sol::thread Thread, uint32 OwnerUUID, const FString& FunctionName)
{
	if (!GEngine)
	{
		return;
	}

	const uint64 ScheduledGeneration = LuaGeneration;
	GEngine->GetTaskScheduler().Schedule(Delay,
		[this, Thread = std::move(Thread), OwnerUUID, FunctionName, ScheduledGeneration]() mutable
		{
			if (!bInitialized || ScheduledGeneration != LuaGeneration)
			{
				return;
			}

			if (OwnerUUID != 0)
			{
				FLuaComponentBinding* Binding = FindComponentBinding(OwnerUUID);
				if (!Binding || Binding->bDisabledByError)
				{
					return;
				}
			}

			ResumeCoroutine(Thread, OwnerUUID, FunctionName, 0);
		},
		OwnerUUID,
		true);
}

float FLuaScriptSubsystem::ExtractYieldDelay(lua_State* ThreadState) const
{
	if (lua_gettop(ThreadState) <= 0 || !lua_isnumber(ThreadState, 1))
	{
		return 0.0f;
	}

	return static_cast<float>(lua_tonumber(ThreadState, 1));
}

void FLuaScriptSubsystem::OnScriptsChanged(const TSet<FString>& ChangedFiles)
{
	TSet<FString> ReloadTargets;

	for (const FString& File : ChangedFiles)
	{
		if (!IsLuaFile(File))
		{
			continue;
		}

		bool bFoundTrackedTarget = false;
		const FString NormalizedPath = ResolveScriptPath(File);

		if (LoadedScripts.find(NormalizedPath) != LoadedScripts.end())
		{
			ReloadTargets.insert(NormalizedPath);
			bFoundTrackedTarget = true;
		}

		auto DependentIt = IncludeDependents.find(NormalizedPath);
		if (DependentIt != IncludeDependents.end())
		{
			for (const FString& DependentScript : DependentIt->second)
			{
				ReloadTargets.insert(DependentScript);
			}
			bFoundTrackedTarget = true;
		}

		if (!bFoundTrackedTarget)
		{
			ReloadTargets.insert(NormalizedPath);
		}
	}

	if (ReloadTargets.empty())
	{
		return;
	}

	UE_LOG("[LuaHotReload] Reloading %zu Lua script(s)...", ReloadTargets.size());
	if (ReloadScriptsAtomically(ReloadTargets))
	{
		for (const FString& Target : ReloadTargets)
		{
			UE_LOG("[LuaHotReload] OK: %s", Target.c_str());
			FNotificationManager::Get().AddNotification("Lua Reloaded: " + Target, ENotificationType::Success, 3.0f);
		}
	}
	else
	{
		UE_LOG("[LuaHotReload] FAILED (keeping previous Lua state)");
		FNotificationManager::Get().AddNotification("Lua Reload Failed: keeping previous state", ENotificationType::Error, 5.0f);
	}
}

void FLuaScriptSubsystem::ConfigureLuaState()
{
	ConfigureLuaState(Lua);
}

void FLuaScriptSubsystem::ConfigureLuaState(sol::state& TargetLua)
{
	TargetLua = sol::state();
	TargetLua.open_libraries(
		sol::lib::base,
		sol::lib::package,
		sol::lib::coroutine,
		sol::lib::math,
		sol::lib::table,
		sol::lib::string);

	RegisterLuaBindings(TargetLua);
	RegisterRuntimeFunctions(TargetLua);

	sol::table Package = TargetLua["package"];
	FString ExistingPath = Package["path"].get_or(FString());

	const FString ScriptDir = ToGenericUtf8(std::filesystem::path(FPaths::ScriptDir()).lexically_normal());
	const FString ScriptPathPrefix = ScriptDir.empty() || ScriptDir.back() == '/'
		? ScriptDir
		: ScriptDir + "/";

	Package["path"] = ScriptPathPrefix + "?.lua;" + ScriptPathPrefix + "?/init.lua;" + ExistingPath;
}

void FLuaScriptSubsystem::RegisterRuntimeFunctions(sol::state& TargetLua)
{
	TargetLua.set_function("print",
		[](sol::variadic_args Args)
		{
			FString Message;
			bool bFirst = true;
			for (sol::object Arg : Args)
			{
				if (!bFirst)
				{
					Message += "\t";
				}
				bFirst = false;

				switch (Arg.get_type())
				{
				case sol::type::nil:
					Message += "nil";
					break;
				case sol::type::boolean:
					Message += Arg.as<bool>() ? "true" : "false";
					break;
				case sol::type::number:
					Message += std::to_string(Arg.as<double>());
					break;
				case sol::type::string:
					Message += Arg.as<FString>();
					break;
				default:
					Message += "<";
					Message += sol::type_name(Arg.lua_state(), Arg.get_type());
					Message += ">";
					break;
				}
			}

			UE_LOG("[Lua] %s", Message.c_str());
		});

	TargetLua.set_function("Include",
		[this](const FString& Path, sol::this_environment ThisEnv, sol::this_state State) -> sol::object
		{
			return IncludeFile(Path, ThisEnv, State);
		});

	TargetLua.set_function("LuaInclude",
		[this](const FString& Path, sol::this_environment ThisEnv, sol::this_state State) -> sol::object
		{
			return IncludeFile(Path, ThisEnv, State);
		});

	TargetLua.set_function("dofile",
		[this](const FString& Path, sol::this_environment ThisEnv, sol::this_state State) -> sol::object
		{
			return IncludeFile(Path, ThisEnv, State);
		});

	TargetLua.set_function("require",
		[this](const FString& ModuleName, sol::this_environment ThisEnv, sol::this_state State) -> sol::object
		{
			return RequireModule(ModuleName, ThisEnv, State);
		});

	TargetLua.set_function("Wait", sol::yielding([](float Seconds)
		{
			return Seconds;
		}));

	TargetLua.set_function("wait", sol::yielding([](float Seconds)
		{
			return Seconds;
		}));
}

bool FLuaScriptSubsystem::ExecuteFileInternal(const FString& Path, bool bTrackAsEntry)
{
	if (!bInitialized)
	{
		UE_LOG("[Lua] Lua Scripting Not Initialized.");
		return false;
	}

	const FString NormalizedPath = ResolveScriptPath(Path);

	if (!bTrackAsEntry)
	{
		if (!CompileFile(Lua, NormalizedPath))
		{
			return false;
		}
		return ExecuteScriptFile(Lua, NormalizedPath);
	}

	TSet<FString> ReloadTargets;
	ReloadTargets.insert(NormalizedPath);
	return ReloadScriptsAtomically(ReloadTargets);
}

bool FLuaScriptSubsystem::ExecuteEntryScript(sol::state_view LuaView, const FString& NormalizedPath, TMap<FString, TSet<FString>>& OutScriptIncludes, TMap<FString, FString>& OutModulePaths)
{
	if (!CompileFile(LuaView, NormalizedPath))
	{
		return false;
	}

	TSet<FString> NewIncludes;
	DependencyContextStack.push_back({ NormalizedPath, &NewIncludes, &OutModulePaths });
	const bool bSuccess = ExecuteScriptFile(LuaView, NormalizedPath);
	DependencyContextStack.pop_back();

	if (!bSuccess)
	{
		return false;
	}

	OutScriptIncludes[NormalizedPath] = std::move(NewIncludes);

	return true;
}

bool FLuaScriptSubsystem::ExecuteScriptFile(sol::state_view LuaView, const FString& NormalizedPath)
{
	return ExecuteScriptFile(LuaView, NormalizedPath, nullptr, nullptr);
}

bool FLuaScriptSubsystem::ExecuteScriptFile(sol::state_view LuaView, const FString& NormalizedPath, sol::environment* Environment, sol::object* OutResult)
{
	const FString AbsolutePath = MakeAbsoluteScriptPath(NormalizedPath);
	if (AbsolutePath.empty())
	{
		UE_LOG("[Lua] Lua File Execution Error (%s): Invalid path", NormalizedPath.c_str());
		return false;
	}

	std::ifstream File(FPaths::ToWide(AbsolutePath), std::ios::binary);
	if (!File)
	{
		UE_LOG("[Lua] Lua File Execution Error (%s): Cannot open file", NormalizedPath.c_str());
		return false;
	}

	const std::string Content((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());

	sol::load_result LoadResult = LuaView.load_buffer(Content.data(), Content.size(), "@" + AbsolutePath);
	if (!LoadResult.valid())
	{
		sol::error Error = LoadResult;
		UE_LOG("[Lua] Lua File Compile Error (%s): %s", NormalizedPath.c_str(), Error.what());
		return false;
	}

	sol::protected_function ScriptFunction = LoadResult;
	if (Environment && Environment->valid())
	{
		sol::set_environment(*Environment, ScriptFunction);
	}

	sol::protected_function_result Result = ScriptFunction();

	if (!Result.valid())
	{
		sol::error Error = Result;
		UE_LOG("[Lua] Lua File Execution Error (%s): %s", NormalizedPath.c_str(), Error.what());
		return false;
	}

	if (OutResult)
	{
		if (Result.return_count() > 0)
		{
			*OutResult = Result[0].get<sol::object>();
		}
		else
		{
			*OutResult = sol::make_object(LuaView, true);
		}
	}

	return true;
}

bool FLuaScriptSubsystem::CompileFile(sol::state_view LuaView, const FString& NormalizedPath)
{
	const FString AbsolutePath = MakeAbsoluteScriptPath(NormalizedPath);
	if (AbsolutePath.empty())
	{
		UE_LOG("[Lua] Lua File Compile Error (%s): Invalid path", NormalizedPath.c_str());
		return false;
	}

	std::ifstream File(FPaths::ToWide(AbsolutePath), std::ios::binary);
	if (!File)
	{
		UE_LOG("[Lua] Lua File Compile Error (%s): Cannot open file", NormalizedPath.c_str());
		return false;
	}
	const std::string Content((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());

	sol::load_result LoadResult = LuaView.load_buffer(Content.data(), Content.size(), "@" + AbsolutePath);
	if (!LoadResult.valid())
	{
		sol::error Error = LoadResult;
		UE_LOG("[Lua] Lua File Compile Error (%s): %s", NormalizedPath.c_str(), Error.what());
		return false;
	}
	return true;
}

bool FLuaScriptSubsystem::ReloadScriptsAtomically(const TSet<FString>& ReloadTargets)
{
	if (ReloadTargets.empty())
	{
		return true;
	}

	sol::state NewLua;
	ConfigureLuaState(NewLua);

	TSet<FString> NewLoadedScripts = LoadedScripts;
	TArray<FString> NewLoadedScriptOrder = LoadedScriptOrder;
	for (const FString& Target : ReloadTargets)
	{
		if (NewLoadedScripts.insert(Target).second)
		{
			NewLoadedScriptOrder.push_back(Target);
		}
	}

	TMap<FString, TSet<FString>> NewScriptIncludes;
	TMap<FString, FString> NewModulePaths;
	TArray<std::pair<uint32, FString>> ComponentBindingsToRestore;
	ComponentBindingsToRestore.reserve(ComponentBindings.size());
	for (const auto& [ComponentUUID, Binding] : ComponentBindings)
	{
		ComponentBindingsToRestore.emplace_back(ComponentUUID, Binding.ScriptPath);
	}

	for (const FString& Script : NewLoadedScriptOrder)
	{
		if (!ExecuteEntryScript(NewLua, Script, NewScriptIncludes, NewModulePaths))
		{
			return false;
		}
	}

	ClearLuaUiEventHandler();
	CancelAllComponentTasks();
	ComponentBindings.clear();

	Lua = std::move(NewLua);
	++LuaGeneration;
	LoadedScripts = std::move(NewLoadedScripts);
	LoadedScriptOrder = std::move(NewLoadedScriptOrder);
	ScriptIncludes = std::move(NewScriptIncludes);
	ModulePaths = std::move(NewModulePaths);
	RebuildIncludeDependents();

	// Keep GameUiSystem connected to the current Lua state only.
	// The router is native-only; it looks up the Lua receiver in the active state at dispatch time.
	InstallLuaUiEventRouter();

	for (const auto& [ComponentUUID, ScriptPath] : ComponentBindingsToRestore)
	{
		UObject* Object = UObjectManager::Get().FindByUUID(ComponentUUID);
		if (ULuaScriptComponent* Component = Cast<ULuaScriptComponent>(Object))
		{
			if (BindComponent(Component, ScriptPath))
			{
				AActor* Owner = ResolveComponentOwner(Component);
				if (Owner && Owner->HasActorBegunPlay())
				{
					CallComponentHotReload(Component);
				}
			}
		}
	}

	return true;
}

FString FLuaScriptSubsystem::ResolveScriptPath(const FString& Path) const
{
#if IS_GAME_CLIENT
	FString Normalized = Path;
	std::replace(Normalized.begin(), Normalized.end(), '\\', '/');
	if (Normalized.rfind("LuaScripts/", 0) != 0)
	{
		Normalized = "LuaScripts/" + Normalized;
	}

	std::filesystem::path ScriptPath(FPaths::ToWide(Normalized));
	if (!ScriptPath.has_extension())
	{
		ScriptPath.replace_extension(L".lua");
	}

	return FPaths::ToUtf8(ScriptPath.generic_wstring());
#else
	std::filesystem::path InputPath(FPaths::ToWide(Path));
	if (InputPath.is_absolute())
	{
		return NormalizeScriptPath(InputPath);
	}

	const std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir());
	const std::filesystem::path ScriptDir = std::filesystem::path(FPaths::ScriptDir());

	std::filesystem::path Candidate = (RootPath / InputPath).lexically_normal();
	if (std::filesystem::exists(Candidate))
	{
		return NormalizeScriptPath(Candidate);
	}

	Candidate = (ScriptDir / InputPath).lexically_normal();
	if (!Candidate.has_extension())
	{
		Candidate.replace_extension(L".lua");
	}
	if (std::filesystem::exists(Candidate))
	{
		return NormalizeScriptPath(Candidate);
	}

	return NormalizeScriptPath(Candidate);
#endif
}

FString FLuaScriptSubsystem::ResolveModulePath(const FString& ModuleName) const
{
	FString RelativePath = ModuleName;
	std::replace(RelativePath.begin(), RelativePath.end(), '.', '/');
	std::replace(RelativePath.begin(), RelativePath.end(), '\\', '/');

#if IS_GAME_CLIENT
	std::filesystem::path ModulePath(FPaths::ToWide(RelativePath));
	if (!ModulePath.has_extension())
	{
		ModulePath.replace_extension(L".lua");
	}

	FString Candidate = FPaths::ToUtf8(ModulePath.generic_wstring());
	std::wstring DiskPath;
	FString Error;
	if (FPaths::TryResolveScriptPath(Candidate, DiskPath, &Error) && std::filesystem::exists(std::filesystem::path(DiskPath)))
	{
		return ResolveScriptPath(Candidate);
	}

	if (ModulePath.extension() == L".lua")
	{
		std::filesystem::path ModuleDir = ModulePath;
		ModuleDir.replace_extension();
		Candidate = FPaths::ToUtf8((ModuleDir / L"init.lua").generic_wstring());
		if (FPaths::TryResolveScriptPath(Candidate, DiskPath, &Error) && std::filesystem::exists(std::filesystem::path(DiskPath)))
		{
			return ResolveScriptPath(Candidate);
		}
	}

	return ResolveScriptPath(RelativePath);
#else
	std::filesystem::path ModulePath(FPaths::ToWide(RelativePath));
	if (!ModulePath.has_extension())
	{
		ModulePath.replace_extension(L".lua");
	}

	std::filesystem::path Candidate = std::filesystem::path(FPaths::ScriptDir()) / ModulePath;
	if (std::filesystem::exists(Candidate))
	{
		return NormalizeScriptPath(Candidate);
	}

	if (ModulePath.extension() == L".lua")
	{
		std::filesystem::path ModuleDir = ModulePath;
		ModuleDir.replace_extension();
		std::filesystem::path InitCandidate = std::filesystem::path(FPaths::ScriptDir()) / ModuleDir / L"init.lua";
		if (std::filesystem::exists(InitCandidate))
		{
			return NormalizeScriptPath(InitCandidate);
		}
	}

	return ResolveScriptPath(RelativePath);
#endif
}

FString FLuaScriptSubsystem::MakeAbsoluteScriptPath(const FString& NormalizedPath) const
{
#if IS_GAME_CLIENT
	std::wstring DiskPath;
	FString Error;
	if (FPaths::TryResolveScriptPath(NormalizedPath, DiskPath, &Error))
	{
		return FPaths::ToUtf8(std::filesystem::path(DiskPath).generic_wstring());
	}
	return "";
#else
	std::filesystem::path Path(FPaths::ToWide(NormalizedPath));
	if (Path.is_absolute())
	{
		return ToGenericUtf8(Path.lexically_normal());
	}
	return ToGenericUtf8((std::filesystem::path(FPaths::RootDir()) / Path).lexically_normal());
#endif
}

void FLuaScriptSubsystem::RecordIncludeDependency(const FString& IncludePath)
{
	if (DependencyContextStack.empty())
	{
		return;
	}

	FLuaDependencyContext& Context = DependencyContextStack.back();
	if (!Context.Includes)
	{
		return;
	}

	const FString NormalizedInclude = ResolveScriptPath(IncludePath);
	if (NormalizedInclude != Context.EntryScript)
	{
		Context.Includes->insert(NormalizedInclude);
	}
}

void FLuaScriptSubsystem::RebuildIncludeDependents()
{
	IncludeDependents.clear();

	for (const auto& [Script, Includes] : ScriptIncludes)
	{
		for (const FString& Include : Includes)
		{
			IncludeDependents[Include].insert(Script);
		}
	}
}

TMap<FString, FString>& FLuaScriptSubsystem::GetActiveModulePaths()
{
	if (!DependencyContextStack.empty() && DependencyContextStack.back().ModulePaths)
	{
		return *DependencyContextStack.back().ModulePaths;
	}

	return ModulePaths;
}

sol::object FLuaScriptSubsystem::IncludeFile(const FString& Path, sol::this_environment ThisEnv, sol::this_state State)
{
	sol::state_view LuaView(State);
	sol::environment Environment = GetCallerEnvironment(ThisEnv);

	const FString NormalizedPath = ResolveScriptPath(Path);
	RecordIncludeDependency(NormalizedPath);

	if (!CompileFile(LuaView, NormalizedPath) || !ExecuteScriptFile(LuaView, NormalizedPath, Environment.valid() ? &Environment : nullptr))
	{
		return sol::make_object(LuaView, false);
	}

	return sol::make_object(LuaView, true);
}

sol::object FLuaScriptSubsystem::RequireModule(const FString& ModuleName, sol::this_environment ThisEnv, sol::this_state State)
{
	sol::state_view LuaView(State);
	sol::environment Environment = GetCallerEnvironment(ThisEnv);
	sol::table Package = LuaView["package"];
	sol::table Loaded = Package["loaded"];

	sol::object CachedModule = Loaded[ModuleName];
	if (CachedModule.valid() && CachedModule.get_type() != sol::type::nil)
	{
		return CachedModule;
	}

	const FString NormalizedPath = ResolveModulePath(ModuleName);
	RecordIncludeDependency(NormalizedPath);
	TMap<FString, FString>& ActiveModulePaths = GetActiveModulePaths();
	ActiveModulePaths[ModuleName] = NormalizedPath;

	sol::object Result;
	// 실행 성공 여부를 bool로 체크하고, 결과값은 Result에 담아옵니다.
	if (!ExecuteScriptFile(LuaView, NormalizedPath, Environment.valid() ? &Environment : nullptr, &Result))
	{
		return sol::nil;
	}

	Loaded[ModuleName] = Result;
	return Result;
}
