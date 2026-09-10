#include "Gameplay/PoiseComponent.h"

namespace
{
    // ★ 경직 내성. 경직(적 16틱)보다 길다 —
    //   회복되는 그 틱에 다시 휘청이면 무한 루프가 된다.
    //   플레이어의 「피격 무적(24) > 경직(18)」과 완전히 같은 이유다.
    constexpr int kStaggerImmuneTicks = 30;   // 0.5 초
}


void PoiseComponent::Reset()
{
    m_immuneTicks = 0;
}


void PoiseComponent::Tick(SceneContext&, bool)
{
    if (m_immuneTicks > 0)
        --m_immuneTicks;
}


bool PoiseComponent::WouldStagger(int impact) const
{
    // ★ 지금은 문턱 비교 한 줄이다.
    //   게이지 방식(다크소울 3식)으로 바꾸려면 여기가
    //   「게이지를 impact 만큼 깎고, 0 이하가 되었는가」로 바뀐다.
    //   부르는 쪽은 한 줄도 안 고쳐도 된다 — 그것이 이 컴포넌트의 값이다.
    return !Immune() && m_value < impact;
}


void PoiseComponent::OnStaggered()
{
    m_immuneTicks = kStaggerImmuneTicks;
}
