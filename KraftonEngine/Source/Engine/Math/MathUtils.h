#pragma once

#include <cmath>
#include "Core/CoreTypes.h"
#include "Vector.h"

namespace FMath
{
	constexpr float Pi = 3.14159265358979323846f;
	constexpr float TwoPi = Pi * 2.0f;
	constexpr float HalfPi = Pi * 0.5f;
	constexpr float DegToRad = Pi / 180.0f;
	constexpr float RadToDeg = 180.0f / Pi;
	constexpr float Epsilon = 1e-4f;

	template<typename T>
	inline T Min(const T& A, const T& B) { return (A < B) ? A : B; }

	template<typename T>
	inline T Max(const T& A, const T& B) { return (A > B) ? A : B; }

	inline float Abs(float Val) { return (Val < 0.0f) ? -Val : Val; }
	
	inline float Clamp(float Val, float Lo, float Hi)
	{
		if (Val >= Hi) return Hi;
		if (Val <= Lo) return Lo;
		return Val;
	}

	inline float Clamp01(float Val) { return Clamp(Val, 0.0f, 1.0f); }

	inline float Lerp(float A, float B, float Alpha) { return A + (B - A) * Alpha; }

	inline float SmoothStep(float T)
	{
		T = Clamp01(T);
		return T * T * (3.0f - 2.0f * T);
	}

	inline float EaseInQuad(float T)
	{
		T = Clamp01(T);
		return T * T;
	}

	inline float EaseOutQuad(float T)
	{
		T = Clamp01(T);
		return 1.0f - (1.0f - T) * (1.0f - T);
	}

	inline float EaseInOut(float T)
	{
		return SmoothStep(T);
	}

	inline float EaseInOutCubic(float T)
	{
		T = Clamp01(T);
		return T < 0.5f ? 4.0f * T * T * T : 1.0f - std::pow(-2.0f * T + 2.0f, 3.0f) * 0.5f;
	}

	inline float ExponentialAlpha(float Speed, float DeltaTime)
	{
		if (Speed <= 0.0f || DeltaTime <= 0.0f)
		{
			return 1.0f;
		}
		return 1.0f - std::exp(-Speed * DeltaTime);
	}

	inline float FInterpTo(float Current, float Target, float DeltaTime, float InterpSpeed)
	{
		if (InterpSpeed <= 0.0f)
		{
			return Target;
		}

		const float Delta = Target - Current;
		if (Abs(Delta) <= Epsilon)
		{
			return Target;
		}

		const float Alpha = Clamp01(DeltaTime * InterpSpeed);
		return Current + Delta * Alpha;
	}

	FVector Lerp(const FVector& A, const FVector& B, float Alpha);
	FVector VInterpTo(const FVector& Current, const FVector& Target, float DeltaTime, float InterpSpeed);

	FVector2 Lerp(const FVector2& A, const FVector2& B, float Alpha);
	FVector2 QuadraticBezier(const FVector2& P0, const FVector2& P1, const FVector2& P2, float T);
	FVector2 CubicBezier(const FVector2& P0, const FVector2& P1, const FVector2& P2, const FVector2& P3, float T);
	float CubicBezierYFromX(float X, const FVector2& P1, const FVector2& P2, int32 Iteration = 6);
	float PerlinNoise1D(float X, int32 Seed);
}

// 기존 매크로 호환 — 이행 완료 후 제거
#define M_PI FMath::Pi
#define DEG_TO_RAD FMath::DegToRad
#define RAD_TO_DEG FMath::RadToDeg
#define EPSILON FMath::Epsilon

// 기존 전역 Clamp 호환
inline float Clamp(float val, float lo, float hi) { return FMath::Clamp(val, lo, hi); }
