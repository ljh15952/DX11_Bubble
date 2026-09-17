#include "Input/Input.h"
#include <algorithm>
#include <cmath>

void Input::Initialize(HWND hwnd)
{
    m_keyboard = std::make_unique<DirectX::Keyboard>();
    m_gamePad  = std::make_unique<DirectX::GamePad>();
    m_mouse    = std::make_unique<DirectX::Mouse>();
    m_mouse->SetWindow(hwnd);
}


void Input::ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Keyboard / Mouse 는 내부적으로 싱글턴이라 인스턴스 없이 static 으로 호출한다.
    DirectX::Keyboard::ProcessMessage(msg, wParam, lParam);
    DirectX::Mouse::ProcessMessage(msg, wParam, lParam);
}


void Input::Poll()
{
    m_kb = m_keyboard->GetState();
    m_kbTracker.Update(m_kb);

    m_pad = m_gamePad->GetState(0);
    if (m_pad.IsConnected())
        m_padTracker.Update(m_pad);

    m_ms = m_mouse->GetState();
    m_mouseTracker.Update(m_ms);

    // ★ 게임플레이 엣지는 |= 로 누적한다. ConsumeEdges 까지 사라지지 않는다.
    //   정지 중에 누른 공격이 다음 스텝에서 살아나는 장치이고,
    //   프레임이 빨라 틱이 0 회 도는 프레임에서 입력이 사라지는 것도 막아 준다.
    using PadTracker = DirectX::GamePad::ButtonStateTracker;

    m_edges.confirm |= m_kbTracker.pressed.Enter
                    || m_kbTracker.pressed.Space
                    || m_padTracker.a == PadTracker::PRESSED;

    m_edges.cancel  |= m_kbTracker.pressed.Escape
                    || m_padTracker.b == PadTracker::PRESSED;

    // ★ 같은 「의도」에 여러 입력을 묶는다. Input 이 키가 아니라 의도로
    //   번역하도록 처음부터 설계해 둔 덕에 장치를 늘려도 게임 코드가 안 바뀐다.
    using MouseTracker = DirectX::Mouse::ButtonStateTracker;

    // ★ 버튼 = 손. 무엇을 하는지는 **그 손에 무엇이 들렸는지**가 정한다.
    m_edges.leftHand  |= m_padTracker.x == PadTracker::PRESSED
                      || m_mouseTracker.leftButton == MouseTracker::PRESSED;

    m_edges.rightHand |= m_padTracker.y == PadTracker::PRESSED
                      || m_mouseTracker.rightButton == MouseTracker::PRESSED;

    // ★ Space 가 공격에서 점프로 옮겨 왔다. 같은 물리 키가 메뉴에서는
    //   여전히 Confirm 이다 — 의도로 번역해 두었기에 충돌하지 않는다.
    m_edges.interact |= m_kbTracker.pressed.E;

    m_edges.jump    |= m_kbTracker.pressed.Space
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
    m_edges.legBreak  |= m_kbTracker.pressed.F4;
    m_edges.armBreak  |= m_kbTracker.pressed.F7;
    m_edges.swapRight |= m_kbTracker.pressed.F8;
    m_edges.swapLeft  |= m_kbTracker.pressed.F9;
    m_edges.darkToggle |= m_kbTracker.pressed.F5;
    m_edges.dataReload |= m_kbTracker.pressed.F6;
}


void Input::ConsumeEdges()
{
    m_edges = {};
}


float Input::MoveX() const
{
    float x = 0.0f;

    if (m_kb.Left  || m_kb.A) x -= 1.0f;
    if (m_kb.Right || m_kb.D) x += 1.0f;

    if (m_pad.IsConnected())
        x += m_pad.thumbSticks.leftX;

    // ★ 축이 하나라 「정규화」가 아니라 **자르기**다.
    //   왼쪽+오른쪽을 같이 누르면 0 이 되는 것도 이 한 줄이 처리한다.
    //   스틱을 살짝 기울인 아날로그 값은 그대로 살아난다.
    return std::clamp(x, -1.0f, 1.0f);
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
