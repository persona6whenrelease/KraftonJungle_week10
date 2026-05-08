#include "SoundManager.h"

void FSoundManager::initialize()
{
	if (!m_bgm.openFromFile(FPaths::ToUtf8(FPaths::Combine(FPaths::AssetDir() , L"Sound/BackgroundMusic.wav"))))
	{
		throw std::runtime_error("BGM Load Failed : Sound/BackGround.wav" );
	}

	m_bgm.setLooping(true);


	LoadEffect(SoundEffect::Jump, FPaths::Combine(FPaths::AssetDir(), L"Sound/Jump.wav"));
	LoadEffect(SoundEffect::Death, FPaths::Combine(FPaths::AssetDir(), L"Sound/Death.wav"));
	LoadEffect(SoundEffect::Parry, FPaths::Combine(FPaths::AssetDir(), L"Sound/Parry.wav"));
	LoadEffect(SoundEffect::Dash, FPaths::Combine(FPaths::AssetDir(), L"Sound/Dash.wav"));

}

void FSoundManager::PlayBGM()
{
	m_bgm.play();
}

void FSoundManager::StopBGM()
{
	m_bgm.stop();
}

void FSoundManager::LoadEffect(SoundEffect ID, const std::wstring& FilePath)
{
	auto buffer = std::make_unique<sf::SoundBuffer>();
	if (!buffer.get()->loadFromFile(FPaths::ToUtf8(FilePath)))

		throw std::runtime_error("Effect Load Failed");

	// unique_ptr로 힙에 생성 → 복사 없이 포인터만 map에 저장
	SoundBufferMap[ID] = std::move(buffer);
	Sounds[ID] = std::make_unique<sf::Sound>(*SoundBufferMap[ID]);
}


void FSoundManager::PlayEffect(SoundEffect ID)
{
	auto it = Sounds.find(ID);
	if (it == Sounds.end())
	{
		throw std::runtime_error("Effect not loaded");
	}

	it->second.get()->play();

}

