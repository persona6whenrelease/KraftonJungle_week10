#pragma once
#include <functional>
#include <atomic>
#include <concepts>
#include <algorithm>
#include "../Core/CoreTypes.h"


#define DECLARE_DELEGATE(Name , ...) \
	TDelegate<__VA_ARGS__> Name;

/*
Delegate 등록 예시
	OnChanged.Add([]()
	{
		// handler body
	});
*/


// AddDynamic에 넘길 수 있는 T의 조건: GetUUID()가 uint32로 변환 가능한 값을 반환해야 함
// RemoveAllByInstance가 instance UUID를 키로 핸들러를 추적하기 위해 필요
template<class T>
concept HasGetUUID = requires(T v)
{
	{ v.GetUUID() } -> std::convertible_to<uint32>;
};

template<class... Args>
class TDelegate
{
public:
	// 구체적인 instance가 다를 수 있지만 function의 type이 void return type, Args... parameter인 함수를 모두 지칭하는 Type
	// Delegate가 가지는 ID - function pair
	using FunctionType = std::function<void(Args...)>;
	using HandlerType = TPair<uint32, FunctionType>;

	TDelegate() = default;
	TDelegate(const TDelegate& Other)
		: Handlers(Other.Handlers)
		, InstanceHandlerIDMap(Other.InstanceHandlerIDMap)
		, NextID(Other.NextID.load())
	{
	}

	TDelegate& operator=(const TDelegate& Other)
	{
		if (this != &Other)
		{
			Handlers = Other.Handlers;
			InstanceHandlerIDMap = Other.InstanceHandlerIDMap;
			NextID.store(Other.NextID.load());
		}
		return *this;
	}


	TDelegate& operator=(TDelegate&& Other) noexcept
	{
		if (this != &Other)
		{
			Handlers = std::move(Other.Handlers);
			InstanceHandlerIDMap = std::move(Other.InstanceHandlerIDMap);
			NextID.store(Other.NextID.load());
		}
		return *this;
	}

	TDelegate(TDelegate&& Other) noexcept
		: Handlers(std::move(Other.Handlers))
		, InstanceHandlerIDMap(std::move(Other.InstanceHandlerIDMap))
		, NextID(Other.NextID.load())
	{
	}

	// instance와 상관없는 normal function 등록
	// 반환된 HandlerID를 보관해야 Remove 가능
	uint32 Add(const FunctionType& handler)
	{
		uint32 NewHandlerID = NextID.fetch_add(1);
		Handlers.push_back(HandlerType(NewHandlerID, handler));
		return NewHandlerID;
	}

	uint32 Add(const FunctionType& handler, uint32 InstanceUUID)
	{
		uint32 NewHandlerID = Add(handler);
		InstanceHandlerIDMap[InstanceUUID].push_back(NewHandlerID);
		return NewHandlerID;
	}

	// instance 정보를 함께 담아가는 dynamic function 등록
	// HandlerID 반환 + InstanceHandlerIDMap에 UUID 기준으로 ID 추적
	template<HasGetUUID T>
	uint32 AddDynamic(T* Instance, void (T::* Function)(Args...))
	{
		uint32 NewHandlerID = NextID.fetch_add(1);
		uint32 InstanceUUID = Instance->GetUUID();

		auto InstanceFunction = [Instance, Function](const Args&... args) {
			(Instance->*Function)(args...);
			};

		Handlers.push_back(HandlerType(NewHandlerID, InstanceFunction));
		InstanceHandlerIDMap[InstanceUUID].push_back(NewHandlerID);

		return NewHandlerID;
	}

	void BroadCast(const Args&... args)
	{
		if (Handlers.empty())
		{
			return;
		}

		const TArray<HandlerType> HandlersSnapshot = Handlers;
		for (const auto& handler : HandlersSnapshot)
		{
			const auto It = std::find_if(Handlers.begin(), Handlers.end(),
				[HandlerID = handler.first](const HandlerType& RegisteredHandler)
				{
					return RegisteredHandler.first == HandlerID;
				});

			if (It != Handlers.end() && handler.second)
			{
				handler.second(args...);
			}
		}
	}

	// HandlerID 단위 통합 Remove
	// InstanceHandlerIDMap은 lazy cleanup 정책이므로 건드리지 않음
	// (stale ID는 RemoveAllByInstance 시 자연스럽게 무시됨)
	void Remove(uint32 HandlerID)
	{
		std::erase_if(Handlers, [HandlerID](const HandlerType& h) {
			return h.first == HandlerID;
			});
	}

	// instance UUID에 등록된 모든 핸들러 일괄 제거
	// AddDynamic으로 등록한 핸들러만 대상 (Add로 등록한 일반 함수는 영향 없음)
	void RemoveAllByInstance(uint32 InstanceUUID)
	{
		auto It = InstanceHandlerIDMap.find(InstanceUUID);
		if (It == InstanceHandlerIDMap.end())
		{
			return;
		}

		const auto& IDsToRemove = It->second;

		// Handlers에서 해당 instance의 모든 핸들러 ID 제거
		// 시간 복잡도 O(N*M): N = Handlers 크기, M = IDsToRemove 크기
		// M이 커질 경우 unordered_set으로 변환하여 O(N)으로 개선 가능
		std::erase_if(Handlers, [&IDsToRemove](const HandlerType& h) {
			return std::find(IDsToRemove.begin(), IDsToRemove.end(), h.first)
				!= IDsToRemove.end();
			});

		InstanceHandlerIDMap.erase(It);
	}

private:
	// 단일 스레드 사용 가정. NextID는 atomic이지만 Handlers/InstanceHandlerIDMap은 보호되지 않음
	// 멀티스레드 확장 시 별도 동기화 필요
	TArray<HandlerType> Handlers;

	// instance UUID → HandlerID 목록
	// AddDynamic 등록 시에만 채워지며, Remove(HandlerID)는 lazy cleanup 정책으로 건드리지 않음
	TMap<uint32, TArray<uint32>> InstanceHandlerIDMap;

	// 0은 invalid sentinel로 예약 (호출자가 "미등록 상태"를 0으로 표현)
	std::atomic<uint32> NextID{ 1 };
};
