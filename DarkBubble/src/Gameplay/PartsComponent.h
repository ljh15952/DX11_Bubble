// ============================================================================
//  PartsComponent.h
//    부위별 HP. 이 게임의 최대 차별점(기획서 §3.2)을 담는 컴포넌트.
//
//  ---- ★ 이 컴포넌트가 「합성」의 증거다 ----
//    2026-09-10 에 **플레이어에게도** 붙었다. 컴포넌트라서 한 줄이었다 —
//    상속 계층(Entity -> Character -> Player / Enemy)이었다면
//    Character 를 뜯어고쳐야 했을 변경이다.
//
//  ---- ★ 같은 컴포넌트, 다른 설정 ----
//    플레이어와 적이 **같은 부위 목록**을 쓰되 값이 다르다.
//      플레이어 : 머리 20 / 팔 24·24 / 몸통 60 / 다리 40
//      잡몹     : 머리 20 / 팔 없음  / 몸통 100 / 다리 40
//
//    「팔이 없는 적」은 maxHp 0 으로 표현한다 — 부위 목록을 두 벌 만들지 않는다.
//    보스에게 팔을 주고 싶으면 숫자만 넣으면 된다.
//
//  ---- 자세는 스스로 안다 ----
//    ★ 5-e-3 에서 자세를 **상태로** 판정했다가 상태를 하나 늘린 순간
//      판정 상자가 공중에 뜨는 버그를 냈다(handoff §8).
//      「몸의 성질로 판정한다」를 구조로 못 박아 둔다.
// ============================================================================
#pragma once

#include "Core/AABB.h"
#include "Core/Component.h"

// ============================================================================
//  부위 — 5개 (design.md §3.2.2)
//
//        ┌──────┐   머리      즉사
//        ├──────┤
//   왼팔─┤ 몸통 ├─오른팔      팔 : 그 손의 무기를 떨군다 + **몸통의 완충재**
//        ├──┬───┤            몸통 : 즉사
//        │  │   │   다리      다리 : 이동 불가
//        └──┴───┘
//
//    ★ 다리는 좌/우로 나누지 않는데 팔은 나눈다.
//      기준은 대칭이 아니라 **결과가 다른가**이다 —
//      팔은 각자 다른 무기를 들고 있어서 어느 쪽이 잘리느냐가 결과를 바꾼다.
//      다리는 한쪽만 부서져도 「못 걷는다」로 같아서 나눌 이유가 없다.
// ============================================================================
enum PartIndex
{
    Part_Head = 0,
    Part_LeftArm,
    Part_RightArm,
    Part_Torso,
    Part_Legs,
    Part_Count
};


// ★ HP·이름과 상자 좌표를 나눈다.
//   HP 는 자세와 무관하다 — 엎드려도 머리 HP 는 그대로다.
//   상자는 자세마다 다르다. 섞어 두면 「자세가 바뀌었는데 판정 상자가
//   공중에 떠 있는」 상태가 된다.
struct PartBox
{
    // 발밑 원점 기준 상대 좌표. 오른쪽을 보는 자세로 적는다.
    float left, top, right, bottom;
};


// ---- 소유자마다 다른 설정 ----
struct PartsProfile
{
    // 0 = **그 부위가 없다.** 판정에서도 표시에서도 빠진다.
    int maxHp[Part_Count]{};

    // 다리가 부서지면 엎드리는가.
    //   적은 기어다니고(true), 플레이어는 절뚝일 뿐 엎드리지 않는다(false).
    bool proneWhenLegsBroken = false;
};


class PartsComponent final : public Component
{
public:
    explicit PartsComponent(const PartsProfile& profile);

    const char* TypeName() const override { return "Parts"; }

    void Tick(SceneContext& ctx, bool consumeEdgeInput) override;
    void RenderDebug(Renderer& renderer) override;

    // ---- 조회 ----
    bool Exists(int part)   const { return m_profile.maxHp[part] > 0; }
    int  Hp(int part)       const { return m_hp[part]; }
    int  MaxHp(int part)    const { return m_profile.maxHp[part]; }
    const char* Name(int part) const;
    bool IsBroken(int part) const { return Exists(part) && m_hp[part] <= 0; }

    bool LegsBroken() const { return IsBroken(Part_Legs); }

    // 엎드려 있는가. 자세 판정의 유일한 출처다.
    bool Prone() const { return m_profile.proneWhenLegsBroken && LegsBroken(); }

    // 죽었는가. 머리 또는 몸통이 부서지면 즉사(design.md §3.2.2).
    bool Fatal() const { return IsBroken(Part_Head) || IsBroken(Part_Torso); }

    AABB Box(int part) const;

    // ---- ★ 판정 : 팔이 몸통을 가린다 ----
    //
    //   기본 규칙은 「겹침 면적이 가장 큰 부위」다. 그것만으로는
    //   중단 공격이 **항상 몸통**에 닿는다 — 몸통 상자가 가장 크기 때문이다.
    //
    //   그래서 규칙 하나를 얹는다:
    //     **몸통이 뽑혔는데 살아 있는 팔이 있으면, 공격자 쪽 팔이 대신 받는다.**
    //
    //   ★ 이것을 상자 좌표로 흉내 내려 하면 안 된다.
    //     팔을 몸통 앞에 겹쳐 두어도 좌우 어느 팔인지가 면적으로는 안 갈린다 —
    //     공격 상자가 넓어서 양팔에 똑같이 걸친다.
    //     「가린다」는 **관계**이므로 규칙으로 적는 것이 맞다.
    //     나중에 방패가 생기면 같은 자리에 한 줄 늘어난다.
    //
    //   fromX = 공격자의 x. 어느 팔이 방패가 되는지를 정한다.
    //   -1 = 안 맞음.
    int PickHit(const AABB& attackBox, float fromX) const;

    // ---- 변경 ----
    void Damage(int part, int amount);
    void Flash(int ticks) { m_flash = ticks; }
    int  FlashTicks() const { return m_flash; }
    void Reset();

    // ---- 표시 ----
    //   ★ RenderUI 를 쓰지 않고 **소유자가 불러 준다.**
    //     플레이어와 적이 둘 다 이 컴포넌트를 갖게 되면서, 컴포넌트가 스스로
    //     그리면 같은 자리에 두 번 그려진다.
    //     어디에 그릴지는 화면 배치를 아는 쪽(소유자)이 정하는 것이 맞다.
    void DrawBodyDiagram(Renderer& renderer, float x, float y) const;  // 몸 그림
    void DrawHpList(Renderer& renderer, float x, float y) const;       // 디버그 목록

private:
    PartsProfile m_profile;
    int m_hp[Part_Count]{};
    int m_flash = 0;
};
