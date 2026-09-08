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


bool Input::AttackPressed() const
{
    return m_kbTracker.pressed.Space
        || m_padTracker.a == DirectX::GamePad::ButtonStateTracker::PRESSED;
}


bool Input::ConfirmPressed() const
{
    return m_kbTracker.pressed.Enter
        || m_kbTracker.pressed.Space
        || m_padTracker.a == DirectX::GamePad::ButtonStateTracker::PRESSED;
}


bool Input::CancelPressed() const
{
    return m_kbTracker.pressed.Escape
        || m_padTracker.b == DirectX::GamePad::ButtonStateTracker::PRESSED;
}


bool Input::DebugTogglePressed() const
{
    return m_kbTracker.pressed.F1;
}
