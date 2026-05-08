#pragma once
#include "Core/EngineTypes.h"
#include "Math/MathUtils.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

#include <algorithm>
#include <cmath>

struct FOBB
{
	FVector Center = { 0, 0, 0 };
	FVector Extent { 0.5f, 0.5f, 0.5f };
	FRotator Rotation = { 0, 0, 0 };

	static constexpr float Epsilon = 1e-6f;
	
	void ApplyTransform(FMatrix Matrix)
	{
		Center = Center * Matrix;
		
		FVector X = Rotation.GetForwardVector() * Extent.X;
		FVector Y = Rotation.GetRightVector() * Extent.Y;
		FVector Z = Rotation.GetUpVector() * Extent.Z;

		Matrix.M[3][0] = 0; Matrix.M[3][1] = 0; Matrix.M[3][2] = 0;

		X = X * Matrix;
		Y = Y * Matrix;
		Z = Z * Matrix;

		Extent.X = X.Length();
		Extent.Y = Y.Length();
		Extent.Z = Z.Length();

		FMatrix RotMat;
		RotMat.SetAxes(X.Normalized(), Y.Normalized(), Z.Normalized());
		Rotation = RotMat.ToRotator();
	}

	void UpdateAsOBB(const FMatrix& Matrix)
	{
		Center = Matrix.GetLocation();
		Rotation = Matrix.ToRotator();
		
		FVector Scale = Matrix.GetScale();
		Extent = Scale * 0.5f;
	}

	// BoxComponent처럼 로컬 half extent가 따로 있는 박스를 월드 OBB로 변환합니다.
	// 기존 UpdateAsOBB(Matrix)는 [-0.5, 0.5] 단위 박스용이므로 유지합니다.
	void UpdateAsOBB(const FMatrix& Matrix, const FVector& LocalExtent)
	{
		const FVector ScaledAxes[3] = {
			GetMatrixAxis(Matrix, 0),
			GetMatrixAxis(Matrix, 1),
			GetMatrixAxis(Matrix, 2)
		};
		const FVector FallbackAxes[3] = {
			FVector(1.0f, 0.0f, 0.0f),
			FVector(0.0f, 1.0f, 0.0f),
			FVector(0.0f, 0.0f, 1.0f)
		};

		Center = Matrix.GetLocation();
		Extent = FVector(
			LocalExtent.X * ScaledAxes[0].Length(),
			LocalExtent.Y * ScaledAxes[1].Length(),
			LocalExtent.Z * ScaledAxes[2].Length());

		FMatrix RotationMatrix;
		RotationMatrix.SetAxes(
			GetSafeNormalizedAxis(ScaledAxes[0], FallbackAxes[0]),
			GetSafeNormalizedAxis(ScaledAxes[1], FallbackAxes[1]),
			GetSafeNormalizedAxis(ScaledAxes[2], FallbackAxes[2]));
		Rotation = RotationMatrix.ToRotator();
	}

	void GetAxes(FVector (&OutAxes)[3]) const
	{
		OutAxes[0] = Rotation.GetForwardVector().Normalized();
		OutAxes[1] = Rotation.GetRightVector().Normalized();
		OutAxes[2] = Rotation.GetUpVector().Normalized();
	}

	FVector WorldToLocal(const FVector& Point) const
	{
		FVector Axes[3];
		GetAxes(Axes);
		return WorldToLocal(Point, Axes);
	}

	FVector WorldToLocal(const FVector& Point, const FVector (&Axes)[3]) const
	{
		const FVector Delta = Point - Center;
		return FVector(
			Delta.Dot(Axes[0]),
			Delta.Dot(Axes[1]),
			Delta.Dot(Axes[2]));
	}

	FVector ClosestPoint(const FVector& Point) const
	{
		FVector Axes[3];
		GetAxes(Axes);

		const FVector Local = WorldToLocal(Point, Axes);
		FVector Closest = Center;
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			const float Distance = Clamp(Local.Data[Axis], -Extent.Data[Axis], Extent.Data[Axis]);
			Closest += Axes[Axis] * Distance;
		}

		return Closest;
	}

	float SegmentDistSq(const FVector& Start, const FVector& End) const
	{
		FVector Axes[3];
		GetAxes(Axes);

		const FVector LocalStart = WorldToLocal(Start, Axes);
		const FVector LocalEnd = WorldToLocal(End, Axes);
		const FBoundingBox LocalBox(Extent * -1.0f, Extent);

		return SegmentAABBDistSq(LocalStart, LocalEnd, LocalBox);
	}

	bool IntersectOBB(const FOBB& Other) const
	{
		FVector AxesA[3];
		FVector AxesB[3];
		GetAxes(AxesA);
		Other.GetAxes(AxesB);

		float R[3][3];
		float AbsR[3][3];
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				R[i][j] = AxesA[i].Dot(AxesB[j]);
				AbsR[i][j] = std::abs(R[i][j]) + Epsilon;
			}
		}

		const FVector Translation = Other.Center - Center;
		const FVector T(
			Translation.Dot(AxesA[0]),
			Translation.Dot(AxesA[1]),
			Translation.Dot(AxesA[2]));

		for (int i = 0; i < 3; ++i)
		{
			const float RA = Extent.Data[i];
			const float RB =
				Other.Extent.X * AbsR[i][0] +
				Other.Extent.Y * AbsR[i][1] +
				Other.Extent.Z * AbsR[i][2];
			if (std::abs(T.Data[i]) > RA + RB)
			{
				return false;
			}
		}

		for (int j = 0; j < 3; ++j)
		{
			const float RA =
				Extent.X * AbsR[0][j] +
				Extent.Y * AbsR[1][j] +
				Extent.Z * AbsR[2][j];
			const float RB = Other.Extent.Data[j];
			const float Distance = std::abs(
				T.X * R[0][j] +
				T.Y * R[1][j] +
				T.Z * R[2][j]);
			if (Distance > RA + RB)
			{
				return false;
			}
		}

		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				const float RA =
					Extent.Data[(i + 1) % 3] * AbsR[(i + 2) % 3][j] +
					Extent.Data[(i + 2) % 3] * AbsR[(i + 1) % 3][j];
				const float RB =
					Other.Extent.Data[(j + 1) % 3] * AbsR[i][(j + 2) % 3] +
					Other.Extent.Data[(j + 2) % 3] * AbsR[i][(j + 1) % 3];
				const float Distance = std::abs(
					T.Data[(i + 2) % 3] * R[(i + 1) % 3][j] -
					T.Data[(i + 1) % 3] * R[(i + 2) % 3][j]);
				if (Distance > RA + RB)
				{
					return false;
				}
			}
		}

		return true;
	}
	
	// Decal receiver narrow phase에서 사용하는 OBB vs AABB SAT 판정입니다.
	bool IntersectOBBAABB(const FBoundingBox& AABB) const
	{
		const FVector AABBCenter = AABB.GetCenter();
		const FVector AABBExtent = AABB.GetExtent();
		FVector OBBAxes[3];
		GetAxes(OBBAxes);
		const FVector AABBAxes[3] = {
			FVector(1.0f, 0.0f, 0.0f),
			FVector(0.0f, 1.0f, 0.0f),
			FVector(0.0f, 0.0f, 1.0f)
		};

		float R[3][3];
		float AbsR[3][3];
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				R[i][j] = OBBAxes[i].Dot(AABBAxes[j]);
				AbsR[i][j] = std::abs(R[i][j]) + Epsilon;
			}
		}

		const FVector Translation = AABBCenter - Center;
		const FVector T(
			Translation.Dot(OBBAxes[0]),
			Translation.Dot(OBBAxes[1]),
			Translation.Dot(OBBAxes[2]));

		float ra = 0.0f;
		float rb = 0.0f;

		for (int i = 0; i < 3; ++i)
		{
			ra = Extent.Data[i];
			rb = AABBExtent.X * AbsR[i][0] + AABBExtent.Y * AbsR[i][1] + AABBExtent.Z * AbsR[i][2];
			if (std::abs(T.Data[i]) > ra + rb)
			{
				return false;
			}
		}

		for (int j = 0; j < 3; ++j)
		{
			ra = Extent.X * AbsR[0][j] + Extent.Y * AbsR[1][j] + Extent.Z * AbsR[2][j];
			rb = AABBExtent.Data[j];
			const float Distance = std::abs(T.X * R[0][j] + T.Y * R[1][j] + T.Z * R[2][j]);
			if (Distance > ra + rb)
			{
				return false;
			}
		}

		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				ra = Extent.Data[(i + 1) % 3] * AbsR[(i + 2) % 3][j]
					+ Extent.Data[(i + 2) % 3] * AbsR[(i + 1) % 3][j];
				rb = AABBExtent.Data[(j + 1) % 3] * AbsR[i][(j + 2) % 3]
					+ AABBExtent.Data[(j + 2) % 3] * AbsR[i][(j + 1) % 3];
				const float Distance = std::abs(
					T.Data[(i + 2) % 3] * R[(i + 1) % 3][j]
					- T.Data[(i + 1) % 3] * R[(i + 2) % 3][j]);
				if (Distance > ra + rb)
				{
					return false;
				}
			}
		}

		return true;
	}

private:
	static FVector GetMatrixAxis(const FMatrix& Matrix, int Axis)
	{
		return FVector(Matrix.M[Axis][0], Matrix.M[Axis][1], Matrix.M[Axis][2]);
	}

	static FVector GetSafeNormalizedAxis(const FVector& Axis, const FVector& Fallback)
	{
		const float Length = Axis.Length();
		if (Length <= Epsilon)
		{
			return Fallback;
		}

		return Axis / Length;
	}

	static bool SegmentIntersectsAABB(const FVector& Start, const FVector& End, const FBoundingBox& AABB)
	{
		const FVector Dir = End - Start;
		float TMin = 0.0f;
		float TMax = 1.0f;

		for (int Axis = 0; Axis < 3; ++Axis)
		{
			const float S = Start.Data[Axis];
			const float D = Dir.Data[Axis];
			const float Min = AABB.Min.Data[Axis];
			const float Max = AABB.Max.Data[Axis];

			if (std::abs(D) <= Epsilon)
			{
				if (S < Min || S > Max)
				{
					return false;
				}
				continue;
			}

			const float InvD = 1.0f / D;
			float T1 = (Min - S) * InvD;
			float T2 = (Max - S) * InvD;

			if (T1 > T2)
			{
				std::swap(T1, T2);
			}

			TMin = (std::max)(TMin, T1);
			TMax = (std::min)(TMax, T2);

			if (TMin > TMax)
			{
				return false;
			}
		}

		return true;
	}

	static float PointAABBDistSq(const FVector& Point, const FBoundingBox& AABB)
	{
		const FVector Closest(
			Clamp(Point.X, AABB.Min.X, AABB.Max.X),
			Clamp(Point.Y, AABB.Min.Y, AABB.Max.Y),
			Clamp(Point.Z, AABB.Min.Z, AABB.Max.Z));

		return FVector::DistSquared(Point, Closest);
	}

	static float SegmentAABBDistSq(const FVector& Start, const FVector& End, const FBoundingBox& AABB)
	{
		if (SegmentIntersectsAABB(Start, End, AABB))
		{
			return 0.0f;
		}

		const FVector Dir = End - Start;
		const float SegmentLenSq = Dir.Dot(Dir);

		if (SegmentLenSq <= Epsilon)
		{
			return PointAABBDistSq(Start, AABB);
		}

		float Breaks[8];
		int BreakCount = 0;

		auto AddBreak = [&](float T)
		{
			if (T < 0.0f || T > 1.0f)
			{
				return;
			}

			Breaks[BreakCount++] = T;
		};

		AddBreak(0.0f);
		AddBreak(1.0f);

		for (int Axis = 0; Axis < 3; ++Axis)
		{
			const float S = Start.Data[Axis];
			const float D = Dir.Data[Axis];

			if (std::abs(D) <= Epsilon)
			{
				continue;
			}

			AddBreak((AABB.Min.Data[Axis] - S) / D);
			AddBreak((AABB.Max.Data[Axis] - S) / D);
		}

		std::sort(Breaks, Breaks + BreakCount);

		int UniqueCount = 0;
		for (int i = 0; i < BreakCount; ++i)
		{
			if (UniqueCount == 0 || std::abs(Breaks[i] - Breaks[UniqueCount - 1]) > Epsilon)
			{
				Breaks[UniqueCount++] = Breaks[i];
			}
		}

		auto Eval = [&](float T)
		{
			return PointAABBDistSq(Start + Dir * T, AABB);
		};

		float BestDistSq = (std::min)(Eval(0.0f), Eval(1.0f));

		for (int i = 0; i < UniqueCount - 1; ++i)
		{
			const float Left = Breaks[i];
			const float Right = Breaks[i + 1];

			if (Right - Left <= Epsilon)
			{
				continue;
			}

			const float Mid = (Left + Right) * 0.5f;
			const FVector MidPoint = Start + Dir * Mid;

			float A = 0.0f;
			float B = 0.0f;

			for (int Axis = 0; Axis < 3; ++Axis)
			{
				const float P = MidPoint.Data[Axis];

				float Bound = 0.0f;
				bool bOutside = false;

				if (P < AABB.Min.Data[Axis])
				{
					Bound = AABB.Min.Data[Axis];
					bOutside = true;
				}
				else if (P > AABB.Max.Data[Axis])
				{
					Bound = AABB.Max.Data[Axis];
					bOutside = true;
				}

				if (bOutside)
				{
					const float S = Start.Data[Axis];
					const float D = Dir.Data[Axis];

					A += D * D;
					B += D * (S - Bound);
				}
			}

			if (A <= Epsilon)
			{
				BestDistSq = (std::min)(BestDistSq, Eval(Mid));
				continue;
			}

			float T = -B / A;
			T = Clamp(T, Left, Right);

			BestDistSq = (std::min)(BestDistSq, Eval(T));
			BestDistSq = (std::min)(BestDistSq, Eval(Left));
			BestDistSq = (std::min)(BestDistSq, Eval(Right));
		}

		return BestDistSq;
	}
};
