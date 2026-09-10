#include "Gameplay/StaminaComponent.h"

#include "Core/AABB.h"
#include "Core/Constants.h"
#include "Graphics/Renderer.h"

#include <DirectXColors.h>
#include <algorithm>
#include <format>

namespace
{
    // ★ 공격이 28 이므로 100 으로 세 번은 되고 네 번째에 고갈된다.
    //   「세 번은 되고 네 번은 안 된다」가 몸으로 익혀지는 배치다.
    constexpr float kMax          = 100.0f;
    constexpr float kRegenPerTick = 0.9f;    // 초당 54
    constexpr int   kRegenDelay   = 36;      // 0.6 초. 행동할 때마다 초기화된다
    constexpr float kExhaustExit  = 25.0f;   // 이 이상 회복되면 고갈이 풀린다

    //   ★ 회복 지연이 없으면 공격하는 동안에도 차서 스태미나가 의미를 잃는다.
    //     지연(36틱)이 공격 길이(24틱)보다 길어서 연속 공격 중에는 전혀 안 찬다.

    // ---- 바 배치 ----
    constexpr float kBarX = 12.0f;
    constexpr float kBarW = 150.0f;
    constexpr float kBarH = 9.0f;
    constexpr float kBarBottomGap = 24.0f;
}


float StaminaComponent::Max() const { return kMax; }


void StaminaComponent::Reset()
{
    m_current  = kMax;
    m_delay    = 0;
    m_depleted = false;
}


void StaminaComponent::Spend(int cost)
{
    m_current -= static_cast<float>(cost);
    m_delay    = kRegenDelay;
}


void StaminaComponent::Tick(SceneContext&, bool)
{
    // 회복 지연 중이면 아직 안 찬다. 행동할 때마다 이 값이 초기화된다.
    if (m_delay > 0)
    {
        --m_delay;
    }
    else if (m_current < kMax)
    {
        // ★ float 이다. int 로 하면 틱당 0.9 가 0 으로 잘려 영원히 회복되지 않는다.
        //   이동 좌표와 같은 이유다 — 계산은 소수로, 표시할 때만 정리.
        m_current = std::min(m_current + kRegenPerTick, kMax);
    }

    // ---- 고갈 래치 ----
    //   ★ 들어가는 문턱(0)과 나오는 문턱(25)이 다르다 = 히스테리시스.
    //     같은 값으로 하면 0 근처에서 경직이 들락날락 떨린다.
    if (m_current < 0.0f)
        m_depleted = true;
    else if (m_current >= kExhaustExit)
        m_depleted = false;
}


void StaminaComponent::RenderUI(Renderer& renderer)
{
    const float barY = Config::kCanvasHeight - kBarBottomGap;

    // 테두리 겸 배경
    renderer.DrawFilledRect(
        AABB::FromXYWH(kBarX - 1.0f, barY - 1.0f, kBarW + 2.0f, kBarH + 2.0f),
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.7f));

    // ★ current 는 음수가 될 수 있으므로 표시 비율은 0 으로 자른다.
    const float ratio = std::clamp(m_current / kMax, 0.0f, 1.0f);

    DirectX::XMVECTOR color = DirectX::XMVectorSet(0.35f, 0.80f, 0.45f, 1.0f);   // 녹색
    if (m_depleted)      color = DirectX::XMVectorSet(0.85f, 0.20f, 0.20f, 1.0f); // 고갈 = 빨강
    else if (ratio < 0.3f) color = DirectX::XMVectorSet(0.90f, 0.75f, 0.25f, 1.0f); // 부족 = 노랑

    if (ratio > 0.0f)
        renderer.DrawFilledRect(AABB::FromXYWH(kBarX, barY, kBarW * ratio, kBarH), color);

    renderer.DrawString("STAM", kBarX + kBarW + 6.0f, barY - 2.0f,
                        DirectX::Colors::DimGray, 1);

    if (renderer.DebugDraw())
    {
        renderer.DrawString(
            std::format("stam {:6.1f} / {:.0f}   regen delay {:2}{}",
                        m_current, kMax, m_delay, m_depleted ? "  DEPLETED" : ""),
            6.0f, 20.0f, DirectX::Colors::Gainsboro, 1);
    }
}
