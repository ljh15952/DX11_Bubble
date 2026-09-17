// ============================================================================
//  WeaponPickup.h
//    땅에 떨어진 무기. 주울 수 있다.
//
//  ---- ★ 이것이 「남는다」 칸의 첫 실체다 ----
//    design.md §3.6.1 은 죽었을 때 무엇이 되돌아가고 무엇이 남는지를 표로 적었다.
//    지금까지 「남는다」 칸에는 장착 방어구 하나뿐이었다.
//
//      되돌아간다 : 플레이어 부위 · 적
//      남는다     : 장착 방어구  +  ★ 땅에 떨어진 무기
//
//    죽어도 무기가 그 자리에 남고 부활한 뒤 다시 주우러 간다 —
//    다크소울의 혈흔과 같은 구조다. Respawn 이 이것을 건드리지 않는 것만으로
//    성립한다. **아무것도 안 하는 것이 기능이 되는** 드문 경우다.
//
//  ---- 왜 GameObject 인가 ----
//    위치가 있고 그려지는 것이므로 Transform 이 필요하다.
//    플레이어·적과 같은 그릇에 담기면 「월드에 있는 것」이 한 종류가 된다 —
//    나중에 상자·함정·투사체가 생겨도 같은 방식으로 붙는다.
//
//    ★★ 그리고 그 값이 여기서 돌아왔다: **떨어지는 것**도 공짜다.
//    공중에서 팔이 잘리면 무기가 **그 자리에 떠 있었다.** 지상에서만 떨궈
//    보는 동안에는 안 보이던 버그다 — 「하나뿐이라 괜찮았던 것」의 친척으로,
//    **한 가지 상황에서만 써 본 것은 확인된 것이 아니다.**
//
//    고치는 방법은 **BodyComponent 를 붙이는 것 하나**였다. 중력도 지형
//    충돌도 발판 착지도 이미 거기 있다 — 「떨어지는 물건」을 위한 코드를
//    새로 쓰면 플레이어와 두 벌이 되고, 발판이 늘 때 한쪽만 고치게 된다.
// ============================================================================
#pragma once

#include <string>

#include "Core/AABB.h"
#include "Core/BodyComponent.h"
#include "Core/Component.h"
#include "Graphics/Assets.h"

class WeaponPickup final : public Component
{
public:
    const char* TypeName() const override { return "WeaponPickup"; }

    void Start(SceneContext& ctx) override;
    void Render(Renderer& renderer) override;
    void RenderDebug(Renderer& renderer) override;

    // 월드에 존재하는가. 주우면 사라지고 떨구면 나타난다.
    bool Active() const { return m_active; }

    // ★ 무엇이 떨어져 있는지 그림으로 알려 준다(8-a).
    //   전에는 사각형 두 개로 그렸다. 무기가 하나뿐일 때는 「무기가 있다」만
    //   알리면 됐지만, 둘이 되는 순간 **어느 쪽이 떨어져 있는지**가 판단이 된다.
    //   ※ 손 슬롯 UI 와 **같은 시트**를 쓴다 — 「UI 의 그것」과 「바닥의 그것」이
    //     같은 물건으로 읽혀야 한다.
    void SetSheet(Assets::TextureHandle sheet) { m_sheet = std::move(sheet); }

    // 떨군다. 위치도 **무엇인지**도 떨어뜨린 쪽이 정한다.
    void DropAt(float x, float y, std::string weaponId, int icon);
    void PickedUp() { m_active = false; }

    // 여기 떨어져 있는 것이 무엇인가. 주운 쪽이 이 이름으로 장착한다.
    const std::string& WeaponId() const { return m_weaponId; }

    // 주울 수 있는 범위. 발밑 기준으로 넉넉히 잡는다 —
    // 픽셀 단위로 정확히 밟게 하면 조작이 답답해진다.
    AABB PickupArea() const;

private:
    bool m_active   = false;
    int  m_bobTicks = 0;   // 눈에 띄게 하려는 위아래 흔들림용

    BodyComponent* m_body = nullptr;        // 중력·지형. Start 에서 캐시한다

    Assets::TextureHandle m_sheet;          // icons.png
    std::string           m_weaponId = "dagger";
    int                   m_icon     = 1;
};
