#include "Scenes/PlayScene.h"
#include "Scenes/PauseScene.h"

#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
    // ---- 스프라이트시트 배치 ----
    constexpr int kCellW = 64;
    constexpr int kCellH = 64;

    // ---- 애니메이션 클립 ----
    //   ticksPerFrame 이 애니메이션 속도. 60 / n = 애니메이션 fps.
    //   나중에 이 값들이 JSON 으로 빠진다.
    constexpr AnimationClip kIdleClip { /*row*/ 0, /*frames*/ 4, /*ticksPerFrame*/ 10, true };  // 6fps
    constexpr AnimationClip kRunClip  { /*row*/ 1, /*frames*/ 4, /*ticksPerFrame*/  4, true };  // 15fps

    // ---- 플레이어 (임시) ----
    //   ★ 좌표와 속도는 모두 캔버스 해상도(640x360) 기준이다.
    constexpr float kPlayerSpeedPerSec  = 150.0f;                      // 640 폭을 약 4.3 초에 횡단
    constexpr float kPlayerSpeedPerTick = kPlayerSpeedPerSec / 60.0f;  // 틱당 2.5 픽셀

    // 히트박스 오프셋. 64×64 스프라이트 안의 24×40 영역.
    constexpr float kHitOffsetX = 20.0f;
    constexpr float kHitOffsetY = 12.0f;
    constexpr float kHitWidth   = 24.0f;
    constexpr float kHitHeight  = 40.0f;

    // 아날로그 스틱은 완전히 0 이 되지 않으므로 여유를 둔다.
    constexpr float kMoveEpsilon = 0.01f;
}


bool PlayScene::Enter(SceneContext& ctx)
{
    m_sheet = ctx.renderer.LoadTexture(L"assets/textures/sheet.png");
    if (!m_sheet)
        return false;   // ★ 실패를 돌려주면 SceneManager 가 전환을 취소한다

    m_playerAnim.Play(kIdleClip);

    Log::Info("[play] 방향키/WASD/스틱 = 이동   Space = 공격   F1 = 히트박스   Esc = 일시정지");
    return true;
}


void PlayScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    // ---- 지속 입력: 이동 ----
    const Input::MoveIntent move = ctx.input.Move();
    const bool moving = (std::abs(move.x) > kMoveEpsilon || std::abs(move.y) > kMoveEpsilon);

    // ---- 상태에 맞는 애니메이션 ----
    //   매 틱 Play() 를 불러도 괜찮다. 같은 클립이면 내부에서 무시하므로
    //   프레임이 0 으로 되돌아가지 않는다. (AnimationPlayer::Play 주석 참조)
    m_playerAnim.Play(moving ? kRunClip : kIdleClip);
    m_playerAnim.Tick();

    m_player.x += move.x * kPlayerSpeedPerTick;
    m_player.y += move.y * kPlayerSpeedPerTick;

    // 화면 밖으로 나가지 않게. 기준은 창이 아니라 캔버스다.
    m_player.x = std::clamp(m_player.x, 0.0f,
                            static_cast<float>(Config::kCanvasWidth)  - kCellW);
    m_player.y = std::clamp(m_player.y, 0.0f,
                            static_cast<float>(Config::kCanvasHeight) - kCellH);

    // ---- 충돌 판정 ----
    //   스프라이트 전체가 아니라 히트박스로 판정한다.
    m_touching = Intersects(PlayerHitbox(), m_obstacle);

    // ---- 엣지 입력: 프레임의 첫 틱에서만 소비 ----
    if (consumeEdgeInput)
    {
        if (ctx.input.CancelPressed())
        {
            // ★ Push 다. Replace 가 아니다.
            //   PlayScene 이 그대로 살아 있어서 플레이어 위치와 애니메이션이 유지된다.
            //   그리고 이 요청은 지금 처리되지 않는다 — 틱 루프가 끝난 뒤에 적용된다.
            //   그래서 아래 코드가 계속 실행돼도 안전하다.
            ctx.scenes.Push(std::make_unique<PauseScene>());
        }

        if (ctx.input.DebugTogglePressed())
        {
            m_showDebug = !m_showDebug;
            Log::Info("[play] 히트박스 표시 {}", m_showDebug ? "ON" : "OFF");
        }

        if (ctx.input.AttackPressed())
            Log::Info("[play] 공격 (나중에 여기에 상태머신이 들어간다)");
    }
}


AABB PlayScene::SpriteBounds() const
{
    return AABB::FromXYWH(m_player.x, m_player.y,
                          static_cast<float>(kCellW), static_cast<float>(kCellH));
}


AABB PlayScene::PlayerHitbox() const
{
    return AABB::FromXYWH(m_player.x + kHitOffsetX, m_player.y + kHitOffsetY,
                          kHitWidth, kHitHeight);
}


void PlayScene::Render(Renderer& renderer)
{
    // ---- 장애물 ---- 겹치면 붉게 변한다
    renderer.DrawFilledRect(
        m_obstacle,
        m_touching ? DirectX::Colors::Crimson : DirectX::Colors::DimGray);

    // ---- 플레이어 ----
    //   지금 프레임에 해당하는 칸만 잘라 그린다.
    //   좌표는 캔버스 기준(640x360). 화면으로의 확대는 Renderer 가 마지막에 한 번만.
    const RECT src = m_playerAnim.SourceRect(kCellW, kCellH);
    renderer.Sprites().Draw(
        m_sheet.Get(), DirectX::XMFLOAT2(m_player.x, m_player.y), &src);

    // ---- 디버그 표시 (F1) ----
    if (m_showDebug)
    {
        // 회색 = 스프라이트 범위(64×64) / 초록 = 실제 히트박스 / 노랑 = 장애물
        renderer.DrawRectOutline(SpriteBounds(), DirectX::Colors::SlateGray);
        renderer.DrawRectOutline(PlayerHitbox(), DirectX::Colors::Lime, 2.0f);
        renderer.DrawRectOutline(m_obstacle,     DirectX::Colors::Yellow);
    }
}
