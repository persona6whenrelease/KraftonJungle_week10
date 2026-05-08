#pragma once

#include <cfloat>
#include "Math/Vector.h"

class AActor;
class UPrimitiveComponent;

struct FRay
{
	FVector Origin;
	FVector Direction;
};

struct FRaycastQueryParams
{
	AActor* IgnoreActor = nullptr;
	UPrimitiveComponent* IgnoreComponent = nullptr;
	bool bIgnoreHidden = true;
	bool bTraceOnlyBlocking = true;
	float MaxDistance = FLT_MAX;
};
