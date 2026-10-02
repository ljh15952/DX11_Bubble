// ============================================================================
//  TitleScene.h
//    시작 화면. 폰트가 아직 없어서 도형으로 만들어 뒀다.
//    SpriteFont 를 넣으면 「DarkBubble」 / 「Press Enter」 문자로 바뀔 자리다.
//
//  ---- ★ 저장이 있으면 고른다 (9) ----
//    CONTINUE / NEW GAME. 고른 쪽의 **저장 하나**를 PlayScene 에 넘긴다 —
//    새 게임은 「기본 저장(SaveData::NewGame)」일 뿐이라 PlayScene 은 둘을
//    구분하지 않는다.
// ============================================================================
#pragma once

#include "Core/Scene.h"
#include "Gameplay/SaveData.h"

class TitleScene final : public Scene
{
public:
    const char* Name() const override { return "Title"; }

    bool Enter(SceneContext& ctx) override;
    void Update(SceneContext& ctx, bool consumeEdgeInput) override;

    // 메뉴 화면이라 월드에 그릴 것이 없다. UI 레이어만 쓴다.
    void Render(Renderer&) override {}
    void RenderUI(Renderer& renderer) override;

private:
    int m_blinkTick = 0;

    // ---- ★ 저장 (9) ----
    //   ★ 여기서 **미리 읽는다.** 그래야 「이어할 것이 있는가」를 화면에 보여
    //     줄 수 있고, 고르면 읽어 둔 그대로 넘긴다.
    SaveData m_save;
    bool     m_hasSave  = false;
    bool     m_newGame  = false;   // 고른 쪽. ★ 기본은 CONTINUE — 대개 이어서 한다
    bool     m_setAside = false;   // 깨진 저장을 옆으로 치웠다 — 화면에 한 줄 알린다
};
