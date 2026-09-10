#include "Gameplay/PlayerController.h"

#include "Core/Constants.h"
#include "Core/GameObject.h"
#include "Core/Log.h"
#include "Core/Motion.h"
#include "Core/Scene.h"
#include "Audio/Audio.h"
#include "Gameplay/PoiseComponent.h"
#include "Gameplay/StaminaComponent.h"
#include "Graphics/Camera.h"
#include "Graphics/Renderer.h"
#include "Graphics/SpriteComponent.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <random>
#include <string>

namespace
{
    constexpr int kCellW = 64;
    constexpr int kCellH = 64;

    // ---- 애니메이션 클립 ----
    //   ※ 공격 클립은 AttackData 안에 있다 — 공격마다 길이가 다르기 때문이다.
    constexpr AnimationClip kIdleClip { /*row*/ 0, 4, 10, /*loop*/ true  };   //  6fps
    constexpr AnimationClip kRunClip  { /*row*/ 1, 6,  5, /*loop*/ true  };   // 12fps
    constexpr AnimationClip kRollClip { /*row*/ 3, 6,  4, /*loop*/ false };   // 15fps

    // ========================================================================
    //  단검의 무브셋 — 무기 하나 = 공격 여러 개 (기획서 §3.2.1)
    //
    //    heightFromFoot 하나가 닿는 부위를 정한다(겹침 면적이 큰 쪽에 맞으므로).
    //    적의 부위 상자는 발밑 기준 머리 -55..-37 / 몸통 -37..-18 / 다리 -18..0.
    //
    //      light    34  몸통      기본
    //      crouch   10  다리      Ctrl. 느리고 약한 대신 **조준할 수 있다**
    //      running  34  몸통      달리다 치면. 길게 뻗지만 비싸다
    //      combo2   48  머리      1타를 맞춘 뒤에만. 머리는 즉사다
    //
    //    ★ 「머리를 노리려면 콤보를 성공시켜야 한다」가 데이터만으로 성립한다.
    //    ★ light(28) + combo2(34) = 62. 100 중 62를 쓰면 38 이 남고 구르기는 30 —
    //      콤보 뒤에는 한 번밖에 못 구른다. 기획서 §3.1 이 요구한 긴장이다.
    //    ★ clip 은 frames × ticks == TotalTicks 가 되도록 맞춰 두었다.
    //
    //    6-g 에서 이 네 덩어리가 그대로 weapons.json 이 된다.
    // ========================================================================
    constexpr AttackData kDaggerLight{
        /*name*/     "LIGHT",
        /*startup*/  8,
        /*active*/   3,
        /*recovery*/ 13,            // 합계 24 = 6프레임 × 4틱
    };

    constexpr AttackData kDaggerCrouch{
        /*name*/     "CROUCH",
        /*startup*/  10,
        /*active*/   3,
        /*recovery*/ 17,            // 합계 30 = 6프레임 × 5틱. light 보다 느리다
        /*reach*/    10.0f,
        /*width*/    26.0f,
        /*height*/   16.0f,
        /*heightFromFoot*/ 10.0f,   // ★ 다리 상자 한가운데
        /*damage*/   10,            // 약하다 — 조준의 대가
        /*staminaCost*/ 26,
        /*impact*/   14,
        /*clip*/     { /*row*/ 4, 6, 5, false },
    };

    constexpr AttackData kDaggerRunning{
        /*name*/     "RUN",
        /*startup*/  8,
        /*active*/   4,
        /*recovery*/ 12,            // 합계 24
        /*reach*/    18.0f,         // 멀리서 닿는다
        /*width*/    34.0f,
        /*height*/   24.0f,
        /*heightFromFoot*/ 34.0f,
        /*damage*/   14,
        /*staminaCost*/ 34,         // 비싸다
        /*impact*/   16,
        /*clip*/     { /*row*/ 6, 6, 4, false },
    };

    constexpr AttackData kDaggerCombo2{
        /*name*/     "THRUST",
        /*startup*/  8,
        /*active*/   3,
        /*recovery*/ 13,            // 합계 24
        /*reach*/    12.0f,
        /*width*/    26.0f,
        /*height*/   22.0f,
        /*heightFromFoot*/ 48.0f,   // ★ 머리 상자 한가운데
        /*damage*/   15,
        /*staminaCost*/ 34,
        /*impact*/   18,
        /*clip*/     { /*row*/ 5, 6, 4, false },
    };

    constexpr RollData kRoll{ /*windup*/ 4, /*invincible*/ 12, /*recovery*/ 10 };
    constexpr HurtData kHurt{ /*ticks*/ 18, /*invuln*/ 24, /*knockback*/ 22.0f };

    constexpr int kMaxHp = 100;

    // ★ 죽은 뒤 사망 화면이 뜨기까지의 한 박자.
    //   즉시 덮으면 무엇에 죽었는지 안 보여 플레이어가 배울 수 없다.
    constexpr int kDeathScreenDelay = 45;   // 0.75 초

    // ---- 갑옷 (임시. F2 로 갈아입어 강인도의 효과를 비교한다) ----
    //   적 공격의 impact 는 swing 18 / bite 12.
    //     CLOTH(10) → 둘 다에 휘청인다   PLATE(24) → 둘 다 버텨낸다
    constexpr ArmorData kArmors[] = { { "CLOTH", 10 }, { "PLATE", 24 } };
    constexpr int kArmorCount = static_cast<int>(std::size(kArmors));

    constexpr float kSpeedPerTick     = 150.0f / 60.0f;   // 틱당 2.5 픽셀
    constexpr float kCrouchSpeedScale = 0.45f;            // 조준의 대가

    constexpr float kOriginX = kCellW * 0.5f;
    constexpr float kOriginY = static_cast<float>(kCellH);

    constexpr float kHitHalfWidth = 10.0f;
    constexpr float kHitHeight    = 44.0f;
    constexpr float kHitFootGap   = 2.0f;

    constexpr float kMoveEpsilon      = 0.01f;
    constexpr int   kStepIntervalTicks = 15;
    constexpr int   kFlashTicks        = 9;

    constexpr float kShakeStrength = 2.0f;
    constexpr int   kShakeTicks    = 8;

    constexpr float kStartX = 120.0f;
    constexpr float kStartY = 260.0f;

    float RandomPitch(float spread)
    {
        static std::mt19937 rng{ 12345 };
        std::uniform_real_distribution<float> dist(-spread, spread);
        return dist(rng);
    }

    const char* AttackPhase(int t, const AttackData& a)
    {
        if (t <  a.startup)            return "startup";
        if (t <  a.startup + a.active) return "ACTIVE";
        return "recovery";
    }

    const char* RollPhase(int t, const RollData& r)
    {
        if (t <  r.windup)                return "windup";
        if (t <  r.windup + r.invincible) return "INVINCIBLE";
        return "recovery";
    }
}


const char* PlayerStateName(PlayerState s)
{
    switch (s)
    {
    case PlayerState::Run:       return "RUN";
    case PlayerState::Attack:    return "ATTACK";
    case PlayerState::Roll:      return "ROLL";
    case PlayerState::Exhausted: return "EXHAUSTED";
    case PlayerState::Hurt:      return "HURT";
    case PlayerState::Dead:      return "DEAD";
    default:                     return "IDLE";
    }
}


void PlayerController::Start(SceneContext& ctx)
{
    m_sprite  = &Owner().Require<SpriteComponent>();
    m_poise   = &Owner().Require<PoiseComponent>();
    m_stamina = &Owner().Require<StaminaComponent>();
    Respawn(ctx);
}


const ArmorData& PlayerController::Armor() const { return kArmors[m_armorIndex]; }


void PlayerController::Respawn(SceneContext& ctx)
{
    Transform& tr = Owner().transform;
    tr.x = kStartX;
    tr.y = kStartY;
    tr.facing = 1;

    m_hp          = kMaxHp;
    m_flash       = 0;
    m_invulnTicks = 0;

    m_rollDirX = 1.0f;  m_rollDirY = 0.0f;
    m_knockDirX = -1.0f; m_knockDirY = 0.0f;

    m_currentAttack = nullptr;
    m_comboStep     = 0;
    m_comboQueued   = false;
    m_hitThisSwing  = false;
    m_crouching     = false;
    m_stepCooldown  = 0;

    m_deathScreenRequested = false;

    m_stamina->Reset();
    m_poise->Reset();
    m_poise->SetValue(Armor().poise);   // ★ 값의 출처는 방어구다

    // ---- 남는다 ----
    //   ★ m_armorIndex 는 **일부러 되돌리지 않는다.** 장비는 죽어도 그대로다 —
    //     design.md §3.6.1 의 「남는다」 칸에 실제로 들어간 첫 항목이다.
    //     되돌리면 「죽을 때마다 장비가 벗겨지는」 게임이 된다.

    // ★ ChangeState 를 쓰지 않는다 — 「같은 상태로의 전이는 무시」에 걸린다.
    //   리셋은 상태를 직접 놓고 애니메이션을 forceRestart 로 다시 건다.
    m_state      = PlayerState::Idle;
    m_stateTicks = 0;
    m_sprite->Play(kIdleClip, true);
    m_sprite->ClearTint();
    m_sprite->SetScale(1.0f, 1.0f);

    (void)ctx;
}


// ----------------------------------------------------------------------------
//  SelectAttack — ★ 무브셋의 전부
//
//    ★ 명시적 입력이 암묵적 맥락을 이긴다.
//      Ctrl 을 누르고 있다는 것은 「다리를 노리겠다」는 의사표시이고,
//      「방금 달리고 있었다」보다 최신 의도다.
// ----------------------------------------------------------------------------
const AttackData& PlayerController::SelectAttack(SceneContext& ctx, PlayerState prev) const
{
    if (ctx.input.CrouchHeld())          return kDaggerCrouch;   // ① 명시적 입력

    // ② 공격 중이었다 -> 2타
    //   ★ 여기에 `&& m_comboStep == 0` 을 넣었다가 콤보가 아예 안 나왔다.
    //     ChangeState 가 이 함수를 부르기 직전에 m_comboStep 을 1 로 올리기 때문이다.
    //     무한 연타 방지는 **예약하는 쪽**에 있다. 막을 곳은 한 곳이면 충분하다.
    if (prev == PlayerState::Attack)     return kDaggerCombo2;
    if (prev == PlayerState::Run)        return kDaggerRunning;  // ③
    return kDaggerLight;                                          // ④
}


const AttackData& PlayerController::CurrentAttack() const
{
    return m_currentAttack ? *m_currentAttack : kDaggerLight;
}


// ----------------------------------------------------------------------------
//  ChangeState — 상태 머신의 "Enter"
//    들어가는 순간 한 번만 해야 하는 일을 모아 둔다.
//    이게 없으면 매 틱 Play() 를 부르게 되어 애니메이션이 프레임 0 에서 멈춘다.
// ----------------------------------------------------------------------------
void PlayerController::ChangeState(SceneContext& ctx, PlayerState next, bool force)
{
    if (m_state == next && !force)
        return;

    // ★ 덮기 전에 붙잡는다. 무브셋 선택이 「직전에 무엇을 하고 있었나」를 본다.
    const PlayerState prev = m_state;

    m_state      = next;
    m_stateTicks = 0;

    Transform& tr = Owner().transform;

    switch (next)
    {
    case PlayerState::Idle:
        m_sprite->Play(kIdleClip);
        break;

    case PlayerState::Run:
        // ★ 여기서 m_stepCooldown 을 0 으로 되돌리면 안 된다 —
        //   UpdateMovement 가 이미 발소리를 내고 쿨다운을 채워 놓았고,
        //   되돌리면 1/60초 간격으로 두 번 울린다.
        m_sprite->Play(kRunClip);
        break;

    case PlayerState::Attack:
    {
        m_comboStep   = (prev == PlayerState::Attack) ? 1 : 0;
        m_comboQueued = false;

        // ★ 어느 공격인지 **여기서 고정한다.**
        m_currentAttack = &SelectAttack(ctx, prev);
        const AttackData& atk = *m_currentAttack;

        m_sprite->Play(atk.clip, true);
        m_hitThisSwing = false;

        // ★ 부족해도 공격은 나간다. 0 미만이면 끝난 뒤 Exhausted 로 간다.
        //   「부족하면 안 나감」이 아니라 「나가고 대가를 치름」이 이 게임의 규칙이다.
        m_stamina->Spend(atk.staminaCost);

        // 공격마다 피치를 달리해 무엇이 나갔는지 소리로도 구분되게 한다.
        const float pitch = (atk.heightFromFoot > 40.0f) ?  0.22f    // 찌르기 = 높게
                          : (atk.heightFromFoot < 20.0f) ? -0.25f    // 웅크리기 = 낮게
                          :  0.0f;
        ctx.audio.Play("swing", 0.55f, pitch + RandomPitch(0.10f), PanFromCanvasX(tr.x));

        Log::Info("[play] {} 발동  [{} {} {}]  높이 {:.0f}  stam -{}",
                  atk.name, atk.startup, atk.active, atk.recovery,
                  atk.heightFromFoot, atk.staminaCost);
        break;
    }

    case PlayerState::Roll:
    {
        m_sprite->Play(kRollClip, true);

        // ★ 방향을 여기서 고정한다. 입력이 없으면 바라보는 방향으로 굴러간다.
        const Input::MoveIntent mv = ctx.input.Move();
        if (std::abs(mv.x) > kMoveEpsilon || std::abs(mv.y) > kMoveEpsilon)
        {
            m_rollDirX = mv.x;
            m_rollDirY = mv.y;
        }
        else
        {
            m_rollDirX = static_cast<float>(tr.facing);
            m_rollDirY = 0.0f;
        }

        m_stamina->Spend(kRoll.staminaCost);
        ctx.audio.Play("swing", 0.4f, -0.35f, PanFromCanvasX(tr.x));  // 낮은 피치
        break;
    }

    case PlayerState::Exhausted:
        m_sprite->Play(kIdleClip);
        ctx.audio.Play("ui_cancel", 0.45f);
        Log::Info("[play] 스태미나 고갈 — 경직 (stam {:.1f})", m_stamina->Current());
        break;

    case PlayerState::Hurt:
        // ★ 소리는 여기서 내지 않는다 — 버텼는지 휘청였는지에 따라 다르므로
        //   원인을 아는 TakeHit 가 낸다.
        m_sprite->Play(kIdleClip, true);
        break;

    case PlayerState::Dead:
        m_sprite->Play(kIdleClip, true);
        ctx.audio.Play("ui_cancel", 0.9f, -0.6f);
        Log::Info("[play] ★★ 플레이어 사망 — {}틱 뒤 사망 화면", kDeathScreenDelay);
        break;
    }
}


// ----------------------------------------------------------------------------
//  UpdateMovement — Idle / Run 에서만 불린다.
//    "공격 중에는 이동 불가" 를 !attacking 조건으로 흩뿌리지 않고
//    아예 호출하지 않는 것으로 표현한다. 이것이 상태 머신의 요점이다.
// ----------------------------------------------------------------------------
void PlayerController::UpdateMovement(SceneContext& ctx, float moveX, float moveY)
{
    Transform& tr = Owner().transform;

    if (moveX < -kMoveEpsilon)      tr.facing = -1;
    else if (moveX > kMoveEpsilon)  tr.facing = +1;

    // ★ 웅크리면 느려진다 — 「다리를 노리려면 멈춰서 노려야 한다」가 한 줄로.
    const float speed = kSpeedPerTick * (m_crouching ? kCrouchSpeedScale : 1.0f);

    tr.x = std::clamp(tr.x + moveX * speed, kOriginX,
                      static_cast<float>(Config::kCanvasWidth) - kOriginX);
    tr.y = std::clamp(tr.y + moveY * speed, kOriginY,
                      static_cast<float>(Config::kCanvasHeight));

    const bool moving = (std::abs(moveX) > kMoveEpsilon || std::abs(moveY) > kMoveEpsilon);
    if (!moving)
    {
        m_stepCooldown = 0;
        return;
    }

    if (--m_stepCooldown <= 0)
    {
        // 웅크려 걸으면 발소리도 그만큼 뜸해야 한다.
        m_stepCooldown = m_crouching
            ? static_cast<int>(kStepIntervalTicks / kCrouchSpeedScale)
            : kStepIntervalTicks;
        ctx.audio.Play("step", 0.45f, RandomPitch(0.15f), PanFromCanvasX(tr.x));
    }
}


// ----------------------------------------------------------------------------
//  SlideDecaying — 감속하며 미끄러진다
//
//      속도
//       │▓▓▓▓▓▓
//       │▓▓▓▓
//       │▓▓
//       └────────→ 틱
//
//    ★ 구르기와 넉백이 공유한다. 카메라 흔들림 감쇠와 같은 발상이다.
// ----------------------------------------------------------------------------
void PlayerController::SlideDecaying(float dirX, float dirY, float distance, int totalTicks)
{
    // ★ 공식은 Core/Motion.h 로 올렸다 — 적의 넉백이 같은 것을 쓰게 되었다.
    //   상태를 갖지 않는 계산이라 컴포넌트가 아니라 자유 함수다.
    const float step = DecayingStep(m_stateTicks, totalTicks, distance);
    if (step <= 0.0f)
        return;

    Transform& tr = Owner().transform;
    tr.x = std::clamp(tr.x + dirX * step, kOriginX,
                      static_cast<float>(Config::kCanvasWidth) - kOriginX);
    tr.y = std::clamp(tr.y + dirY * step, kOriginY,
                      static_cast<float>(Config::kCanvasHeight));
}


AABB PlayerController::SpriteBounds() const
{
    const Transform& tr = Owner().transform;
    return { tr.x - kOriginX, tr.y - kOriginY, tr.x - kOriginX + kCellW, tr.y };
}


// ★ 용어를 나눠 쓴다. 섞으면 "내 공격이 나를 때리는" 코드를 쓰게 된다.
//   hurtbox = 내가 맞는 범위 (몸) / hitbox = 내가 때리는 범위 (무기)
AABB PlayerController::Hurtbox() const
{
    const Transform& tr = Owner().transform;
    return { tr.x - kHitHalfWidth, tr.y - kHitFootGap - kHitHeight,
             tr.x + kHitHalfWidth, tr.y - kHitFootGap };
}


AABB PlayerController::AttackHitbox() const
{
    const Transform& tr = Owner().transform;
    return MakeAttackBox(tr.x, tr.y, tr.facing, CurrentAttack());
}


bool PlayerController::AttackActive() const
{
    if (m_state != PlayerState::Attack)
        return false;

    const AttackData& a = CurrentAttack();
    return m_stateTicks >= a.startup
        && m_stateTicks <  a.startup + a.active;
}


bool PlayerController::RollInvincible() const
{
    if (m_state != PlayerState::Roll)
        return false;

    return m_stateTicks >= kRoll.windup
        && m_stateTicks <  kRoll.windup + kRoll.invincible;
}


bool PlayerController::Invincible() const
{
    // ② 피격 무적 — 경직(18)보다 길게(24) 남는다.
    //   그 6틱의 여유가 「경직이 풀리는 그 틱에 다시 맞는」 스턴락을 막는다.
    if (m_invulnTicks > 0)
        return true;

    return RollInvincible();   // ①
}


bool PlayerController::ConsumeDeathScreenRequest()
{
    if (m_state != PlayerState::Dead)      return false;
    if (m_deathScreenRequested)            return false;
    if (m_stateTicks < kDeathScreenDelay)  return false;

    m_deathScreenRequested = true;
    return true;
}


// ----------------------------------------------------------------------------
//  TakeHit — ★ 맞았을 때 무슨 일이 일어나는지를 **데이터가 정한다**
//
//      poise >= impact  ->  데미지만. 자리에서 버틴다 (상태를 바꾸지 않는다)
//      poise <  impact  ->  Hurt(경직) + 넉백 + 피격 무적
//
//    ※ 무적 판정은 이 함수에 오기 전에 끝나 있다(PlayScene::TryEnemyHit).
// ----------------------------------------------------------------------------
void PlayerController::TakeHit(SceneContext& ctx, const AttackData& atk,
                               float fromX, float fromY)
{
    const Transform& tr = Owner().transform;

    m_hp   -= atk.damage;
    m_flash = kFlashTicks;

    // ---- 사망이 가장 먼저다. 강인도로 버텨도 HP 는 깎였다 ----
    if (m_hp <= 0)
    {
        m_hp = 0;
        ctx.camera.Shake(kShakeStrength * 2.5f, kShakeTicks * 3);
        ctx.audio.Play("hit", 1.0f, -0.55f, PanFromCanvasX(tr.x));
        ChangeState(ctx, PlayerState::Dead);
        return;
    }

    // ---- ★ 경직 여부를 강인도가 정한다 ----
    //   판정 자체는 PoiseComponent 로 옮겼다 — 적도 같은 규칙을 쓰기 때문이다.
    //   나중에 게이지 방식으로 바꾸면 이 줄은 그대로 두고 그쪽만 고친다.
    if (!m_poise->WouldStagger(atk.impact))
    {
        // 버텨냈다 — **상태를 바꾸지 않는다.** 공격 중이었다면 그대로 이어진다.
        // ★ 피격 무적을 주지 않는다. 못 움직이는 구간이 없으므로 스턴락 위험이
        //   없고, 무적은 그 위험을 막기 위한 장치이기 때문이다.
        //   대신 맞을 때마다 HP 가 확실히 깎인다 — 이것이 버티기의 비용이다.
        ctx.camera.Shake(kShakeStrength * 0.6f, kShakeTicks);
        ctx.audio.Play("hit", 0.5f, -0.75f, PanFromCanvasX(tr.x));   // 둔탁하게
        Log::Info("[play] 버텨냄  poise {} >= impact {}   dmg {}  HP {}",
                  m_poise->Value(), atk.impact, atk.damage, m_hp);
        return;
    }

    // ---- 휘청였다 : 넉백 방향 = 공격자 -> 나 ----
    float dx = tr.x - fromX;
    float dy = tr.y - fromY;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0001f)
    {
        dx /= len;
        dy /= len;
    }
    else
    {
        // 완전히 겹쳐 있으면 방향이 없다. 바라보는 반대쪽으로 민다.
        dx = -static_cast<float>(tr.facing);
        dy = 0.0f;
    }
    m_knockDirX = dx;
    m_knockDirY = dy;

    m_invulnTicks = kHurt.invuln;
    m_poise->OnStaggered();

    ctx.camera.Shake(kShakeStrength * 1.8f, kShakeTicks * 2);
    ctx.audio.Play("hit", 0.95f, -0.25f, PanFromCanvasX(tr.x));
    Log::Info("[play] 피격  poise {} < impact {}   dmg {}  HP {}   경직 {}틱 / 무적 {}틱",
              m_poise->Value(), atk.impact, atk.damage, m_hp, kHurt.ticks, kHurt.invuln);

    ChangeState(ctx, PlayerState::Hurt);
}


void PlayerController::Tick(SceneContext& ctx, bool consumeEdgeInput)
{
    // ---- 시간은 상태와 무관하게 매 틱 흐른다 ----
    ++m_stateTicks;

    if (m_flash > 0)        --m_flash;
    if (m_invulnTicks > 0)  --m_invulnTicks;

    const Input::MoveIntent move = ctx.input.Move();
    const bool moving = (std::abs(move.x) > kMoveEpsilon || std::abs(move.y) > kMoveEpsilon);

    // 웅크리기는 **지속 입력**이라 엣지가 아니다. 매 틱 물어봐도 된다.
    m_crouching = ctx.input.CrouchHeld();

    const bool attackPressed = (consumeEdgeInput && ctx.input.AttackPressed());
    const bool rollPressed   = (consumeEdgeInput && ctx.input.RollPressed());

    switch (m_state)
    {
    case PlayerState::Idle:
    case PlayerState::Run:
        UpdateMovement(ctx, move.x, move.y);

        // 구르기를 공격보다 먼저 본다. 둘이 동시에 눌리면 회피가 우선이다.
        if (rollPressed)        ChangeState(ctx, PlayerState::Roll);
        else if (attackPressed) ChangeState(ctx, PlayerState::Attack);
        else                    ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        break;

    case PlayerState::Attack:
    {
        // ★ 이동 입력을 처리하지 않는다 = 공격 중에는 못 움직인다.
        //   소울류의 "한 번 휘두르면 끝까지 간다" 감각.
        const AttackData& atk = CurrentAttack();

        // ---- 콤보 예약 ----
        //   ★ active 가 끝난 뒤부터만 받는다. startup 중에도 받으면 연타만으로
        //     2타가 확정되어 「1타를 내보고 이어칠지 판단한다」가 사라진다.
        //   ★ 예약해 두었다가 상태가 끝날 때 꺼낸다 — 지금 전이하면 1타의 후딜을
        //     건너뛴다.
        if (attackPressed && m_comboStep == 0
            && m_stateTicks >= atk.startup + atk.active)
        {
            m_comboQueued = true;
        }

        // ※ 적을 실제로 때렸는지는 PlayScene 이 검사한다(헤더 주석 참조).

        // ★ 상태의 길이는 애니메이션이 아니라 프레임 데이터가 정한다.
        if (m_stateTicks >= atk.TotalTicks())
        {
            // ★ 고갈이 콤보보다 우선한다. 「2타를 예약해 두면 고갈을 피한다」가
            //   되면 스태미나 시스템에 구멍이 생긴다.
            if (m_stamina->Depleted())
                ChangeState(ctx, PlayerState::Exhausted);
            else if (m_comboQueued)
                // ★ force = true. Attack -> Attack 이라 「같은 상태면 무시」에 걸린다.
                ChangeState(ctx, PlayerState::Attack, true);
            else
                ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        }
        break;
    }

    case PlayerState::Roll:
        // ★ 이동 입력을 처리하지 않는다. 시작할 때 고정한 방향으로만 간다.
        SlideDecaying(m_rollDirX, m_rollDirY, kRoll.distance, kRoll.TotalTicks());

        if (m_stateTicks >= kRoll.TotalTicks())
        {
            if (m_stamina->Depleted())
                ChangeState(ctx, PlayerState::Exhausted);
            else
                ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        }
        break;

    case PlayerState::Exhausted:
        // ★ 아무 입력도 처리하지 않는다. 완전히 무방비.
        //   상태 머신 덕분에 이 한 줄이 「모든 입력에 !exhausted 붙이기」를 대신한다.
        if (!m_stamina->Depleted())
            ChangeState(ctx, PlayerState::Idle);
        break;

    case PlayerState::Hurt:
        // ★ 입력을 처리하지 않는다 — Exhausted 와 같은 구조. 대신 밀려난다.
        SlideDecaying(m_knockDirX, m_knockDirY, kHurt.knockback, kHurt.ticks);

        if (m_stateTicks >= kHurt.ticks)
        {
            // ★ 「고갈 경직 중에 맞으면 경직이 풀린다」를 막는 코드가 여기 있었는데,
            //   고갈 래치를 StaminaComponent 로 옮기면서 **필요 없어졌다.**
            //   Depleted() 는 25 이상 회복될 때까지 참으로 남으므로,
            //   경직에서 나와도 고갈이면 그대로 Exhausted 로 돌아간다.
            //   상태를 올바른 곳에 두면 방어 코드가 저절로 사라진다.
            if (m_stamina->Depleted())
                ChangeState(ctx, PlayerState::Exhausted);
            else
                ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        }
        break;

    case PlayerState::Dead:
        // 사망 화면 요청은 PlayScene 이 ConsumeDeathScreenRequest 로 가져간다.
        break;
    }

    // ---- 상태와 무관한 입력 ----
    //   ★ 임시 키. 강인도의 두 분기를 눈으로 비교하기 위한 것이다.
    //     같은 공격에 CLOTH 는 튕겨나가고 PLATE 는 그대로 서서 휘두른다.
    if (consumeEdgeInput && ctx.input.ArmorSwapPressed())
    {
        m_armorIndex = (m_armorIndex + 1) % kArmorCount;
        m_poise->SetValue(Armor().poise);   // 방어구가 바뀌면 강인도도 바뀐다
        ctx.audio.Play("ui_confirm", 0.5f);
        Log::Info("[play] 갑옷 → {}  (poise {})   swing impact 18 / bite impact 12",
                  Armor().name, Armor().poise);
    }
}


void PlayerController::Render(Renderer&)
{
    // ★ 그리는 것은 SpriteComponent 다. 여기서는 **표현만 넘긴다.**
    DirectX::XMVECTOR tint = DirectX::Colors::White;
    if (IsDead())
        tint = DirectX::XMVectorSet(0.30f, 0.28f, 0.30f, 1.0f);   // 사망 = 회색
    else if (m_state == PlayerState::Exhausted)
        tint = DirectX::XMVectorSet(0.45f, 0.45f, 0.55f, 1.0f);   // 지쳐서 어둡게
    else if (RollInvincible())
        tint = DirectX::XMVectorSet(0.55f, 0.75f, 1.00f, 1.0f);   // ① 구르기 무적 = 푸르게
    else if (m_flash > 0)
        tint = DirectX::XMVectorSet(1.00f, 0.35f, 0.30f, 1.0f);   // 피격 순간 = 붉게
    else if (m_invulnTicks > 0 && (m_invulnTicks / 3) % 2 == 0)
        // ★ ② 피격 무적 = 깜빡임. ①과 다른 표현을 쓰는 이유는 성격이 달라서다 —
        //   푸른색은 「벌어낸 무적」, 깜빡임은 「봐주는 무적」.
        tint = DirectX::XMVectorSet(1.00f, 1.00f, 1.00f, 0.45f);

    m_sprite->SetTint(tint);

    // ★ 웅크린 자세를 세로로 눌러서 표현한다.
    //   ★★ 원점을 발밑에 둔 결정이 여기서 값을 한다 — 눌러도 발이 그 자리에 남는다.
    //   공격 중에는 끈다. CROUCH 공격은 전용 행에 눌린 자세가 이미 구워져 있다.
    const bool squash = m_crouching && (m_state != PlayerState::Attack);
    m_sprite->SetScale(1.0f, squash ? 0.78f : 1.0f);
}


void PlayerController::RenderDebug(Renderer& renderer)
{
    if (!renderer.DebugDraw())
        return;

    renderer.DrawRectOutline(SpriteBounds(), DirectX::Colors::SlateGray);

    // ★ hurtbox 는 항상 그린다. 색만 바꿔서 무적을 보여 준다.
    //   빈 사각형으로 만들었다면 「지금 무적인가」를 눈으로 못 본다.
    //   그리고 무적의 **종류까지** 색으로 나눈다.
    DirectX::XMVECTOR hurtColor = DirectX::Colors::Lime;
    if (RollInvincible())       hurtColor = DirectX::Colors::DeepSkyBlue;  // ① 구르기
    else if (m_invulnTicks > 0) hurtColor = DirectX::Colors::Yellow;       // ② 피격
    renderer.DrawRectOutline(Hurtbox(), hurtColor, 2.0f);

    // ★ 공격 히트박스 — active 구간에서만 나타난다.
    //   , 로 멈추고 . 로 밟으면 t8 에 나타나 t10 까지 있는 것을 볼 수 있다.
    if (AttackActive())
    {
        renderer.DrawFilledRect(AttackHitbox(),
            DirectX::XMVectorSet(1.0f, 0.2f, 0.2f, 0.35f));
        renderer.DrawRectOutline(AttackHitbox(), DirectX::Colors::Red, 2.0f);
    }

    // 원점(발밑)을 십자로
    const Transform& tr = Owner().transform;
    renderer.DrawFilledRect({ tr.x - 5.0f, tr.y - 1.0f, tr.x + 5.0f, tr.y + 1.0f },
                            DirectX::Colors::Magenta);
    renderer.DrawFilledRect({ tr.x - 1.0f, tr.y - 5.0f, tr.x + 1.0f, tr.y + 5.0f },
                            DirectX::Colors::Magenta);
}


void PlayerController::RenderUI(Renderer& renderer)
{
    // ---- HP 바 ----
    //   ※ 6-c-4 에서 부위별 HP 로 바뀌면서 **없어질 예정**이다(design.md §3.2.3).
    //     몸통 20% 와 팔 20% 는 완전히 다른 상황인데 한 줄로는 구분되지 않는다.
    constexpr float kBarX = 12.0f;
    constexpr float kBarW = 150.0f;
    constexpr float kBarH = 9.0f;
    const float     barY  = Config::kCanvasHeight - 38.0f;

    renderer.DrawFilledRect(
        AABB::FromXYWH(kBarX - 1.0f, barY - 1.0f, kBarW + 2.0f, kBarH + 2.0f),
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.7f));

    const float ratio = std::clamp(static_cast<float>(m_hp) / kMaxHp, 0.0f, 1.0f);
    DirectX::XMVECTOR hpColor = DirectX::XMVectorSet(0.78f, 0.22f, 0.22f, 1.0f);
    if (ratio <= 0.0f)     hpColor = DirectX::XMVectorSet(0.30f, 0.10f, 0.10f, 1.0f);
    else if (ratio < 0.3f) hpColor = DirectX::XMVectorSet(1.00f, 0.35f, 0.30f, 1.0f);

    if (ratio > 0.0f)
        renderer.DrawFilledRect(AABB::FromXYWH(kBarX, barY, kBarW * ratio, kBarH), hpColor);

    renderer.DrawString(std::format("HP {}", m_hp), kBarX + kBarW + 6.0f, barY - 2.0f,
                        DirectX::Colors::DimGray, 1);

    // ---- 방어구 ----
    //   ★ F2 로 바뀌는 값이므로 **항상** 보여야 한다.
    //     안 보이면 「같은 공격에 왜 이번엔 안 밀렸지?」를 확인할 수 없다.
    renderer.DrawString(
        std::format("ARMOR {} (poise {})  F2 to swap", Armor().name, Armor().poise),
        12.0f, Config::kCanvasHeight - 52.0f, DirectX::Colors::SlateGray, 1);

    // ---- 상태 ----
    //   ★ 상태 머신을 눈으로 보기 위한 표시.
    //     , 로 멈추고 . 로 밟으며 t 를 세면 프레임 데이터를 직접 확인할 수 있다.
    if (m_state == PlayerState::Attack)
    {
        const AttackData& a = CurrentAttack();
        renderer.DrawString(
            std::format("{}{}  t{:<3}{}   [{} {} {}]  h{:.0f}{}",
                        a.name, (m_comboStep > 0) ? "-2" : "",
                        m_stateTicks, AttackPhase(m_stateTicks, a),
                        a.startup, a.active, a.recovery, a.heightFromFoot,
                        m_comboQueued ? "  >> NEXT" : ""),
            6.0f, 6.0f,
            AttackActive() ? DirectX::Colors::Red : DirectX::Colors::Orange, 1);
    }
    else if (m_state == PlayerState::Roll)
    {
        renderer.DrawString(
            std::format("STATE ROLL  t{:<3}{}   [{} {} {}]",
                        m_stateTicks, RollPhase(m_stateTicks, kRoll),
                        kRoll.windup, kRoll.invincible, kRoll.recovery),
            6.0f, 6.0f,
            RollInvincible() ? DirectX::Colors::DeepSkyBlue : DirectX::Colors::Orange, 1);
    }
    else if (m_state == PlayerState::Hurt)
    {
        // ★ 경직 18틱 / 무적 24틱이 **둘 다** 보여야 한다.
        renderer.DrawString(
            std::format("STATE HURT  t{:<3}stagger {}   invuln {}",
                        m_stateTicks, kHurt.ticks, m_invulnTicks),
            6.0f, 6.0f, DirectX::Colors::Yellow, 1);
    }
    else
    {
        renderer.DrawString(
            std::format("STATE {}{}  t{}{}", PlayerStateName(m_state),
                        m_crouching ? " (CROUCH)" : "", m_stateTicks,
                        (m_invulnTicks > 0) ? std::format("   invuln {}", m_invulnTicks)
                                            : std::string{}),
            6.0f, 6.0f,
            (m_state == PlayerState::Exhausted || IsDead()) ? DirectX::Colors::Red
                                                            : DirectX::Colors::Orange, 1);
    }
}
