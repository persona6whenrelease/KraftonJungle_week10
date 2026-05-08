#pragma once
#include "Core/CoreTypes.h"
#include "Core/Singleton.h"

#include "Platform/Paths.h"
#include "ThirdParty/SFML/Audio.hpp"


enum class SoundEffect : uint32
{
	Jump, 
	Parry,
	Death,
	Dash
};


class FSoundManager : public TSingleton<FSoundManager>
{
	friend class TSingleton<FSoundManager>;

public:
	// sound file mapping 

	void PlayBGM();
	void StopBGM();
	void initialize();


	void LoadEffect(SoundEffect ID, const std::wstring& FilePath);
	void PlayEffect(SoundEffect ID);


private:
	FSoundManager() = default;
	TMap<SoundEffect, std::unique_ptr<sf::SoundBuffer>> SoundBufferMap;
	TMap<SoundEffect, std::unique_ptr<sf::Sound>>       Sounds;
	sf::Music m_bgm; 


};