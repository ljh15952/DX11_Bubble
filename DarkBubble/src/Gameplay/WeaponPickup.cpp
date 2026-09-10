#include "Gameplay/WeaponPickup.h"

#include "Core/GameObject.h"
#include "Graphics/Renderer.h"

#include <DirectXColors.h>
#include <cmath>

namespace
{
    // 칼날 색은 플레이어 시트의 칼과 같게 — 「저게 내 무기다」가 바로 읽혀야 한다.
    constexpr float kBladeW = 16.0f;
    constexpr float kBladeH =  4.0f;

    // 주울 수 있는 범위. 그림보다 넉넉하다.
    constexpr float kAreaHalfW = 16.0f;
    constexpr float kAreaH     = 20.0f;
}


void WeaponPickup::DropAt(float x, float y)
{
    Transform& tr = Owner().transform;
    tr.x = x;
    tr.y = y;
    m_active = true;
}


AABB WeaponPickup::PickupArea() const
{
    const Transform& tr = Owner().transform;
    return { tr.x - kAreaHalfW, tr.y - kAreaH, tr.x + kAreaHalfW, tr.y };
}


void WeaponPickup::Render(Renderer& renderer)
{
    if (!m_active)
        return;

    const Transform& tr = Owner().transform;

    // ★ 위아래로 살짝 흔든다. 바닥에 놓인 물건은 배경에 묻히기 쉬운데,
    //   움직이는 것은 눈이 먼저 찾는다. 애니메이션 한 줄이 UI 를 대신한다.
    //   ※ Tick 을 쓰지 않고 그리기에서 세는 이유: 이 흔들림은 **표현**일 뿐
    //     게임 상태가 아니다. 정지(,)해도 멈추는 편이 오히려 자연스럽다.
    ++m_bobTicks;
    const float bob = std::round(std::sin(m_bobTicks * 0.08f) * 1.5f);

    const float cx = std::round(tr.x);
    const float cy = std::round(tr.y) - 4.0f + bob;

    // 그림자 — 「땅에 있다」를 알려 준다
    renderer.DrawFilledRect(
        { cx - 7.0f, std::round(tr.y) - 2.0f, cx + 7.0f, std::round(tr.y) },
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.35f));

    // 칼날 (테두리 + 본색). 스프라이트 없이 도형 두 개면 충분하다.
    renderer.DrawFilledRect(
        { cx - kBladeW * 0.5f - 1.0f, cy - kBladeH * 0.5f - 1.0f,
          cx + kBladeW * 0.5f + 1.0f, cy + kBladeH * 0.5f + 1.0f },
        DirectX::XMVectorSet(0.07f, 0.07f, 0.10f, 1.0f));
    renderer.DrawFilledRect(
        { cx - kBladeW * 0.5f, cy - kBladeH * 0.5f,
          cx + kBladeW * 0.5f, cy + kBladeH * 0.5f },
        DirectX::XMVectorSet(0.81f, 0.82f, 0.86f, 1.0f));
}


void WeaponPickup::RenderDebug(Renderer& renderer)
{
    if (!m_active || !renderer.DebugDraw())
        return;

    renderer.DrawRectOutline(PickupArea(), DirectX::Colors::Aqua, 1.0f);
}
