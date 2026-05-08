#pragma once

#include "Core/CoreTypes.h"

class AActor;
class UActorComponent;

namespace ComponentReferenceUtils
{
	FString MakeComponentPath(const UActorComponent* Component);
	UActorComponent* ResolveComponentPath(AActor* Owner, const FString& Path);
}
