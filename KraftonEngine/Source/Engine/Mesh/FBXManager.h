#pragma once
#include "Core/CoreTypes.h"
#include "Mesh/SkeletalMeshAsset.h"

class USkeletalMesh;

class FFBXManager
{
public:
	// 파일 경로를 넘기면 캐시를 확인하고 로드해주는 정적(Static) 함수
	static USkeletalMesh* LoadSkeletalMesh(const FString& FilePath);

	// 엔진 종료 시 캐싱된 모든 메모리를 안전하게 해제하는 함수
	static void ReleaseAll();

private:
	// [캐시 창고] 파일 경로(Key)와 로드된 결과물 에셋(Value)을 짝지어 저장
	static TMap<FString, USkeletalMesh*> SkeletalMeshCache;
};