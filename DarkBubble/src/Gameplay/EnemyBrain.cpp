#include "Gameplay/EnemyBrain.h"

#include "Core/Constants.h"
#include "Core/BodyComponent.h"
#include "Core/GameObject.h"
#include "Core/Log.h"
#include "Core/Scene.h"
#include "Audio/Audio.h"
#include "Core/Motion.h"
#include "Gameplay/PartsComponent.h"
#include "Gameplay/PoiseComponent.h"
#include "Graphics/Camera.h"
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
        // ★★ 발끝 26~36 의 **띠**다. 두껍게 만들면 안 된다 —
        //   위아래 양쪽에 「맞지 않아야 하는 것」이 붙어 있기 때문이다:
        //
        //       37 ~ 55   서 있는 머리   ← 닿으면 평타 두 대에 죽는다
        //     ★ 37
        //       중단 띠 (28~37)          팔 20~36 · 몸통 18~37 에 닿는다
        //     ★ 28
        //       ~ 27      웅크린 몸 전체 ← 닿으면 웅크리기가 의미를 잃는다
        //       ~ 25      엎드린 몸 전체
        //
        //   위쪽을 넘기면 잡몹의 평타가 머리에 닿아 두 대에 죽고,
        //   아래쪽을 넘기면 「웅크려 흘린다」가 사라진다. 9픽셀이 그 사이다.
        /*height*/    9.0f,
        /*heightFromFoot*/ 32.5f,
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

    // ---- ★ 시야 (design.md §3.9 B) ----
    //
    //   전에는 거리 하나뿐이라 **등 뒤에 있어도 봤다.** 각도를 더하면
    //   「뒤에서 다가간다」가 성립하고, 「어느 쪽에서 접근할까」가 판단이 된다.
    //
    //       ＼                    ／
    //         ＼      적 ▶      ／      부채꼴 ±55도 · 220
    //           ＼  ( · )     ／        청각 40 — 각도와 무관
    //
    //   ★ 청각이 없으면 **뒤에 붙어 무한히 때릴 수 있다.**
    //     「붙기 전까지는 안전하지만, 때리려면 들킨다」가 되어야 거래가 성립한다.
    //     플레이어의 공격 사거리(12~40)가 청각 반경 40 과 겹치는 것이 요점이다.
    //
    //   ★★ cos 으로 비교한다. 각도를 구하려면 atan2 가 필요하지만,
    //     **비교만 할 거라면 cos 끼리 비교하면 된다** — 삼각함수 호출이 사라진다.
    //     cos 은 0~180도에서 단조감소하므로 「각도가 작다」 = 「cos 이 크다」.
    constexpr float kSightCos      = 0.5736f;   // cos(55도)
    constexpr float kHearRange     =  40.0f;

    //   눈높이. 표시용이고 판정은 발끝 기준이다 — 상수를 하나로 줄이려다
    //   「그림과 판정이 다른」 상태를 만들지 않도록, 쓰는 곳을 표시로 한정한다.
    constexpr float kEyeHeight     =  40.0f;

    //   시야에서 벗어나고 이만큼 지나면 잊는다.
    //   ★ 없으면 기습이 **첫 1회만** 의미 있다 — 한 번 들키면 영원히 쫓기므로
    //     발판으로 도망치는 것도 소용없어진다(6-d 의 「지형이 방패」).
    constexpr int   kForgetTicks   = 120;       // 2초
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
    constexpr float kHurtKnockback = 12.0f;
    constexpr float kHurtLift      =  1.6f;   // 넉백에 섞는 상승. 플레이어(2.0)보다 작다   // 플레이어(22)보다 짧다 — 적이 더 무겁다
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
    m_body   = &Owner().Require<BodyComponent>();
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


bool EnemyBrain::CanSeeTarget() const
{
    const Transform& tr = Owner().transform;
    const float dx = m_target.x - tr.x;
    const float dy = m_target.y - tr.y;
    const float dist2 = dx * dx + dy * dy;

    // ① 청각 — 각도와 무관하다. 바로 뒤에 붙으면 안다.
    if (dist2 <= kHearRange * kHearRange)
        return true;

    // ② 시야 — 거리부터. ★ 제곱끼리 비교해 sqrt 를 미룬다.
    if (dist2 > kSightRange * kSightRange)
        return false;

    const float dist = std::sqrt(dist2);
    if (dist <= 0.0001f)
        return true;

    // ③ 각도. 바라보는 쪽으로 얼마나 기울어 있는가.
    //   ★ dx * facing 은 「바라보는 방향과의 내적」이다. dist 로 나누면 cos 이 된다.
    //   ★ 세로 차이가 크면 자동으로 cos 이 작아진다 — **머리 위는 잘 못 본다.**
    //     발판 위로 올라가면 숨을 수 있는 이유가 이 한 줄에서 나온다.
    return (dx * static_cast<float>(tr.facing)) / dist >= kSightCos;
}


bool EnemyBrain::WouldBite() const
{
    // ---- 어쩔 수 없이 무는 경우 ----
    //     ① 내가 엎드려 있다   : 팔을 휘두를 자세가 안 된다
    //     ② 상대가 엎드려 있다 : 휘둘러 봐야 몸 위로 지나간다(kSwing 주석)
    //
    //   ②가 없으면 엎드린 플레이어가 **무적**이 된다. 흘리는 것과 안 맞는 것은
    //   다르다 — 엎드리기는 휘두르기를 피하는 대신 **물기에 목을 내주는** 거래다.
    if (m_parts->Prone() || m_targetProne)
        return true;

    // ---- ★ 그 밖에는 번갈아 낸다 ----
    //
    //   이것이 없으면 잡몹은 **영원히 휘두르기만** 한다. 그러면 중단(26~36)
    //   밖에 안 나오므로 플레이어의 다리(0~18)를 아무도 못 건드리고,
    //   **다리 파괴도 엎드리기도 도달할 수 없는 기능**이 된다.
    //   실제로 그랬다 — 「엎드려야 물고, 물려야 엎드리는」 순환이었다.
    //
    //   번갈아 두면 두 공격이 서로 다른 대처를 요구한다:
    //       SWING  예고 24틱 · 중단 · 18뎀  -> 구르거나 **엎드려** 흘린다
    //       BITE   예고 18틱 · 하단 · 10뎀  -> 엎드리면 못 피한다. 구른다
    //   예고 길이도 그림도 다르므로 **보고 구분할 수 있다.**
    return m_biteTurn;
}


float EnemyBrain::AttackRange() const
{
    // 이쪽은 latch 가 아니라 **현재 자세**를 본다 —
    // 「지금 다가갈까 공격할까」를 판단하는 값이므로 최신이어야 한다.
    //   ★ 물기는 사거리가 짧다. 엎드린 상대에게는 **더 붙어야** 한다 —
    //     엎드리기가 거리를 벌어 주는 셈이다.
    return WouldBite() ? kBiteRange : kSwingRange;
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
    tr.facing = -1;

    // ★ 세로는 바닥이 정한다. 전에는 270 이라고 적혀 있었는데,
    //   플레이어(260)와 **달랐다** — 벨트스크롤에서는 「깊이가 다른」 것이라
    //   문제가 아니었지만, 플랫포머에서는 둘이 같은 땅을 밟아야 한다.
    m_body->SnapToGround();

    m_parts->Reset();

    m_attackCooldown = 0;
    m_attackIsBite   = false;
    m_biteTurn       = false;   // ★ 첫 공격은 휘두르기. 예고가 길어 배우기 쉽다

    // ★ 잊은 상태에서 시작한다. 부활 직후 적이 이미 노려보고 있으면
    //   「뒤로 돌아 들어간다」를 시도할 기회 자체가 없다.
    m_lostTicks      = kForgetTicks;
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
void EnemyBrain::Stagger(SceneContext& ctx, float fromX, float /*fromY*/)
{
    if (m_state == EnemyState::Dead)
        return;

    const Transform& tr = Owner().transform;

    // 넉백 방향 = 공격자 -> 나. ★ 수평만 — 플레이어의 넉백과 같은 규칙이다.
    const float dx = tr.x - fromX;
    m_knockDirX = (std::abs(dx) > 0.0001f)
        ? ((dx > 0.0f) ? 1.0f : -1.0f)
        : static_cast<float>(tr.facing);

    // ★ 살짝 뜬다. 위에서 내려찍혔을 때 반응이 보이도록 —
    //   점프 공격(§3.8.2)의 타격감이 여기서 나온다.
    m_body->Lift(kHurtLift);

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
        m_sprite->Play(m_parts->Prone() ? kCrawlClip : kIdleClip, true);
        ctx.audio.Play("ui_cancel", 0.5f, -0.4f, PanFromWorldX(Owner().transform.x, ctx.camera.X()));
        break;

    case EnemyState::Attack:
        // ★ 어느 공격인지 여기서 고정한다. 도중에 다리가 부서져도 안 바뀐다.
        m_attackIsBite = WouldBite();

        // ★ 다음 차례를 여기서 뒤집는다. 「직전에 무엇을 냈는가」만 기억하면
        //   번갈아가 성립한다 — 별도의 카운터가 필요 없다.
        m_biteTurn = !m_attackIsBite;
        m_hitThisSwing = false;

        // forceRestart : 같은 공격을 연달아 낼 때 처음부터 다시 재생되어야 한다.
        m_sprite->Play(CurrentAttack().clip, true);

        // ★ 이 소리가 **청각 예고**다. 시각 예고(팔을 젖히는 모션)와 이중으로 둔다.
        //   화면을 안 보고 있어도 반응할 수 있게 해 준다.
        ctx.audio.Play("swing", 0.4f, -0.55f, PanFromWorldX(Owner().transform.x, ctx.camera.X()));
        break;

    case EnemyState::Dead:
        break;   // 마지막 프레임에서 멈춘다
    }

    Log::Info("[enemy] -> {}", EnemyStateName(next));
}


void EnemyBrain::MoveTowardTarget(float speedPerTick)
{
    Transform& tr = Owner().transform;

    // ★ **1차원 추격.** 플랫포머에서 적이 세로로 다가가는 방법은 없다 —
    //   걸어가거나(6-d 의 발판), 못 가거나 둘 중 하나다.
    //   세로를 향해 밀면 그냥 허공을 떠다니게 된다.
    const float dx = m_target.x - tr.x;

    // 바라보는 방향은 거리와 무관하게 갱신한다
    if (dx < -1.0f)     tr.facing = -1;
    else if (dx > 1.0f) tr.facing = +1;

    // ★ 공격 위치에 도달하면 멈춘다.
    //   「멈추는 조건」과 「공격하는 조건」이 같은 함수다 —
    //   따로 두면 「멈췄는데 닿지 않는」 적이 생긴다.
    //   ★ 플레이어가 점프하면 InAttackPosition 의 세로 조건이 깨지므로
    //     적은 **계속 쫓아온다.** 머리 위로 뛰어넘어도 따라붙는다.
    if (InAttackPosition() || std::abs(dx) <= 0.0001f)
        return;

    const float step = ((dx > 0.0f) ? 1.0f : -1.0f) * speedPerTick;

    // ★ 낭떠러지에서 멈춘다.
    //   **움직이기 전에** 묻는다 — 움직인 뒤에 물으면 이미 허공이다.
    //
    //   ★★ 이 한 줄이 지형을 전술로 바꾼다. 발판 위로 올라가면 잡몹이 못
    //     따라오므로 스태미나를 회복할 수 있다. 「도망칠 곳이 있다」는 것이
    //     §3.1 의 리듬(버티다 흘리다 회복)을 지형만으로 만든다.
    //
    //   ※ 적 AI 는 일단 여기까지다. 점프로 쫓아오거나 뛰어내리는 것은 나중에.
    if (m_body->WouldStepOffLedge(tr.x + step))
        return;

    m_body->MoveX(step);
}


void EnemyBrain::Tick(SceneContext& ctx, bool)
{
    ++m_stateTicks;

    // ★ 자세를 몸에 알려 준다. 기어다니면 지형 상자도 낮아져야 한다 —
    //   안 그러면 엎드린 적이 **선 키 그대로** 벽에 걸린다.
    //   적은 웅크리지 않으므로 부위가 아는 자세가 곧 전부다.
    m_body->SetPosture(m_parts->CurrentPosture());

    // 쿨다운도 시간이다. 상태와 무관하게 흐른다.
    if (m_attackCooldown > 0)
        --m_attackCooldown;

    if (m_state != EnemyState::Dead)
    {
        // ★ 인지를 **상태 전이보다 먼저** 갱신한다.
        //   「보인다/안 보인다」가 아니라 **잊어가는 중**으로 둔다 —
        //   시야에서 잠깐 벗어날 때마다 멍해지면 싸움이 토막 난다.
        if (CanSeeTarget()) m_lostTicks = 0;
        else                ++m_lostTicks;

        const bool alerted = (m_lostTicks < kForgetTicks);

        // 사거리 안 + 쿨다운 끝. Chase 와 Crawl 이 공유하는 조건.
        const bool canAttack = InAttackPosition() && (m_attackCooldown <= 0);

        switch (m_state)
        {
        case EnemyState::Idle:
            // ★ 거리만 보던 것을 **시야**로 바꿨다. 등 뒤로 접근하면 안 걸린다.
            if (alerted)
                ChangeState(ctx, m_parts->Prone() ? EnemyState::Crawl
                                                       : EnemyState::Chase);
            break;

        case EnemyState::Chase:
            // ★ 부위 파괴가 행동을 바꾸는 지점.
            if (m_parts->Prone())
            {
                ChangeState(ctx, EnemyState::Crawl);
                break;
            }
            // ★ 잊으면 멈춘다. 발판 위로 도망치면 추격이 끊긴다.
            if (!alerted)  ChangeState(ctx, EnemyState::Idle);
            else if (canAttack) ChangeState(ctx, EnemyState::Attack);
            else           MoveTowardTarget(kWalkPerTick);
            break;

        case EnemyState::Crawl:
            // 기어가는 중에는 다시 일어나지 않는다. 다리는 회복되지 않는다.
            // ★ 하지만 무해하지는 않다 — 사거리에 들어오면 물어뜯는다.
            if (!alerted)  ChangeState(ctx, EnemyState::Idle);
            else if (canAttack) ChangeState(ctx, EnemyState::Attack);
            else           MoveTowardTarget(kCrawlPerTick);
            break;

        case EnemyState::Hurt:
            // ★ 입력도 판단도 없다. 밀려나기만 한다 — 플레이어의 Hurt 와 같은 구조.
            {
                // ★ 넉백도 몸을 거친다. 벽에 밀어붙이면 거기서 멈춘다.
                //   낭떠러지 판정은 **하지 않는다** — 맞아서 밀려 떨어지는 것은
                //   막을 이유가 없다. 「스스로 걸어 나가지 않는다」가 규칙이다.
                const float step = DecayingStep(m_stateTicks, kHurtTicks, kHurtKnockback);
                m_body->MoveX(m_knockDirX * step);
            }
            if (m_stateTicks >= kHurtTicks)
            {
                ChangeState(ctx, m_parts->Prone() ? EnemyState::Crawl
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
                ChangeState(ctx, m_parts->Prone() ? EnemyState::Crawl
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
    if (!renderer.DebugDraw())
        return;

    // ---- ★ 시야 부채꼴 ----
    //   눈에 안 보이면 각도를 **맞출 수가 없다.** 55도가 넓은지 좁은지는
    //   숫자로는 판단이 안 서고, 그려 놓으면 한 번에 보인다.
    //
    //   ※ 선 그리기 도구가 없으므로 **점을 뿌려** 두 변과 호를 만든다.
    //     디버그 표시에 새 렌더 기능을 추가할 만큼의 값은 없다.
    if (!IsDead())
    {
        const Transform& tr = Owner().transform;
        const float eyeY = tr.y - kEyeHeight;
        const float f    = static_cast<float>(tr.facing);

        // cos 에서 sin 을 되찾는다. cos²+sin²=1.
        const float sin55 = std::sqrt(1.0f - kSightCos * kSightCos);

        const bool sees = CanSeeTarget();
        const DirectX::XMVECTOR edge = sees
            ? DirectX::XMVectorSet(1.0f, 0.35f, 0.30f, 0.75f)   // 들켰다
            : DirectX::XMVectorSet(0.45f, 0.75f, 1.00f, 0.45f); // 아직 안 보인다

        // 두 변
        for (float r = 12.0f; r <= kSightRange; r += 9.0f)
        {
            const float px = tr.x + f * kSightCos * r;
            for (int s = -1; s <= 1; s += 2)
            {
                const float py = eyeY + static_cast<float>(s) * sin55 * r;
                renderer.DrawFilledRect({ px - 1.0f, py - 1.0f, px + 1.0f, py + 1.0f }, edge);
            }
        }

        // 호 — 끝의 둥근 경계
        for (float a = -1.0f; a <= 1.0f; a += 0.08f)
        {
            const float c = kSightCos + (1.0f - kSightCos) * (1.0f - std::abs(a));
            const float s = sin55 * a;
            const float px = tr.x + f * c * kSightRange;
            const float py = eyeY + s * kSightRange;
            renderer.DrawFilledRect({ px - 1.0f, py - 1.0f, px + 1.0f, py + 1.0f }, edge);
        }

        // 청각 반경 — 각도와 무관하므로 사각형으로 충분히 읽힌다
        renderer.DrawRectOutline(
            { tr.x - kHearRange, eyeY - kHearRange, tr.x + kHearRange, eyeY + kHearRange },
            DirectX::XMVectorSet(0.9f, 0.9f, 0.4f, 0.40f), 1.0f);
    }

    if (!AttackActive())
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

    // 부위별 HP 는 컴포넌트가 그린다. 위치만 여기서 정한다(화면 오른쪽 위).
    if (renderer.DebugDraw())
        m_parts->DrawHpList(renderer, static_cast<float>(Config::kCanvasWidth) - 150.0f, 40.0f);

    // ★ 「보고 있나 / 잊어가는 중인가」를 글자로도 남긴다.
    //   부채꼴은 F1 을 켜야 보이지만, 이 한 줄은 늘 보인다.
    const std::string sight =
        CanSeeTarget()                ? std::string{ "  [SEES YOU]" }
      : (m_lostTicks < kForgetTicks)  ? std::format("  [losing {}]", kForgetTicks - m_lostTicks)
      :                                 std::string{ "  [unaware]" };

    renderer.DrawString(
        std::format("ENEMY {}{}{}", EnemyStateName(m_state),
                    m_parts->LegsBroken() ? "  (legs broken)" : "",
                    m_attackCooldown > 0 ? std::format("  cd {}", m_attackCooldown)
                                         : std::string{})
            + std::format("  poise {}{}", m_poise->Value(),
                          m_poise->Immune() ? std::format(" (immune {})", m_poise->ImmuneTicks())
                                            : std::string{})
            + sight,
        6.0f, 34.0f,
        m_parts->LegsBroken() ? DirectX::Colors::Orange : DirectX::Colors::Gold, 1);
}
