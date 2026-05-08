#include "Games/Crossy/Scripting/CrossyLuaBindings.h"
#include "Scripting/SolInclude.h"

#include "Core/CoreTypes.h"
#include "Core/Log.h"
#include "Engine/Platform/Paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	std::wstring GetScoreSavePath()
	{
		FPaths::CreateDir(FPaths::SaveDir());
		return FPaths::Combine(FPaths::SaveDir(), L"Score.sav");
	}

	int32 ParseBestScoreText(const std::string& Text)
	{
		// Supported formats:
		//   BestScore=123
		//   { "BestScore": 123 }
		//   123
		std::size_t Start = Text.find("BestScore");
		if (Start == std::string::npos)
		{
			Start = 0;
		}

		while (Start < Text.size() && !std::isdigit(static_cast<unsigned char>(Text[Start])) && Text[Start] != '-')
		{
			++Start;
		}

		if (Start >= Text.size())
		{
			return 0;
		}

		std::size_t End = Start;
		if (Text[End] == '-')
		{
			++End;
		}
		while (End < Text.size() && std::isdigit(static_cast<unsigned char>(Text[End])))
		{
			++End;
		}

		try
		{
			return (std::max)(0, std::stoi(Text.substr(Start, End - Start)));
		}
		catch (...)
		{
			return 0;
		}
	}

	int32 ParseScoreLineValue(const std::string& Line)
	{
		std::size_t Start = Line.find('=');
		if (Start != std::string::npos)
		{
			++Start;
		}
		else
		{
			Start = 0;
		}

		while (Start < Line.size() && !std::isdigit(static_cast<unsigned char>(Line[Start])) && Line[Start] != '-')
		{
			++Start;
		}

		if (Start >= Line.size())
		{
			return 0;
		}

		std::size_t End = Start;
		if (Line[End] == '-')
		{
			++End;
		}
		while (End < Line.size() && std::isdigit(static_cast<unsigned char>(Line[End])))
		{
			++End;
		}

		try
		{
			return (std::max)(0, std::stoi(Line.substr(Start, End - Start)));
		}
		catch (...)
		{
			return 0;
		}
	}

	std::vector<int32> ParseScoreListText(const std::string& Text)
	{
		std::vector<int32> Scores;

		std::istringstream Stream(Text);
		std::string Line;
		while (std::getline(Stream, Line))
		{
			const bool bLooksLikeScoreLine =
				Line.find("Score") != std::string::npos ||
				Line.find("BestScore") != std::string::npos;
			if (!bLooksLikeScoreLine)
			{
				continue;
			}

			int32 Score = ParseScoreLineValue(Line);
			if (Score > 0)
			{
				Scores.push_back(Score);
			}
		}

		if (Scores.empty())
		{
			const int32 LegacyScore = ParseBestScoreText(Text);
			if (LegacyScore > 0)
			{
				Scores.push_back(LegacyScore);
			}
		}

		std::sort(Scores.begin(), Scores.end(), std::greater<int32>());
		if (Scores.size() > 5)
		{
			Scores.resize(5);
		}
		return Scores;
	}

	std::vector<int32> LoadScoreList()
	{
		const std::wstring Path = GetScoreSavePath();
		std::ifstream File(Path, std::ios::binary);
		if (!File)
		{
			return {};
		}

		std::ostringstream Buffer;
		Buffer << File.rdbuf();
		std::vector<int32> Scores = ParseScoreListText(Buffer.str());
		UE_LOG("[SaveGame] LoadTopScores: %zu entries (%s)", Scores.size(), FPaths::ToUtf8(Path).c_str());
		return Scores;
	}

	bool SaveScoreList(std::vector<int32> Scores)
	{
		Scores.erase(
			std::remove_if(Scores.begin(), Scores.end(), [](int32 Score) { return Score <= 0; }),
			Scores.end());
		std::sort(Scores.begin(), Scores.end(), std::greater<int32>());
		if (Scores.size() > 5)
		{
			Scores.resize(5);
		}

		const std::wstring Path = GetScoreSavePath();
		std::ofstream File(Path, std::ios::binary | std::ios::trunc);
		if (!File)
		{
			UE_LOG("[SaveGame] SaveTopScores failed: %s", FPaths::ToUtf8(Path).c_str());
			return false;
		}

		for (size_t Index = 0; Index < Scores.size(); ++Index)
		{
			File << "Score" << (Index + 1) << "=" << Scores[Index] << "\n";
		}

		const bool bOk = File.good();
		UE_LOG("[SaveGame] SaveTopScores: %zu entries (%s)", Scores.size(), FPaths::ToUtf8(Path).c_str());
		return bOk;
	}
}

void RegisterCrossySaveGameBinding(sol::state& Lua)
{
	sol::table SaveGame = Lua.get_or("SaveGame", Lua.create_table());
	Lua["SaveGame"] = SaveGame;

	SaveGame.set_function("GetScoreSavePath", []() -> std::string
	{
		return FPaths::ToUtf8(GetScoreSavePath());
	});

	SaveGame.set_function("LoadBestScore", []() -> int32
	{
		std::vector<int32> Scores = LoadScoreList();
		return Scores.empty() ? 0 : Scores.front();
	});

	SaveGame.set_function("SaveBestScore", [](int32 BestScore) -> bool
	{
		return SaveScoreList({ (std::max)(0, BestScore) });
	});

	SaveGame.set_function("LoadTopScores", [](sol::this_state State) -> sol::table
	{
		sol::state_view Lua(State);
		sol::table Result = Lua.create_table();

		std::vector<int32> Scores = LoadScoreList();
		for (size_t Index = 0; Index < Scores.size(); ++Index)
		{
			Result[static_cast<int>(Index + 1)] = Scores[Index];
		}

		return Result;
	});

	SaveGame.set_function("SaveTopScores", [](sol::table Scores) -> bool
	{
		std::vector<int32> Values;
		for (int Index = 1; Index <= 5; ++Index)
		{
			sol::object Value = Scores[Index];
			if (Value.valid() && Value.get_type() == sol::type::number)
			{
				Values.push_back((std::max)(0, Value.as<int32>()));
			}
		}

		return SaveScoreList(std::move(Values));
	});
}
