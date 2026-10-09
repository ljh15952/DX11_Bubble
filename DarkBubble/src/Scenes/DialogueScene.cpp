#include "Scenes/DialogueScene.h"

#include "Audio/Audio.h"
#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Gameplay/PartsComponent.h"
#include "Gameplay/PlayerController.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kTop     = 14.0f;   // 장비 화면과 같은 높이 — 둘이 같이 뜨는 일은 없다
    constexpr float kPadX    = 12.0f;
    constexpr float kNameH   = 22.0f;   // 이름 줄 + 틈
    constexpr float kRowH    = 14.0f;   // 글꼴 한 칸 높이(8x14)
    constexpr float kHintH   = 16.0f;   // 「E : NEXT」 줄
}


bool DialogueScene::Enter(SceneContext&)
{
    m_hpAtStart = BodyHp();
    Log::Info("[talk] {} — E / Enter = 다음   Esc = 닫기   ★ 게임은 안 멈춘다 (맞으면 끊긴다)",
              m_runner.Speaker());
    return true;
}


int DialogueScene::BodyHp() const
{
    int sum = 0;
    for (int p = 0; p < Part_Count; ++p)
        sum += m_parts.Hp(p);
    return sum;
}


void DialogueScene::Close(SceneContext& ctx)
{
    // ★ 한 번만 요청한다. 한 프레임에 틱이 여러 번 돌면 Pop 이 여러 번
    //   요청되어 「전환이 두 번 요청됨」이 된다(handoff §8).
    if (m_closing)
        return;
    m_closing = true;
    ctx.scenes.Pop();
}


void DialogueScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    if (m_closing)
        return;

    ++m_blinkTick;

    // ★★ 맞거나 죽으면 닫힌다.
    //   죽음은 장비 화면과 같은 이유도 있다 — 이 위에 사망 화면이 쌓이면
    //   부활 통지(Resume)가 PlayScene 이 아니라 이 Scene 에 간다.
    if (m_player.IsDead() || BodyHp() < m_hpAtStart)
    {
        Log::Info("[talk] 맞았다 — 대화가 끊겼다");
        Close(ctx);
        return;
    }

    m_runner.Tick();

    if (!consumeEdgeInput)
        return;

    const Input& in = ctx.input;

    if (in.CancelPressed())
    {
        ctx.audio.Play("ui_cancel", 0.5f);
        Close(ctx);
        return;
    }

    // ★ E 로 말을 걸었으니 **E 로 넘긴다.** Enter / Space(패드 A)도 같다.
    if (in.InteractPressed() || in.ConfirmPressed())
    {
        m_runner.Press();
        if (m_runner.Finished())
        {
            ctx.audio.Play("ui_cancel", 0.4f);
            Close(ctx);
            return;
        }
        ctx.audio.Play("step", 0.3f, 0.6f);
    }
}


void DialogueScene::RenderUI(Renderer& renderer)
{
    if (m_runner.Finished())
        return;

    // ★ 상자 폭은 **글자 수에서 계산한다**(DialogueBox::kColumns).
    //   폭을 픽셀로 따로 적으면, 둘 중 하나만 고쳤을 때 글이 상자를 뚫고 나간다.
    const float charW  = renderer.MeasureString("M");
    const float width  = static_cast<float>(DialogueBox::kColumns) * charW + kPadX * 2.0f;
    const float height = kNameH + static_cast<float>(DialogueBox::kRows) * kRowH + kHintH;
    const float left   = std::round((Config::kCanvasWidth - width) * 0.5f);

    // ---- 판 — 장비 화면과 같은 판. 화면 전체를 덮지 않는다(게임이 보여야 한다) ----
    const AABB panel{ left, kTop, left + width, kTop + height };
    renderer.DrawFilledRect(panel, DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.78f));
    renderer.DrawRectOutline(panel, DirectX::XMVectorSet(0.55f, 0.55f, 0.62f, 1.0f), 1.0f);

    // ---- 누가 말하는가 ----
    renderer.DrawString(m_runner.Speaker(), left + kPadX, kTop + 6.0f, DirectX::Colors::Gold, 1);

    // ---- 대사 — **다 드러난 뒤의 모양으로** 접어 두고, 드러난 만큼만 그린다 ----
    //   ★ 드러나는 만큼만 그때그때 접으면 단어가 윗줄에서 아랫줄로 튀어 내려간다(WrapText 주석).
    const std::string&     text  = m_runner.Text();
    const std::string_view view  = text;
    const size_t           shown = m_runner.Shown();
    const auto             rows  = WrapText(text, DialogueBox::kColumns);

    for (size_t r = 0; r < rows.size() && r < DialogueBox::kRows; ++r)
    {
        const WrappedRow& row = rows[r];
        if (shown <= row.start)
            break;   // 아직 이 줄까지 안 왔다

        const size_t n = std::min(row.length, shown - row.start);
        renderer.DrawString(view.substr(row.start, n),
                            left + kPadX, kTop + kNameH + static_cast<float>(r) * kRowH,
                            DirectX::Colors::Gainsboro, 1);
    }

    // ---- 다 나왔으면 「누르면 무엇이 되는가」 ----
    //   ★ 「다음」과 「끝」을 구분해 적는다. 똑같이 적으면 마지막 줄에서 한 번
    //     더 눌러 보고서야 끝났다는 것을 안다.
    if (m_runner.LineDone() && (m_blinkTick / 30) % 2 == 0)
    {
        const char* hint = m_runner.LastLine() ? "E : END" : "E : NEXT";
        renderer.DrawString(hint, left + width - kPadX - renderer.MeasureString(hint),
                            kTop + height - kHintH + 1.0f, DirectX::Colors::DimGray, 1);
    }
}
