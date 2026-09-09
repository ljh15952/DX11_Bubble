#include "Scenes/TitleScene.h"
#include "Scenes/PlayScene.h"

#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <memory>


bool TitleScene::Enter(SceneContext&)
{
    Log::Info("[title] Enter / Space / GamePad A = start   Esc = quit");
    return true;
}


void TitleScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    ++m_blinkTick;

    if (!consumeEdgeInput)
        return;

    if (ctx.input.ConfirmPressed())
    {
        // ★ Replace 다. 타이틀은 돌아올 필요가 없으므로 스택에서 사라진다.
        ctx.scenes.Replace(std::make_unique<PlayScene>());
    }
    else if (ctx.input.CancelPressed())
    {
        // 스택을 전부 비우면 Game 이 그것을 종료 신호로 받는다.
        ctx.scenes.Clear();
    }
}


void TitleScene::Render(Renderer& renderer)
{
    const float cx = Config::kCanvasWidth  * 0.5f;

    // 폰트 셀이 8×14 이므로 scale 3 이면 24×42 픽셀 글자가 된다.
    renderer.DrawStringCentered("DARKBUBBLE", cx, 90.0f, DirectX::Colors::Gainsboro, 3);
    renderer.DrawStringCentered("- prototype -", cx, 140.0f, DirectX::Colors::DimGray, 1);

    // 30 틱(0.5초)마다 깜빡인다.
    if ((m_blinkTick / 30) % 2 == 0)
        renderer.DrawStringCentered("PRESS ENTER", cx, 220.0f, DirectX::Colors::White, 2);

    renderer.DrawStringCentered("ESC : QUIT", cx, 300.0f, DirectX::Colors::DimGray, 1);
}
