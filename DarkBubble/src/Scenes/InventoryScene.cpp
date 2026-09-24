#include "Scenes/InventoryScene.h"

#include "Audio/Audio.h"
#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Gameplay/PlayerController.h"
#include "Graphics/Renderer.h"
#include "Input/Input.h"

#include <DirectXColors.h>
#include <format>

namespace
{
    constexpr int   kIconSize  = 16;     // icons.png 한 칸
    constexpr float kIconScale = 2.0f;   // 메뉴에서는 두 배 — 바닥·HUD 보다 크게
    constexpr float kCell      = kIconSize * kIconScale;
    constexpr float kGap       = 6.0f;
    constexpr float kGroupGap  = 18.0f;  // 손과 가방 사이 — 둘이 다른 곳임을 보여 준다

    constexpr int   kIconTeeth   = 3;    // 빈손 = 이빨(무기가 없으면 문다)
    constexpr int   kMessageTicks = 90;  // 결과 한 줄이 떠 있는 시간 (1.5초)
}


bool InventoryScene::Enter(SceneContext& ctx)
{
    m_icons = ctx.assets.Texture(L"assets/textures/icons.png");
    if (!m_icons)
        return false;

    Log::Info("[inv] ← → 고르기   좌/우클릭 = 그 손에   Enter = 가방에   X = 버리기   Tab = 닫기");
    Log::Info("[inv] ★ 게임은 **안 멈춘다** — 적이 온다");
    return true;
}


int InventoryScene::SlotCount() const
{
    return kHandSlots + PlayerController::kBagSize;
}


void InventoryScene::Say(std::string text, bool good)
{
    m_message      = std::move(text);
    m_messageGood  = good;
    m_messageTicks = kMessageTicks;
}


void InventoryScene::Update(SceneContext& ctx, bool consumeEdgeInput)
{
    if (m_messageTicks > 0)
        --m_messageTicks;

    // ★★ 죽으면 **스스로 닫힌다.** 게임이 안 멈추니 메뉴를 연 채 죽을 수 있다.
    //   그대로 두면 사망 화면이 이 위에 쌓이고, 부활할 때 Resume 이
    //   PlayScene 이 아니라 **이 화면**에 불려 부활이 안 일어난다.
    //   사망 화면은 45틱 뒤에 뜨므로 그 전에 빠진다.
    //
    //   ★ 닫히면 PlayScene::Resume 이 불리지만 **부활하지 않는다** — 부활은
    //     「사망 화면을 띄웠는가」로 정한다. 처음엔 `IsDead()` 로 정하고 있어서
    //     여기서 닫히는 순간 **사망 화면을 건너뛰고 부활했다**(Resume 주석).
    if (m_player.IsDead())
    {
        ctx.scenes.Pop();
        return;
    }

    if (consumeEdgeInput)
        Act(ctx);
}


void InventoryScene::Act(SceneContext& ctx)
{
    const Input& in = ctx.input;

    if (in.InventoryPressed() || in.CancelPressed())
    {
        ctx.audio.Play("ui_cancel", 0.6f);
        ctx.scenes.Pop();
        return;
    }

    // ---- 칸 고르기 (끝에서 반대쪽으로 넘어간다) ----
    const int n = SlotCount();
    if (in.MenuLeftPressed())  { m_cursor = (m_cursor + n - 1) % n; ctx.audio.Play("step", 0.3f, 0.6f); }
    if (in.MenuRightPressed()) { m_cursor = (m_cursor + 1) % n;     ctx.audio.Play("step", 0.3f, 0.6f); }

    const bool onHand = (m_cursor < kHandSlots);
    const WeaponHand cursorHand = (m_cursor == 0) ? WeaponHand::Left : WeaponHand::Right;
    const int        bagSlot    = m_cursor - kHandSlots;

    // ---- 가방 → 손 : 누른 버튼이 **어느 손**인지 정한다 ----
    //   ★ 필드와 **같은 규칙**이다(§3.2.1.1). 줍기에서 미뤄 둔
    //     「어느 손에 들까」가 여기서 답을 얻는다.
    const WeaponHand pressedHand =
          in.LeftHandPressed()  ? WeaponHand::Left
        : in.RightHandPressed() ? WeaponHand::Right
        :                         WeaponHand::None;

    if (pressedHand != WeaponHand::None && !onHand)
    {
        const std::string id = m_player.BagItem(bagSlot);
        if (id.empty())
            return;

        if (m_player.EquipFromBag(bagSlot, pressedHand))
        {
            ctx.audio.Play("ui_confirm", 0.7f);
            Say(std::format("{} -> {} HAND", m_player.Weapon(pressedHand).name,
                            pressedHand == WeaponHand::Left ? "LEFT" : "RIGHT"), true);
        }
        else
        {
            // ★ 왜 안 되는지를 말한다. 조용히 안 되면 「버그인가?」가 된다.
            ctx.audio.Play("ui_cancel", 0.6f);
            Say("CAN'T - ARM LOST OR BAG FULL", false);
        }
        return;
    }

    // ---- 손 → 가방 ----
    if (in.ConfirmPressed() && onHand)
    {
        if (m_player.HandItem(cursorHand).empty())
            return;

        const std::string name = m_player.Weapon(cursorHand).name;
        if (m_player.StowHand(cursorHand))
        {
            ctx.audio.Play("ui_confirm", 0.6f);
            Say(name + " -> BAG", true);
        }
        else
        {
            ctx.audio.Play("ui_cancel", 0.6f);
            Say("BAG FULL", false);
        }
        return;
    }

    // ---- 버리기 = 발밑에 떨군다 ----
    //   ★ 없애지 않는다. 떨어진 물건 목록(8-b)에 오르므로 **다시 주울 수 있다.**
    //     실수로 눌러도 되돌릴 길이 있는 것이, 확인 창을 띄우는 것보다 낫다.
    if (in.DiscardPressed())
    {
        if (onHand)
        {
            if (m_player.HandItem(cursorHand).empty())
                return;
            Say(m_player.Weapon(cursorHand).name + " DROPPED", true);
            m_player.DiscardHand(cursorHand);
        }
        else
        {
            if (m_player.BagItem(bagSlot).empty())
                return;
            Say(m_player.BagItem(bagSlot) + " DROPPED", true);
            m_player.DiscardBag(bagSlot);
        }
        ctx.audio.Play("ui_cancel", 0.7f, -0.5f);
    }
}


void InventoryScene::RenderUI(Renderer& renderer)
{
    const float totalW = kHandSlots * kCell + (kHandSlots - 1) * kGap
                       + kGroupGap
                       + PlayerController::kBagSize * kCell
                       + (PlayerController::kBagSize - 1) * kGap;

    const float left = (Config::kCanvasWidth - totalW) * 0.5f;

    // ★ 화면 **위쪽**이다. 아래는 HUD(몸 · 스태미나 · 손 슬롯)가 있고,
    //   가운데는 플레이어와 적이 있다 — 게임이 안 멈추니 **다가오는 적이
    //   보여야** 「지금 바꿀 여유가 있나」가 판단이 된다.
    const float top  = 36.0f;

    // ---- 판 ----
    //   ★ 화면 전체를 덮지 않는다(PauseScene 과 다르다).
    const AABB panel{ left - 12.0f, top - 22.0f,
                      left + totalW + 12.0f, top + kCell + 40.0f };
    renderer.DrawFilledRect(panel, DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.78f));
    renderer.DrawRectOutline(panel, DirectX::XMVectorSet(0.55f, 0.55f, 0.62f, 1.0f), 1.0f);

    renderer.DrawString("L", left + kCell * 0.5f - 3.0f, top - 14.0f, DirectX::Colors::SlateGray, 1);
    renderer.DrawString("R", left + kCell * 1.5f + kGap - 3.0f, top - 14.0f, DirectX::Colors::SlateGray, 1);
    renderer.DrawString(std::format("BAG {}", PlayerController::kBagSize),
                        left + kHandSlots * (kCell + kGap) + kGroupGap, top - 14.0f,
                        DirectX::Colors::SlateGray, 1);

    // ---- 칸 ----
    for (int i = 0; i < SlotCount(); ++i)
    {
        const bool  hand = (i < kHandSlots);
        const float x = hand ? left + i * (kCell + kGap)
                             : left + kHandSlots * (kCell + kGap) + kGroupGap
                                    + (i - kHandSlots) * (kCell + kGap);

        const AABB box{ x, top, x + kCell, top + kCell };
        renderer.DrawFilledRect(box, DirectX::XMVectorSet(0.12f, 0.12f, 0.16f, 1.0f));

        // 무엇이 들어 있나
        int icon = -1;
        if (hand)
        {
            const WeaponHand h = (i == 0) ? WeaponHand::Left : WeaponHand::Right;
            // ★ 빈손은 **이빨**이다. HUD 와 같은 규칙 — 무기가 없으면 문다.
            icon = m_player.HandArmed(h) ? m_player.Weapon(h).icon : kIconTeeth;
        }
        else
        {
            const std::string& id = m_player.BagItem(i - kHandSlots);
            if (!id.empty())
            {
                auto it = m_player.Weapons().find(id);
                if (it != m_player.Weapons().end())
                    icon = it->second.icon;
            }
        }

        if (icon >= 0)
        {
            const RECT src{ icon * kIconSize, 0, (icon + 1) * kIconSize, kIconSize };
            renderer.Sprites().Draw(m_icons.Get(), DirectX::XMFLOAT2(x, top), &src,
                                    DirectX::Colors::White, 0.0f,
                                    DirectX::XMFLOAT2(0.0f, 0.0f), kIconScale);
        }

        // 커서
        if (i == m_cursor)
            renderer.DrawRectOutline({ x - 2.0f, top - 2.0f, x + kCell + 2.0f, top + kCell + 2.0f },
                                     DirectX::Colors::Gold, 2.0f);
    }

    // ---- 고른 칸의 설명 ----
    //   ★ 숫자를 보여 주되 **고르는 이유가 되는 것만.** 무기는 한 방·비용·양손,
    //     방패는 막는 띠·비율·단단함. 전부 늘어놓으면 아무것도 안 읽힌다.
    std::string info;
    const WeaponType* w = nullptr;
    if (m_cursor < kHandSlots)
    {
        const WeaponHand h = (m_cursor == 0) ? WeaponHand::Left : WeaponHand::Right;
        if (m_player.HandArmed(h)) w = &m_player.Weapon(h);
        else                       info = "EMPTY - BITES";
    }
    else
    {
        auto it = m_player.Weapons().find(m_player.BagItem(m_cursor - kHandSlots));
        if (it != m_player.Weapons().end()) w = &it->second;
        else                                info = "-";
    }

    if (w)
    {
        info = w->IsShield()
            ? std::format("{}  GUARD {:.0f}-{:.0f}  DEF {}%  HARD {}",
                          w->name, w->guardBottom, w->guardTop, w->defense, w->hardness)
            : std::format("{}{}  DMG {}  STAM {}",
                          w->name, w->twoHanded ? " [2H]" : "",
                          w->light.damage, w->light.staminaCost);
    }

    renderer.DrawString(info, left, top + kCell + 8.0f, DirectX::Colors::Gainsboro, 1);

    if (m_messageTicks > 0)
        renderer.DrawString(m_message, left, top + kCell + 20.0f,
                            m_messageGood ? DirectX::Colors::Gold : DirectX::Colors::Crimson, 1);
    else
        renderer.DrawString("LMB/RMB: HOLD  ENTER: STOW  X: DROP  TAB: CLOSE",
                            left, top + kCell + 20.0f, DirectX::Colors::DimGray, 1);
}
