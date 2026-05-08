#pragma once

#include "Core/CoreTypes.h"

#include <string>

namespace json
{
	class JSON;
}

struct FCurveAsset
{
	int32 Version = 1;
	int32 PresetIndex = -1;
	float ControlPoints[4] = { 0.390f, 0.575f, 0.565f, 1.000f };
};

class FCurveSaveManager
{
public:
	static constexpr const wchar_t* CurveExtension = L".curve";
	static constexpr int32 CurrentVersion = 1;

	static std::wstring GetCurveDirectory();
	static FCurveAsset MakeDefaultBezier();

	static json::JSON SerializeCurve(const FCurveAsset& Curve);
	static bool DeserializeCurve(json::JSON& Root, FCurveAsset& OutCurve, FString* OutError = nullptr);

	static bool SaveToFile(const FCurveAsset& Curve, const FString& CurveNameOrPath);
	static bool LoadFromFile(const FString& CurveNameOrPath, FCurveAsset& OutCurve, FString* OutError = nullptr);
};
