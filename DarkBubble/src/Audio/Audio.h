// ============================================================================
//  Audio.h
//    DirectXTK Audio(XAudio2) 를 감싸서 이름으로 소리를 재생한다.
//
//      ctx.audio.Play("hit");
//
//    호출부가 파일 경로도, XAudio2 도 몰라야 한다.
//    나중에 음원을 교체하거나 포맷을 바꿔도 Scene 은 안 고친다.
//
//  ---- 오디오가 그래픽과 결정적으로 다른 점 ----
//    ① 별도 스레드에서 계속 돈다
//       Play() 는 "재생 시작" 만 하고 즉시 돌아온다. 소리는 XAudio2 가
//       자기 스레드에서 계속 흘려보낸다. 게임 루프가 잠깐 멈춰도 소리는 이어진다.
//
//    ② 장치가 사라질 수 있다
//       헤드폰을 뽑거나 오디오 장치를 바꾸면 엔진이 죽는다.
//       그래서 매 프레임 Update() 로 상태를 확인하고 필요하면 Reset() 한다.
//       그래픽에는 없는 종류의 관리다.
//
//    ③ 장치가 아예 없어도 게임은 돌아가야 한다
//       AudioEngine 은 "무음 모드" 로 뜬다. 실패로 취급하지 않는다.
// ============================================================================
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include <Audio.h>

class Audio
{
public:
    // 오디오 장치가 없어도 true 를 돌려준다(무음 모드). 게임을 막지 않는다.
    bool Initialize();
    void Shutdown();

    // ★ 매 프레임 호출. 끝난 소리 정리 + 장치 분실 복구.
    void Update();

    // 이름을 열쇠로 캐시한다. Assets 와 같은 발상.
    bool Load(std::string_view name, const wchar_t* path);

    // volume 0~1 / pitch -1~1 (반음이 아니라 비율) / pan -1(왼쪽)~1(오른쪽)
    void Play(std::string_view name, float volume = 1.0f, float pitch = 0.0f, float pan = 0.0f);

    void  SetMasterVolume(float v);
    float MasterVolume() const { return m_masterVolume; }

    bool   Silent() const { return m_silent; }
    size_t Count()  const { return m_sounds.size(); }

private:
    std::unique_ptr<DirectX::AudioEngine> m_engine;
    std::unordered_map<std::string, std::unique_ptr<DirectX::SoundEffect>> m_sounds;

    float m_masterVolume = 1.0f;
    bool  m_silent       = false;   // 오디오 장치가 없어 무음으로 도는 중
};
