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

    // ★ 틱이 실제로 돈 뒤에 Game 이 호출한다. 붙잡아 둔 엣지 입력을 비운다.
    //
    //   왜 필요한가:
    //     엣지 입력("방금 눌림")은 Poll 시점에만 참이다. 그런데 프레임 정지 중이거나
    //     프레임이 아주 빨라 이번 프레임에 틱이 0 회 도는 경우, 그 입력을 아무도
    //     읽지 못하고 사라진다.
    //     그래서 Poll 에서 엣지를 누적해 두고, 틱이 실제로 돌았을 때만 비운다.
    //
    //   이 덕분에 "정지 → Space → 스텝" 순서로 공격을 처음부터 관찰할 수 있다.
    void ConsumeEdges();

    // ---- 지속 입력 (여러 틱에 걸쳐 여러 번 처리해도 되는 것) ----
    MoveIntent Move() const;

    // ---- 게임플레이 엣지 입력 ----
    //   Scene::Update 안(= 틱 안)에서 읽힌다. 그래서 ConsumeEdges 까지 붙잡아 둔다.
    //   메뉴용(Confirm/Cancel)과 게임플레이용(Attack)을 나눠 둔다.
    //   같은 물리 키를 쓰더라도 이름이 다르면 Scene 마다 의미가 분명해진다.
    //   메뉴용과 게임플레이용을 나눠 둔다. 같은 물리 키를 공유해도
    //   이름이 다르면 Scene 마다 의미가 분명해지고, 나중에 키 재설정이 쉽다.
    bool ConfirmPressed() const { return m_edges.confirm; }   // Enter / Space / 패드 A  (메뉴)
    bool CancelPressed()  const { return m_edges.cancel;  }   // Esc / 패드 B            (메뉴)

    bool AttackPressed()  const { return m_edges.attack;  }   // Space / 패드 A          (게임)
    bool RollPressed()    const { return m_edges.roll;    }   // Shift / 패드 B          (게임)
    bool PausePressed()   const { return m_edges.pause;   }   // Esc / 패드 Start        (게임)

    // ---- 디버그 / 엔진 키 ----
    //   Game 이 틱 밖에서 프레임당 1회 읽는다. 붙잡아 둘 필요가 없다.
    bool DebugTogglePressed()  const;  // F1  — 히트박스 표시
    bool StatsTogglePressed()  const;  // F3  — FPS / 틱 오버레이
    bool FreezeTogglePressed() const;  // ,   — 프레임 정지
    bool StepPressed()         const;  // .   — 1 틱 전진
    bool SlowTogglePressed()   const;  // /   — 슬로우 모션

private:
    std::unique_ptr<DirectX::Keyboard> m_keyboard;
    std::unique_ptr<DirectX::GamePad>  m_gamePad;

    // Tracker 는 직전 프레임의 상태를 들고 있다가
    // 「지금 눌려 있다」와 「방금 눌렸다」를 구분해 준다.
    DirectX::Keyboard::KeyboardStateTracker m_kbTracker;
    DirectX::GamePad::ButtonStateTracker    m_padTracker;

    DirectX::Keyboard::State m_kb  = {};
    DirectX::GamePad::State  m_pad = {};

    // 틱이 돌 때까지 붙잡아 두는 게임플레이 엣지 입력
    struct Edges
    {
        bool confirm = false;
        bool cancel  = false;
        bool attack  = false;
        bool roll    = false;
        bool pause   = false;
    };
    Edges m_edges;
};
