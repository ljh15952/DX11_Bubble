#include "Scenes/PlayScene.h"
#include "Scenes/PauseScene.h"

#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Audio/Audio.h"
#include "Graphics/Assets.h"
#include "Graphics/Camera.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <random>

namespace
{
    // ---- 스프라이트시트 배치 (player.png : 6 열 × 3 행, 64×64 셀) ----
    constexpr int kCellW = 64;
    constexpr int kCellH = 64;

    // ---- 애니메이션 클립 ----
    //   ticksPerFrame 이 애니메이션 속도. 60 / n = 애니메이션 fps.
    //   ★ Attack 은 loop = false 다. 끝나면 Finished() 가 true 가 되어
    //     상태를 되돌리는 신호가 된다.
    constexpr AnimationClip kIdleClip   { /*row*/ 0, /*frames*/ 4, /*ticks*/ 10, /*loop*/ true  };  //  6fps
    constexpr AnimationClip kRunClip    { /*row*/ 1, /*frames*/ 6, /*ticks*/  5, /*loop*/ true  };  // 12fps
    constexpr AnimationClip kAttackClip { /*row*/ 2, /*frames*/ 6, /*ticks*/  4, /*loop*/ false };  // 15fps

    // ---- 무기 프레임 데이터 ----
    //   ★ 6단계에서 이 값들이 weapons.json 으로 빠진다.
    //     지금은 애니메이션(6프레임 × 4틱 = 24틱)과 총합을 일부러 맞춰 뒀다.
    constexpr AttackData kDaggerLight{
        /*startup*/  8,
        /*active*/   3,
        /*recovery*/ 13,     // 합계 24 틱 = 0.4 초
    };

    // ---- 플레이어 (임시) ----
    //   ★ 좌표와 속도는 모두 캔버스 해상도(640x360) 기준이다.
    constexpr float kPlayerSpeedPerSec  = 150.0f;
    constexpr float kPlayerSpeedPerTick = kPlayerSpeedPerSec / 60.0f;   // 틱당 2.5 픽셀

    // ---- 원점(피벗) : 스프라이트 안에서 "발밑 가운데" ----
    constexpr float kOriginX = kCellW * 0.5f;                 // 32
    constexpr float kOriginY = static_cast<float>(kCellH);    // 64 = 셀의 아래 끝

    // ---- 히트박스 : 발밑 기준의 상대 좌표 ----
    constexpr float kHitHalfWidth = 10.0f;
    constexpr float kHitHeight    = 44.0f;
    constexpr float kHitFootGap   = 2.0f;

    constexpr float kMoveEpsilon = 0.01f;

    constexpr int kStepIntervalTicks = 15;   // 0.25 초
    constexpr int kFlashTicks        = 9;    // 0.15 초 (피격 표현. 지금은 미사용)

    constexpr float kShakeStrength = 2.0f;
    constexpr int   kShakeTicks    = 8;

    // ---- 스태미나 ----
    //   공격 28 이므로 100 으로 3 번은 되고 4 번째에 고갈된다.
    //   「세 번은 되고 네 번은 안 된다」가 몸으로 익혀지는 배치.
    constexpr float kStaminaMax          = 100.0f;
    constexpr float kStaminaRegenPerTick = 0.9f;   // 초당 54
    constexpr int   kStaminaRegenDelay   = 36;     // 0.6 초. 행동할 때마다 초기화된다
    constexpr float kStaminaExhaustExit  = 25.0f;  // 이 이상 회복되면 경직 해제

    //   ★ 회복 지연이 없으면 공격하는 동안에도 회복되어 스태미나가 의미를 잃는다.
    //     지연(36틱)이 공격 길이(24틱)보다 길어서 연속 공격 중에는 전혀 안 찬다.

    float RandomPitch(float spread)
    {
        static std::mt19937 rng{ 12345 };
        std::uniform_real_distribution<float> dist(-spread, spread);
        return dist(rng);
    }

    float PanFromX(float x)
    {
        return std::clamp(x / static_cast<float>(Config::kCanvasWidth) * 2.0f - 1.0f,
                          -1.0f, 1.0f);
    }

    const char* StateName(PlayerState s)
    {
        switch (s)
        {
        case PlayerState::Run:       return "RUN";
        case PlayerState::Attack:    return "ATTACK";
        case PlayerState::Exhausted: return "EXHAUSTED";
        default:                     return "IDLE";
        }
    }

    // 지금 공격의 어느 구간인가. 화면에 찍어서 프레임 데이터를 눈으로 확인한다.
    const char* AttackPhase(int t, const AttackData& a)
    {
        if (t <  a.startup)            return "startup";
        if (t <  a.startup + a.active) return "ACTIVE";
        return "recovery";
    }
}


bool PlayScene::Enter(SceneContext& ctx)
{
    m_sheet = ctx.assets.Texture(L"assets/textures/player.png");
    if (!m_sheet)
        return false;

    m_playerAnim.Play(kIdleClip);

    Log::Info("[play] Arrows/WASD/Stick = move   Space = attack   Esc = pause");
    Log::Info("[play] F1 = hitbox   F3 = stats");
    Log::Info("[play] ,  = freeze    . = step 1 tick    / = slow motion (1/8)");
    Log::Info("[play] TIP: , 로 멈춘 뒤 Space 를 누르고 . 로 한 틱씩 밟으면");
    Log::Info("[play]      공격이 몇 틱짜리인지 눈으로 셀 수 있다");
    return true;
}


// ----------------------------------------------------------------------------
//  ChangeState — 상태 머신의 "Enter"
//
//    들어가는 순간 한 번만 해야 하는 일을 모아 둔다.
//    이게 없으면 매 틱 Play() 를 부르게 되어 애니메이션이 프레임 0 에서 멈춘다.
// ----------------------------------------------------------------------------
void PlayScene::ChangeState(SceneContext& ctx, PlayerState next)
{
    if (m_state == next)
        return;

    m_state      = next;
    m_stateTicks = 0;

    switch (next)
    {
    case PlayerState::Idle:
        m_playerAnim.Play(kIdleClip);
        break;

    case PlayerState::Run:
        m_playerAnim.Play(kRunClip);
        m_stepCooldown = 0;   // 달리기 시작하자마자 첫 발소리
        break;

    case PlayerState::Attack:
        // forceRestart = true : 같은 클립이라도 처음부터 다시 재생한다.
        // 연속 공격을 넣을 때 필요해진다.
        m_playerAnim.Play(kAttackClip, true);

        // 이번 휘두르기의 "이미 맞춘 대상" 기록을 비운다.
        m_hitObstacleThisSwing = false;

        // ★ 스태미나를 여기서 소모한다.
        //   부족해도 공격은 나간다. 0 미만이 되면 공격이 끝난 뒤 Exhausted 로 간다.
        //   「부족하면 안 나감」이 아니라 「나가고 대가를 치름」이 이 게임의 규칙이다.
        m_stamina.current -= static_cast<float>(kDaggerLight.staminaCost);
        m_stamina.delay    = kStaminaRegenDelay;

        // 휘두르는 소리. ★ 맞는 소리(hit)는 실제로 겹칠 때만 낸다.
        //   둘을 나눠야 헛치기와 명중이 소리로 구분된다.
        ctx.audio.Play("swing", 0.55f, RandomPitch(0.12f), PanFromX(m_player.x));
        break;

    case PlayerState::Exhausted:
        // 지친 전용 애니메이션이 아직 없으므로 idle 을 쓰고 색으로 구분한다.
        m_playerAnim.Play(kIdleClip);
        ctx.audio.Play("ui_cancel", 0.45f);
        Log::Info("[play] 스태미나 고갈 — 경직 (stam {:.1f})", m_stamina.current);
        break;
    }
}


// ----------------------------------------------------------------------------
//  UpdateMovement — Idle / Run 에서만 불린다.
//    "공격 중에는 이동 불가" 를 !attacking 조건으로 흩뿌리지 않고
//    아예 호출하지 않는 것으로 표현한다. 이것이 상태 머신의 요점이다.
// ----------------------------------------------------------------------------
void PlayScene::UpdateMovement(SceneContext& ctx, float moveX, float moveY)
{
    // 바라보는 방향은 좌우 입력이 있을 때만 갱신한다.
    if (moveX < -kMoveEpsilon)      m_player.facing = -1;
    else if (moveX > kMoveEpsilon)  m_player.facing = +1;

    m_player.x += moveX * kPlayerSpeedPerTick;
    m_player.y += moveY * kPlayerSpeedPerTick;

    m_player.x = std::clamp(m_player.x, kOriginX,
                            static_cast<float>(Config::kCanvasWidth) - kOriginX);
    m_player.y = std::clamp(m_player.y, kOriginY,
                            static_cast<float>(Config::kCanvasHeight));

    // 발소리 — 틱을 세어 일정 간격마다
    const bool moving = (std::abs(moveX) > kMoveEpsilon || std::abs(moveY) > kMoveEpsilon);
    if (moving)
    {
        if (--m_stepCooldown <= 0)
        {
            m_stepCooldown = kStepIntervalTicks;
            ctx.audio.Play("step", 0.45f, RandomPitch(0.15f), PanFromX(m_player.x));
        }
    }
    else
    {
        m_stepCooldown = 0;
    }
}


// ----------------------------------------------------------------------------
//  UpdateStamina — 상태와 무관하게 매 틱
// ----------------------------------------------------------------------------
void PlayScene::UpdateStamina()
{
    // 회복 지연 중이면 아직 안 찬다. 행동할 때마다 이 값이 초기화된다.
    if (m_stamina.delay > 0)
    {
        --m_stamina.delay;
        return;
    }

    if (m_stamina.current < kStaminaMax)
    {
        m_stamina.current += kStaminaRegenPerTick;
        if (m_stamina.current > kStaminaMax)
            m_stamina.current = kStaminaMax;
    }
}


void PlayScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    // ---- 시간은 상태와 무관하게 매 틱 흐른다 ----
    //   ★ 애니메이션 진행도 여기다. 상태 분기 안에 넣으면 안 된다.
    //     빠뜨리면 프레임이 0 에서 멈추고, loop=false 클립의 Finished() 가
    //     영원히 false 가 되어 Attack 상태에서 빠져나오지 못한다.
    ++m_stateTicks;
    m_playerAnim.Tick();
    UpdateStamina();

    if (m_player.flash > 0)
        --m_player.flash;

    const Input::MoveIntent move = ctx.input.Move();
    const bool moving = (std::abs(move.x) > kMoveEpsilon || std::abs(move.y) > kMoveEpsilon);
    const bool attackPressed = (consumeEdgeInput && ctx.input.AttackPressed());

    // ---- 상태별 처리 ----
    switch (m_state)
    {
    case PlayerState::Idle:
    case PlayerState::Run:
        UpdateMovement(ctx, move.x, move.y);

        if (attackPressed)
            ChangeState(ctx, PlayerState::Attack);
        else
            ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        break;

    case PlayerState::Attack:
        // ★ 이동 입력을 처리하지 않는다 = 공격 중에는 못 움직인다.
        //   방향 전환도 막힌다. 소울류의 "한 번 휘두르면 끝까지 간다" 감각.

        // ---- 공격 판정 ----
        //   active 구간에서만, 그리고 이번 휘두르기에 아직 안 맞췄을 때만.
        if (AttackActive() && !m_hitObstacleThisSwing)
        {
            if (Intersects(AttackHitbox(), m_obstacle))
            {
                m_hitObstacleThisSwing = true;   // 3틱 동안 3번 맞는 것을 막는다
                m_obstacleFlash        = kFlashTicks;

                // ★ 소리와 흔들림은 "맞는 순간" 에 낸다. 휘두르는 순간이 아니다.
                ctx.camera.Shake(kShakeStrength, kShakeTicks);
                ctx.audio.Play("hit", 0.85f, RandomPitch(0.12f), PanFromX(m_player.x));

                Log::Info("[play] 타격!  t{}  damage {}", m_stateTicks, kDaggerLight.damage);
            }
        }

        // ★ 상태의 길이는 애니메이션이 아니라 프레임 데이터가 정한다.
        //   Finished() 로 판정하면 프레임 데이터 숫자를 바꿔도 타이밍이 안 바뀐다.
        //   데이터가 진실이고, 애니메이션은 거기에 맞춘다.
        if (m_stateTicks >= kDaggerLight.TotalTicks())
        {
            // ★ 공격이 끝난 시점에 스태미나가 0 미만이면 경직에 들어간다.
            //   공격 자체는 정상적으로 나갔다 — 대가를 뒤에 치르는 것이다.
            if (m_stamina.current < 0.0f)
                ChangeState(ctx, PlayerState::Exhausted);
            else
                ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        }
        break;

    case PlayerState::Exhausted:
        // ★ 아무 입력도 처리하지 않는다. 완전히 무방비.
        //   상태 머신 덕분에 이 한 줄이 「모든 입력 처리에 !exhausted 를 붙이기」를
        //   대신한다.
        if (m_stamina.current >= kStaminaExhaustExit)
            ChangeState(ctx, PlayerState::Idle);
        break;
    }

    // ---- 상태와 무관한 판정 ----
    m_touching = Intersects(PlayerHurtbox(), m_obstacle);
    if (m_obstacleFlash > 0)
        --m_obstacleFlash;

    // ---- 상태와 무관한 엣지 입력 ----
    if (consumeEdgeInput)
    {
        if (ctx.input.CancelPressed())
        {
            ctx.audio.Play("ui_cancel");
            ctx.scenes.Push(std::make_unique<PauseScene>());
        }

        if (ctx.input.DebugTogglePressed())
        {
            m_showDebug = !m_showDebug;
            Log::Info("[play] 히트박스 표시 {}", m_showDebug ? "ON" : "OFF");
        }
    }
}


AABB PlayScene::SpriteBounds() const
{
    return {
        m_player.x - kOriginX,
        m_player.y - kOriginY,
        m_player.x - kOriginX + kCellW,
        m_player.y
    };
}


AABB PlayScene::PlayerHurtbox() const
{
    return {
        m_player.x - kHitHalfWidth,
        m_player.y - kHitFootGap - kHitHeight,
        m_player.x + kHitHalfWidth,
        m_player.y - kHitFootGap
    };
}


bool PlayScene::AttackActive() const
{
    if (m_state != PlayerState::Attack)
        return false;

    const AttackData& a = kDaggerLight;
    return m_stateTicks >= a.startup
        && m_stateTicks <  a.startup + a.active;
}


AABB PlayScene::AttackHitbox() const
{
    const AttackData& a = kDaggerLight;

    // 바라보는 방향으로 뻗는다. 발밑 원점 덕분에 계산이 이만큼 짧다.
    // (좌상단 원점이었다면 스프라이트 폭까지 계산해야 했다)
    const float inner = m_player.x + m_player.facing * a.reach;
    const float outer = inner      + m_player.facing * a.width;

    // ★ facing 이 -1 이면 outer < inner 가 되어 사각형이 뒤집힌다.
    //   AABB 는 left <= right 를 전제하고 Intersects 가 그 전제에 의존하므로,
    //   정규화하지 않으면 왼쪽을 볼 때 공격이 절대 맞지 않는다.
    const float left  = (inner < outer) ? inner : outer;
    const float right = (inner < outer) ? outer : inner;

    const float centerY = m_player.y - a.heightFromFoot;
    return { left, centerY - a.height * 0.5f, right, centerY + a.height * 0.5f };
}


void PlayScene::Render(Renderer& renderer)
{
    // ---- 장애물 ----
    //   공격에 맞으면 밝게 번쩍, 몸이 닿으면 붉게, 평소엔 회색.
    DirectX::XMVECTOR obstacleColor = DirectX::Colors::DimGray;
    if (m_obstacleFlash > 0)   obstacleColor = DirectX::Colors::LightGoldenrodYellow;
    else if (m_touching)       obstacleColor = DirectX::Colors::Crimson;

    renderer.DrawFilledRect(m_obstacle, obstacleColor);

    // ---- 플레이어 ----
    DirectX::XMVECTOR tint = DirectX::Colors::White;
    if (m_state == PlayerState::Exhausted)
        tint = DirectX::XMVectorSet(0.45f, 0.45f, 0.55f, 1.0f);   // 지쳐서 어둡게
    else if (m_player.flash > 0)
        tint = DirectX::XMVectorSet(1.0f, 0.35f, 0.30f, 1.0f);

    const DirectX::SpriteEffects fx = (m_player.facing < 0)
        ? DirectX::SpriteEffects_FlipHorizontally
        : DirectX::SpriteEffects_None;

    // ★ 그릴 때는 정수 좌표로. 소수 위치에 그리면 시트의 옆 칸을 물어온다.
    const DirectX::XMFLOAT2 drawPos{
        std::round(m_player.x),
        std::round(m_player.y)
    };

    const RECT src = m_playerAnim.SourceRect(kCellW, kCellH);
    renderer.Sprites().Draw(
        m_sheet.Get(), drawPos, &src, tint,
        0.0f,
        DirectX::XMFLOAT2(kOriginX, kOriginY),
        1.0f,
        fx);

    // ---- 디버그 표시 (F1) ----
    if (m_showDebug)
    {
        renderer.DrawRectOutline(SpriteBounds(),   DirectX::Colors::SlateGray);
        renderer.DrawRectOutline(PlayerHurtbox(),  DirectX::Colors::Lime, 2.0f);
        renderer.DrawRectOutline(m_obstacle,       DirectX::Colors::Yellow);

        // ★ 공격 히트박스 — active 구간에서만 나타난다.
        //   , 로 멈추고 . 로 밟으면 t8 에 나타나 t10 까지 있는 것을 볼 수 있다.
        if (AttackActive())
        {
            renderer.DrawFilledRect(AttackHitbox(),
                DirectX::XMVectorSet(1.0f, 0.2f, 0.2f, 0.35f));
            renderer.DrawRectOutline(AttackHitbox(), DirectX::Colors::Red, 2.0f);
        }

        // 원점(발밑)을 십자로
        renderer.DrawFilledRect(
            { m_player.x - 5.0f, m_player.y - 1.0f, m_player.x + 5.0f, m_player.y + 1.0f },
            DirectX::Colors::Magenta);
        renderer.DrawFilledRect(
            { m_player.x - 1.0f, m_player.y - 5.0f, m_player.x + 1.0f, m_player.y + 5.0f },
            DirectX::Colors::Magenta);
    }
}


// ----------------------------------------------------------------------------
//  DrawStaminaBar
//    스태미나는 화면에 보이지 않으면 게임이 성립하지 않는다.
//    플레이어가 남은 양을 모르면 관리할 수가 없다.
// ----------------------------------------------------------------------------
void PlayScene::DrawStaminaBar(Renderer& renderer) const
{
    constexpr float kBarX = 12.0f;
    constexpr float kBarW = 150.0f;
    constexpr float kBarH = 9.0f;
    const float     barY  = Config::kCanvasHeight - 24.0f;

    // 테두리 겸 배경
    renderer.DrawFilledRect(
        AABB::FromXYWH(kBarX - 1.0f, barY - 1.0f, kBarW + 2.0f, kBarH + 2.0f),
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.7f));

    // ★ current 는 음수가 될 수 있으므로 표시 비율은 0 으로 자른다.
    const float ratio = std::clamp(m_stamina.current / kStaminaMax, 0.0f, 1.0f);

    DirectX::XMVECTOR color = DirectX::XMVectorSet(0.35f, 0.80f, 0.45f, 1.0f);   // 녹색
    if (m_state == PlayerState::Exhausted)
        color = DirectX::XMVectorSet(0.85f, 0.20f, 0.20f, 1.0f);                // 경직 = 빨강
    else if (ratio < 0.3f)
        color = DirectX::XMVectorSet(0.90f, 0.75f, 0.25f, 1.0f);                // 부족 = 노랑

    if (ratio > 0.0f)
    {
        renderer.DrawFilledRect(
            AABB::FromXYWH(kBarX, barY, kBarW * ratio, kBarH), color);
    }

    renderer.DrawString("STAM", kBarX + kBarW + 6.0f, barY - 2.0f,
                        DirectX::Colors::DimGray, 1);
}


void PlayScene::RenderUI(Renderer& renderer)
{
    DrawStaminaBar(renderer);

    // ★ 상태 머신을 눈으로 보기 위한 표시.
    //   F2 로 멈추고 F4 를 눌러 가며 STATE 와 t 를 세면
    //   「공격이 몇 틱짜리인가」를 직접 확인할 수 있다.
    if (m_state == PlayerState::Attack)
    {
        const AttackData& a = kDaggerLight;
        renderer.DrawString(
            std::format("STATE ATTACK  t{:<3}{}   [{} {} {}]",
                        m_stateTicks, AttackPhase(m_stateTicks, a),
                        a.startup, a.active, a.recovery),
            6.0f, 6.0f,
            AttackActive() ? DirectX::Colors::Red : DirectX::Colors::Orange, 1);
    }
    else
    {
        renderer.DrawString(
            std::format("STATE {}  t{}", StateName(m_state), m_stateTicks),
            6.0f, 6.0f,
            m_state == PlayerState::Exhausted ? DirectX::Colors::Red
                                              : DirectX::Colors::Orange, 1);
    }

    if (m_showDebug)
    {
        renderer.DrawString(
            std::format("stam {:6.1f} / {:.0f}   regen delay {:2}",
                        m_stamina.current, kStaminaMax, m_stamina.delay),
            6.0f, 20.0f, DirectX::Colors::Gainsboro, 1);
    }

    if (m_showDebug)
    {
        renderer.DrawString("green=hurtbox  red=hitbox  magenta=origin(feet)",
                            6.0f, Config::kCanvasHeight - 34.0f,
                            DirectX::Colors::Lime, 1);
        renderer.DrawString(m_player.facing < 0 ? "FACING <<" : "FACING >>",
                            6.0f, Config::kCanvasHeight - 50.0f,
                            DirectX::Colors::Gainsboro, 1);
    }

    if (m_touching)
        renderer.DrawString("TOUCHING", 6.0f, Config::kCanvasHeight - 18.0f,
                            DirectX::Colors::Crimson, 1);
}
