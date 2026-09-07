// ============================================================================
//  Input.h
//    키보드와 게임패드를 "게임이 이해하는 의도"로 번역한다.
//
//    게임 로직은 「오른쪽 방향키가 눌렸나」가 아니라 「오른쪽으로 가고 싶나」만
//    알면 된다. 그래야 나중에 키 재설정이나 새 입력 장치를 넣을 때
//    게임 로직을 한 줄도 안 고쳐도 된다.
// ============================================================================
#pragma once

#include <windows.h>
#include <memory>
#include <Keyboard.h>
#include <GamePad.h>

class Input
{
public:
    // 이동 의도. 이미 정규화되어 있어서 대각선이 빨라지지 않는다.
    struct MoveIntent
    {
        float x = 0.0f;
        float y = 0.0f;   // 화면 좌표계 기준. 아래로 갈수록 +
    };

    void Initialize();

    // 창 프로시저에서 호출한다.
    // DirectXTK Keyboard 는 스스로 메시지를 받을 수 없어 배달이 필요하다.
    static void ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // 프레임당 정확히 1회 호출. "방금 눌렸다" 판정의 기준이 여기서 갱신된다.
    void Poll();

    // ---- 지속 입력 (여러 틱에 걸쳐 여러 번 처리해도 되는 것) ----
    MoveIntent Move() const;

    // ---- 엣지 입력 (한 번만 소비해야 하는 것) ----
    bool AttackPressed()      const;
    bool QuitPressed()        const;
    bool DebugTogglePressed() const;   // F1 — 히트박스 표시 on/off

private:
    std::unique_ptr<DirectX::Keyboard> m_keyboard;
    std::unique_ptr<DirectX::GamePad>  m_gamePad;

    // Tracker 는 직전 프레임의 상태를 들고 있다가
    // 「지금 눌려 있다」와 「방금 눌렸다」를 구분해 준다.
    DirectX::Keyboard::KeyboardStateTracker m_kbTracker;
    DirectX::GamePad::ButtonStateTracker    m_padTracker;

    DirectX::Keyboard::State m_kb  = {};
    DirectX::GamePad::State  m_pad = {};
};
