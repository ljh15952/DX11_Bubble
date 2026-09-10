#include "Scenes/DeathScene.h"

#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Audio/Audio.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <algorithm>

namespace
{
    // ---- 연출 타이밍 (틱) ----
    //   ★ 「한 박자」가 필요하다. 맞은 즉시 화면이 덮이면 무엇에 죽었는지
    //     보이지 않아서 플레이어가 배울 수 없다. 그 지연은 PlayScene 쪽에 있고,
    //     여기서는 이미 죽은 화면을 어떻게 덮을지만 정한다.
    constexpr int kFadeInTicks = 45;                          // 0.75 초에 걸쳐 어두워진다
    constexpr int kHoldTicks   = 195;                         // 3.25 초 유지
    constexpr int kTotalTicks  = kFadeInTicks + kHoldTicks;   // 합계 4 초 뒤 자동 부활

    //   ★ 자동 부활은 **길게 잡는 편이 안전하다.**
    //     페이드가 끝나면(0.75초) 언제든 키로 건너뛸 수 있으므로,
    //     길어서 답답한 사람은 누르면 되고 짧아서 여운이 없는 것은 되돌릴 수 없다.
    //     비대칭인 선택에서는 되돌릴 수 있는 쪽으로 기운다.

    // 글자는 화면이 조금 어두워진 뒤에 나타난다.
    // 동시에 나오면 밝은 배경 위에 붉은 글자가 겹쳐 읽기 어렵다.
    constexpr int kTextDelayTicks = 18;
    constexpr int kTextFadeTicks  = 27;

    constexpr float kOverlayAlpha = 0.80f;

    // 0 -> 1 로 올라가는 비율. 시작 틱과 길이를 주면 된다.
    float FadeRatio(int ticks, int startTick, int lengthTicks)
    {
        if (lengthTicks <= 0)
            return 1.0f;
        const float r = static_cast<float>(ticks - startTick) / static_cast<float>(lengthTicks);
        return std::clamp(r, 0.0f, 1.0f);
    }
}


bool DeathScene::Enter(SceneContext& ctx)
{
    // 낮게 깔리는 소리. 피격음(hit)과 구분되어야 「끝났다」로 들린다.
    ctx.audio.Play("ui_cancel", 0.9f, -0.8f);
    Log::Info("[death] YOU DIED  ({}틱 뒤 자동 부활 / Enter·Esc 로 건너뛰기)", kTotalTicks);
    return true;
}


void DeathScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    ++m_ticks;

    if (m_leaving)
        return;

    // ★ 페이드가 끝나기 전에는 건너뛰지 못한다.
    //   죽은 순간에 눌린 키로 화면이 뜨자마자 사라지는 것을 막는다.
    //
    //   Confirm 은 Enter / Space / 패드 A, Cancel 은 Esc / 패드 B 다.
    //   Space 가 Attack 과 같은 키지만 문제되지 않는다 — 엣지 입력은 틱이 돌 때마다
    //   비워지므로, 죽는 순간의 입력은 45틱 뒤까지 남아 있을 수 없다.
    //   화면 안내에는 대표로 ENTER / ESC 만 적는다.
    const bool skip = consumeEdgeInput
                   && m_ticks > kFadeInTicks
                   && (ctx.input.ConfirmPressed() || ctx.input.CancelPressed());

    if (skip || m_ticks >= kTotalTicks)
    {
        m_leaving = true;

        // ★ Pop 이다. Replace 가 아니다.
        //   아래 PlayScene 이 살아 있어야 「무엇을 되돌리고 무엇을 남길지」를
        //   그쪽이 고를 수 있다. Replace 로 새로 만들면 전부 초기화가 강제된다.
        //   Pop 이 적용되는 순간 PlayScene::Resume 이 불려 부활이 일어난다.
        ctx.scenes.Pop();
    }
}


void DeathScene::RenderUI(Renderer& renderer)
{
    const float cx = Config::kCanvasWidth  * 0.5f;
    const float cy = Config::kCanvasHeight * 0.5f;

    // ---- 화면을 덮는다 ----
    const float darkness = FadeRatio(m_ticks, 0, kFadeInTicks);
    const AABB full = AABB::FromXYWH(
        0.0f, 0.0f,
        static_cast<float>(Config::kCanvasWidth),
        static_cast<float>(Config::kCanvasHeight));
    renderer.DrawFilledRect(full,
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, kOverlayAlpha * darkness));

    // ---- 글자 ----
    //   어두운 붉은색. 흰색으로 쓰면 「메뉴」처럼 보이고 무게가 사라진다.
    const float textAlpha = FadeRatio(m_ticks, kTextDelayTicks, kTextFadeTicks);
    if (textAlpha > 0.0f)
    {
        renderer.DrawStringCentered("YOU DIED", cx, cy - 22.0f,
            DirectX::XMVectorSet(0.66f, 0.10f, 0.10f, textAlpha), 3);
    }

    // ---- 건너뛰기 안내 ----
    //   건너뛸 수 있게 된 뒤에만, 깜빡이며 나타난다.
    //   ※ 화면 문자열은 ASCII 만 쓸 수 있다 — BitmapFont 가 ASCII 전용이다.
    if (m_ticks > kFadeInTicks && ((m_ticks / 30) % 2 == 0))
    {
        renderer.DrawStringCentered("ENTER / ESC", cx, cy + 34.0f,
            DirectX::XMVectorSet(0.45f, 0.42f, 0.42f, 1.0f), 1);
    }
}
