// LuaMathBindings.cpp

#include "LuaBindings.h"
#include "SolInclude.h"

#include "Math/Vector.h"
#include "Math/Rotator.h"

// Lua에 FVector를 등록
void RegisterFVectorBinding(sol::state& Lua)
{
	Lua.new_usertype<FVector>(
		"FVector",
		sol::constructors<FVector(), FVector(float, float, float)>(),

		"x",
		sol::property([](const FVector& V) { return V.X; }, [](FVector& V, float Value) { V.X = Value; }),

		"y",
		sol::property([](const FVector& V) { return V.Y; }, [](FVector& V, float Value) { V.Y = Value; }),

		"z",
		sol::property([](const FVector& V) { return V.Z; }, [](FVector& V, float Value) { V.Z = Value; }),

		"X", &FVector::X,
		"Y", &FVector::Y,
		"Z", &FVector::Z,

		"Length",
		[](const FVector& Self) { return Self.Length(); },

		"Normalized",
		[](const FVector& Self) { return Self.Normalized(); },

		"Dot",
		[](const FVector& Self, const FVector& Other) { return Self.Dot(Other); },

		"Cross",
		[](const FVector& Self, const FVector& Other) { return Self.Cross(Other); },

		sol::meta_function::addition,
		[](const FVector& A, const FVector& B) { return A + B; },

		sol::meta_function::subtraction,
		[](const FVector& A, const FVector& B) { return A - B; },

		sol::meta_function::multiplication,
		sol::overload([](const FVector& V, float Scalar) { return V * Scalar; }, [](float Scalar, const FVector& V) { return V * Scalar; }),

		sol::meta_function::division,
		[](const FVector& V, float Scalar) { return V / Scalar; }

	);

	Lua["VectorZero"] = FVector::ZeroVector;
	Lua["VectorOne"] = FVector::OneVector;
	Lua["VectorUp"] = FVector::UpVector;
	Lua["VectorRight"] = FVector::RightVector;
	Lua["VectorForward"] = FVector::ForwardVector;
}

void RegisterFVector4Binding(sol::state& Lua)
{
	Lua.new_usertype<FVector4>(
		"FVector4",

		sol::constructors<FVector4(), FVector4(float, float, float, float), FVector4(const FVector&, float), FVector4(const FVector&)>(),

		"x",
		sol::property([](const FVector4& V) { return V.X; }, [](FVector4& V, float Value) { V.X = Value; }),

		"y",
		sol::property([](const FVector4& V) { return V.Y; }, [](FVector4& V, float Value) { V.Y = Value; }),

		"z",
		sol::property([](const FVector4& V) { return V.Z; }, [](FVector4& V, float Value) { V.Z = Value; }),

		"w",
		sol::property([](const FVector4& V) { return V.W; }, [](FVector4& V, float Value) { V.W = Value; }),

		"r",
		sol::property([](const FVector4& V) { return V.R; }, [](FVector4& V, float Value) { V.R = Value; }),

		"g",
		sol::property([](const FVector4& V) { return V.G; }, [](FVector4& V, float Value) { V.G = Value; }),

		"b",
		sol::property([](const FVector4& V) { return V.B; }, [](FVector4& V, float Value) { V.B = Value; }),

		"a",
		sol::property([](const FVector4& V) { return V.A; }, [](FVector4& V, float Value) { V.A = Value; }),

		"X", &FVector4::X,
		"Y", &FVector4::Y,
		"Z", &FVector4::Z,
		"W", &FVector4::W,
		"R", &FVector4::R,
		"G", &FVector4::G,
		"B", &FVector4::B,
		"A", &FVector4::A
	);
}

void RegisterFRotatorBinding(sol::state& Lua)
{
	Lua.new_usertype<FRotator>(
		"FRotator",
		sol::constructors<FRotator(), FRotator(float, float, float)>(),

		"pitch",
		sol::property(
			[](const FRotator& R) { return R.Pitch; },
			[](FRotator& R, float Value) { R.Pitch = Value; }
		),

		"yaw",
		sol::property(
			[](const FRotator& R) { return R.Yaw; },
			[](FRotator& R, float Value) { R.Yaw = Value; }
		),

		"roll",
		sol::property(
			[](const FRotator& R) { return R.Roll; },
			[](FRotator& R, float Value) { R.Roll = Value; }
		),

		"Pitch", &FRotator::Pitch,
		"Yaw", &FRotator::Yaw,
		"Roll", &FRotator::Roll
	);
}
