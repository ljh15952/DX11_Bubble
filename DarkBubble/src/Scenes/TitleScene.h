// ============================================================================
//  TitleScene.h
//    시작 화면. 폰트가 아직 없어서 도형으로 만들어 뒀다.
//    SpriteFont 를 넣으면 「DarkBubble」 / 「Press Enter」 문자로 바뀔 자리다.
// ============================================================================
#pragma once

#include "Core/Scene.h"

class TitleScene final : public Scene
{
public:
    const char* Name() const override { return "Title"; }

    bool Enter(SceneContext& ctx) override;
    void Update(SceneContext& ctx, bool consumeEdgeInput) override;
    void Render(Renderer& renderer) override;

private:
    int m_blinkTick = 0;
};
