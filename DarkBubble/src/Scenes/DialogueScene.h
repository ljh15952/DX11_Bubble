// ============================================================================
//  DialogueScene.h
//    NPC 와의 대화 — 화면 위쪽 상자 (10-a, design.md §6.5).
//
//  ---- ★★ 게임을 **멈추지 않는다** ----
//    장비 화면(InventoryScene)과 같은 장치다: UpdatesBelow = true 이고,
//    SceneManager 가 아래 Scene 에 **빈 입력**을 넘긴다. 그래서 플레이어는
//    그 자리에 선 채이고 적은 평소대로 움직인다 — 새로 만든 엔진 코드가 없다.
//    ★ 다크소울도 대화 중에 세상이 안 멈춘다. 적 옆에서 수다를 떨면 맞는다.
//
//  ---- 닫히는 때 ----
//    · 마지막 줄에서 E / Enter      · Esc (언제든)
//    · ★ **맞거나 죽으면.** 맞는 동안 대사를 읽게 두면, 강인도가 높아
//      휘청이지도 않는 몸이 **글을 읽으며 조용히 깎인다.**
//
//  ---- 왜 위쪽인가 ----
//    아래에는 몸 그림 · 스태미나 · 손 슬롯(HUD)이 있다. 게임이 안 멈추니
//    대화 중에도 **몸 상태가 보여야** 한다. 장비 화면이 위에 있는 것과 같은 이유다.
// ============================================================================
#pragma once

#include "Core/Scene.h"
#include "Gameplay/Dialogue.h"

#include <functional>

class PlayerController;
class PartsComponent;

class DialogueScene final : public Scene
{
public:
    // ★ 플레이어는 **보기만** 한다(const). 대화는 몸을 바꾸지 않는다 —
    //   「맞았는가 · 죽었는가」를 묻는 데만 쓴다.
    //   수명은 InventoryScene 과 같은 이유로 안전하다(PlayScene 위에 쌓인다).
    //
    //   ★ onComplete = **끝까지 들었을 때** 한 번 불린다(10-b). Esc 로 닫거나 맞아서
    //     끊기면 안 불린다 — 그러면 NPC 는 다음에 **같은 말을 다시** 한다.
    //   ★★ 이 Scene 은 플래그를 **모른다.** 「끝까지 들었다」만 알리고, 무엇을
    //     기억할지는 부른 쪽(PlayScene)이 정한다 — 사망 화면이 부활을 모르는 것과 같다.
    DialogueScene(const PlayerController& player, const PartsComponent& parts, Dialogue dialogue,
                  std::function<void()> onComplete = {})
        : m_player(player), m_parts(parts), m_runner(std::move(dialogue))
        , m_onComplete(std::move(onComplete)) {}

    const char* Name() const override { return "Dialogue"; }

    bool Enter(SceneContext& ctx) override;
    void Update(SceneContext& ctx, bool consumeEdgeInput) override;

    void Render(Renderer&) override {}
    void RenderUI(Renderer& renderer) override;

    bool DrawsBelow()   const override { return true; }   // 게임이 보이고
    bool UpdatesBelow() const override { return true; }   // ★ 게임이 계속 돈다

private:
    // 몸의 부위 HP 합. ★ 「맞았는가」를 **결과로** 묻는다 — 줄었으면 맞은 것이다.
    //   맞는 길(적 · 나중의 함정 · 투사체)을 하나하나 알 필요가 없다.
    int BodyHp() const;

    // 닫는다 — 한 번만 요청한다(한 프레임에 틱이 여러 번 돌 수 있다).
    void Close(SceneContext& ctx);

    const PlayerController& m_player;
    const PartsComponent&   m_parts;
    DialogueRunner          m_runner;
    std::function<void()>   m_onComplete;

    int  m_hpAtStart = 0;
    int  m_blinkTick = 0;
    bool m_closing   = false;
};
