#include "MathUtils.h"

#include <cmath>

namespace
{
	float Fade(float T)
	{
		return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
	}

	float Grad(uint32 Hash, float X)
	{
		const float Gradient = (Hash & 1) ? 1.0f : -1.0f;
		return Gradient * X;
	}

	uint32 HashInt(int32 X, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X);
		H ^= static_cast<uint32>(Seed) * 0x9E3779B9u;
		H ^= H >> 16;
		H *= 0x7FEB352Du;
		H ^= H >> 15;
		H *= 0x846CA68Bu;
		H ^= H >> 16;
		return H;
	}

	float CubeBezierX(float T, const FVector2& P1, const FVector2& P2)
	{
		const float U = 1.0f - T;
		return 3.0f * U * U * T * P1.X + 3.0f * U * T * T * P2.X + T * T * T;
	}

	float CubeBezierXDerivative(float T, const FVector2& P1, const FVector2& P2)
	{
		const float U = 1.0f - T;
		return 3.0f * U * U * P1.X + 6.0f * U * T * (P2.X - P1.X) + 3.0f * T * T * (1.0f - P2.X);
	}
}

FVector FMath::Lerp(const FVector& A, const FVector& B, float Alpha)
{
	return A + (B - A) * Alpha;
}
FVector FMath::VInterpTo(const FVector& Current, const FVector& Target, float DeltaTime, float InterpSpeed)
{
	if (InterpSpeed <= 0.0f)
	{
		return Target;
	}

	const FVector Delta = Target - Current;
	if (Delta.IsNearlyZero())
	{
		return Target;
	}

	const float Alpha = Clamp01(DeltaTime * InterpSpeed);
	return Current + Delta * Alpha;
}

FVector2 FMath::Lerp(const FVector2& A, const FVector2& B, float Alpha)
{
	return A + (B - A) * Alpha;
}

FVector2 FMath::QuadraticBezier(const FVector2& P0, const FVector2& P1, const FVector2& P2, float T)
{
	T = Clamp01(T);
	const float U = 1.0f - T;
	return P0 * (U * U) + P1 * (2.0f * U * T) + P2 * (T * T);
}

FVector2 FMath::CubicBezier(const FVector2& P0, const FVector2& P1, const FVector2& P2, const FVector2& P3, float T)
{
	T = Clamp01(T);
	const float U = 1.0f - T;
	return P0 * (U * U * U) + P1 * (3.0f * U * U * T) + P2 * (3.0f * U * T * T) + P3 * (T * T * T);
}

float FMath::CubicBezierYFromX(float X, const FVector2& P1, const FVector2& P2, int32 Iteration)
{
	X = Clamp01(X);

	float T = X;
	for (int32 i = 0; i < Iteration; ++i)
	{
		const float CurrentX = CubeBezierX(T, P1, P2);
		const float Dx = CubeBezierXDerivative(T, P1, P2);
		if (Abs(Dx) < Epsilon)
		{
			break;
		}
		T = Clamp01(T - (CurrentX - X) / Dx);
	}

	const FVector2 P0(0.0f, 0.0f);
	const FVector2 P3(1.0f, 1.0f);
	return CubicBezier(P0, P1, P2, P3, T).Y;
}

float FMath::PerlinNoise1D(float X, int32 Seed)
{
	const int32 X0 = static_cast<int32>(std::floor(X));
	const int32 X1 = X0 + 1;
	const float LocalX = X - static_cast<float>(X0);

	const float N0 = Grad(HashInt(X0, Seed), LocalX);
	const float N1 = Grad(HashInt(X1, Seed), LocalX - 1.0f);
	const float U = Fade(LocalX);

	return Lerp(N0, N1, U) * 2.0f;
}
