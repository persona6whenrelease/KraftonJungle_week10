#pragma once
#include "Core/CoreTypes.h"
#include "Core/Singleton.h"

#include "ThirdParty/SFML/Audio.hpp"

using FSoundId = FString;

class FSoundManager : public TSingleton<FSoundManager>
{
	friend class TSingleton<FSoundManager>;

public:
	void initialize();

	bool LoadMusic(const FSoundId& ID, const std::wstring& FilePath, bool bLoop = true);
	void PlayMusic(const FSoundId& ID);
	void StopMusic(const FSoundId& ID);
	void StopAllMusic();

	// Legacy aliases for callers that only need one active music channel.
	void PlayBGM();
	void StopBGM();

	void LoadEffect(const FSoundId& ID, const std::wstring& FilePath);
	void PlayEffect(const FSoundId& ID);
	void StopEffect(const FSoundId& ID);

	bool IsEffectPlaying(const FSoundId& ID) const;
	float GetEffectDuration(const FSoundId& ID) const;

private:
	FSoundManager() = default;
	TMap<FSoundId, std::unique_ptr<sf::SoundBuffer>> SoundBufferMap;
	TMap<FSoundId, std::unique_ptr<sf::Sound>> Sounds;
	TMap<FSoundId, std::unique_ptr<sf::Music>> MusicMap;
	FSoundId LastMusicId;
};
