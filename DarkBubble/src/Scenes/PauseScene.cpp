#include "Scenes/PauseScene.h"

#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

#include <DirectXColors.h>


bool PauseScene::Enter(SceneContext&)
{
    Log::Info("[pause] Esc / Enter = resume");
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

    const float cx = Config::kCanvasWidth  * 0.5f;
    const float cy = Config::kCanvasHeight * 0.5f;

    renderer.DrawStringCentered("PAUSED", cx, cy - 40.0f, DirectX::Colors::White, 3);

    if ((m_blinkTick / 30) % 2 == 0)
        renderer.DrawStringCentered("ESC / ENTER : RESUME", cx, cy + 20.0f,
                                    DirectX::Colors::Gainsboro, 1);
}
