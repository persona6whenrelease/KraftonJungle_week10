#pragma once
#include <functional>
#include <atomic>
#include <concepts>
#include <algorithm>
#include "../Core/CoreTypes.h"
#include "Delegate.h"


class FDelegateSubscriptionBox
{
private:
	struct FSubscription
	{
		uint64 LocalID;                       // Box 안에서의 고유 ID
		std::function<void()> UnsubscribeFn;
	};
	TArray<FSubscription> Subscriptions;
	uint64 NextLocalID = 1;

public:
	// 핸들 반환 (선택적으로 보관)
	template<typename TDeleg, typename TInst, typename TFunc>
	uint64 Subscribe(TDeleg& Delegate, TInst* Instance, TFunc Func , UObject* DelegateOwner)
	{
		Delegate.AddDynamic(Instance, Func);
		uint64 ID = NextLocalID++;
		uint32 OwnerUUID = DelegateOwner->GetUUID();
		Subscriptions.push_back({
			ID,
			[&Delegate, Instance,OwnerUUID]() {

				if(UObjectManager::Get().FindByUUID(OwnerUUID))
				Delegate.RemoveAllByInstance(Instance->GetUUID()); 
			}
});
		return ID;  // 보관해두면 부분 해제 가능
	}

	// 특정 구독만 해제
	void Unsubscribe(uint64 LocalID)
	{
		auto It = std::find_if(Subscriptions.begin(), Subscriptions.end(),
			[LocalID](const FSubscription& s) { return s.LocalID == LocalID; });
		if (It != Subscriptions.end())
		{
			It->UnsubscribeFn();
			Subscriptions.erase(It);
		}
	}

	~FDelegateSubscriptionBox()
	{
		for (auto& S : Subscriptions) S.UnsubscribeFn();
	}
};