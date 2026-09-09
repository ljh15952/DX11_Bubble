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

    // ---- 원점(피벗) : 스프라이트 안에서 "발밑 가운데" ----
    //   ★ 이 값을 Draw 의 origin 으로 넘기면 m_player.x / y 가
    //     스프라이트의 좌상단이 아니라 캐릭터의 발 위치를 뜻하게 된다.
    //
    //     좌상단 기준                  발밑 기준
    //       ●────────┐                 ┌────────┐
    //       │  캐릭터 │                 │  캐릭터 │
    //       └────────┘                 └───●────┘
    //
    //   바닥에 세우기 / 크기가 다른 적을 섞기 / 그림자 붙이기 / y 정렬이
    //   전부 보정 없이 된다. 단위는 소스 셀 안의 픽셀이다(화면 픽셀이 아니다).
    constexpr float kOriginX = kCellW * 0.5f;    // 32
    constexpr float kOriginY = static_cast<float>(kCellH);   // 64 = 셀의 아래 끝

    // ---- 히트박스 : 발밑 기준의 상대 좌표 ----
    //   스프라이트(64×64)보다 훨씬 작다. 여백까지 판정에 넣으면
    //   "안 맞았는데 맞았다" 가 되어 소울라이크의 재미가 사라진다.
    constexpr float kHitHalfWidth = 12.0f;   // 좌우로 각각 12 → 폭 24
    constexpr float kHitHeight    = 40.0f;
    constexpr float kHitFootGap   = 12.0f;   // 발끝에서 히트박스 아래변까지

    // 아날로그 스틱은 완전히 0 이 되지 않으므로 여유를 둔다.
    constexpr float kMoveEpsilon = 0.01f;

    // 발소리 간격. 틱 단위라 어느 PC 에서도 같은 리듬이 된다.
    constexpr int kStepIntervalTicks = 18;   // 0.3 초

    // 피격/공격 시 붉게 번쩍이는 시간.
    //   ※ color 인자는 "곱셈" 이라 원본보다 밝게는 못 만든다.
    //     흰색 번쩍이 필요하면 가산 블렌드로 한 번 더 그려야 한다.
    //     어두운 분위기의 게임이라 붉은 틴트로 충분하다.
    constexpr int kFlashTicks = 9;           // 0.15 초

    // 화면 흔들림. 캔버스(640x360) 기준 픽셀이므로 화면에서는 2 배로 보인다.
    constexpr float kShakeStrength = 3.0f;
    constexpr int   kShakeTicks    = 10;     // 약 0.17 초

    // 같은 효과음을 그대로 반복하면 기계처럼 들린다.
    // 피치를 조금씩 흔들면 훨씬 자연스러워진다. 게임 오디오의 기본 기법.
    float RandomPitch(float spread)
    {
        static std::mt19937 rng{ 12345 };
        std::uniform_real_distribution<float> dist(-spread, spread);
        return dist(rng);
    }

    // 화면 x 좌표를 스테레오 정위(-1 왼쪽 ~ +1 오른쪽)로 바꾼다.
    float PanFromX(float x)
    {
        return std::clamp(x / static_cast<float>(Config::kCanvasWidth) * 2.0f - 1.0f,
                          -1.0f, 1.0f);
    }
}


bool PlayScene::Enter(SceneContext& ctx)
{
    // ★ Renderer 가 아니라 Assets 를 통해 얻는다.
    //   두 번째부터는 디스크를 읽지 않고 캐시에서 나온다.
    m_sheet = ctx.assets.Texture(L"assets/textures/sheet.png");
    if (!m_sheet)
        return false;   // ★ 실패를 돌려주면 SceneManager 가 전환을 취소한다

    m_playerAnim.Play(kIdleClip);

    Log::Info("[play] Arrows/WASD/Stick = move  Space = attack  Esc = pause");
    Log::Info("[play] F1 = hitbox   F2 = freeze   F3 = stats   F4 = step 1 tick");
    return true;
}


void PlayScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    // 번쩍임 타이머는 매 틱 줄인다. 엣지 입력과 무관하게 시간이 흘러야 한다.
    if (m_player.flash > 0)
        --m_player.flash;

    // ---- 지속 입력: 이동 ----
    const Input::MoveIntent move = ctx.input.Move();
    const bool moving = (std::abs(move.x) > kMoveEpsilon || std::abs(move.y) > kMoveEpsilon);

    // ---- 바라보는 방향 ----
    //   좌우 입력이 있을 때만 갱신한다. 위/아래로만 움직이거나 멈췄을 때
    //   방향이 초기화되면 캐릭터가 홱 돌아보는 것처럼 보인다.
    if (move.x < -kMoveEpsilon)      m_player.facing = -1;
    else if (move.x > kMoveEpsilon)  m_player.facing = +1;

    // ---- 상태에 맞는 애니메이션 ----
    //   매 틱 Play() 를 불러도 괜찮다. 같은 클립이면 내부에서 무시하므로
    //   프레임이 0 으로 되돌아가지 않는다. (AnimationPlayer::Play 주석 참조)
    m_playerAnim.Play(moving ? kRunClip : kIdleClip);
    m_playerAnim.Tick();

    // ---- 발소리 ----
    //   틱을 세어 일정 간격마다 울린다. 정지하면 카운터를 리셋해서
    //   다시 걷기 시작할 때 곧바로 한 번 울리게 한다.
    if (moving)
    {
        if (--m_stepCooldown <= 0)
        {
            m_stepCooldown = kStepIntervalTicks;
            ctx.audio.Play("step", 0.5f, RandomPitch(0.15f), PanFromX(m_player.x));
        }
    }
    else
    {
        m_stepCooldown = 0;
    }

    m_player.x += move.x * kPlayerSpeedPerTick;
    m_player.y += move.y * kPlayerSpeedPerTick;

    // 화면 밖으로 나가지 않게. 기준은 창이 아니라 캔버스다.
    // ★ 원점이 발밑이므로 경계값이 바뀌었다.
    //   x 는 좌우로 셀의 절반, y 는 위로 셀 높이만큼 여유가 필요하다.
    m_player.x = std::clamp(m_player.x, kOriginX,
                            static_cast<float>(Config::kCanvasWidth) - kOriginX);
    m_player.y = std::clamp(m_player.y, kOriginY,
                            static_cast<float>(Config::kCanvasHeight));

    // ---- 충돌 판정 ----
    //   스프라이트 전체가 아니라 히트박스로 판정한다.
    m_touching = Intersects(PlayerHitbox(), m_obstacle);

    // ---- 엣지 입력: 프레임의 첫 틱에서만 소비 ----
    if (consumeEdgeInput)
    {
        if (ctx.input.CancelPressed())
        {
            ctx.audio.Play("ui_cancel");
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
        {
            m_player.flash = kFlashTicks;

            // 캔버스가 640x360 이므로 3 픽셀이면 화면에서는 6 픽셀. 충분히 세다.
            ctx.camera.Shake(kShakeStrength, kShakeTicks);

            ctx.audio.Play("hit", 0.8f, RandomPitch(0.12f), PanFromX(m_player.x));
            Log::Info("[play] 공격 (나중에 여기에 상태머신이 들어간다)");
        }
    }
}


// 원점이 발밑이므로, 스프라이트는 그 지점에서 위로/좌우로 펼쳐진다.
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
    // 발밑 기준의 상대 좌표라 좌우 반전과 무관하게 그대로 쓸 수 있다.
    // (좌상단 기준이었다면 반전할 때 오프셋도 뒤집어야 했다)
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
    //   Draw 의 인자를 전부 쓰는 곳이다.

    // ④ color 는 곱셈이다. White(1,1,1,1) 를 곱하면 원본 그대로.
    //    붉은 색을 곱하면 G·B 가 깎여 붉게 보인다.
    DirectX::XMVECTOR tint = DirectX::Colors::White;
    if (m_player.flash > 0)
        tint = DirectX::XMVectorSet(1.0f, 0.35f, 0.30f, 1.0f);

    // ⑧ effects 는 텍스처 좌표를 뒤집는다.
    //    화면에서 차지하는 사각형은 그대로고 그림만 거울처럼 뒤집힌다.
    const DirectX::SpriteEffects fx = (m_player.facing < 0)
        ? DirectX::SpriteEffects_FlipHorizontally
        : DirectX::SpriteEffects_None;

    // ★ 그릴 때는 정수 좌표로 맞춘다.
    //
    //   m_player.x 는 틱당 2.5 픽셀씩 움직여 소수가 된다(122.5 등).
    //   소수 위치에 그리면 스프라이트 가장자리 픽셀의 중심이 소스 사각형 밖
    //   0.5 텍셀을 가리켜 시트의 "옆 칸" 을 물어온다 = 1 픽셀 선이 생긴다.
    //   (텍스처 블리딩. PointClamp 는 텍스처 전체 경계만 막아 주고
    //    시트 안의 칸 경계는 텍스처 내부라 그냥 옆 칸을 읽는다)
    //
    //   계산은 소수로, 그리기는 정수로 — 픽셀아트에서 계속 반복되는 규칙이다.
    const DirectX::XMFLOAT2 drawPos{
        std::round(m_player.x),
        std::round(m_player.y)
    };

    const RECT src = m_playerAnim.SourceRect(kCellW, kCellH);
    renderer.Sprites().Draw(
        m_sheet.Get(),
        drawPos,                                     // ② 발밑 위치 (정수)
        &src,                                        // ③ 시트의 어느 칸
        tint,                                        // ④ 곱할 색
        0.0f,                                        // ⑤ 회전(라디안)
        DirectX::XMFLOAT2(kOriginX, kOriginY),       // ⑥ 원점 = 발밑 가운데
        1.0f,                                        // ⑦ 확대
        fx);                                         // ⑧ 좌우 반전

    // ---- 디버그 표시 (F1) ----
    if (m_showDebug)
    {
        // 회색 = 스프라이트 범위(64×64) / 초록 = 실제 히트박스 / 노랑 = 장애물
        renderer.DrawRectOutline(SpriteBounds(), DirectX::Colors::SlateGray);
        renderer.DrawRectOutline(PlayerHitbox(), DirectX::Colors::Lime, 2.0f);
        renderer.DrawRectOutline(m_obstacle,     DirectX::Colors::Yellow);

        // ★ 원점(발밑)을 십자로 표시한다.
        //   m_player.x / y 가 실제로 어디를 가리키는지 눈으로 확인할 수 있다.
        renderer.DrawFilledRect(
            { m_player.x - 5.0f, m_player.y - 1.0f, m_player.x + 5.0f, m_player.y + 1.0f },
            DirectX::Colors::Magenta);
        renderer.DrawFilledRect(
            { m_player.x - 1.0f, m_player.y - 5.0f, m_player.x + 1.0f, m_player.y + 5.0f },
            DirectX::Colors::Magenta);

    }
}


// ----------------------------------------------------------------------------
//  RenderUI — 카메라를 무시한다. 화면이 흔들려도 글자는 제자리에 있어야 한다.
// ----------------------------------------------------------------------------
void PlayScene::RenderUI(Renderer& renderer)
{
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
