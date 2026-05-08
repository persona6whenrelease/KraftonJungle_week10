#pragma once

//FBoundingBox — AABB (Axis-Aligned Bounding Box)
#include "Core/EngineTypes.h"
#include "Math/Vector.h"

class UPrimitiveComponent;
class AActor;
class UBoxComponent;
class USphereComponent;
class UCapsuleComponent;

struct FHitResult;
struct FOverlapInfo;

struct FPrimitiveCollision
{
public:
	static bool Intersect(const UPrimitiveComponent* A, const UPrimitiveComponent* B);
	static bool IntersectBroadPhase(const UPrimitiveComponent* A, const UPrimitiveComponent* B);

private:
	/**
	 * 6가지 충돌 경우.
	 * sphere-sphere, sphere-box, sphere-capsule, box-box, box-capsule, capsule-capsule
	 * 여기에 box-box는 OBB까지 고려됨
	 */
	static bool IntersectSphereSphere(const USphereComponent* A, const USphereComponent* B);
	static bool IntersectSphereBox(const USphereComponent* Sphere, const UBoxComponent* Box);
	static bool IntersectSphereCapsule(const USphereComponent* Sphere, const UCapsuleComponent* Capsule);
	static bool IntersectBoxBox(const UBoxComponent* A, const UBoxComponent* B);
	static bool IntersectBoxCapsule(const UBoxComponent* Box, const UCapsuleComponent* Capsule);
	static bool IntersectCapsuleCapsule(const UCapsuleComponent* A, const UCapsuleComponent* B);

private:
	//아래 함수들은 cpp에서 namespace로 빼두었는데 추후 충돌 관련
	//확장을 위해서는 다시 public으로 올려될 수도 있어요
	//
	//Box 충돌 관련해서 내부적으로 SAT가 호출됨
	//static bool IntersectAABBAABB(const FBoundingBox& A, const FBoundingBox& B);
	//static FVector ClosestPointOnAABB(const FVector& Point, const FBoundingBox& AABB);
	//static FVector ClosestPointOnSegment(const FVector& Point, const FVector& Start, const FVector& End);
	//static float SegmentSegmentDistSq(const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2);
};
