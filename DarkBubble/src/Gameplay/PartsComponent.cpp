#include "Gameplay/PartsComponent.h"

#include "Core/GameObject.h"
#include "Graphics/Renderer.h"

#include <DirectXColors.h>
#include <algorithm>

namespace
{
    // ---- 부위 이름과 HP : 자세와 무관 ----
    constexpr const char* kPartName[Part_Count]  = { "HEAD", "TORSO", "LEGS" };
    constexpr int         kPartMaxHp[Part_Count] = {     20,     100,     40 };

    // ---- 서 있는 자세의 상자 ----
    //   스프라이트(발끝 y=62) 기준.
    //     머리 y  9..27  ->  발밑 기준 -55..-37
    //     몸통 y 27..46  ->              -37..-18
    //     다리 y 46..62  ->              -18..  0
    //   좌우 대칭이라 뒤집어도 같다.
    constexpr PartBox kBoxStand[Part_Count] = {
        { -10.0f, -55.0f,  10.0f, -37.0f },   // HEAD
        { -11.0f, -37.0f,  11.0f, -18.0f },   // TORSO
        { -10.0f, -18.0f,  10.0f,   0.0f },   // LEGS
    };

    // ---- 엎드린 자세의 상자 ----
    //   ★ 좌우 비대칭이다. facing 이 -1 이면 상자도 뒤집어야 한다.
    constexpr PartBox kBoxCrawl[Part_Count] = {
        {   4.0f, -27.0f,  20.0f, -11.0f },   // HEAD  — 앞으로 나온다
        { -10.0f, -16.0f,  12.0f,  -3.0f },   // TORSO — 낮게 엎드린다
        { -10.0f, -14.0f,   0.0f,  -3.0f },   // LEGS  — 이미 부서져 있어 실제로는 안 쓰인다
    };

    // 두 사각형이 겹치는 면적. 안 겹치면 0.
    float OverlapArea(const AABB& a, const AABB& b)
    {
        const float w = std::min(a.right,  b.right)  - std::max(a.left, b.left);
        const float h = std::min(a.bottom, b.bottom) - std::max(a.top,  b.top);
        if (w <= 0.0f || h <= 0.0f)
            return 0.0f;
        return w * h;
    }
}


int PartsComponent::MaxHp(int part) const     { return kPartMaxHp[part]; }
const char* PartsComponent::Name(int part) const { return kPartName[part]; }


void PartsComponent::Reset()
{
    for (int i = 0; i < Part_Count; ++i)
        m_hp[i] = kPartMaxHp[i];
    m_flash = 0;
}


void PartsComponent::Tick(SceneContext&)
{
    if (m_flash > 0)
        --m_flash;
}


void PartsComponent::Damage(int part, int amount)
{
    m_hp[part] -= amount;
}


AABB PartsComponent::Box(int part) const
{
    const Transform& tr = Owner().transform;

    // ★ 자세를 **상태가 아니라 몸으로** 판정한다.
    //   5-e-2 에서는 `state == Crawl` 로 골랐고 그때는 맞는 코드였다.
    //   5-e-3 에서 Attack 이 생기자 엎드린 적이 Attack 상태가 될 수 있게 되었고,
    //   그 순간 상자가 서 있는 위치로 튀어 공중에 떴다.
    //   자세는 행동이 아니라 몸의 상태다 — 다리가 부서졌으면 무엇을 하든 엎드려 있다.
    //   컴포넌트로 옮기면서 이 판단이 아예 **바깥에서 보이지 않게** 되었다.
    const PartBox& b = LegsBroken() ? kBoxCrawl[part] : kBoxStand[part];

    // ★ 좌우 비대칭 자세를 위해 facing 에 따라 x 를 뒤집는다.
    //   [left, right] 를 0 기준으로 뒤집으면 [-right, -left] 가 된다.
    //   서 있는 자세는 대칭이라 이 연산이 아무 영향을 주지 않는다 — 한 갈래로 처리된다.
    float left  = b.left;
    float right = b.right;
    if (tr.facing < 0)
    {
        left  = -b.right;
        right = -b.left;
    }

    return { tr.x + left, tr.y + b.top, tr.x + right, tr.y + b.bottom };
}


int PartsComponent::PickHit(const AABB& attackBox) const
{
    int   best     = -1;
    float bestArea = 0.0f;

    for (int i = 0; i < Part_Count; ++i)
    {
        // ★ 이미 부서진 부위는 건너뛴다.
        //   다리를 부순 뒤에는 같은 높이로 휘둘러도 다른 부위에 닿는다.
        if (IsBroken(i))
            continue;

        const float area = OverlapArea(attackBox, Box(i));
        if (area > bestArea)
        {
            bestArea = area;
            best     = i;
        }
    }
    return best;
}


void PartsComponent::RenderDebug(Renderer& renderer)
{
    // ★ 자기 디버그 표시를 스스로 그린다.
    //   전에는 PlayScene::DrawEnemyDebug 가 적의 내부(hp 배열·상자)를 알아야 했다.
    //   컴포넌트가 자기 것을 그리면 그 지식이 밖으로 새지 않는다(캡슐화).
    if (!renderer.DebugDraw())
        return;

    for (int i = 0; i < Part_Count; ++i)
    {
        // ★ 부서진 부위는 **테두리만** 그린다.
        //
        //   전에는 반투명 어두운 판으로 덮었는데, 엎드린 자세에서
        //   LEGS 상자가 TORSO 안에 완전히 들어가 있어서(둘 다 y -14..-3)
        //   그 판이 몸통을 통째로 덮어 **상자가 하나로 보였다.**
        //   부서진 부위는 어차피 못 맞추므로 존재만 알려 주면 된다.
        if (IsBroken(i))
        {
            renderer.DrawRectOutline(Box(i), DirectX::Colors::DimGray, 1.0f);
            continue;
        }

        renderer.DrawRectOutline(Box(i), DirectX::Colors::Gold, 1.0f);
    }
}
