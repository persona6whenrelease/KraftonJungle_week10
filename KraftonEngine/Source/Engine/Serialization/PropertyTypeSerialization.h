#pragma once

#include "Core/PropertyTypes.h"
#include "Serialization/Archive.h"

inline FArchive& operator<<(FArchive& Ar, FMaterialSlot& Slot)
{
	Ar << Slot.Path;
	return Ar;
}
