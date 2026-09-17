#include "Gameplay/WeaponPickup.h"

#include "Core/GameObject.h"
#include "Graphics/Renderer.h"

#include <DirectXColors.h>
#include <cmath>

namespace
{
    // icons.png 한 칸의 크기.
    constexpr int kIconSize = 16;

    // 주울 수 있는 범위. 그림보다 넉넉하다.
    constexpr float kAreaHalfW = 16.0f;
    constexpr float kAreaH     = 20.0f;
}


void WeaponPickup::DropAt(float x, float y, std::string weaponId, int icon)
{
    Transform& tr = Owner().transform;
    tr.x = x;
    tr.y = y;
    m_weaponId = std::move(weaponId);
    m_icon     = icon;
    m_active   = true;
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

    // ★ 시트가 없으면 아무것도 안 그린다. 「없으면 대신 사각형」을 남겨 두면
    //   시트를 못 읽은 것을 **아무도 눈치채지 못한다** — 조용한 실패가 제일 나쁘다.
    if (!m_sheet)
        return;

    const float half = kIconSize * 0.5f;
    const RECT  src{ m_icon * kIconSize, 0, (m_icon + 1) * kIconSize, kIconSize };

    renderer.Sprites().Draw(
        m_sheet.Get(),
        DirectX::XMFLOAT2(cx - half, cy - half), &src,
        DirectX::Colors::White);
}


void WeaponPickup::RenderDebug(Renderer& renderer)
{
    if (!m_active || !renderer.DebugDraw())
        return;

    renderer.DrawRectOutline(PickupArea(), DirectX::Colors::Aqua, 1.0f);
}
