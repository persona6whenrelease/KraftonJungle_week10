#pragma once
#include "Matrix.h"
#include "Serialization/Archive.h"

inline FArchive& operator<<(FArchive& Ar, FMatrix& Matrix)
{
	Ar.Serialize(Matrix.Data, sizeof(Matrix.Data));
	return Ar;
}
