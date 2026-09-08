#include "Scenes/TitleScene.h"
#include "Scenes/PlayScene.h"

#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <cmath>
#include <memory>

namespace
{
    // 로고 대신 쓰는 블록 패턴. 폰트가 들어오면 사라질 코드다.
    constexpr float kBlock = 14.0f;
    constexpr float kGap   =  4.0f;
    constexpr int   kBlockCount = 9;
}


bool TitleScene::Enter(SceneContext&)
{
    Log::Info("[title] Enter / Space / 패드A = 시작   Esc = 종료");
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
    const float cy = Config::kCanvasHeight * 0.5f;

    // ---- 로고 자리 (블록 줄) ----
    const float totalW = kBlockCount * kBlock + (kBlockCount - 1) * kGap;
    float x = cx - totalW * 0.5f;
    for (int i = 0; i < kBlockCount; ++i)
    {
        // 가운데로 갈수록 높은 블록. 산 모양이 되어 로고처럼 보인다.
        const float t = 1.0f - std::abs(i - (kBlockCount - 1) * 0.5f) / ((kBlockCount - 1) * 0.5f);
        const float h = 16.0f + t * 34.0f;

        renderer.DrawFilledRect(
            AABB::FromXYWH(x, cy - 40.0f - h * 0.5f, kBlock, h),
            DirectX::Colors::DarkSlateGray);
        x += kBlock + kGap;
    }

    // ---- 「Press Enter」 자리 (깜빡이는 막대) ----
    if ((m_blinkTick / 30) % 2 == 0)
    {
        renderer.DrawFilledRect(
            AABB::FromXYWH(cx - 70.0f, cy + 30.0f, 140.0f, 10.0f),
            DirectX::Colors::White);
    }
}
