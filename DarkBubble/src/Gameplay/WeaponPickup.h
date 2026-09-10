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
// ============================================================================
#pragma once

#include "Core/AABB.h"
#include "Core/Component.h"

class WeaponPickup final : public Component
{
public:
    const char* TypeName() const override { return "WeaponPickup"; }

    void Render(Renderer& renderer) override;
    void RenderDebug(Renderer& renderer) override;

    // 월드에 존재하는가. 주우면 사라지고 떨구면 나타난다.
    bool Active() const { return m_active; }

    // 떨군다. 위치는 떨어뜨린 쪽이 정한다.
    void DropAt(float x, float y);
    void PickedUp() { m_active = false; }

    // 주울 수 있는 범위. 발밑 기준으로 넉넉히 잡는다 —
    // 픽셀 단위로 정확히 밟게 하면 조작이 답답해진다.
    AABB PickupArea() const;

private:
    bool m_active   = false;
    int  m_bobTicks = 0;   // 눈에 띄게 하려는 위아래 흔들림용
};
