#include "Gameplay/PartsComponent.h"

#include "Core/GameObject.h"
#include "Graphics/Renderer.h"

#include <DirectXColors.h>
#include <algorithm>
#include <format>

namespace
{
    constexpr const char* kPartName[Part_Count] =
        { "HEAD", "L.ARM", "R.ARM", "TORSO", "LEGS" };

    // ---- 서 있는 자세의 상자 ----
    //   스프라이트(발끝 y=62) 기준을 발밑 원점으로 옮겨 적는다.
    //
    //   ★ 팔은 몸통 좌우에 **겹쳐서** 둔다. 실제로 몸 앞에 있기 때문이다.
    //     다만 「어느 팔이 막는가」는 면적으로 안 갈리므로 PickHit 의 규칙이 정한다.
    constexpr PartBox kBoxStand[Part_Count] = {
        { -10.0f, -55.0f,  10.0f, -37.0f },   // HEAD
        { -14.0f, -36.0f,  -3.0f, -20.0f },   // L.ARM  (오른쪽을 보는 자세에서 뒤쪽)
        {   3.0f, -36.0f,  14.0f, -20.0f },   // R.ARM  (앞쪽)
        { -11.0f, -37.0f,  11.0f, -18.0f },   // TORSO
        { -10.0f, -18.0f,  10.0f,   0.0f },   // LEGS
    };

    // ---- 엎드린 자세의 상자 ----
    //   ★ 좌우 비대칭이다. facing 이 -1 이면 상자도 뒤집힌다.
    constexpr PartBox kBoxCrawl[Part_Count] = {
        {   4.0f, -27.0f,  20.0f, -11.0f },   // HEAD  — 앞으로 나온다
        {  -6.0f, -14.0f,   2.0f,  -6.0f },   // L.ARM
        {   0.0f, -14.0f,   8.0f,  -6.0f },   // R.ARM
        { -10.0f, -16.0f,  12.0f,  -3.0f },   // TORSO — 낮게 엎드린다
        { -10.0f, -14.0f,   0.0f,  -3.0f },   // LEGS  — 이미 부서져 있어 안 쓰인다
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

    // ---- 몸 그림 ----
    //   ※ 폰트가 ASCII 전용이라 부위 이름을 글자로 못 쓴다 —
    //     그림으로 가는 것이 제약 회피가 아니라 오히려 맞는 선택이다.
    struct DiagramCell { int part; float dx, dy, w, h; };

    constexpr DiagramCell kDiagram[] = {
        { Part_Head,      8.0f,  0.0f,  8.0f,  7.0f },
        { Part_LeftArm,   0.0f,  9.0f,  6.0f, 12.0f },
        { Part_Torso,     7.0f,  9.0f, 10.0f, 12.0f },
        { Part_RightArm, 18.0f,  9.0f,  6.0f, 12.0f },
        { Part_Legs,      7.0f, 23.0f, 10.0f, 11.0f },
    };

    DirectX::XMVECTOR HealthColor(float ratio)
    {
        // 초록 -> 노랑 -> 빨강. 「얼마나 남았나」가 색 하나로 읽혀야 한다.
        if (ratio > 0.6f) return DirectX::XMVectorSet(0.35f, 0.78f, 0.42f, 1.0f);
        if (ratio > 0.3f) return DirectX::XMVectorSet(0.88f, 0.76f, 0.26f, 1.0f);
        return DirectX::XMVectorSet(0.88f, 0.24f, 0.22f, 1.0f);
    }
}


PartsComponent::PartsComponent(const PartsProfile& profile)
    : m_profile(profile)
{
    Reset();
}


const char* PartsComponent::Name(int part) const { return kPartName[part]; }


void PartsComponent::Reset()
{
    for (int i = 0; i < Part_Count; ++i)
        m_hp[i] = m_profile.maxHp[i];
    m_flash = 0;
}


void PartsComponent::Tick(SceneContext&, bool)
{
    if (m_flash > 0)
        --m_flash;
}


void PartsComponent::Damage(int part, int amount)
{
    if (Exists(part))
        m_hp[part] -= amount;
}


AABB PartsComponent::Box(int part) const
{
    const Transform& tr = Owner().transform;

    // ★ 자세를 상태가 아니라 **몸**으로 판정한다.
    const PartBox& b = Prone() ? kBoxCrawl[part] : kBoxStand[part];

    // ★ 좌우 비대칭 자세를 위해 facing 에 따라 x 를 뒤집는다.
    //   [left, right] 를 0 기준으로 뒤집으면 [-right, -left] 가 된다.
    float left  = b.left;
    float right = b.right;
    if (tr.facing < 0)
    {
        left  = -b.right;
        right = -b.left;
    }

    return { tr.x + left, tr.y + b.top, tr.x + right, tr.y + b.bottom };
}


int PartsComponent::PickHit(const AABB& attackBox, float fromX) const
{
    // ---- ① 겹침 면적이 가장 큰 부위 ----
    //   ★ 「겹친 부위 전부」로 하면 한 번 휘두를 때 온몸이 깎여 부위 파괴가
    //     무의미해진다. 「가장 위 부위」로 하면 항상 머리만 맞아 다리를 못 벤다.
    //     면적으로 고르면 「낮게 휘두르면 다리」가 되어 **조준하게 된다.**
    int   best     = -1;
    float bestArea = 0.0f;

    for (int i = 0; i < Part_Count; ++i)
    {
        // 없는 부위와 이미 부서진 부위는 건너뛴다.
        if (!Exists(i) || IsBroken(i))
            continue;

        const float area = OverlapArea(attackBox, Box(i));
        if (area > bestArea)
        {
            bestArea = area;
            best     = i;
        }
    }

    if (best != Part_Torso)
        return best;

    // ---- ② ★ 팔이 몸통을 가린다 ----
    //   중단 공격은 팔이 먼저 받는다. 팔이 잘려야 몸통에 닿는다.
    //   부위 파괴가 「어디를 노릴까」에서 **「무엇을 버릴까」**로 바뀌는 지점이다.
    const Transform& tr = Owner().transform;

    // 공격자가 **바라보는 쪽**에 있는가. 그쪽 팔이 방패가 된다.
    //   ★ 그래서 「몸을 돌려 성한 팔로 막는다」가 성립한다 —
    //     대신 등을 보이는 대가를 치른다. 규칙 한 줄이 전술을 만든다.
    const bool inFront  = ((fromX - tr.x) * static_cast<float>(tr.facing)) > 0.0f;
    const int  frontArm = (tr.facing > 0) ? Part_RightArm : Part_LeftArm;
    const int  backArm  = (tr.facing > 0) ? Part_LeftArm  : Part_RightArm;

    const int first  = inFront ? frontArm : backArm;
    const int second = inFront ? backArm  : frontArm;

    if (Exists(first)  && !IsBroken(first)  && OverlapArea(attackBox, Box(first))  > 0.0f)
        return first;
    if (Exists(second) && !IsBroken(second) && OverlapArea(attackBox, Box(second)) > 0.0f)
        return second;

    return Part_Torso;   // 팔이 둘 다 잘렸다(또는 애초에 없다)
}


void PartsComponent::RenderDebug(Renderer& renderer)
{
    if (!renderer.DebugDraw())
        return;

    for (int i = 0; i < Part_Count; ++i)
    {
        if (!Exists(i))
            continue;

        // ★ 부서진 부위는 **테두리만** 그린다.
        //   반투명 판으로 덮으면 겹친 상자(엎드린 자세의 LEGS/TORSO)를 가려
        //   상자가 하나로 보인다.
        if (IsBroken(i))
        {
            renderer.DrawRectOutline(Box(i), DirectX::Colors::DimGray, 1.0f);
            continue;
        }

        // 팔은 몸통과 겹쳐 있으므로 색을 달리해 구분한다.
        const bool arm = (i == Part_LeftArm || i == Part_RightArm);
        renderer.DrawRectOutline(Box(i),
            arm ? DirectX::Colors::MediumPurple : DirectX::Colors::Gold, 1.0f);
    }
}


// ----------------------------------------------------------------------------
//  DrawBodyDiagram — ★ HP 바를 대신하는 몸 그림 (design.md §3.2.3)
//
//    부위별 HP 가 생기면 「전체 HP」라는 숫자가 의미를 잃는다.
//    몸통 20% 와 팔 20% 는 완전히 다른 상황인데 한 줄로는 구분되지 않는다.
//    그림이 곧 상태 표시다.
// ----------------------------------------------------------------------------
void PartsComponent::DrawBodyDiagram(Renderer& renderer, float x, float y) const
{
    // 배경 판 — 밝은 배경 위에서도 읽히게
    renderer.DrawFilledRect(AABB::FromXYWH(x - 3.0f, y - 3.0f, 30.0f, 40.0f),
                            DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 0.55f));

    for (const DiagramCell& c : kDiagram)
    {
        if (!Exists(c.part))
            continue;

        const AABB box = AABB::FromXYWH(x + c.dx, y + c.dy, c.w, c.h);

        if (IsBroken(c.part))
        {
            // ★ 잘린 부위는 사라진다 — 테두리만 남겨 「여기 있었다」를 보여 준다.
            renderer.DrawRectOutline(box,
                DirectX::XMVectorSet(0.30f, 0.28f, 0.30f, 1.0f), 1.0f);
            continue;
        }

        const float ratio = static_cast<float>(m_hp[c.part])
                          / static_cast<float>(m_profile.maxHp[c.part]);
        renderer.DrawFilledRect(box, HealthColor(std::clamp(ratio, 0.0f, 1.0f)));
    }
}


void PartsComponent::DrawHpList(Renderer& renderer, float x, float y) const
{
    int row = 0;
    for (int i = 0; i < Part_Count; ++i)
    {
        if (!Exists(i))
            continue;

        const bool broken = IsBroken(i);
        renderer.DrawString(
            std::format("{:<6}{:>4}/{:<4}{}", Name(i),
                        std::max(0, Hp(i)), MaxHp(i), broken ? " BROKEN" : ""),
            x, y + row * 14.0f,
            broken ? DirectX::Colors::DimGray : DirectX::Colors::Gold, 1);
        ++row;
    }
}
