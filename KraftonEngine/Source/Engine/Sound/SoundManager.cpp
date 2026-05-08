#include "SoundManager.h"

#include "Core/Log.h"

#include <filesystem>
#include <stdexcept>

void FSoundManager::initialize()
{
	SoundBufferMap.clear();
	Sounds.clear();
	MusicMap.clear();
	LastMusicId.clear();
}

bool FSoundManager::LoadMusic(const FSoundId& ID, const std::wstring& FilePath, bool bLoop)
{
	if (ID.empty())
	{
		return false;
	}

	auto Music = std::make_unique<sf::Music>();
	if (!Music->openFromFile(std::filesystem::path(FilePath)))
	{
		UE_LOG("[Sound] Music load failed: %s", ID.c_str());
		return false;
	}

	Music->setLooping(bLoop);
	MusicMap[ID] = std::move(Music);
	LastMusicId = ID;
	return true;
}

void FSoundManager::PlayMusic(const FSoundId& ID)
{
	auto It = MusicMap.find(ID);
	if (It == MusicMap.end() || !It->second)
	{
		UE_LOG("[Sound] Music not loaded: %s", ID.c_str());
		return;
	}

	LastMusicId = ID;
	It->second->play();
}

void FSoundManager::StopMusic(const FSoundId& ID)
{
	auto It = MusicMap.find(ID);
	if (It != MusicMap.end() && It->second)
	{
		It->second->stop();
	}
}

void FSoundManager::StopAllMusic()
{
	for (auto& Pair : MusicMap)
	{
		if (Pair.second)
		{
			Pair.second->stop();
		}
	}
}

void FSoundManager::PlayBGM()
{
	if (!LastMusicId.empty())
	{
		PlayMusic(LastMusicId);
	}
}

void FSoundManager::StopBGM()
{
	if (!LastMusicId.empty())
	{
		StopMusic(LastMusicId);
	}
}

void FSoundManager::LoadEffect(const FSoundId& ID, const std::wstring& FilePath)
{
	auto buffer = std::make_unique<sf::SoundBuffer>();
	if (!buffer->loadFromFile(std::filesystem::path(FilePath)))
	{
		throw std::runtime_error("Effect Load Failed");
	}

	SoundBufferMap[ID] = std::move(buffer);
	Sounds[ID] = std::make_unique<sf::Sound>(*SoundBufferMap[ID]);
}

void FSoundManager::PlayEffect(const FSoundId& ID)
{
	auto it = Sounds.find(ID);
	if (it == Sounds.end())
	{
		UE_LOG("[Sound] Effect not loaded: %s", ID.c_str());
		return;
	}

	it->second->play();
}

void FSoundManager::StopEffect(const FSoundId& ID)
{
	auto it = Sounds.find(ID);
	if (it != Sounds.end() && it->second)
	{
		it->second->stop();
	}
}

bool FSoundManager::IsEffectPlaying(const FSoundId& ID) const
{
	auto it = Sounds.find(ID);
	if (it != Sounds.end() && it->second)
	{
		return it->second->getStatus() == sf::Sound::Status::Playing;
	}
	return false;
}

float FSoundManager::GetEffectDuration(const FSoundId& ID) const
{
	auto it = SoundBufferMap.find(ID);
	if (it != SoundBufferMap.end() && it->second)
	{
		return it->second->getDuration().asSeconds();
	}
	return 0.0f;
}
