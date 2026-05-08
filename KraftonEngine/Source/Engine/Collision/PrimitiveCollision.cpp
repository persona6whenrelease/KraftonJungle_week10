#include "Component/SceneComponent.h"
#include "Collision/PrimitiveCollision.h"
#include "Collision/OBB.h" //SAT
#include "Component/PrimitiveComponent.h"

#include "Component/Collision/CollisionTypes.h"
#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/SphereComponent.h"
#include "Component/Collision/CapsuleComponent.h"
#include "Math/MathUtils.h"

#include <algorithm>
#include <cmath>

namespace
{
	// 두 값 중 절대값이 더 큰 값을 반환합니다.
	// 비균등 스케일이 적용된 도형에서 가장 크게 늘어난 축을 찾을 때 사용합니다.
	float GetMaxAbs(float A, float B)
	{
		return std::max(std::abs(A), std::abs(B));
	}

	// 세 값 중 절대값이 가장 큰 값을 반환합니다.
	// 구처럼 모든 방향으로 동일한 반지름을 가져야 하는 도형이
	// 비균등 스케일을 받을 때 보수적인 충돌 반지름을 구하는 데 사용합니다.
	float GetMaxAbs(float A, float B, float C)
	{
		return std::max({ std::abs(A), std::abs(B), std::abs(C) });
	}

	// 월드 스케일이 적용된 구의 충돌 반지름을 반환합니다.
	// 구는 방향에 따른 반지름이 같아야 하므로 X/Y/Z 스케일 중 가장 큰 값을 사용해
	// 실제 메시보다 작게 판정되지 않도록 보수적으로 계산합니다.
	float GetScaledSphereRadius(const USphereComponent* Sphere)
	{
		const FVector Scale = Sphere->GetWorldScale();
		return Sphere->GetSphereRadius() * GetMaxAbs(Scale.X, Scale.Y, Scale.Z);
	}

	// 월드 스케일이 적용된 캡슐의 반지름을 반환합니다.
	// 이 엔진의 캡슐 중심축은 Z축이라고 가정하므로,
	// 캡슐의 두께에 영향을 주는 X/Y 스케일 중 더 큰 값을 반지름에 적용합니다.
	float GetScaledCapsuleRadius(const UCapsuleComponent* Capsule)
	{
		//캡슐은 중심축이 z니까 x,y로 반지름을 구함
		const FVector Scale = Capsule->GetWorldScale();
		return Capsule->GetCapsuleRadius() * GetMaxAbs(Scale.X, Scale.Y);
	}

	// BoxComponent의 월드 행렬과 로컬 Extent를 이용해 충돌용 OBB를 생성합니다.
	// 실제 OBB 수학은 OBB.h의 FOBB에 모아 둡니다.
	FOBB MakeBoxOBB(const UBoxComponent* Box)
	{
		FOBB Result;
		Result.UpdateAsOBB(Box->GetWorldMatrix(), Box->GetBoxExtent());
		return Result;
	}

	// 점에서 선분 위의 가장 가까운 점을 반환합니다.
	// 점을 선분 방향으로 투영한 뒤, 투영 비율을 [0, 1]로 제한합니다.
	FVector ClosestPointOnSegment(const FVector& Point, const FVector& Start, const FVector& End)
	{
		const FVector Segment = End - Start;
		const float SegmentLenSq = Segment.Dot(Segment);

		if (SegmentLenSq <= 1e-6f)
		{
			return Start;
		}

		float T = (Point - Start).Dot(Segment) / SegmentLenSq;
		T = Clamp(T, 0.0f, 1.0f);
		return Start + Segment * T;
	}

	// 두 AABB가 서로 겹치는지 검사합니다.
	// X/Y/Z 모든 축에서 투영 구간이 겹치면 충돌로 판정합니다.
	// 하나의 축이라도 분리되어 있으면 충돌하지 않습니다.
	bool IntersectAABBAABB(const FBoundingBox& A, const FBoundingBox& B)
	{
		return (A.Min.X <= B.Max.X && A.Max.X >= B.Min.X) &&
			(A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y) &&
			(A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z);
	}

	// 두 선분 사이의 최소 거리 제곱값을 반환합니다.
	// 각 선분 위의 가장 가까운 두 점을 매개변수 S, T로 구한 뒤 거리 제곱을 계산합니다.
	// 캡슐-캡슐 충돌처럼 중심축끼리의 거리를 구할 때 사용할 수 있습니다.
	float SegmentSegmentDistSq(
		const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2)
	{
		const FVector D1 = Q1 - P1;
		const FVector D2 = Q2 - P2;
		const FVector R = P1 - P2;

		const float A = D1.Dot(D1);
		const float E = D2.Dot(D2);
		const float F = D2.Dot(R);

		float S = 0.0f;
		float T = 0.0f;

		const float Epsilon = 1e-6f;

		if (A <= Epsilon && E <= Epsilon)
		{
			return R.Dot(R);
		}

		if (A <= Epsilon)
		{
			S = 0.0f;
			T = Clamp(F / E, 0.0f, 1.0f);
		}
		else
		{
			const float C = D1.Dot(R);

			if (E <= Epsilon)
			{
				T = 0.0f;
				S = Clamp(-C / A, 0.0f, 1.0f);
			}
			else
			{
				const float B = D1.Dot(D2);
				const float Denom = A * E - B * B;

				if (Denom > Epsilon)
				{
					S = Clamp((B * F - C * E) / Denom, 0.0f, 1.0f);
				}
				else
				{
					S = 0.0f;
				}

				const float TNom = B * S + F;

				if (TNom < 0.0f)
				{
					T = 0.0f;
					S = Clamp(-C / A, 0.0f, 1.0f);
				}
				else if (TNom > E)
				{
					T = 1.0f;
					S = Clamp((B - C) / A, 0.0f, 1.0f);
				}
				else
				{
					T = TNom / E;
				}
			}
		}

		const FVector C1 = P1 + D1 * S;
		const FVector C2 = P2 + D2 * T;
		return FVector::DistSquared(C1, C2);
	}
}

bool FPrimitiveCollision::IntersectBroadPhase(const UPrimitiveComponent* A, const UPrimitiveComponent* B)
{
	//BVH?
	return IntersectAABBAABB(A->GetWorldAABB(), B->GetWorldAABB());
}

bool FPrimitiveCollision::Intersect(const UPrimitiveComponent* A, const UPrimitiveComponent* B)
{
	//일단 for문을 돌면서 BoradPhase를 미리 돌아 후보군을 추려서 함수 호출 비용을 아낄지
	//아니면 2중 for문을 돌면서 BoradPhase 내에 없으면 바로 return할 지 고민해볼 것.
	//if (!IntersectBroadPhase(A, B))
	//{
	//	return false;
	//}

	const ECollisionShapeType TypeA = A->GetCollisionShapeType();
	const ECollisionShapeType TypeB = B->GetCollisionShapeType();

	// 도형 타입이 3개이므로 순서 있는 조합은 3+9개지만,
	// 충돌 판정은 A-B와 B-A가 사실 동일하므로 실제 narrow phase 함수는 9개만 필요합니다.
	if (TypeA == ECollisionShapeType::Sphere && TypeB == ECollisionShapeType::Sphere)
	{
		return IntersectSphereSphere(static_cast<const USphereComponent*>(A), static_cast<const USphereComponent*>(B));
	}
	if (TypeA == ECollisionShapeType::Box && TypeB == ECollisionShapeType::Box)
	{
		return IntersectBoxBox(static_cast<const UBoxComponent*>(A), static_cast<const UBoxComponent*>(B));
	}
	if (TypeA == ECollisionShapeType::Capsule && TypeB == ECollisionShapeType::Capsule)
	{
		return IntersectCapsuleCapsule(static_cast<const UCapsuleComponent*>(A), static_cast<const UCapsuleComponent*>(B));
	}

	//같은 함수를 인자 순서만 바꿔 호출.
	if (TypeA == ECollisionShapeType::Sphere && TypeB == ECollisionShapeType::Box)
	{
		return IntersectSphereBox(static_cast<const USphereComponent*>(A), static_cast<const UBoxComponent*>(B));
	}
	if (TypeA == ECollisionShapeType::Box && TypeB == ECollisionShapeType::Sphere)
	{
		return IntersectSphereBox(static_cast<const USphereComponent*>(B), static_cast<const UBoxComponent*>(A));
	}

	if (TypeA == ECollisionShapeType::Sphere && TypeB == ECollisionShapeType::Capsule)
	{
		return IntersectSphereCapsule(static_cast<const USphereComponent*>(A), static_cast<const UCapsuleComponent*>(B));
	}
	if (TypeA == ECollisionShapeType::Capsule && TypeB == ECollisionShapeType::Sphere)
	{
		return IntersectSphereCapsule(static_cast<const USphereComponent*>(B), static_cast<const UCapsuleComponent*>(A));
	}

	if (TypeA == ECollisionShapeType::Box && TypeB == ECollisionShapeType::Capsule)
	{
		return IntersectBoxCapsule(static_cast<const UBoxComponent*>(A), static_cast<const UCapsuleComponent*>(B));
	}
	if (TypeA == ECollisionShapeType::Capsule && TypeB == ECollisionShapeType::Box)
	{
		return IntersectBoxCapsule(static_cast<const UBoxComponent*>(B), static_cast<const UCapsuleComponent*>(A));
	}

	return false;
}

bool FPrimitiveCollision::IntersectSphereSphere(const USphereComponent* A, const USphereComponent* B)
{
	const FVector CenterA = A->GetWorldLocation();
	const FVector CenterB = B->GetWorldLocation();
	const float RadiusA = GetScaledSphereRadius(A);
	const float RadiusB = GetScaledSphereRadius(B);
	const float Sum = RadiusA + RadiusB;
	return FVector::DistSquared(CenterA, CenterB) <= Sum * Sum;
}

bool FPrimitiveCollision::IntersectBoxBox(const UBoxComponent* A, const UBoxComponent* B)
{
	// AABB broad phase를 먼저 통과한 뒤 실제 박스 방향은 OBB SAT로 판정합니다.
	if (!IntersectAABBAABB(A->GetWorldAABB(), B->GetWorldAABB()))
	{
		return false;
	}

	return MakeBoxOBB(A).IntersectOBB(MakeBoxOBB(B));
}

bool FPrimitiveCollision::IntersectSphereBox(const USphereComponent* Sphere, const UBoxComponent* Box)
{
	const FVector Center = Sphere->GetWorldLocation();
	const float Radius = GetScaledSphereRadius(Sphere);
	const FVector Closest = MakeBoxOBB(Box).ClosestPoint(Center);
	return FVector::DistSquared(Center, Closest) <= Radius * Radius;
}

bool FPrimitiveCollision::IntersectSphereCapsule(const USphereComponent* Sphere, const UCapsuleComponent* Capsule)
{
	const FVector Center = Sphere->GetWorldLocation();
	const float SphereRadius = GetScaledSphereRadius(Sphere);
	const float CapsuleRadius = GetScaledCapsuleRadius(Capsule);

	FVector P0;
	FVector P1;
	Capsule->GetSegmentPoints(P0, P1);

	const FVector Closest = ClosestPointOnSegment(Center, P0, P1);
	const float Sum = SphereRadius + CapsuleRadius;
	return FVector::DistSquared(Center, Closest) <= Sum * Sum;
}

bool FPrimitiveCollision::IntersectBoxCapsule(const UBoxComponent* Box, const UCapsuleComponent* Capsule)
{
	const float CapsuleRadius = GetScaledCapsuleRadius(Capsule);

	FVector P0;
	FVector P1;
	Capsule->GetSegmentPoints(P0, P1);

	return MakeBoxOBB(Box).SegmentDistSq(P0, P1) <= CapsuleRadius * CapsuleRadius;
}

bool FPrimitiveCollision::IntersectCapsuleCapsule(const UCapsuleComponent* A, const UCapsuleComponent* B)
{
	FVector A0;
	FVector A1;
	FVector B0;
	FVector B1;

	A->GetSegmentPoints(A0, A1);
	B->GetSegmentPoints(B0, B1);

	const float RadiusSum = GetScaledCapsuleRadius(A) + GetScaledCapsuleRadius(B);
	return SegmentSegmentDistSq(A0, A1, B0, B1) <= RadiusSum * RadiusSum;
}
