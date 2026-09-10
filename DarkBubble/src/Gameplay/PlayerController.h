// ============================================================================
//  PlayerController.h
//    플레이어의 상태 머신 · 입력 · 무브셋 · 피격. 게임 고유 컴포넌트다.
//
//    PlayScene 에 1,300줄로 남아 있던 것을 여기로 옮겼다.
//    Scene 은 이제 **조립과 두 몸 사이의 판정**만 한다.
//
//  ---- ★ 판정은 여기서 하지 않는다 (EnemyBrain 과 같은 규칙) ----
//    「지금 공격 판정이 켜져 있고 상자는 여기다」까지만 말한다.
//    실제로 적을 때렸는지는 **PlayScene 이** 검사한다.
//    그래야 플레이어가 적의 존재를 몰라도 되고,
//    「플레이어 → 적」과 「적 → 플레이어」가 같은 모양이 된다.
// ============================================================================
#pragma once

#include "Core/AABB.h"
#include "Core/Component.h"
#include "Gameplay/AttackData.h"

class PoiseComponent;
class SpriteComponent;
class StaminaComponent;


// ============================================================================
//  PlayerState
//    ★ 상태는 늘어나지 않는다. 다양성은 **데이터**(프레임 데이터)에서 나온다.
//      단검도 대검도 전부 Attack 을 쓰고 발생·지속·후딜 숫자만 다르다.
//      공격이 4종이어도 Attack 은 하나다.
//
//    ※ 원칙의 진짜 내용은 「조건이 여러 곳에 흩어지면 상태로 승격한다」이므로,
//      플랫포머 전환 때 Jump 가 추가되어 8개가 된다(design.md §3.8.1).
// ============================================================================
enum class PlayerState
{
    Idle,
    Run,
    Attack,
    Roll,        // 이동이 목적이 아니라 **무적 프레임**이 목적이다
    Exhausted,   // 스태미나 고갈 경직. 부족해도 행동은 나가고 대가를 뒤에 치른다
    Hurt,        // 피격 경직. 들어갈지 말지는 **강인도(poise)가 정한다**
    Dead,
};

const char* PlayerStateName(PlayerState s);


// ============================================================================
//  RollData — 구르기 프레임 데이터
//
//      t0        windup           +invincible          +recovery
//      │─ 준비 ─│──── ★무적★ ────│───── 후딜 ──────│
//      │  맞는다 │    안 맞는다     │     맞는다        │
//
//    ★ 앞뒤가 취약한 것이 핵심이다. 무적이 처음부터 끝까지 있으면
//      「구르기 연타 = 무적」이 되어 게임이 무너진다.
//      공격의 startup/active/recovery 와 완전히 같은 발상이다.
// ============================================================================
struct RollData
{
    int windup     = 4;    // 무적 전 (맞는다)
    int invincible = 12;   // 무적 구간
    int recovery   = 10;   // 후딜 (맞는다)

    float distance    = 80.0f;
    int   staminaCost = 30;   // 공격(28)보다 살짝 비싸다 = 구르기는 공짜가 아니다

    int TotalTicks() const { return windup + invincible + recovery; }
};


// ============================================================================
//  HurtData — 피격 경직
//
//    ★ invuln > ticks 여야 한다.
//      같게 두면 **경직이 풀리는 그 틱에 다시 맞는다.** 적이 두 마리면
//      영원히 못 움직인다 — 스턴락이다. 버그처럼 보이지 않아 숫자로 못 박아 둔다.
// ============================================================================
struct HurtData
{
    int   ticks     = 18;
    int   invuln    = 24;    // ★ 반드시 ticks 보다 길게
    float knockback = 22.0f; // 넉백은 타격감 때문만이 아니다 —
                             // 밀려나면 적의 다음 공격 사거리에서 벗어난다
};


// ============================================================================
//  ArmorData — 강인도(poise)
//
//        poise >= impact  →  데미지만 받고 자리에서 버틴다 (경직 없음)
//        poise <  impact  →  Hurt(경직) + 넉백 + 피격 무적
//
//    ★ 버티기는 안전한 것이 아니라 **다른 지불 방식**이다.
//      구르기는 스태미나로, 버티기는 HP 로 낸다.
// ============================================================================
struct ArmorData
{
    const char* name  = "CLOTH";
    int         poise = 10;
};


class PlayerController final : public Component
{
public:
    const char* TypeName() const override { return "PlayerController"; }

    void Start(SceneContext& ctx) override;
    void Tick(SceneContext& ctx, bool consumeEdgeInput) override;
    void Render(Renderer& renderer) override;        // 틴트·눌림을 스프라이트에 넘긴다
    void RenderDebug(Renderer& renderer) override;   // hurtbox · 공격 상자 · 원점
    void RenderUI(Renderer& renderer) override;      // 상태 표시 · HP 바 · 방어구

    // ---- 조회 (PlayScene 의 판정이 쓴다) ----
    PlayerState State()      const { return m_state; }
    int         StateTicks() const { return m_stateTicks; }
    bool        IsDead()     const { return m_state == PlayerState::Dead; }

    AABB Hurtbox() const;                     // 내가 맞는 범위 (몸)
    AABB AttackHitbox() const;                // 내가 때리는 범위 (무기)
    bool AttackActive() const;                // 지금 판정이 존재하는가
    const AttackData& CurrentAttack() const;

    bool HitThisSwing() const { return m_hitThisSwing; }
    void MarkHitThisSwing()   { m_hitThisSwing = true; }

    // ★ 무적은 두 종류다. 같은 함수로 판정되지만 성격이 다르다.
    //     ① 구르기 무적 — 플레이어가 **벌어낸** 것 (스태미나 + 타이밍)
    //     ② 피격 무적   — 게임이 **주는** 안전장치 (스턴락 방지)
    //   목적이 다르므로 길이를 따로 두고 F1 표시 색도 나눈다.
    //   ★ ②는 **경직의 부속품**이다. 강인도로 버텨내면 못 움직이는 구간이
    //     없으므로 스턴락 위험도 없고, 그래서 피격 무적도 주지 않는다.
    bool RollInvincible() const;
    bool Invincible()     const;

    // 사망 화면을 띄울 때가 되었는가. 한 번만 true 를 돌려준다.
    // ★ Scene 전환은 Scene 의 일이라 여기서 Push 하지 않는다 —
    //   Gameplay 가 Scenes 를 알기 시작하면 층이 뒤엉킨다.
    bool ConsumeDeathScreenRequest();

    // ---- 변경 ----
    // fromX / fromY = **공격자의 위치.** 넉백 방향이 여기서 나온다.
    //   ★ 컨트롤러가 「누가 때렸는지」를 알 필요는 없다. 좌표 하나면 충분하다 —
    //     그래서 적이 몇 종이 되든, 나중에 함정이나 투사체가 생겨도 그대로다.
    void TakeHit(SceneContext& ctx, const AttackData& atk, float fromX, float fromY);
    void Respawn(SceneContext& ctx);

    const ArmorData& Armor() const;

private:
    void ChangeState(SceneContext& ctx, PlayerState next, bool force = false);
    const AttackData& SelectAttack(SceneContext& ctx, PlayerState prev) const;

    void UpdateMovement(SceneContext& ctx, float moveX, float moveY);

    // ★ 남은 틱에 비례해 감속하며 미끄러진다. 총 이동량이 distance 가 되도록 정규화.
    //   구르기와 넉백이 공유한다 — 일정 속도로 움직이면 둘 다 어색하다.
    void SlideDecaying(float dirX, float dirY, float distance, int totalTicks);

    AABB SpriteBounds() const;

    // Start 에서 캐시한다. 널이 될 수 없다(Require).
    SpriteComponent*  m_sprite  = nullptr;
    PoiseComponent*   m_poise   = nullptr;
    StaminaComponent* m_stamina = nullptr;

    PlayerState m_state      = PlayerState::Idle;
    int         m_stateTicks = 0;

    // ★ 플레이어는 아직 **단일 HP** 다.
    //   6-c-4 에서 PartsComponent 로 바뀐다(design.md §3.2.2).
    int m_hp    = 100;
    int m_flash = 0;   // 피격 번쩍임 남은 틱

    int m_invulnTicks = 0;

    // 구르기 방향은 **시작 시점에 고정**된다. 중간에 방향키를 바꿔도 무시된다.
    float m_rollDirX = 1.0f, m_rollDirY = 0.0f;
    float m_knockDirX = -1.0f, m_knockDirY = 0.0f;

    // ★ 무브셋 : 어느 공격인지도 시작 시점에 고정된다.
    //   매 틱 다시 고르면 휘두르는 도중에 Ctrl 을 떼는 순간 프레임 데이터가 갈려
    //   active 구간을 건너뛰거나 두 번 지나간다.
    const AttackData* m_currentAttack = nullptr;
    int  m_comboStep   = 0;      // 0 = 1타, 1 = 2타. 2타에서는 더 안 이어진다
    bool m_comboQueued = false;
    bool m_hitThisSwing = false;

    bool m_crouching    = false; // 수식자다. 상태가 아니다
    int  m_stepCooldown = 0;

    int  m_armorIndex = 0;       // F2 로 바뀐다(임시)
    bool m_deathScreenRequested = false;
};
