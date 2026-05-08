#include "Serialization/CurveSaveManager.h"

#include "Platform/Paths.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
	constexpr const char* CurveType = "KraftonCurve";
	constexpr const char* CurveKind = "CubicBezier01";

	namespace CurveKey
	{
		constexpr const char* Type = "Type";
		constexpr const char* Version = "Version";
		constexpr const char* Kind = "Kind";
		constexpr const char* PresetIndex = "PresetIndex";
		constexpr const char* ControlPoints = "ControlPoints";
	}

	void SetError(FString* OutError, const FString& Error)
	{
		if (OutError)
		{
			*OutError = Error;
		}
	}

	std::filesystem::path ResolveCurveFilePath(const FString& CurveNameOrPath)
	{
		std::wstring WideInput = FPaths::ToWide(CurveNameOrPath);
		std::replace(WideInput.begin(), WideInput.end(), L'\\', L'/');

		std::filesystem::path InputPath(WideInput);
		if (InputPath.has_parent_path())
		{
			if (!InputPath.has_extension())
			{
				InputPath += FCurveSaveManager::CurveExtension;
			}

			if (InputPath.is_absolute())
			{
				return InputPath.lexically_normal();
			}

			return (std::filesystem::path(FPaths::RootDir()) / InputPath).lexically_normal();
		}

		std::wstring FileName = WideInput;
		std::filesystem::path FilePath(FileName);
		if (!FilePath.has_extension())
		{
			FileName += FCurveSaveManager::CurveExtension;
		}

		return std::filesystem::path(FCurveSaveManager::GetCurveDirectory()) / FileName;
	}

	bool ReadTextFile(const std::filesystem::path& FilePath, std::string& OutContent)
	{
		std::ifstream File(FilePath);
		if (!File.is_open())
		{
			return false;
		}

		OutContent.assign(
			std::istreambuf_iterator<char>(File),
			std::istreambuf_iterator<char>());
		return true;
	}

	bool ReadNumber(json::JSON& Value, float& OutValue)
	{
		const json::JSON::Class Type = Value.JSONType();
		if (Type == json::JSON::Class::Floating)
		{
			OutValue = static_cast<float>(Value.ToFloat());
			return true;
		}
		if (Type == json::JSON::Class::Integral)
		{
			OutValue = static_cast<float>(Value.ToInt());
			return true;
		}

		return false;
	}

	bool ReadInt(json::JSON& Value, int32& OutValue)
	{
		if (Value.JSONType() != json::JSON::Class::Integral)
		{
			return false;
		}

		OutValue = static_cast<int32>(Value.ToInt());
		return true;
	}
}

std::wstring FCurveSaveManager::GetCurveDirectory()
{
	return FPaths::RootDir() + L"Curve\\";
}

FCurveAsset FCurveSaveManager::MakeDefaultBezier()
{
	FCurveAsset Curve;
	Curve.Version = CurrentVersion;
	Curve.PresetIndex = 9;
	Curve.ControlPoints[0] = 0.390f;
	Curve.ControlPoints[1] = 0.575f;
	Curve.ControlPoints[2] = 0.565f;
	Curve.ControlPoints[3] = 1.000f;
	return Curve;
}

json::JSON FCurveSaveManager::SerializeCurve(const FCurveAsset& Curve)
{
	using namespace json;

	JSON Root = Object();
	Root[CurveKey::Type] = CurveType;
	Root[CurveKey::Version] = CurrentVersion;
	Root[CurveKey::Kind] = CurveKind;
	Root[CurveKey::PresetIndex] = Curve.PresetIndex;

	JSON Points = Array();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Points.append(static_cast<double>(Curve.ControlPoints[Index]));
	}
	Root[CurveKey::ControlPoints] = Points;

	return Root;
}

bool FCurveSaveManager::DeserializeCurve(json::JSON& Root, FCurveAsset& OutCurve, FString* OutError)
{
	if (Root.IsNull() || Root.JSONType() != json::JSON::Class::Object)
	{
		SetError(OutError, "Curve JSON root must be an object.");
		return false;
	}

	if (!Root.hasKey(CurveKey::Type) || Root[CurveKey::Type].ToString() != CurveType)
	{
		SetError(OutError, "Curve JSON has an invalid Type.");
		return false;
	}

	if (!Root.hasKey(CurveKey::Kind) || Root[CurveKey::Kind].ToString() != CurveKind)
	{
		SetError(OutError, "Curve JSON has an unsupported Kind.");
		return false;
	}

	FCurveAsset Curve = MakeDefaultBezier();

	if (Root.hasKey(CurveKey::Version))
	{
		if (!ReadInt(Root[CurveKey::Version], Curve.Version))
		{
			SetError(OutError, "Curve Version must be an integer.");
			return false;
		}
	}

	if (Curve.Version > CurrentVersion)
	{
		SetError(OutError, "Curve Version is newer than this engine supports.");
		return false;
	}

	if (Root.hasKey(CurveKey::PresetIndex))
	{
		if (!ReadInt(Root[CurveKey::PresetIndex], Curve.PresetIndex))
		{
			SetError(OutError, "Curve PresetIndex must be an integer.");
			return false;
		}
	}

	if (!Root.hasKey(CurveKey::ControlPoints))
	{
		SetError(OutError, "Curve JSON is missing ControlPoints.");
		return false;
	}

	json::JSON Points = Root[CurveKey::ControlPoints];
	if (Points.JSONType() != json::JSON::Class::Array || Points.length() != 4)
	{
		SetError(OutError, "Curve ControlPoints must be an array with four numbers.");
		return false;
	}

	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (!ReadNumber(Points[Index], Curve.ControlPoints[Index]))
		{
			SetError(OutError, "Curve ControlPoints contains a non-number value.");
			return false;
		}
	}

	OutCurve = Curve;
	return true;
}

bool FCurveSaveManager::SaveToFile(const FCurveAsset& Curve, const FString& CurveNameOrPath)
{
	if (CurveNameOrPath.empty())
	{
		std::cerr << "[Curve] Save failed: path is empty\n";
		return false;
	}

	std::filesystem::path FilePath = ResolveCurveFilePath(CurveNameOrPath);
	if (FilePath.has_parent_path())
	{
		std::filesystem::create_directories(FilePath.parent_path());
	}

	std::ofstream File(FilePath);
	if (!File.is_open())
	{
		std::cerr << "[Curve] Save failed: could not open file - " << FPaths::ToUtf8(FilePath.wstring()) << "\n";
		return false;
	}

	File << SerializeCurve(Curve).dump();
	File.flush();
	File.close();

	std::cout << "[Curve] Saved: " << FPaths::ToUtf8(FilePath.wstring()) << "\n";
	return true;
}

bool FCurveSaveManager::LoadFromFile(const FString& CurveNameOrPath, FCurveAsset& OutCurve, FString* OutError)
{
	if (CurveNameOrPath.empty())
	{
		SetError(OutError, "Curve path is empty.");
		std::cerr << "[Curve] Load failed: path is empty\n";
		return false;
	}

	const std::filesystem::path FilePath = ResolveCurveFilePath(CurveNameOrPath);
	if (!std::filesystem::exists(FilePath))
	{
		const FString Error = "Curve file not found: " + FPaths::ToUtf8(FilePath.wstring());
		SetError(OutError, Error);
		std::cerr << "[Curve] Load failed: file not found - " << FPaths::ToUtf8(FilePath.wstring()) << "\n";
		return false;
	}

	std::string FileContent;
	if (!ReadTextFile(FilePath, FileContent))
	{
		const FString Error = "Could not open curve file: " + FPaths::ToUtf8(FilePath.wstring());
		SetError(OutError, Error);
		std::cerr << "[Curve] Load failed: could not open file - " << FPaths::ToUtf8(FilePath.wstring()) << "\n";
		return false;
	}

	json::JSON Root = json::JSON::Load(FileContent);
	if (!DeserializeCurve(Root, OutCurve, OutError))
	{
		std::cerr << "[Curve] Load failed: invalid file - " << FPaths::ToUtf8(FilePath.wstring()) << "\n";
		return false;
	}

	return true;
}
