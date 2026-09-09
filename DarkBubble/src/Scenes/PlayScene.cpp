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

    // 6 프레임 × 4 틱 = 공격 한 번에 24 틱 (0.4 초).
    // F2 로 멈추고 F4 를 24 번 누르면 정확히 셀 수 있다.

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
        case PlayerState::Run:    return "RUN";
        case PlayerState::Attack: return "ATTACK";
        default:                  return "IDLE";
        }
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

        // ※ 지금은 공격 "시작" 에서 소리와 흔들림을 낸다.
        //   5-b 에서 프레임 데이터가 들어오면 실제로 맞는 순간(지속 구간)으로 옮긴다.
        ctx.camera.Shake(kShakeStrength, kShakeTicks);
        ctx.audio.Play("hit", 0.7f, RandomPitch(0.12f), PanFromX(m_player.x));
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


void PlayScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    // ---- 시간은 상태와 무관하게 매 틱 흐른다 ----
    //   ★ 애니메이션 진행도 여기다. 상태 분기 안에 넣으면 안 된다.
    //     빠뜨리면 프레임이 0 에서 멈추고, loop=false 클립의 Finished() 가
    //     영원히 false 가 되어 Attack 상태에서 빠져나오지 못한다.
    ++m_stateTicks;
    m_playerAnim.Tick();

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
        //
        //   모션이 끝나면(loop=false 클립이 마지막 프레임에 도달) 복귀.
        if (m_playerAnim.Finished())
            ChangeState(ctx, moving ? PlayerState::Run : PlayerState::Idle);
        break;
    }

    // ---- 상태와 무관한 판정 ----
    m_touching = Intersects(PlayerHitbox(), m_obstacle);

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


AABB PlayScene::PlayerHitbox() const
{
    return {
        m_player.x - kHitHalfWidth,
        m_player.y - kHitFootGap - kHitHeight,
        m_player.x + kHitHalfWidth,
        m_player.y - kHitFootGap
    };
}


void PlayScene::Render(Renderer& renderer)
{
    // ---- 장애물 ---- 겹치면 붉게 변한다
    renderer.DrawFilledRect(
        m_obstacle,
        m_touching ? DirectX::Colors::Crimson : DirectX::Colors::DimGray);

    // ---- 플레이어 ----
    DirectX::XMVECTOR tint = DirectX::Colors::White;
    if (m_player.flash > 0)
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
        renderer.DrawRectOutline(SpriteBounds(), DirectX::Colors::SlateGray);
        renderer.DrawRectOutline(PlayerHitbox(), DirectX::Colors::Lime, 2.0f);
        renderer.DrawRectOutline(m_obstacle,     DirectX::Colors::Yellow);

        // 원점(발밑)을 십자로
        renderer.DrawFilledRect(
            { m_player.x - 5.0f, m_player.y - 1.0f, m_player.x + 5.0f, m_player.y + 1.0f },
            DirectX::Colors::Magenta);
        renderer.DrawFilledRect(
            { m_player.x - 1.0f, m_player.y - 5.0f, m_player.x + 1.0f, m_player.y + 5.0f },
            DirectX::Colors::Magenta);
    }
}


void PlayScene::RenderUI(Renderer& renderer)
{
    // ★ 상태 머신을 눈으로 보기 위한 표시.
    //   F2 로 멈추고 F4 를 눌러 가며 STATE 와 t 를 세면
    //   「공격이 몇 틱짜리인가」를 직접 확인할 수 있다.
    renderer.DrawString(
        std::string("STATE ") + StateName(m_state) + "  t" + std::to_string(m_stateTicks),
        6.0f, 6.0f, DirectX::Colors::Orange, 1);

    if (m_showDebug)
    {
        renderer.DrawString("gray=sprite  green=hitbox  magenta=origin(feet)",
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
