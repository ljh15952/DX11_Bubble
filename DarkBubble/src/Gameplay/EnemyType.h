// ============================================================================
//  EnemyType.h
//    적 **한 종류**의 값 전부 + enemies.json 읽기.
//
//  ---- ★ 무브셋과 다른 점이 하나 있다 ----
//    무기는 플레이어 **하나**가 쓰지만, 적 데이터는 **여럿이 공유한다.**
//    잡몹 셋이 같은 파일을 세 번 읽을 수는 없다.
//
//    그래서 **카탈로그**(종류 이름 -> 값)를 Scene 이 하나 들고,
//    스폰마다 「어느 종류인가」로 찾아 쓴다.
//    `EnemyBrain` 은 그 값을 **참조로** 본다 — 복사하면 리로드가 안 먹는다.
//
//  ---- ★★ 주소가 안정해야 한다 ----
//    리로드는 카탈로그의 **값을 덮어쓸 뿐** 원소를 새로 만들지 않는다.
//    그래서 EnemyBrain 이 들고 있는 참조가 계속 유효하다 —
//    무기 카탈로그를 컨트롤러가 값으로 소유한 것과 같은 이유다.
//
//    ※ 그래서 카탈로그는 `std::map` 이다. `vector` 면 재배치할 때 주소가 바뀐다.
// ============================================================================
#pragma once

#include <map>
#include <string>

#include "Gameplay/AttackData.h"
#include "Gameplay/PartsComponent.h"

struct EnemyType
{
    // ---- 몸 ----
    PartsProfile parts{};
    int          poise = 15;

    // ---- 공격 ----
    //   ★ 플레이어와 **같은 스키마**다(AttackData). 6-g 에서 정한 그대로,
    //     items.json 과 enemies.json 이 같은 모양의 블록을 쓴다.
    AttackData swing{};
    AttackData bite{};

    int   attackCooldown = 40;

    // ---- 이동 ----
    float walkPerTick  = 1.0f;
    float crawlPerTick = 0.3f;

    // ---- 사거리 ----
    float swingRange = 34.0f;
    float biteRange  = 20.0f;
    float yTolerance = 14.0f;

    //   뒤에 선 동료가 물러서는 거리
    float personalSpace = 26.0f;

    // ---- 시야 (design.md §3.9 B) ----
    //   ★ 각도를 **도(degree)**로 적고 읽을 때 cos 으로 바꾼다.
    //     파일에 0.5736 이 적혀 있으면 아무도 그게 55도인 줄 모른다.
    //     **사람이 읽는 값과 계산에 쓰는 값이 다를 때는 사람 쪽으로 적는다.**
    float sightHalfAngleDeg = 55.0f;
    float sightCos          = 0.5736f;   // 위 값에서 계산된다. 직접 적지 않는다
    float sightRange        = 220.0f;
    float hearRange         =  40.0f;
    int   forgetTicks       = 120;

    // ---- 피격 ----
    int   hurtTicks     = 16;
    float hurtKnockback = 12.0f;
    float hurtLift      = 1.6f;
};


// ★ 이름 -> 종류. map 인 이유는 파일 첫머리 주석 참조(주소 안정성).
using EnemyCatalog = std::map<std::string, EnemyType>;


// ----------------------------------------------------------------------------
//  DefaultGrunt — 잡몹의 **기본값**
//
//    ★ enemies.json 이 없거나 키가 빠져도 이 값으로 돈다.
//      그리고 **왜 그 숫자인지는 여기 주석에 남는다** — JSON 에는 못 적는다.
//      무기(DefaultDagger)와 완전히 같은 구조다.
// ----------------------------------------------------------------------------
EnemyType DefaultGrunt();


namespace EnemyTypeIO
{
    // ------------------------------------------------------------------------
    //  LoadInto — 파일의 값으로 **덮어쓴다**
    //
    //    성공하면 true. ★ 실패하면 **out 을 건드리지 않고** false —
    //    무기 로더와 같은 약속이다(ItemIO::LoadInto).
    //
    //    ★★ 이미 있는 종류는 **값만 갱신**한다. 원소를 지웠다 다시 만들면
    //      EnemyBrain 이 들고 있는 참조가 끊긴다.
    // ------------------------------------------------------------------------
    bool LoadInto(const wchar_t* path, EnemyCatalog& out, std::string* error);
}
