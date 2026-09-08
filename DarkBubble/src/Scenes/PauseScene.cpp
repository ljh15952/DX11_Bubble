#include "Scenes/PauseScene.h"

#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

#include <DirectXColors.h>

namespace
{
    // 폰트가 아직 없으므로 도형으로 표시한다.
    // SpriteFont 를 넣으면 「PAUSED」 문자로 바뀔 자리다.
    constexpr float kBarW = 160.0f;
    constexpr float kBarH = 12.0f;
}


bool PauseScene::Enter(SceneContext&)
{
    Log::Info("[pause] Esc 또는 Enter 로 재개");
    return true;
}


void PauseScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    ++m_blinkTick;

    if (!consumeEdgeInput)
        return;

    // Esc 로 들어왔으니 Esc 로 나간다. Enter 도 허용.
    if (ctx.input.CancelPressed() || ctx.input.ConfirmPressed())
        ctx.scenes.Pop();   // ★ Pop 이므로 PlayScene 이 그대로 되살아난다
}


void PauseScene::Render(Renderer& renderer)
{
    // ---- 화면 전체를 어둡게 ----
    //   알파 0.65 의 검정을 덮는다.
    //   BeginFrame 이 NonPremultiplied 블렌드를 걸어둔 덕분에 그대로 반투명이 된다.
    const AABB full = AABB::FromXYWH(
        0.0f, 0.0f,
        static_cast<float>(Config::kCanvasWidth),
        static_cast<float>(Config::kCanvasHeight));

    renderer.DrawFilledRect(full, DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.65f));

    // ---- 「일시정지」 표시 (임시로 막대 두 개) ----
    const float cx = Config::kCanvasWidth  * 0.5f;
    const float cy = Config::kCanvasHeight * 0.5f;

    renderer.DrawFilledRect(
        AABB::FromXYWH(cx - kBarW * 0.5f, cy - kBarH - 4.0f, kBarW, kBarH),
        DirectX::Colors::White);

    // 30 틱(0.5초)마다 깜빡인다. 60 으로 나눈 나머지가 30 미만일 때만 그린다.
    if ((m_blinkTick / 30) % 2 == 0)
    {
        renderer.DrawFilledRect(
            AABB::FromXYWH(cx - kBarW * 0.5f, cy + 4.0f, kBarW, kBarH),
            DirectX::Colors::Gray);
    }
}
