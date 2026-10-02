#include "Scenes/TitleScene.h"
#include "Scenes/PlayScene.h"

#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Audio/Audio.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <memory>
#include <string>


bool TitleScene::Enter(SceneContext&)
{
    std::string err;
    switch (SaveIO::Load(SaveIO::kPath, m_save, &err))
    {
    case SaveIO::LoadResult::Ok:
        m_hasSave = true;
        Log::Info("[title] 저장이 있다 — 마지막으로 쉰 곳 {}:{}", m_save.restMap,
                  m_save.restPoint.empty() ? "start" : m_save.restPoint);
        break;

    case SaveIO::LoadResult::Missing:
        break;   // 처음이다 — 고를 것이 없다

    case SaveIO::LoadResult::Broken:
        // ★★ 그대로 두면 새 게임의 첫 저장이 1초 뒤에 **덮어쓴다** — 무엇이
        //   깨졌는지 영영 볼 수 없게 된다. 지우지 않고 옆으로 치운다.
        Log::Info("[title] 저장 파일이 깨졌다 ({})", err);
        if (SaveIO::SetAside(SaveIO::kPath, &err))
        {
            m_setAside = true;
            Log::Info("[title]   -> saves/save.bad.json 으로 옮겨 두었다. 새로 시작한다");
        }
        else
        {
            Log::Info("[title]   -> 옮기지도 못했다 ({}). 새로 시작하면 덮어쓴다", err);
        }
        break;
    }

    Log::Info("[title] Enter / Space / GamePad A = start   {}Esc = quit",
              m_hasSave ? "<- -> = CONTINUE / NEW GAME   " : "");
    return true;
}


void TitleScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    ++m_blinkTick;

    if (!consumeEdgeInput)
        return;

    // ★ 둘뿐이라 ← → 어느 쪽이든 **바꾼다.** 장비 화면에서 칸을 고르는 키와 같다.
    if (m_hasSave && (ctx.input.MenuLeftPressed() || ctx.input.MenuRightPressed()))
    {
        m_newGame = !m_newGame;
        ctx.audio.Play("ui_confirm", 0.4f);
    }

    if (ctx.input.ConfirmPressed())
    {
        ctx.audio.Play("ui_confirm");

        // ★★ 새 게임 = **기본 저장**이다. 어느 쪽이든 저장 하나를 넘긴다(같은 길).
        //   ※ 옮기지(move) 않고 **복사한다.** PlayScene 진입이 실패하면 타이틀이
        //     남는데, 그때 m_save 가 비어 있으면 다시 CONTINUE 를 눌렀을 때 맨몸이 된다.
        SaveData start = (m_hasSave && !m_newGame) ? m_save : SaveData::NewGame();

        // ★ Replace 다. 타이틀은 돌아올 필요가 없으므로 스택에서 사라진다.
        ctx.scenes.Replace(std::make_unique<PlayScene>(std::move(start)));
    }
    else if (ctx.input.CancelPressed())
    {
        ctx.audio.Play("ui_cancel");
        // 스택을 전부 비우면 Game 이 그것을 종료 신호로 받는다.
        ctx.scenes.Clear();
    }
}


void TitleScene::RenderUI(Renderer& renderer)
{
    const float cx = Config::kCanvasWidth  * 0.5f;

    // 폰트 셀이 8×14 이므로 scale 3 이면 24×42 픽셀 글자가 된다.
    renderer.DrawStringCentered("DARKBUBBLE", cx, 90.0f, DirectX::Colors::Gainsboro, 3);
    renderer.DrawStringCentered("- prototype -", cx, 140.0f, DirectX::Colors::DimGray, 1);

    // 30 틱(0.5초)마다 깜빡인다.
    const bool blinkOn = (m_blinkTick / 30) % 2 == 0;

    if (m_hasSave)
    {
        // ★ **고른 쪽만 밝게.** 둘 다 같은 밝기면 Enter 를 누르기 전에 무엇이 나갈지
        //   모른다. 고른 쪽의 꺾쇠가 깜빡인다 — 「PRESS ENTER」가 깜빡이던 것과
        //   같은 뜻(「이걸 누른다」)이다. 글자 자체를 깜빡이면 절반은 안 보인다.
        struct Option { const char* label; float x; bool selected; };
        const Option options[2] = {
            { "CONTINUE", cx - 104.0f, !m_newGame },
            { "NEW GAME", cx + 104.0f,  m_newGame },
        };

        for (const Option& o : options)
        {
            const std::string text = (o.selected && blinkOn)
                ? std::string("> ") + o.label + " <"
                : std::string(o.label);

            renderer.DrawStringCentered(text, o.x, 212.0f,
                                        o.selected ? DirectX::Colors::White
                                                   : DirectX::Colors::DimGray, 2);
        }

        renderer.DrawStringCentered("<- ->  SELECT      ENTER  START", cx, 252.0f,
                                    DirectX::Colors::DimGray, 1);
    }
    else if (blinkOn)
    {
        renderer.DrawStringCentered("PRESS ENTER", cx, 220.0f, DirectX::Colors::White, 2);
    }

    renderer.DrawStringCentered("ESC : QUIT", cx, 300.0f, DirectX::Colors::DimGray, 1);

    // ★ 깨진 저장을 치웠으면 **말해 준다.** 말없이 새 게임이 시작되면 「저장이
    //   사라졌다」로만 읽힌다 — 어디에 있는지 알려야 꺼내 볼 수 있다.
    if (m_setAside)
        renderer.DrawStringCentered("BROKEN SAVE MOVED TO saves/save.bad.json", cx, 326.0f,
                                    DirectX::Colors::Orange, 1);
}
