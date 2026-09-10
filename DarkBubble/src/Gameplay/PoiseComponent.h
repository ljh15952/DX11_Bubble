// ============================================================================
//  PoiseComponent.h
//    강인도 — 「이 공격에 휘청이는가」를 정한다.
//
//        poise >= impact  ->  버틴다. 데미지만 받는다
//        poise <  impact  ->  휘청인다 (경직 + 넉백)
//
//  ---- ★ 왜 지금 컴포넌트가 되었는가 ----
//    두 가지 이유가 겹쳤다.
//
//    ① **두 번째 사용자가 생겼다** (design.md §9)
//       플레이어만 갖고 있던 것을 적도 갖게 되었다.
//       그 전까지는 PlayerController 안의 ArmorData.poise 로 충분했다.
//
//    ② **기획서가 바뀔 자리라고 미리 적어 두었다** (§3.1.1)
//       > 방식은 문턱(threshold)이다. 나중에 소모(게이지) 방식으로 바꾸려면
//       > 비교 한 줄이 게이지 감산으로 바뀔 뿐이다. 자리를 잡아 두는 것으로 충분하다.
//
//       그 「한 줄」이 아래 WouldStagger 다. 컴포넌트로 빼 두면 나중에
//       게이지로 바꿀 때 **이 파일 하나만** 고치고 플레이어와 적이 동시에 바뀐다.
//
//  ---- 값의 출처는 소유자마다 다르다 ----
//        플레이어 : 장착한 방어구 (F2 로 바뀐다)  -> SetValue 로 갱신
//        적       : 종류마다 타고난 값            -> 생성자에서 한 번
//    컴포넌트는 「어디서 온 값인지」를 모른다. 그래서 양쪽에 붙는다.
// ============================================================================
#pragma once

#include "Core/Component.h"

class PoiseComponent final : public Component
{
public:
    explicit PoiseComponent(int value) : m_value(value) {}

    const char* TypeName() const override { return "Poise"; }

    void Tick(SceneContext& ctx, bool consumeEdgeInput) override;   // 내성 카운트다운

    // ★ 나중에 게이지 방식으로 바뀔 때 고칠 곳은 이 함수 하나다.
    bool WouldStagger(int impact) const;

    // 휘청였다고 알린다 -> 경직 내성 창이 열린다.
    //
    //   ★ 왜 내성이 필요한가:
    //     플레이어는 **피격 무적**으로 무한 경직을 막는데, 적에게 무적을 주면
    //     콤보가 아예 안 들어간다. 그래서 적은 「데미지는 들어가지만 다시
    //     휘청이지는 않는」 창으로 막는다.
    //     회복 직후가 안전해야 루프가 안 생긴다 — 플레이어의 invuln > ticks 와
    //     같은 발상이고, 창의 길이도 경직보다 길게 잡는다.
    void OnStaggered();

    int  Value() const        { return m_value; }
    void SetValue(int v)      { m_value = v; }
    bool Immune() const       { return m_immuneTicks > 0; }
    int  ImmuneTicks() const  { return m_immuneTicks; }

    void Reset();

private:
    int m_value       = 10;
    int m_immuneTicks = 0;
};
