// ============================================================================
//  InventoryScene.h
//    장비 화면 — 손 둘 + 가방 여섯 칸 (design.md §3.10.3).
//
//  ---- ★★ 게임을 **멈추지 않는다** ----
//    UpdatesBelow = true. 메뉴를 여는 동안에도 적은 움직인다.
//
//    멈추면 **장비의 기회비용이 메뉴 한 번으로 사라진다** — 대검으로 싸우다
//    방패가 필요하면 멈추고, 바꾸고, 재개하면 그만이다. §1.2 의 「무엇을
//    포기하는가」가 공짜가 된다. 다크소울이 메뉴를 안 멈추는 이유와 같다.
//
//    ★ 그래서 화면 전체를 어둡게 덮지 **않는다**(PauseScene 과 다르다).
//      뒤에서 적이 다가오는 것이 보여야 「지금 바꿀 여유가 있나」가 판단이 된다.
//
//  ---- 입력은 이 Scene 이 먹는다 ----
//    SceneManager 가 아래 Scene 에는 **빈 입력**을 넘긴다. 그래서 플레이어는
//    그 자리에 선 채이고 — 메뉴를 고르는 방향키가 캐릭터를 걷게 하지 않는다.
//
//  ---- 조작 : 「버튼은 손」이 메뉴 안에서도 같다 ----
//    ← →          칸 고르기
//    좌클릭 / 우클릭  가방의 것을 **그 손에** 든다
//    Enter         손의 것을 가방에 넣는다
//    X             버린다 (발밑에 떨군다)
//    Tab / Esc     닫기
// ============================================================================
#pragma once

#include "Core/Scene.h"
#include "Graphics/Assets.h"

#include <string>

class PlayerController;

class InventoryScene final : public Scene
{
public:
    // ★ 플레이어를 **참조로** 받는다. 이 화면이 하는 일이 전부 플레이어의
    //   손과 가방을 옮기는 것이라 다른 방법이 없다.
    //   ★ 수명은 안전하다 — 이 Scene 은 PlayScene **위에** 쌓이고, PlayScene
    //     이 먼저 사라지는 길(Replace)은 이 화면에서 열리지 않는다.
    explicit InventoryScene(PlayerController& player) : m_player(player) {}

    const char* Name() const override { return "Inventory"; }

    bool Enter(SceneContext& ctx) override;
    void Update(SceneContext& ctx, bool consumeEdgeInput) override;

    void Render(Renderer&) override {}
    void RenderUI(Renderer& renderer) override;

    bool DrawsBelow()   const override { return true; }   // 게임이 보이고
    bool UpdatesBelow() const override { return true; }   // ★ 게임이 계속 돈다

private:
    // 칸 번호 : 0 = 왼손, 1 = 오른손, 2.. = 가방.
    //   ★ 화면의 **왼쪽부터 오른쪽**과 같은 순서다. 왼손이 왼쪽에 있어야
    //     「좌클릭 = 왼손」이 눈으로도 맞는다.
    static constexpr int kHandSlots = 2;
    int SlotCount() const;

    void Act(SceneContext& ctx);             // 이번 틱의 버튼을 처리한다
    void Say(std::string text, bool good);   // 결과 한 줄

    PlayerController&     m_player;
    Assets::TextureHandle m_icons;

    int         m_cursor = kHandSlots;   // 가방 첫 칸에서 시작한다
    std::string m_message;
    bool        m_messageGood = true;
    int         m_messageTicks = 0;
};
