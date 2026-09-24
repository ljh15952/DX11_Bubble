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

    // ---- ★ 죽은 자세 ----
    //   **프레임 하나 · 반복 없음** = 그 자리에서 멎는다.
    //
    //   ★★ 전에는 죽을 때 클립을 아예 안 걸고 `break` 만 두고서
    //     「마지막 프레임에서 멈춘다」고 적어 두었다. 그건 규칙이 아니라
    //     **직전 클립에 대한 가정**이었다 — 비반복 클립(공격) 중에 죽으면
    //     맞았지만, 추격·대기처럼 **반복 클립** 중에 죽으면 시체가 영원히
    //     걸어 다녔다. 그리고 죽는 순간의 상태는 대부분 그쪽이다.
    //
    //   ★★ **전용 「쓰러짐」 행을 쓴다.** 전에는 기어가기 행을 빌려 썼는데,
    //     기어가기는 **다리가 잘렸을 때의 자세**라 어떻게 죽든 다리가 잘린 채
    //     죽은 것처럼 보였다.
    //     빌려 쓴 그림은 **원래 뜻을 같이 가져온다** — 「형태가 비슷하다」가
    //     아니라 **「그 그림이 무엇을 뜻하는가」**로 골라야 한다.
    constexpr AnimationClip kDeadClip  { /*row*/ 4, 1,  1, /*loop*/ false };


    // ========================================================================
    //  ★ 숫자는 **enemies.json 으로 나갔다** (7-b)
    //
    //    여기 있던 상수들은 EnemyType 의 기본값이 되었고, 파일이 그 위에
    //    덮어쓴다 — 무기(weapons.json)와 같은 구조다.
    //
    //    ★★ 값은 나갔지만 **왜 그 값인지는 여기 남는다.** JSON 에는 주석을
    //      못 달기 때문이다. 아래는 그 이유들이고, 지우면 다시 못 찾는다.
    //
    //  ---- 시야 (design.md §3.9 B) ----
    //    전에는 거리 하나뿐이라 **등 뒤에 있어도 봤다.** 각도를 더하면
    //    「뒤에서 다가간다」가 성립하고, 「어느 쪽에서 접근할까」가 판단이 된다.
    //
    //        ＼                    ／
    //          ＼      적 ▶      ／      부채꼴 ±55도 · 220
    //            ＼  ( · )     ／        청각 40 — 각도와 무관
    //
    //    ★ 청각이 없으면 **뒤에 붙어 무한히 때릴 수 있다.**
    //      「붙기 전까지는 안전하지만, 때리려면 들킨다」가 되어야 거래가 성립한다.
    //      플레이어의 공격 사거리(12~40)가 청각 반경 40 과 겹치는 것이 요점이다.
    //
    //    ★★ cos 으로 비교한다. 각도를 구하려면 atan2 가 필요하지만,
    //      **비교만 할 거라면 cos 끼리 비교하면 된다** — 삼각함수 호출이 사라진다.
    //      cos 은 0~180도에서 단조감소하므로 「각도가 작다」 = 「cos 이 크다」.
    //      ※ 그래서 파일에는 **도(degree)**로 적고 읽을 때 cos 으로 바꾼다 —
    //        0.5736 이 적혀 있으면 아무도 그게 55도인 줄 모른다.
    //
    //    forgetTicks 가 없으면 기습이 **첫 1회만** 의미 있다 — 한 번 들키면
    //    영원히 쫓기므로 발판으로 도망치는 것도 소용없어진다(6-d).
    //
    //  ---- 공격 위치 ----
    //    ★ 가로(사거리)와 세로(허용폭)를 따로 둔다. 히트박스가 가로로 뻗으므로
    //      원형 거리로 판정하면 위아래로 떨어진 플레이어를 영원히 헛친다.
    //    ★ 그리고 이 값들이 「멈추는 위치」와 「공격하는 위치」 양쪽에 쓰인다.
    //      따로 두면 「멈췄는데 닿지 않는」 적이 생긴다.
    //
    //    personalSpace(26) 는 몸 너비(18)보다 넉넉해야 안 겹친다.
    //    attackCooldown 이 없으면 사거리 안에서 무한 공격이 된다.
    //
    //  ---- 경직 ----
    //    ★ hurtTicks(16)는 경직 내성(PoiseComponent 30틱)보다 **짧아야** 한다.
    //      회복되는 그 틱에 다시 휘청이면 무한 루프가 된다.
    //      플레이어(18)보다도 짧다 — 적이 더 무겁다.
    // ========================================================================

    //   눈높이. **표시용이고 판정은 발끝 기준이다** — 상수를 하나로 줄이려다
    //   「그림과 판정이 다른」 상태를 만들지 않도록, 쓰는 곳을 표시로 한정한다.
    //   그래서 이것만 파일로 안 나갔다.
    constexpr float kEyeHeight  = 40.0f;

    constexpr int   kFlashTicks = 9;
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

    // ★★ **자기 자리를 여기서 기억한다.** Scene 이 놓아 준 그 자리가 집이다.
    //   전에는 Reset 안에 좌표가 박혀 있었다(`tr.x = 470`). 적이 하나였을
    //   때는 맞는 값이었는데, 셋이 되자 **Start 가 Reset 을 부르면서 전부
    //   같은 자리로 모였다.** 스폰 좌표는 Scene 이 정하는 것이지
    //   적이 아는 값이 아니다.
    m_homeX      = Owner().transform.x;
    m_homeFacing = Owner().transform.facing;

    Reset(ctx);
}


const AttackData& EnemyBrain::CurrentAttack() const
{
    // ★ latch 된 값을 본다. 매 틱 다시 고르면 휘두르는 도중에 다리가 부서지는
    //   순간 프레임 데이터가 통째로 바뀌어(60틱 -> 54틱) active 를 건너뛴다.
    return m_attackIsBite ? m_type->bite : m_type->swing;
}


const AnimationClip& EnemyBrain::PostureClip(EnemyState s) const
{
    // ★ 자세가 상태를 이긴다. 다리가 부서졌으면 무슨 상태든 기어가는 그림이다.
    if (m_parts->Prone())
        return kCrawlClip;

    return (s == EnemyState::Chase) ? kChaseClip : kIdleClip;
}


bool EnemyBrain::CanSeeTarget() const
{
    const Transform& tr = Owner().transform;
    const float dx = m_target.x - tr.x;
    const float dy = m_target.y - tr.y;
    const float dist2 = dx * dx + dy * dy;

    // ① 청각 — 각도와 무관하다. 바로 뒤에 붙으면 안다.
    if (dist2 <= m_type->hearRange * m_type->hearRange)
        return true;

    // ② 시야 — 거리부터. ★ 제곱끼리 비교해 sqrt 를 미룬다.
    if (dist2 > m_type->sightRange * m_type->sightRange)
        return false;

    const float dist = std::sqrt(dist2);
    if (dist <= 0.0001f)
        return true;

    // ③ 각도. 바라보는 쪽으로 얼마나 기울어 있는가.
    //   ★ dx * facing 은 「바라보는 방향과의 내적」이다. dist 로 나누면 cos 이 된다.
    //   ★ 세로 차이가 크면 자동으로 cos 이 작아진다 — **머리 위는 잘 못 본다.**
    //     발판 위로 올라가면 숨을 수 있는 이유가 이 한 줄에서 나온다.
    return (dx * static_cast<float>(tr.facing)) / dist >= m_type->sightCos;
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
    return WouldBite() ? m_type->biteRange : m_type->swingRange;
}


bool EnemyBrain::InAttackPosition() const
{
    const Transform& tr = Owner().transform;
    const float dx = std::abs(m_target.x - tr.x);
    const float dy = std::abs(m_target.y - tr.y);
    return dx <= AttackRange() && dy <= m_type->yTolerance;
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

    // ★ 집으로 돌아간다. 「어디가 집인가」는 Start 에서 받아 두었다.
    tr.x      = m_homeX;
    tr.facing = m_homeFacing;

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
    m_lostTicks      = m_type->forgetTicks;
    m_proneLast      = false;
    m_yieldRoom      = false;
    m_hitThisSwing   = false;

    // ★ ChangeState 를 쓰지 않는다 — 「같은 상태로의 전이는 무시」에 걸린다.
    //   리셋은 상태를 직접 놓고 애니메이션을 forceRestart 로 다시 건다.
    m_state      = EnemyState::Idle;
    m_stateTicks = 0;

    // ★ 여기서도 자세를 묻는다. 지금은 바로 위에서 부위를 되돌리므로 서 있는
    //   것이 맞지만, 「여기만 자세를 안 본다」는 예외를 남기지 않는다 —
    //   나중에 「부활해도 다리는 안 낫는다」 같은 규칙이 생기면 이 줄만
    //   조용히 틀린다. 지금까지 난 버그는 전부 그 모양이었다.
    m_proneLast  = m_parts->Prone();
    m_sprite->Play(PostureClip(m_state), true);
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
    m_body->Lift(m_type->hurtLift);

    m_poise->OnStaggered();

    // ★ force 가 필요 없다 — Attack 에서 Hurt 로 가는 것이라 상태가 다르다.
    //   그리고 이 전이 자체가 「공격 취소」다. 취소를 위한 코드가 따로 없다.
    ChangeState(ctx, EnemyState::Hurt);
}


// ----------------------------------------------------------------------------
//  Deflect — 튕겼다. 휘두르기가 끊기고 친 방향의 반대로 밀린다.
//
//    ★ Stagger 와 다른 점: **맞은 게 아니다.** 그래서
//        · 뜨지 않는다 (Lift 없음) — 충격은 무기에서 왔지 몸에 온 게 아니다
//        · 강인도를 안 건드린다 — 강인도는 「맞았을 때」의 규칙이다
//      플레이어의 Deflect 가 붉은 번쩍임·무적을 안 가져오는 것과 같은 판단이다.
// ----------------------------------------------------------------------------
void EnemyBrain::Deflect(SceneContext& ctx)
{
    if (m_state != EnemyState::Attack)
        return;

    m_hitThisSwing = true;
    m_knockDirX    = -static_cast<float>(Owner().transform.facing);

    // ★ Attack -> Hurt 전이 자체가 「공격 취소」다(Stagger 주석 참조).
    //   튕김을 위한 상태를 따로 만들지 않는다.
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
    // ★ 그림은 상태가 아니라 **자세**가 정한다.
    //   전에는 Idle 이 kIdleClip 을 그대로 썼다. 그래서 다리가 부서진 채
    //   플레이어를 놓치고 Idle 로 돌아오면 **서 있는 그림**이 되었다 —
    //   상태는 「가만히 있다」인데 몸은 기어다니는 중이었다.
    case EnemyState::Idle:  m_sprite->Play(PostureClip(next)); break;
    case EnemyState::Chase: m_sprite->Play(PostureClip(next)); break;
    case EnemyState::Crawl: m_sprite->Play(kCrawlClip);        break;

    case EnemyState::Hurt:
        // 전용 그림이 없으므로 자세를 유지하고 틴트로 구분한다(ApplyTint).
        m_sprite->Play(PostureClip(EnemyState::Idle), true);
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

        // ★★ **플레이어에는 있는데 적에는 없던 로그.**
        //   「무엇이 나갔는지」를 화면으로만 확인해야 하면, 판정이 이상할 때
        //   그게 **고른 공격이 틀린 건지 상자가 틀린 건지**를 가를 수가 없다.
        //   대칭인 쪽에 같은 도구가 없으면 진단도 반쪽이 된다.
        {
            const AttackData& a = CurrentAttack();
            Log::Info("[enemy] {} 발동  [{} {} {}]  높이 {:.1f} ({:.1f}~{:.1f})",
                      a.name, a.startup, a.active, a.recovery, a.heightFromFoot,
                      a.heightFromFoot - a.height * 0.5f,
                      a.heightFromFoot + a.height * 0.5f);
        }
        break;

    case EnemyState::Dead:
        // ★ **멈추는 것도 걸어 줘야 한다.** 안 걸면 직전 클립이 계속 돈다.
        m_sprite->Play(kDeadClip, true);
        break;
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

    // ★ 「멈추는 거리」는 공격 사거리와 **다를 수 있다.**
    //   나보다 가까운 동료가 있으면 한 칸 물러서서 기다린다(SetYieldRoom).
    if (m_yieldRoom && std::abs(dx) <= AttackRange() + m_type->personalSpace)
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

    // ---- ★ 자세를 알려 준다 ----
    //   ★★ **공격 모션이 몸을 낮추는 것**까지 부위에 알려 준다.
    //     잡몹의 물기는 덤벼들며 몸을 던지는 그림이라 실루엣이 발끝 25까지
    //     내려가는데, 부위 상자는 그걸 몰라서 **서 있는 55 그대로**였다 —
    //     낮게 무는 중인 적을 때리면 판정이 허공에 있었다.
    m_parts->SetAttackPosture(m_state == EnemyState::Attack
                                  ? CurrentAttack().posture
                                  : Posture::Stand);

    //   ★ 지형 상자에는 **StancePosture** 를 준다 — 공격 모션은 빼고.
    //     공격 중에 지형 상자까지 줄이면 낮은 틈에서 공격이 끝나는 순간
    //     몸이 커지며 천장에 박힌다. 「맞는 범위」와 「설 수 있는 자리」는
    //     원래 다른 것이다.
    m_body->SetPosture(m_parts->StancePosture());

    // ★ 자세가 바뀌면 **그림도** 바꾼다.
    //   다리가 부서지는 것은 상태 전이가 아니므로 ChangeState 가 안 불린다.
    //   Chase 중이었다면 다음 틱에 Crawl 로 넘어가며 갱신되지만,
    //   **Idle 이면 아무 일도 안 일어나** 서 있는 그림이 그대로 남는다.
    //   플레이어에서 Ctrl 을 눌렀을 때와 **완전히 같은 함정**이다.
    if (m_parts->Prone() != m_proneLast)
    {
        m_proneLast = m_parts->Prone();
        if (m_state == EnemyState::Idle || m_state == EnemyState::Chase
            || m_state == EnemyState::Crawl)
        {
            m_sprite->Play(PostureClip(m_state), true);
        }
    }

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

        const bool alerted = (m_lostTicks < m_type->forgetTicks);

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
            else           MoveTowardTarget(m_type->walkPerTick);
            break;

        case EnemyState::Crawl:
            // 기어가는 중에는 다시 일어나지 않는다. 다리는 회복되지 않는다.
            // ★ 하지만 무해하지는 않다 — 사거리에 들어오면 물어뜯는다.
            if (!alerted)  ChangeState(ctx, EnemyState::Idle);
            else if (canAttack) ChangeState(ctx, EnemyState::Attack);
            else           MoveTowardTarget(m_type->crawlPerTick);
            break;

        case EnemyState::Hurt:
            // ★ 입력도 판단도 없다. 밀려나기만 한다 — 플레이어의 Hurt 와 같은 구조.
            {
                // ★ 넉백도 몸을 거친다. 벽에 밀어붙이면 거기서 멈춘다.
                //   낭떠러지 판정은 **하지 않는다** — 맞아서 밀려 떨어지는 것은
                //   막을 이유가 없다. 「스스로 걸어 나가지 않는다」가 규칙이다.
                const float step = DecayingStep(m_stateTicks, m_type->hurtTicks, m_type->hurtKnockback);
                m_body->MoveX(m_knockDirX * step);
            }
            if (m_stateTicks >= m_type->hurtTicks)
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
                m_attackCooldown = m_type->attackCooldown;
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
        const float sin55 = std::sqrt(1.0f - m_type->sightCos * m_type->sightCos);

        const bool sees = CanSeeTarget();
        const DirectX::XMVECTOR edge = sees
            ? DirectX::XMVectorSet(1.0f, 0.35f, 0.30f, 0.75f)   // 들켰다
            : DirectX::XMVectorSet(0.45f, 0.75f, 1.00f, 0.45f); // 아직 안 보인다

        // 두 변
        for (float r = 12.0f; r <= m_type->sightRange; r += 9.0f)
        {
            const float px = tr.x + f * m_type->sightCos * r;
            for (int s = -1; s <= 1; s += 2)
            {
                const float py = eyeY + static_cast<float>(s) * sin55 * r;
                renderer.DrawFilledRect({ px - 1.0f, py - 1.0f, px + 1.0f, py + 1.0f }, edge);
            }
        }

        // 호 — 끝의 둥근 경계
        for (float a = -1.0f; a <= 1.0f; a += 0.08f)
        {
            const float c = m_type->sightCos + (1.0f - m_type->sightCos) * (1.0f - std::abs(a));
            const float s = sin55 * a;
            const float px = tr.x + f * c * m_type->sightRange;
            const float py = eyeY + s * m_type->sightRange;
            renderer.DrawFilledRect({ px - 1.0f, py - 1.0f, px + 1.0f, py + 1.0f }, edge);
        }

        // 청각 반경 — 각도와 무관하므로 사각형으로 충분히 읽힌다
        renderer.DrawRectOutline(
            { tr.x - m_type->hearRange, eyeY - m_type->hearRange, tr.x + m_type->hearRange, eyeY + m_type->hearRange },
            DirectX::XMVectorSet(0.9f, 0.9f, 0.4f, 0.40f), 1.0f);
    }

    // ---- ★ 공격 상자 : **예고 때부터** 보여 준다 ----
    //   판정이 켜져 있는 것은 3~4틱, 즉 50~67ms 다. 눈으로 잡을 수가 없다.
    //   그래서 「하단 공격인데 상자가 안 바뀐다」 같은 것을 확인할 방법이
    //   사실상 없었다 — 보이는 시간이 없으니까.
    //
    //   ★★ 예고 구간에는 **테두리만**, 판정 구간에는 **채워서** 그린다.
    //     거짓말을 하지 않으면서(언제 맞는지는 채움으로 구분된다)
    //     **어디에 떨어질지**를 미리 보여 준다.
    //     프레임 정지(`,`) 없이도 높이 차이가 읽힌다.
    if (Telegraph())
    {
        renderer.DrawRectOutline(AttackHitbox(),
            DirectX::XMVectorSet(1.0f, 0.55f, 0.10f, 0.45f), 1.0f);
        return;
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
    // ★ 번갈아가 실제로 도는지 **눈으로** 본다. 안 보이면 「안 나온다」와
    //   「아직 차례가 아니다」를 구분할 수 없다.
    const std::string nextAtk =
        std::format("  next {}", WouldBite() ? "BITE" : "SWING");

    const std::string sight =
        CanSeeTarget()                ? std::string{ "  [SEES YOU]" }
      : (m_lostTicks < m_type->forgetTicks)  ? std::format("  [losing {}]", m_type->forgetTicks - m_lostTicks)
      :                                 std::string{ "  [unaware]" };

    renderer.DrawString(
        std::format("ENEMY {}{}{}", EnemyStateName(m_state),
                    m_parts->LegsBroken() ? "  (legs broken)" : "",
                    m_attackCooldown > 0 ? std::format("  cd {}", m_attackCooldown)
                                         : std::string{})
            + std::format("  poise {}{}", m_poise->Value(),
                          m_poise->Immune() ? std::format(" (immune {})", m_poise->ImmuneTicks())
                                            : std::string{})
            + nextAtk + sight,
        6.0f, 34.0f,
        m_parts->LegsBroken() ? DirectX::Colors::Orange : DirectX::Colors::Gold, 1);
}
