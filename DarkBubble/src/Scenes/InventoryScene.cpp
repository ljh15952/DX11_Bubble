#include "Scenes/InventoryScene.h"

#include "Audio/Audio.h"
#include "Core/Constants.h"
#include "Core/Log.h"
#include "Core/SceneManager.h"
#include "Gameplay/ItemType.h"
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
    constexpr float kGap       = 5.0f;
    constexpr float kGroupGap  = 14.0f;  // 무리 사이 — 손 / 몸 / 가방이 다른 곳임을 보여 준다

    constexpr int kHands = 2;
    constexpr int kBag   = PlayerController::kBagSize;

    constexpr int kIconTeeth    = 3;     // 빈손 = 이빨(무기가 없으면 문다)
    constexpr int kMessageTicks = 90;    // 결과 한 줄이 떠 있는 시간 (1.5초)

    // 칸 번호 → 손. 0 = 왼손(화면 왼쪽), 1 = 오른손.
    WeaponHand HandOf(int index) { return index == 0 ? WeaponHand::Left : WeaponHand::Right; }
}


bool InventoryScene::Enter(SceneContext& ctx)
{
    m_icons = ctx.assets.Texture(L"assets/textures/icons.png");
    if (!m_icons)
        return false;

    // ★ 가방 첫 칸에서 시작한다. 여는 이유는 대개 「무엇을 꺼내 쓸까」다.
    m_cursor = kHands + kArmorSlots;

    Log::Info("[inv] ← → 고르기   좌/우클릭 = 그 손에   Enter = 입기/넣기   X = 버리기   Tab = 닫기");
    Log::Info("[inv] ★ 게임은 **안 멈춘다** — 적이 온다");
    return true;
}


int InventoryScene::SlotCount() const
{
    return kHands + kArmorSlots + kBag;
}


InventoryScene::Slot InventoryScene::SlotAt(int cursor) const
{
    if (cursor < kHands)               return { SlotKind::Hand,  cursor };
    if (cursor < kHands + kArmorSlots) return { SlotKind::Armor, cursor - kHands };
    return { SlotKind::Bag, cursor - kHands - kArmorSlots };
}


const std::string& InventoryScene::IdAt(const Slot& s) const
{
    switch (s.kind)
    {
    case SlotKind::Hand:  return m_player.HandItem(HandOf(s.index));
    case SlotKind::Armor: return m_player.ArmorItem(static_cast<ArmorSlot>(s.index));
    case SlotKind::Bag:   break;
    }
    return m_player.BagItem(s.index);
}


const ItemType* InventoryScene::ItemAt(const Slot& s) const
{
    const std::string& id = IdAt(s);
    if (id.empty())
        return nullptr;

    auto it = m_player.Items().find(id);
    return (it != m_player.Items().end()) ? &it->second : nullptr;
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

    const Slot      slot = SlotAt(m_cursor);
    const ItemType* item = ItemAt(slot);

    const WeaponHand pressedHand =
          in.LeftHandPressed()  ? WeaponHand::Left
        : in.RightHandPressed() ? WeaponHand::Right
        :                         WeaponHand::None;

    // ---- 좌/우클릭 : 가방의 것을 **그 손에** ----
    //   ★ 필드와 **같은 규칙**이다(§3.2.1.1). 줍기에서 미뤄 둔
    //     「어느 손에 들까」가 여기서 답을 얻는다.
    if (pressedHand != WeaponHand::None)
    {
        if (slot.kind != SlotKind::Bag || !item)
            return;

        if (item->IsArmor())
        {
            // ★ 방어구는 손에 드는 것이 아니다 — 왜 안 되는지와 **어떻게 하는지**를 같이.
            ctx.audio.Play("ui_cancel", 0.5f);
            Say("ARMOR - PRESS ENTER TO WEAR", false);
            return;
        }

        const std::string name = item->name;
        if (m_player.EquipFromBag(slot.index, pressedHand))
        {
            ctx.audio.Play("ui_confirm", 0.7f);
            Say(std::format("{} -> {} HAND", name,
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

    // ---- Enter : 칸 종류가 뜻을 정한다 ----
    if (in.ConfirmPressed() && item)
    {
        const std::string name = item->name;
        bool ok = false;

        switch (slot.kind)
        {
        case SlotKind::Bag:
            if (!item->IsArmor())
            {
                Say("WEAPON - LMB / RMB = WHICH HAND", false);
                ctx.audio.Play("ui_cancel", 0.5f);
                return;
            }
            ok = m_player.WearFromBag(slot.index);   // 입던 것은 가방으로(맞바꾸기)
            if (ok) Say(std::format("WORE {}  ->  {} {}  POISE {}", name,
                                    WeightClassName(m_player.Weight()),
                                    m_player.EquipWeight(), m_player.TotalPoise()), true);
            break;

        case SlotKind::Hand:
            ok = m_player.StowHand(HandOf(slot.index));
            if (ok) Say(name + " -> BAG", true);
            break;

        case SlotKind::Armor:
            ok = m_player.TakeOffArmor(static_cast<ArmorSlot>(slot.index));
            if (ok) Say(name + " -> BAG", true);
            break;
        }

        if (ok)
            ctx.audio.Play("ui_confirm", 0.6f);
        else
        {
            ctx.audio.Play("ui_cancel", 0.6f);
            Say("BAG FULL", false);
        }
        return;
    }

    // ---- X : 버리기 = 발밑에 떨군다 ----
    //   ★ 없애지 않는다. 떨어진 물건 목록(8-b)에 오르므로 **다시 주울 수 있다.**
    //     실수로 눌러도 되돌릴 길이 있는 것이, 확인 창을 띄우는 것보다 낫다.
    if (in.DiscardPressed() && item)
    {
        Say(item->name + " DROPPED", true);

        switch (slot.kind)
        {
        case SlotKind::Hand:  m_player.DiscardHand(HandOf(slot.index));                     break;
        case SlotKind::Armor: m_player.DiscardArmor(static_cast<ArmorSlot>(slot.index));   break;
        case SlotKind::Bag:   m_player.DiscardBag(slot.index);                              break;
        }
        ctx.audio.Play("ui_cancel", 0.7f, -0.5f);
    }
}


void InventoryScene::RenderUI(Renderer& renderer)
{
    auto groupW = [](int cells) { return cells * kCell + (cells - 1) * kGap; };

    const float totalW = groupW(kHands) + kGroupGap + groupW(kArmorSlots)
                       + kGroupGap + groupW(kBag);

    const float left = (Config::kCanvasWidth - totalW) * 0.5f;

    // ★ 화면 **위쪽**이다. 아래는 HUD(몸 · 스태미나 · 손 슬롯)가 있고,
    //   가운데는 플레이어와 적이 있다 — 게임이 안 멈추니 **다가오는 적이
    //   보여야** 「지금 바꿀 여유가 있나」가 판단이 된다.
    const float top = 36.0f;

    const float armorX = left + groupW(kHands) + kGroupGap;
    const float bagX   = armorX + groupW(kArmorSlots) + kGroupGap;

    // ---- 판 ----
    //   ★ 화면 전체를 덮지 않는다(PauseScene 과 다르다).
    const AABB panel{ left - 10.0f, top - 22.0f,
                      left + totalW + 10.0f, top + kCell + 40.0f };
    renderer.DrawFilledRect(panel, DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.78f));
    renderer.DrawRectOutline(panel, DirectX::XMVectorSet(0.55f, 0.55f, 0.62f, 1.0f), 1.0f);

    // ---- 무리 이름 ----
    //   ★ 몸 칸 위에는 **지금의 합계**를 적는다. 한 조각을 바꿀 때마다
    //     등급이 넘어가는지가 바로 보여야 「이 투구를 쓸까」가 판단이 된다.
    renderer.DrawString("L", left + kCell * 0.5f - 3.0f, top - 14.0f, DirectX::Colors::SlateGray, 1);
    renderer.DrawString("R", left + kCell * 1.5f + kGap - 3.0f, top - 14.0f, DirectX::Colors::SlateGray, 1);
    renderer.DrawString(std::format("{} {}  POISE {}", WeightClassName(m_player.Weight()),
                                    m_player.EquipWeight(), m_player.TotalPoise()),
                        armorX, top - 14.0f, DirectX::Colors::Gainsboro, 1);
    renderer.DrawString(std::format("BAG {}", kBag), bagX, top - 14.0f, DirectX::Colors::SlateGray, 1);

    // ---- 칸 ----
    for (int i = 0; i < SlotCount(); ++i)
    {
        const Slot s = SlotAt(i);

        float x = 0.0f;
        switch (s.kind)
        {
        case SlotKind::Hand:  x = left   + s.index * (kCell + kGap); break;
        case SlotKind::Armor: x = armorX + s.index * (kCell + kGap); break;
        case SlotKind::Bag:   x = bagX   + s.index * (kCell + kGap); break;
        }

        const AABB box{ x, top, x + kCell, top + kCell };
        renderer.DrawFilledRect(box, DirectX::XMVectorSet(0.12f, 0.12f, 0.16f, 1.0f));

        // 무엇이 들어 있나
        int icon = -1;
        if (const ItemType* it = ItemAt(s))
            icon = it->icon;

        // ★ 손 칸은 「들었는가」가 아니라 「쓸 수 있는가」다 — 빈손이거나
        //   팔이 잘렸으면 **이빨**. HUD 와 같은 규칙이다.
        if (s.kind == SlotKind::Hand && !m_player.HandArmed(HandOf(s.index)))
            icon = kIconTeeth;

        if (icon >= 0)
        {
            const RECT src{ icon * kIconSize, 0, (icon + 1) * kIconSize, kIconSize };
            renderer.Sprites().Draw(m_icons.Get(), DirectX::XMFLOAT2(x, top), &src,
                                    DirectX::Colors::White, 0.0f,
                                    DirectX::XMFLOAT2(0.0f, 0.0f), kIconScale);
        }
        else if (s.kind == SlotKind::Armor)
        {
            // 빈 방어구 칸에는 **부위 이름**을 흐리게 — 어디에 무엇이 들어가는지.
            renderer.DrawStringCentered(ArmorSlotName(static_cast<ArmorSlot>(s.index)),
                                        x + kCell * 0.5f, top + kCell * 0.5f - 3.0f,
                                        DirectX::Colors::DimGray, 1);
        }

        if (i == m_cursor)
            renderer.DrawRectOutline({ x - 2.0f, top - 2.0f, x + kCell + 2.0f, top + kCell + 2.0f },
                                     DirectX::Colors::Gold, 2.0f);
    }

    // ---- 고른 칸의 설명 ----
    //   ★ 숫자를 보여 주되 **고르는 이유가 되는 것만.** 전부 늘어놓으면
    //     아무것도 안 읽힌다.
    const Slot      cur  = SlotAt(m_cursor);
    const ItemType* item = ItemAt(cur);

    std::string info;
    if (!item)
    {
        info = (cur.kind == SlotKind::Hand) ? "EMPTY - BITES" : "-";
    }
    else if (item->IsArmor())
    {
        info = std::format("{}  {}  POISE {}  W {}", item->name,
                           ArmorSlotName(item->armorSlot), item->poise, item->weight);
    }
    else if (item->IsShield())
    {
        info = std::format("{}  GUARD {:.0f}-{:.0f}  DEF {}%  HARD {}  W {}",
                           item->name, item->guardBottom, item->guardTop,
                           item->defense, item->hardness, item->weight);
    }
    else
    {
        info = std::format("{}{}  DMG {}  STAM {}  W {}",
                           item->name, item->twoHanded ? " [2H]" : "",
                           item->light.damage, item->light.staminaCost, item->weight);
    }

    // ★ 특수 효과도 적는다. **숨은 효과는 없는 효과와 같다.**
    if (item)
        for (const ItemEffect& e : item->effects)
            if (e.kind == EffectKind::BonusDamage)
                info += std::format("  +{}% vs {}", e.value, e.vs.empty() ? "ALL" : e.vs);

    renderer.DrawString(info, left, top + kCell + 8.0f, DirectX::Colors::Gainsboro, 1);

    if (m_messageTicks > 0)
        renderer.DrawString(m_message, left, top + kCell + 20.0f,
                            m_messageGood ? DirectX::Colors::Gold : DirectX::Colors::Crimson, 1);
    else
        renderer.DrawString("LMB/RMB: HOLD  ENTER: WEAR/STOW  X: DROP  TAB: CLOSE",
                            left, top + kCell + 20.0f, DirectX::Colors::DimGray, 1);
}
