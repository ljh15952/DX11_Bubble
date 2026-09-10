// ============================================================================
//  PartsComponent.h
//    부위별 HP. 이 게임의 최대 차별점(기획서 §3.2)을 담는 컴포넌트.
//
//  ---- ★ 이 컴포넌트가 「합성」의 증거다 ----
//    적은 부위 파괴가 있고 플레이어는 없다(플레이어는 단일 HP).
//    상속 계층(Entity -> Character -> Player / Enemy)으로 만들었다면
//      · Character 에 넣는다  -> 플레이어가 안 쓰는 코드를 물려받는다
//      · Enemy 에만 넣는다    -> 「부위 파괴가 있는 보스 플레이어」를 못 만든다
//    붙였다 뗐다 할 수 있어야 하는 것 — 그것이 컴포넌트다.
//
//  ---- 자세는 스스로 안다 ----
//    다리가 부서지면 엎드린다. 그 사실은 이 컴포넌트 안에 있으므로
//    바깥이 「지금 엎드렸나」를 알려 줄 필요가 없다.
//    ★ 5-e-3 에서 자세를 **상태로** 판정했다가 상태를 하나 늘린 순간
//      판정 상자가 공중에 뜨는 버그를 냈다(handoff §8).
//      「몸의 성질로 판정한다」를 구조로 못 박아 둔다.
// ============================================================================
#pragma once

#include "Core/AABB.h"
#include "Core/Component.h"

// ============================================================================
//  부위
//    ★ 다리는 좌/우로 나누지 않고 하나다.
//      둘로 나누면 「한쪽만 부서지면 어떻게 되나」가 애매해진다.
// ============================================================================
enum PartIndex
{
    Part_Head = 0,
    Part_Torso,
    Part_Legs,
    Part_Count
};


// ★ HP·이름과 상자 좌표를 나눈다.
//   HP 는 자세와 무관하다 — 엎드려도 머리 HP 는 그대로다.
//   상자는 자세마다 다르다 — 서 있을 때와 엎드렸을 때 머리 위치가 다르다.
//   섞어 두면 「자세가 바뀌었는데 판정 상자가 공중에 떠 있는」 상태가 된다.
struct PartBox
{
    // 발밑 원점 기준 상대 좌표. 오른쪽을 보는 자세로 적는다.
    float left, top, right, bottom;
};


class PartsComponent final : public Component
{
public:
    const char* TypeName() const override { return "Parts"; }

    void Tick(SceneContext& ctx, bool consumeEdgeInput) override;      // 번쩍임 감소
    // ★ Render 가 아니라 RenderDebug 다 — 판정 상자는 그림 **위**에 와야 한다.
    void RenderDebug(Renderer& renderer) override;
    void RenderUI(Renderer& renderer) override;     // 부위별 HP (F1)

    // ---- 조회 ----
    int  Hp(int part)       const { return m_hp[part]; }
    int  MaxHp(int part)    const;
    const char* Name(int part) const;
    bool IsBroken(int part) const { return m_hp[part] <= 0; }

    // 다리가 부서졌는가 = 엎드려 있는가. 자세 판정의 유일한 출처다.
    bool LegsBroken() const { return IsBroken(Part_Legs); }

    // 발밑 원점 + facing 을 적용한 실제 판정 상자.
    AABB Box(int part) const;

    // ---- 판정 ----
    //   공격 히트박스와 **가장 크게 겹치는** 부위. -1 = 안 맞음.
    //
    //   ★ 「겹친 부위 전부」로 하면 한 번 휘두를 때 온몸이 깎여 부위 파괴가
    //     무의미해진다. 「가장 위 부위」로 하면 항상 머리만 맞아 다리를 못 벤다.
    //     겹침 면적이 가장 큰 곳을 고르면 「낮게 휘두르면 다리」가 되어
    //     플레이어가 **조준하게 된다** — 그것이 기획서 §3.2.1 의 목표다.
    int PickHit(const AABB& attackBox) const;

    // ---- 변경 ----
    void Damage(int part, int amount);
    void Flash(int ticks) { m_flash = ticks; }
    int  FlashTicks() const { return m_flash; }

    // 전부 최대 HP 로. 부활(§3.6.1)에서 부른다.
    void Reset();

private:
    int m_hp[Part_Count]{};
    int m_flash = 0;   // 피격 번쩍임 남은 틱
};
