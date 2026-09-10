#include "Gameplay/EnemyBrain.h"

#include "Core/Constants.h"
#include "Core/GameObject.h"
#include "Core/Log.h"
#include "Core/Scene.h"
#include "Audio/Audio.h"
#include "Core/Motion.h"
#include "Gameplay/PartsComponent.h"
#include "Gameplay/PoiseComponent.h"
#include "Graphics/Renderer.h"
#include "Graphics/SpriteComponent.h"

#include <DirectXColors.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace
{
    // ---- 애니메이션 클립 (enemy.png : 6열 × 4행) ----
    //   idle 과 chase 는 같은 그림을 속도만 바꿔 쓴다.
    constexpr AnimationClip kIdleClip  { /*row*/ 0, 4, 14, /*loop*/ true  };
    constexpr AnimationClip kChaseClip { /*row*/ 0, 4,  6, /*loop*/ true  };
    constexpr AnimationClip kCrawlClip { /*row*/ 1, 4, 10, /*loop*/ true  };
    constexpr AnimationClip kSwingClip { /*row*/ 2, 5, 12, /*loop*/ false };
    constexpr AnimationClip kBiteClip  { /*row*/ 3, 6,  9, /*loop*/ false };

    // ---- 공격 ----
    //   ★ startup 24틱은 일부러 길다. 예고를 보고 반응할 시간이다.
    //     플레이어 구르기의 무적은 [t+4, t+16) 이고 판정은 [24, 28) 이므로
    //     9틱(= invincible 12 − active 4 + 1)의 회피 창이 생긴다.
    //   ★ 24 / 12 = 2 — 팔이 뻗는 프레임이 판정과 **같은 틱**에 시작한다.
    //     어긋나면 플레이어가 아무리 연습해도 회피를 배울 수 없다.
    constexpr AttackData kSwing{
        /*name*/     "SWING",
        /*startup*/  24,
        /*active*/    4,
        /*recovery*/ 32,            // 합계 60 = 5프레임 × 12틱
        /*reach*/     8.0f,
        /*width*/    26.0f,
        /*height*/   26.0f,
        /*heightFromFoot*/ 30.0f,
        /*damage*/   18,
        /*staminaCost*/ 0,          // 적은 스태미나를 쓰지 않는다
        /*impact*/   18,
        /*clip*/     kSwingClip,
    };

    // 물어뜯기 — 다리가 부서져 기어다닐 때.
    //   ★ 「다리 파괴 = 무해」로 만들지 않기 위한 데이터다.
    //     그렇게 하면 항상 다리부터 노리는 것이 정답이 되어
    //     기획서 §3.2 의 전술적 선택이 사라진다.
    constexpr AttackData kBite{
        /*name*/     "BITE",
        /*startup*/  18,            // 예고가 짧다 (18 / 9 = 2)
        /*active*/    3,
        /*recovery*/ 33,            // 합계 54 = 6프레임 × 9틱
        /*reach*/     4.0f,
        /*width*/    20.0f,
        /*height*/   16.0f,
        /*heightFromFoot*/ 12.0f,   // 낮게 — 발밑을 노린다
        /*damage*/   10,
        /*staminaCost*/ 0,
        /*impact*/   12,
        /*clip*/     kBiteClip,
    };

    constexpr int kAttackCooldown = 40;   // 0.67 초. 없으면 사거리 안에서 무한 공격

    constexpr float kSightRange    = 220.0f;
    constexpr float kWalkPerTick   = 60.0f / 60.0f;
    constexpr float kCrawlPerTick  = 18.0f / 60.0f;

    // ---- 공격 위치 ----
    //   ★ 가로(사거리)와 세로(허용폭)를 따로 둔다. 히트박스가 가로로 뻗으므로
    //     원형 거리로 판정하면 위아래로 떨어진 플레이어를 영원히 헛친다.
    //   ★ 그리고 이 값들이 「멈추는 위치」와 「공격하는 위치」 양쪽에 쓰인다.
    //     따로 두면 「멈췄는데 닿지 않는」 적이 생긴다.
    constexpr float kSwingRange  = 34.0f;
    constexpr float kBiteRange   = 20.0f;
    constexpr float kYTolerance  = 14.0f;

    constexpr int kFlashTicks = 9;

    // ---- 경직 ----
    //   ★ 경직 내성(PoiseComponent 30틱)보다 짧다. 회복되는 그 틱에 다시
    //     휘청이면 무한 루프가 된다.
    constexpr int   kHurtTicks     = 16;
    constexpr float kHurtKnockback = 12.0f;   // 플레이어(22)보다 짧다 — 적이 더 무겁다
}


const char* EnemyStateName(EnemyState s)
{
    switch (s)
    {
    case EnemyState::Chase:  return "CHASE";
    case EnemyState::Attack: return "ATTACK";
    case EnemyState::Hurt:   return "HURT";
    case EnemyState::Crawl:  return "CRAWL";
    case EnemyState::Dead:   return "DEAD";
    default:                 return "IDLE";
    }
}


// ----------------------------------------------------------------------------
//  Start — ★ 컴포넌트끼리 손을 잡는 곳
// ----------------------------------------------------------------------------
void EnemyBrain::Start(SceneContext& ctx)
{
    // Require : 없으면 조립이 잘못된 것이므로 그 자리에서 죽는다.
    m_parts  = &Owner().Require<PartsComponent>();
    m_poise  = &Owner().Require<PoiseComponent>();
    m_sprite = &Owner().Require<SpriteComponent>();

    Reset(ctx);
}


const AttackData& EnemyBrain::CurrentAttack() const
{
    // ★ latch 된 값을 본다. 매 틱 다시 고르면 휘두르는 도중에 다리가 부서지는
    //   순간 프레임 데이터가 통째로 바뀌어(60틱 -> 54틱) active 를 건너뛴다.
    return m_attackIsBite ? kBite : kSwing;
}


float EnemyBrain::AttackRange() const
{
    // 이쪽은 latch 가 아니라 **현재 자세**를 본다 —
    // 「지금 다가갈까 공격할까」를 판단하는 값이므로 최신이어야 한다.
    return m_parts->LegsBroken() ? kBiteRange : kSwingRange;
}


bool EnemyBrain::InAttackPosition() const
{
    const Transform& tr = Owner().transform;
    const float dx = std::abs(m_target.x - tr.x);
    const float dy = std::abs(m_target.y - tr.y);
    return dx <= AttackRange() && dy <= kYTolerance;
}


bool EnemyBrain::AttackActive() const
{
    if (m_state != EnemyState::Attack)
        return false;

    const AttackData& a = CurrentAttack();
    return m_stateTicks >= a.startup
        && m_stateTicks <  a.startup + a.active;
}


bool EnemyBrain::Telegraph() const
{
    return m_state == EnemyState::Attack
        && m_stateTicks < CurrentAttack().startup;
}


AABB EnemyBrain::AttackHitbox() const
{
    const Transform& tr = Owner().transform;
    return MakeAttackBox(tr.x, tr.y, tr.facing, CurrentAttack());
}


void EnemyBrain::Reset(SceneContext& ctx)
{
    Transform& tr = Owner().transform;
    tr.x = 470.0f;
    tr.y = 270.0f;
    tr.facing = -1;

    m_parts->Reset();

    m_attackCooldown = 0;
    m_attackIsBite   = false;
    m_hitThisSwing   = false;

    // ★ ChangeState 를 쓰지 않는다 — 「같은 상태로의 전이는 무시」에 걸린다.
    //   리셋은 상태를 직접 놓고 애니메이션을 forceRestart 로 다시 건다.
    m_state      = EnemyState::Idle;
    m_stateTicks = 0;
    m_sprite->Play(kIdleClip, true);
    m_sprite->ClearTint();

    (void)ctx;
}


// ----------------------------------------------------------------------------
//  Stagger — 휘청인다. 휘두르던 공격이 취소된다.
// ----------------------------------------------------------------------------
void EnemyBrain::Stagger(SceneContext& ctx, float fromX, float fromY)
{
    if (m_state == EnemyState::Dead)
        return;

    const Transform& tr = Owner().transform;

    // 넉백 방향 = 공격자 -> 나
    float dx = tr.x - fromX;
    float dy = tr.y - fromY;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0001f) { dx /= len; dy /= len; }
    else               { dx = static_cast<float>(tr.facing); dy = 0.0f; }
    m_knockDirX = dx;
    m_knockDirY = dy;

    m_poise->OnStaggered();

    // ★ force 가 필요 없다 — Attack 에서 Hurt 로 가는 것이라 상태가 다르다.
    //   그리고 이 전이 자체가 「공격 취소」다. 취소를 위한 코드가 따로 없다.
    ChangeState(ctx, EnemyState::Hurt);
}


void EnemyBrain::Kill(SceneContext& ctx)
{
    ChangeState(ctx, EnemyState::Dead);
}


void EnemyBrain::ChangeState(SceneContext& ctx, EnemyState next)
{
    if (m_state == next)
        return;

    m_state      = next;
    m_stateTicks = 0;

    switch (next)
    {
    case EnemyState::Idle:  m_sprite->Play(kIdleClip);  break;
    case EnemyState::Chase: m_sprite->Play(kChaseClip); break;
    case EnemyState::Crawl: m_sprite->Play(kCrawlClip); break;

    case EnemyState::Hurt:
        // 전용 그림이 없으므로 자세를 유지하고 틴트로 구분한다(ApplyTint).
        m_sprite->Play(m_parts->LegsBroken() ? kCrawlClip : kIdleClip, true);
        ctx.audio.Play("ui_cancel", 0.5f, -0.4f, PanFromCanvasX(Owner().transform.x));
        break;

    case EnemyState::Attack:
        // ★ 어느 공격인지 여기서 고정한다. 도중에 다리가 부서져도 안 바뀐다.
        m_attackIsBite = m_parts->LegsBroken();
        m_hitThisSwing = false;

        // forceRestart : 같은 공격을 연달아 낼 때 처음부터 다시 재생되어야 한다.
        m_sprite->Play(CurrentAttack().clip, true);

        // ★ 이 소리가 **청각 예고**다. 시각 예고(팔을 젖히는 모션)와 이중으로 둔다.
        //   화면을 안 보고 있어도 반응할 수 있게 해 준다.
        ctx.audio.Play("swing", 0.4f, -0.55f, PanFromCanvasX(Owner().transform.x));
        break;

    case EnemyState::Dead:
        break;   // 마지막 프레임에서 멈춘다
    }

    Log::Info("[enemy] -> {}", EnemyStateName(next));
}


void EnemyBrain::MoveTowardTarget(float speedPerTick)
{
    Transform& tr = Owner().transform;

    const float dx = m_target.x - tr.x;
    const float dy = m_target.y - tr.y;
    const float dist = std::sqrt(dx * dx + dy * dy);

    // 바라보는 방향은 거리와 무관하게 갱신한다
    if (dx < -1.0f)     tr.facing = -1;
    else if (dx > 1.0f) tr.facing = +1;

    // ★ 공격 위치에 도달하면 멈춘다.
    //   「멈추는 조건」과 「공격하는 조건」이 같은 함수다 —
    //   따로 두면 「멈췄는데 닿지 않는」 적이 생긴다.
    if (InAttackPosition() || dist <= 0.0001f)
        return;

    tr.x += dx / dist * speedPerTick;
    tr.y += dy / dist * speedPerTick;

    tr.x = std::clamp(tr.x, 32.0f, static_cast<float>(Config::kCanvasWidth) - 32.0f);
    tr.y = std::clamp(tr.y, 64.0f, static_cast<float>(Config::kCanvasHeight));
}


void EnemyBrain::Tick(SceneContext& ctx, bool)
{
    ++m_stateTicks;

    // 쿨다운도 시간이다. 상태와 무관하게 흐른다.
    if (m_attackCooldown > 0)
        --m_attackCooldown;

    if (m_state != EnemyState::Dead)
    {
        const Transform& tr = Owner().transform;
        const float dx = m_target.x - tr.x;
        const float dy = m_target.y - tr.y;
        const float dist = std::sqrt(dx * dx + dy * dy);

        // 사거리 안 + 쿨다운 끝. Chase 와 Crawl 이 공유하는 조건.
        const bool canAttack = InAttackPosition() && (m_attackCooldown <= 0);

        switch (m_state)
        {
        case EnemyState::Idle:
            if (dist <= kSightRange)
                ChangeState(ctx, m_parts->LegsBroken() ? EnemyState::Crawl
                                                       : EnemyState::Chase);
            break;

        case EnemyState::Chase:
            // ★ 부위 파괴가 행동을 바꾸는 지점.
            if (m_parts->LegsBroken())
            {
                ChangeState(ctx, EnemyState::Crawl);
                break;
            }
            if (canAttack) ChangeState(ctx, EnemyState::Attack);
            else           MoveTowardTarget(kWalkPerTick);
            break;

        case EnemyState::Crawl:
            // 기어가는 중에는 다시 일어나지 않는다. 다리는 회복되지 않는다.
            // ★ 하지만 무해하지는 않다 — 사거리에 들어오면 물어뜯는다.
            if (canAttack) ChangeState(ctx, EnemyState::Attack);
            else           MoveTowardTarget(kCrawlPerTick);
            break;

        case EnemyState::Hurt:
            // ★ 입력도 판단도 없다. 밀려나기만 한다 — 플레이어의 Hurt 와 같은 구조.
            {
                const float step = DecayingStep(m_stateTicks, kHurtTicks, kHurtKnockback);
                Transform& tr2 = Owner().transform;
                tr2.x = std::clamp(tr2.x + m_knockDirX * step, 32.0f,
                                   static_cast<float>(Config::kCanvasWidth) - 32.0f);
                tr2.y = std::clamp(tr2.y + m_knockDirY * step, 64.0f,
                                   static_cast<float>(Config::kCanvasHeight));
            }
            if (m_stateTicks >= kHurtTicks)
            {
                ChangeState(ctx, m_parts->LegsBroken() ? EnemyState::Crawl
                                                       : EnemyState::Chase);
            }
            break;

        case EnemyState::Attack:
            // ★ 이동하지 않는다. 한 번 휘두르면 끝까지 간다.
            //   그래서 플레이어가 **걸어서 빠져나갈 수도** 있다 —
            //   회피 수단이 구르기 하나만인 게임이 되지 않는다.
            //
            //   ※ 실제 피격 판정은 여기서 하지 않는다(헤더 주석 참조).
            if (m_stateTicks >= CurrentAttack().TotalTicks())
            {
                m_attackCooldown = kAttackCooldown;
                ChangeState(ctx, m_parts->LegsBroken() ? EnemyState::Crawl
                                                       : EnemyState::Chase);
            }
            break;

        default:
            break;
        }
    }

    ApplyTint();
}


// ----------------------------------------------------------------------------
//  ApplyTint — 스프라이트의 색을 **한 곳에서** 정한다
//
//    죽음 · 피격 · 판정 중 · 예고 네 가지가 색을 요구한다.
//    각자 자기 시점에 SetTint 를 부르면 마지막에 부른 쪽이 이기고,
//    「왜 이 색이지」를 추적할 수 없게 된다. 쓰는 곳을 하나로 모은다.
// ----------------------------------------------------------------------------
void EnemyBrain::ApplyTint()
{
    if (IsDead())
        m_sprite->SetTint(DirectX::XMVectorSet(0.35f, 0.30f, 0.32f, 1.0f));
    else if (m_state == EnemyState::Hurt)
        m_sprite->SetTint(DirectX::XMVectorSet(1.00f, 0.90f, 0.45f, 1.0f));   // 휘청 = 노랗게
    else if (m_parts->FlashTicks() > 0)
        m_sprite->SetTint(DirectX::XMVectorSet(1.00f, 0.75f, 0.70f, 1.0f));
    else if (AttackActive())
        m_sprite->SetTint(DirectX::XMVectorSet(1.00f, 0.45f, 0.35f, 1.0f));
    else if (Telegraph())
        m_sprite->SetTint(DirectX::XMVectorSet(1.00f, 0.78f, 0.60f, 1.0f));
    else
        m_sprite->ClearTint();
}


void EnemyBrain::Render(Renderer& renderer)
{
    // ---- ★ 공격 예고 (telegraph) ----
    //   적의 startup 이 24틱이든 60틱이든, **플레이어가 그 시작을 볼 수 없으면
    //   회피는 운이다.** 격투게임과 소울류가 예외 없이 예고를 주는 이유다.
    //
    //   ★ 예고가 두 겹이고, 그게 그대로 기획서의 설계다.
    //     ① 애니메이션(팔을 젖힌다) — 누구에게나 보이지만 **읽는 기술이 필요하다**
    //     ② 이 `!` 표시            — 기획서 §1.1 의 「初心者の指輪」
    //     ①만 있으면 초보자가 반응 못 하고 ②만 있으면 숙련의 여지가 없다.
    //     나중에 이 if 를 지문 장착 여부로 감싸면 그대로 첫 아이템이 된다.
    if (Telegraph())
    {
        const AABB head = m_parts->Box(Part_Head);
        renderer.DrawString("!",
            std::round((head.left + head.right) * 0.5f - 8.0f),   // 8x14 폰트 x2 = 폭 16
            std::round(head.top - 30.0f),
            DirectX::Colors::Red, 2);
    }

}


// ★ 예고 `!` 와 공격 히트박스를 다른 패스에 둔 이유:
//   `!` 는 **플레이어에게 보여 주는 게임 요소**라 스프라이트와 같은 층에 있어야 하고,
//   히트박스는 **개발자용 표시**라 무조건 맨 위여야 한다. 목적이 다르면 층도 다르다.
void EnemyBrain::RenderDebug(Renderer& renderer)
{
    if (!renderer.DebugDraw() || !AttackActive())
        return;

    renderer.DrawFilledRect(AttackHitbox(),
        DirectX::XMVectorSet(1.0f, 0.55f, 0.10f, 0.35f));
    renderer.DrawRectOutline(AttackHitbox(), DirectX::Colors::Orange, 2.0f);
}


// ★ 적 상태 한 줄. 부위 파괴가 행동을 바꾸는 것을 눈으로 확인하는 표시다.
void EnemyBrain::RenderUI(Renderer& renderer)
{
    if (m_state == EnemyState::Attack)
    {
        // 플레이어와 같은 프레임 데이터 표시를 쓴다 — 구조가 같으니 도구도 같다.
        const AttackData& a = CurrentAttack();
        const char* phase = (m_stateTicks <  a.startup)            ? "startup"
                          : (m_stateTicks <  a.startup + a.active) ? "ACTIVE"
                          :                                          "recovery";
        renderer.DrawString(
            std::format("ENEMY {} t{:<3}{}   [{} {} {}]  imp {}",
                        a.name, m_stateTicks, phase,
                        a.startup, a.active, a.recovery, a.impact),
            6.0f, 34.0f,
            AttackActive() ? DirectX::Colors::Red : DirectX::Colors::Orange, 1);
        return;
    }

    renderer.DrawString(
        std::format("ENEMY {}{}{}", EnemyStateName(m_state),
                    m_parts->LegsBroken() ? "  (legs broken)" : "",
                    m_attackCooldown > 0 ? std::format("  cd {}", m_attackCooldown)
                                         : std::string{})
            + std::format("  poise {}{}", m_poise->Value(),
                          m_poise->Immune() ? std::format(" (immune {})", m_poise->ImmuneTicks())
                                            : std::string{}),
        6.0f, 34.0f,
        m_parts->LegsBroken() ? DirectX::Colors::Orange : DirectX::Colors::Gold, 1);
}
