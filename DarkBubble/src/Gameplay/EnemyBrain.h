// ============================================================================
//  EnemyBrain.h
//    적의 상태 머신 · 추격 · 공격. 게임 고유 컴포넌트다.
//
//  ---- ★ 컴포넌트끼리 어떻게 대화하는가 ----
//    Start() 에서 Require<T>() 로 찾아 **포인터를 캐시**한다(Unity 방식).
//
//      · 생성자가 아니라 Start 인 이유 : 생성자 시점에는 뒤에 붙을 컴포넌트가
//        아직 없다. 조립이 끝난 뒤가 Start 다.
//      · Get 이 아니라 Require 인 이유 : 스프라이트도 부위도 **없으면 안 된다.**
//        없으면 조립이 잘못된 것이고, 그 자리에서 죽는 편이 낫다.
//      · 매번 Get 하지 않는 이유 : dynamic_cast 가 매 틱 돈다. 그리고 장황하다.
//
//  ---- ★ 판정은 여기서 하지 않는다 ----
//    이 컴포넌트는 「지금 공격 판정이 켜져 있고 상자는 여기다」까지만 말한다.
//    실제로 플레이어를 때렸는지는 **PlayScene 이** 검사한다.
//
//    그래야 EnemyBrain 이 플레이어의 존재를 몰라도 되고,
//    「플레이어 -> 적」 방향과 「적 -> 플레이어」 방향이 **같은 모양**이 된다.
//    양쪽 판정을 아는 것은 둘 다 보고 있는 Scene 하나뿐이다.
// ============================================================================
#pragma once

#include "Core/AABB.h"
#include "Core/Component.h"
#include "Core/Transform.h"
#include "Gameplay/AttackData.h"

class PartsComponent;
class SpriteComponent;

// ============================================================================
//  EnemyState — 플레이어와 같은 구조의 상태 머신
//
//        ┌──────┐  발견   ┌───────┐  사거리   ┌────────┐
//        │ Idle │ ──────→ │ Chase │ ────────→ │ Attack │
//        └──────┘         └───┬───┘           └────────┘
//                             │ 다리 파괴
//                             ↓
//                         ┌───────┐
//                         │ Crawl │  기어서 쫓아온다
//                         └───────┘
//
//    ★ 부위 파괴의 진짜 의미는 데미지가 아니라 **행동의 변화**다.
// ============================================================================
enum class EnemyState
{
    Idle,
    Chase,
    Attack,
    Crawl,
    Dead,
};

const char* EnemyStateName(EnemyState s);


class EnemyBrain final : public Component
{
public:
    // 쫓아갈 대상. ★ GameObject 가 아니라 Transform 만 받는다 —
    //   필요한 것이 위치뿐이므로, 그 이상을 알면 결합만 늘어난다.
    explicit EnemyBrain(const Transform& target) : m_target(target) {}

    const char* TypeName() const override { return "EnemyBrain"; }

    void Start(SceneContext& ctx) override;
    void Tick(SceneContext& ctx, bool consumeEdgeInput) override;
    void Render(Renderer& renderer) override;        // 예고 `!` — **게임 요소다**
    void RenderDebug(Renderer& renderer) override;   // 공격 상자 — 디버그다
    void RenderUI(Renderer& renderer) override;      // 상태 한 줄

    // ---- 조회 ----
    EnemyState State()      const { return m_state; }
    int        StateTicks() const { return m_stateTicks; }
    int        Cooldown()   const { return m_attackCooldown; }
    bool       IsDead()     const { return m_state == EnemyState::Dead; }

    const AttackData& CurrentAttack() const;
    bool  AttackActive() const;
    bool  Telegraph()    const;   // startup 구간 = 예고
    AABB  AttackHitbox() const;

    // 이번 휘두르기에 이미 맞췄는가. active 가 4틱이면 이것 없이는 4번 들어간다.
    bool HitThisSwing() const { return m_hitThisSwing; }
    void MarkHitThisSwing()   { m_hitThisSwing = true; }

    // ---- 변경 ----
    void Kill(SceneContext& ctx);    // 몸통·머리가 부서졌을 때 PlayScene 이 부른다
    void Reset(SceneContext& ctx);   // 부활(§3.6.1)

private:
    void ChangeState(SceneContext& ctx, EnemyState next);
    void MoveTowardTarget(float speedPerTick);
    bool InAttackPosition() const;
    float AttackRange() const;
    void ApplyTint();

    const Transform& m_target;

    // Start 에서 캐시한다. 널이 될 수 없다(Require).
    PartsComponent*  m_parts  = nullptr;
    SpriteComponent* m_sprite = nullptr;

    EnemyState m_state      = EnemyState::Idle;
    int        m_stateTicks = 0;

    int  m_attackCooldown = 0;
    bool m_attackIsBite   = false;   // ★ 공격 시작 시점에 고정된다
    bool m_hitThisSwing   = false;
};
