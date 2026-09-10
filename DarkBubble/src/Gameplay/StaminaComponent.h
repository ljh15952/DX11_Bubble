// ============================================================================
//  StaminaComponent.h
//    스태미나. 기획서 §3.1 의 「완전 스태미나제」를 담는다.
//
//  ---- ★ 「고갈」은 스태미나의 상태다 ----
//    전에는 PlayScene 이 `m_stamina.current < 0` 을 여러 곳에서 직접 비교하고,
//    「25 이상 회복되면 경직 해제」라는 규칙도 거기 흩어져 있었다.
//
//    그런데 그 규칙은 **스태미나 자신의 것**이다.
//    `PlayerState::Exhausted` 는 고갈의 **결과**이지 고갈 그 자체가 아니다.
//    래치를 여기로 옮기면 바깥은 `Depleted()` 하나만 물어보면 된다.
//
//      고갈된다   : current 가 0 미만이 된 순간
//      풀린다     : current 가 kExhaustExit 이상으로 회복된 순간
//
//    ★ 히스테리시스(들어가는 문턱 0, 나오는 문턱 25)가 요점이다.
//      같은 값으로 하면 0 근처에서 경직이 들락날락 떨린다.
//
//  ---- 자기 바를 자기가 그린다 ----
//    RenderUI 패스가 있으므로 컴포넌트가 자기 표시를 소유할 수 있다.
//    스태미나가 화면에 안 보이면 게임이 성립하지 않는다 —
//    플레이어가 남은 양을 모르면 관리할 수가 없다.
// ============================================================================
#pragma once

#include "Core/Component.h"

class StaminaComponent final : public Component
{
public:
    const char* TypeName() const override { return "Stamina"; }

    void Tick(SceneContext& ctx, bool consumeEdgeInput) override;        // 회복 + 고갈 래치
    void RenderUI(Renderer& renderer) override;   // 바

    // ---- 쓰기 ----
    //   ★ 부족해도 소모는 나간다. 0 미만이 되면 그 대가로 경직에 들어간다.
    //     「부족하면 행동이 안 나감」이 아니라 「나가고 대가를 치름」이
    //     이 게임의 규칙이다(기획서 §3.1, 방식 B).
    void Spend(int cost);

    // ---- 읽기 ----
    float Current() const     { return m_current; }
    float Max() const;
    int   RegenDelay() const  { return m_delay; }

    // 고갈되었는가. PlayerController 는 이것만 보고 Exhausted 를 켜고 끈다.
    bool  Depleted() const    { return m_depleted; }

    void Reset();

private:
    float m_current  = 0.0f;
    int   m_delay    = 0;     // 회복이 시작되기까지 남은 틱
    bool  m_depleted = false; // 위 주석의 래치
};
