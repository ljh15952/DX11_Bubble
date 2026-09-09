#include "Audio/Audio.h"
#include "Core/Log.h"

#include <algorithm>


bool Audio::Initialize()
{
    DirectX::AUDIO_ENGINE_FLAGS flags = DirectX::AudioEngine_Default;
#ifdef _DEBUG
    // D3D 의 디버그 레이어에 해당한다. 잘못된 사용을 출력 창에 알려준다.
    flags |= DirectX::AudioEngine_Debug;
#endif

    m_engine = std::make_unique<DirectX::AudioEngine>(flags);

    // ★ 오디오 장치가 없어도(원격 데스크톱, 장치 없는 PC 등) 실패로 보지 않는다.
    //   AudioEngine 은 "무음 모드" 로 돌고, 나중에 장치가 붙으면 복구된다.
    //   여기서 false 를 돌려주면 소리가 없다는 이유로 게임이 안 켜진다.
    m_silent = !m_engine->IsAudioDevicePresent();
    if (m_silent)
        Log::Warn("[audio] 오디오 장치 없음 — 무음 모드로 진행");
    else
        Log::Info("[audio] 초기화 완료");

    return true;
}


void Audio::Shutdown()
{
    // ★ 소리(SoundEffect)를 엔진보다 먼저 놓아야 한다.
    //   SoundEffect 는 엔진에 자신을 등록해 두므로 순서가 뒤집히면 위험하다.
    m_sounds.clear();

    if (m_engine)
        m_engine->Suspend();   // 재생 중인 소리를 멈춘다
    m_engine.reset();
}


void Audio::Update()
{
    if (!m_engine)
        return;

    // Update() 는 끝난 소리를 정리하고, 문제가 있으면 false 를 돌려준다.
    if (m_engine->Update())
        return;

    // 헤드폰을 뽑았다든가 기본 오디오 장치가 바뀌었다.
    // 그래픽의 "디바이스 로스트" 에 해당한다.
    if (m_engine->IsCriticalError())
    {
        Log::Warn("[audio] 오디오 장치 분실 — 복구 시도");
        if (m_engine->Reset())
        {
            m_silent = false;
            Log::Info("[audio] 복구 성공");
        }
        else
        {
            m_silent = true;   // 다음 프레임에 다시 시도된다
        }
    }
    else
    {
        // 장치가 없어 무음으로 도는 상태. 에러는 아니다.
        m_silent = true;
    }
}


bool Audio::Load(std::string_view name, const wchar_t* path)
{
    const std::string key(name);

    if (m_sounds.find(key) != m_sounds.end())
        return true;   // 이미 있음. Assets 캐시와 같은 발상.

    if (!m_engine)
        return false;

    try
    {
        // SoundEffect 는 WAV 를 통째로 메모리에 올린다.
        // 짧은 효과음에 적합하다. BGM 처럼 긴 것은 나중에
        // 스트리밍(SoundStreamInstance)으로 따로 다뤄야 한다.
        auto sound = std::make_unique<DirectX::SoundEffect>(m_engine.get(), path);
        m_sounds.emplace(key, std::move(sound));
        Log::Info("[audio] 로드 : {} <- {}", key, Log::ToUtf8(path));
        return true;
    }
    catch (const std::exception& e)
    {
        // ★ DirectXTK 는 실패를 HRESULT 가 아니라 예외로 던진다.
        //   D3D 쪽과 오류 처리 방식이 달라서 잊기 쉽다.
        Log::Error("[audio] 로드 실패 : {} ({})", Log::ToUtf8(path), e.what());
        return false;
    }
}


void Audio::Play(std::string_view name, float volume, float pitch, float pan)
{
    if (!m_engine || m_silent)
        return;

    const auto it = m_sounds.find(std::string(name));
    if (it == m_sounds.end())
    {
        Log::Warn("[audio] 없는 소리 : {}", name);
        return;
    }

    // 인자 범위를 넘기면 DirectXTK 가 예외를 던진다. 여기서 잘라 준다.
    volume = std::clamp(volume * m_masterVolume, 0.0f, 1.0f);
    pitch  = std::clamp(pitch,  -1.0f, 1.0f);
    pan    = std::clamp(pan,    -1.0f, 1.0f);

    // 발사 후 잊는(fire-and-forget) 재생.
    // 멈추거나 볼륨을 바꿔야 하는 소리(BGM, 루프)는
    // SoundEffectInstance 를 따로 만들어 들고 있어야 한다.
    it->second->Play(volume, pitch, pan);
}


void Audio::SetMasterVolume(float v)
{
    m_masterVolume = std::clamp(v, 0.0f, 1.0f);
    if (m_engine)
        m_engine->SetMasterVolume(m_masterVolume);
}
