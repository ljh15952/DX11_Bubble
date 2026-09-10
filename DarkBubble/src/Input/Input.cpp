#include "Input/Input.h"
#include <cmath>

void Input::Initialize()
{
    m_keyboard = std::make_unique<DirectX::Keyboard>();
    m_gamePad  = std::make_unique<DirectX::GamePad>();
}


void Input::ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Keyboard 는 내부적으로 싱글턴이라 인스턴스 없이 static 으로 호출할 수 있다.
    DirectX::Keyboard::ProcessMessage(msg, wParam, lParam);
}


void Input::Poll()
{
    m_kb = m_keyboard->GetState();
    m_kbTracker.Update(m_kb);

    m_pad = m_gamePad->GetState(0);
    if (m_pad.IsConnected())
        m_padTracker.Update(m_pad);

    // ★ 게임플레이 엣지는 |= 로 누적한다. ConsumeEdges 까지 사라지지 않는다.
    //   정지 중에 누른 공격이 다음 스텝에서 살아나는 장치이고,
    //   프레임이 빨라 틱이 0 회 도는 프레임에서 입력이 사라지는 것도 막아 준다.
    using PadTracker = DirectX::GamePad::ButtonStateTracker;

    m_edges.confirm |= m_kbTracker.pressed.Enter
                    || m_kbTracker.pressed.Space
                    || m_padTracker.a == PadTracker::PRESSED;

    m_edges.cancel  |= m_kbTracker.pressed.Escape
                    || m_padTracker.b == PadTracker::PRESSED;

    m_edges.attack  |= m_kbTracker.pressed.Space
                    || m_padTracker.a == PadTracker::PRESSED;

    m_edges.roll    |= m_kbTracker.pressed.LeftShift
                    || m_padTracker.b == PadTracker::PRESSED;

    // ★ 일시정지를 Cancel 과 분리했다.
    //   패드 B 가 구르기이므로, B 로 일시정지가 같이 걸리면 안 된다.
    m_edges.pause   |= m_kbTracker.pressed.Escape
                    || m_padTracker.start == PadTracker::PRESSED;

    // ★ F2 도 누적한다. 이유는 Input.h 의 선언부 주석 참조 —
    //   게임 상태(장착 방어구)를 바꾸므로 틱 안에서 소비되어야 한다.
    m_edges.armorSwap |= m_kbTracker.pressed.F2;
}


void Input::ConsumeEdges()
{
    m_edges = {};
}


Input::MoveIntent Input::Move() const
{
    MoveIntent m;

    if (m_kb.Left  || m_kb.A) m.x -= 1.0f;
    if (m_kb.Right || m_kb.D) m.x += 1.0f;
    if (m_kb.Up    || m_kb.W) m.y -= 1.0f;   // 화면 좌표는 아래로 갈수록 +
    if (m_kb.Down  || m_kb.S) m.y += 1.0f;

    if (m_pad.IsConnected())
    {
        m.x += m_pad.thumbSticks.leftX;
        m.y -= m_pad.thumbSticks.leftY;      // 스틱은 위가 +, 화면은 아래가 +
    }

    // 대각선 보정: 길이가 1 을 넘을 때만 정규화한다.
    // 이렇게 하면 키보드 대각선(√2 ≒ 1.41배)은 억제되고,
    // 스틱을 살짝 기울인 아날로그 입력은 그대로 살아난다.
    const float len = std::sqrt(m.x * m.x + m.y * m.y);
    if (len > 1.0f)
    {
        m.x /= len;
        m.y /= len;
    }
    return m;
}


bool Input::CrouchHeld() const
{
    // 좌우 Ctrl 을 모두 받는다. 어느 손으로 잡든 되게 하는 편이 낫다.
    return m_kb.LeftControl
        || m_kb.RightControl
        || (m_pad.IsConnected() && m_pad.IsLeftShoulderPressed());
}


// ---- 디버그 / 엔진 키 ----
//   Game 이 틱 밖에서 프레임당 1회 읽으므로 누적할 필요가 없다.
//   , . / 는 키보드에서 나란히 있고 게임플레이에 쓰이지 않는다.
//   그리고 , . 는 영상 편집기의 프레임 이동 키와 같은 관례다.
bool Input::DebugTogglePressed()  const { return m_kbTracker.pressed.F1; }
bool Input::StatsTogglePressed()  const { return m_kbTracker.pressed.F3; }
bool Input::FreezeTogglePressed() const { return m_kbTracker.pressed.OemComma;    }   // ,
bool Input::StepPressed()         const { return m_kbTracker.pressed.OemPeriod;   }   // .
bool Input::SlowTogglePressed()   const { return m_kbTracker.pressed.OemQuestion; }   // /
