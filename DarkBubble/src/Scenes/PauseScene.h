// ============================================================================
//  PauseScene.h
//    일시정지 오버레이.
//
//    ★ 이 Scene 이 Scene 설계의 핵심을 증명한다.
//      DrawsBelow   = true   -> 아래 PlayScene 이 계속 그려진다 (게임 화면이 보인다)
//      UpdatesBelow = false  -> 아래 PlayScene 은 갱신되지 않는다 (게임이 멈춘다)
//
//    Cocos2d 는 pushScene 하면 아래가 렌더도 멈추므로,
//    이런 오버레이는 Scene 이 아니라 Layer 로 우회해서 만들어야 한다.
//    우리는 Scene 이 스스로 선언하므로 그럴 필요가 없다.
// ============================================================================
#pragma once

#include "Core/Scene.h"

class PauseScene final : public Scene
{
public:
    const char* Name() const override { return "Pause"; }

    bool Enter(SceneContext& ctx) override;
    void Update(SceneContext& ctx, bool consumeEdgeInput) override;
    void Render(Renderer& renderer) override;

    bool DrawsBelow()   const override { return true;  }   // 게임 화면은 보이게
    bool UpdatesBelow() const override { return false; }   // 하지만 멈춰 있게

private:
    int m_blinkTick = 0;   // 「재개」 표시를 깜빡이게 하는 카운터
};
