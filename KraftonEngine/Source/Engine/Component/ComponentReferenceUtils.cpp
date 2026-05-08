#include "Component/ComponentReferenceUtils.h"

#include "Component/ActorComponent.h"
#include "Component/SceneComponent.h"
#include "GameFramework/AActor.h"

#include <cstring>
#include <cstdlib>
#include <sstream>

namespace
{
	constexpr const char* RootToken = "Root";
	constexpr const char* ComponentToken = "Component";

	bool BuildRootPath(const USceneComponent* Root, const USceneComponent* Target, FString& OutPath)
	{
		if (!Root || !Target)
		{
			return false;
		}

		if (Root == Target)
		{
			OutPath = RootToken;
			return true;
		}

		const TArray<USceneComponent*>& Children = Root->GetChildren();
		for (int32 Index = 0; Index < static_cast<int32>(Children.size()); ++Index)
		{
			FString ChildPath;
			if (BuildRootPath(Children[Index], Target, ChildPath))
			{
				OutPath = FString(RootToken) + "/" + std::to_string(Index);
				if (ChildPath != RootToken)
				{
					const size_t PrefixLength = std::strlen(RootToken);
					OutPath += ChildPath.substr(PrefixLength);
				}
				return true;
			}
		}

		return false;
	}

	USceneComponent* ResolveRootPath(USceneComponent* Root, const FString& Path)
	{
		if (!Root || Path.empty())
		{
			return nullptr;
		}

		std::stringstream Stream(Path);
		FString Segment;
		bool bExpectRoot = true;
		USceneComponent* Current = Root;

		while (std::getline(Stream, Segment, '/'))
		{
			if (Segment.empty())
			{
				continue;
			}

			if (bExpectRoot)
			{
				bExpectRoot = false;
				if (Segment != RootToken)
				{
					return nullptr;
				}
				continue;
			}

			char* ParseEnd = nullptr;
			const long ParsedIndex = std::strtol(Segment.c_str(), &ParseEnd, 10);
			if (ParseEnd == Segment.c_str() || *ParseEnd != '\0')
			{
				return nullptr;
			}

			const int32 ChildIndex = static_cast<int32>(ParsedIndex);
			const TArray<USceneComponent*>& Children = Current->GetChildren();
			if (ChildIndex < 0 || ChildIndex >= static_cast<int32>(Children.size()))
			{
				return nullptr;
			}

			Current = Children[ChildIndex];
			if (!Current)
			{
				return nullptr;
			}
		}

		return Current;
	}
}

namespace ComponentReferenceUtils
{
	FString MakeComponentPath(const UActorComponent* Component)
	{
		if (!Component || !Component->GetOwner())
		{
			return FString();
		}

		AActor* Owner = Component->GetOwner();
		if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			FString RootPath;
			if (BuildRootPath(Owner->GetRootComponent(), SceneComponent, RootPath))
			{
				return RootPath;
			}
		}

		const TArray<UActorComponent*>& Components = Owner->GetComponents();
		for (int32 Index = 0; Index < static_cast<int32>(Components.size()); ++Index)
		{
			if (Components[Index] == Component)
			{
				return FString(ComponentToken) + "/" + std::to_string(Index);
			}
		}

		return FString();
	}

	UActorComponent* ResolveComponentPath(AActor* Owner, const FString& Path)
	{
		if (!Owner || Path.empty())
		{
			return nullptr;
		}

		if (Path == RootToken || Path.rfind(FString(RootToken) + "/", 0) == 0)
		{
			return ResolveRootPath(Owner->GetRootComponent(), Path);
		}

		if (Path.rfind(FString(ComponentToken) + "/", 0) == 0)
		{
			const FString IndexText = Path.substr(std::strlen(ComponentToken) + 1);
			char* ParseEnd = nullptr;
			const long ParsedIndex = std::strtol(IndexText.c_str(), &ParseEnd, 10);
			if (ParseEnd == IndexText.c_str() || *ParseEnd != '\0')
			{
				return nullptr;
			}

			const int32 Index = static_cast<int32>(ParsedIndex);
			const TArray<UActorComponent*>& Components = Owner->GetComponents();
			if (Index < 0 || Index >= static_cast<int32>(Components.size()))
			{
				return nullptr;
			}
			return Components[Index];
		}

		return nullptr;
	}
}
