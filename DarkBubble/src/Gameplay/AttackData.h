// ============================================================================
//  AttackData.h
//    공격 하나의 정의. **플레이어와 적이 같이 쓴다.**
//
//  ---- ★ 왜 공유하는가 ----
//    「공격」은 누가 하든 같은 개념이다 —
//    발생·지속·후딜이 있고, 상자가 있고, 데미지와 충격력이 있다. 방향만 반대다.
//    6-g 에서 weapons.json 과 enemies.json 이 **같은 스키마**를 쓰게 된다.
//
//    적은 staminaCost 를 쓰지 않고 플레이어는 아직 impact 를 쓰지 않는다.
//    안 쓰는 칸이 남는 것은 값이 싸다 — 구조를 두 벌 만드는 것보다 훨씬 싸다.
//
//  ※ 원래 PlayScene.h 에 있었다. 적이 EnemyBrain 으로 나가면서 두 파일이
//    같은 타입을 필요로 하게 되어 여기로 올렸다 — design.md §9 의
//    「두 번째 사용자가 생겼을 때 올린다」 그대로다.
// ============================================================================
#pragma once

#include "Core/AABB.h"
#include "Graphics/Animation.h"

// ============================================================================
//  AttackData — 프레임 데이터
//
//    공격 하나를 세 구간으로 나눈다. 단위는 전부 틱(1/60초).
//
//      t0                 startup   +active                    +recovery
//      │──── startup ────│ active  │──────── recovery ────────│
//      │   판정 없음      │ ★판정★  │        판정 없음           │
//
//    ★ 이 세 숫자가 무기의 성격 전부다.
//        │ startup │ active │ recovery │  총    │
//      단검 │    8   │   3    │    13    │  24틱  │  빠르고 안전
//      대검 │   22   │   6    │    34    │  62틱  │  느리고 위험
// ============================================================================
struct AttackData
{
    // 화면·로그에 찍는 이름. ★ ASCII 만 — BitmapFont 가 ASCII 전용이다.
    const char* name = "LIGHT";

    // ---- 프레임 데이터 (틱) ----
    int startup  = 8;    // 판정이 나오기까지
    int active   = 3;    // 판정이 존재하는 구간
    int recovery = 13;   // 판정 끝 ~ 다시 움직일 수 있기까지

    // ---- 히트박스 (발밑 원점 기준. facing 으로 좌우 반전된다) ----
    float reach          = 12.0f;   // 몸 중심에서 히트박스 안쪽 끝까지
    float width          = 28.0f;   // 히트박스 폭
    float height         = 24.0f;   // 히트박스 높이
    float heightFromFoot = 34.0f;   // ★ 이 하나가 닿는 부위를 정한다

    int damage      = 12;
    int staminaCost = 28;

    // ★ 강인도 데미지. 맞는 쪽의 poise 와 비교되어 **경직 여부**를 정한다.
    //   damage 와 일부러 다른 숫자로 둔다. 합치면
    //   「약하지만 크게 휘청이게 하는 공격」(방패 밀치기 같은 것)을 못 만든다.
    int impact      = 14;

    // ★ 공격이 자기 그림 속도를 직접 들고 다닌다.
    //   frameCount × ticksPerFrame == TotalTicks() 가 되도록 짝을 맞춘다.
    AnimationClip clip{ /*row*/ 2, /*frames*/ 6, /*ticks*/ 4, /*loop*/ false };

    int TotalTicks() const { return startup + active + recovery; }
};


// ----------------------------------------------------------------------------
//  MakeAttackBox — 히트박스 생성. 플레이어와 적이 **공유한다.**
//
//    발밑 원점 + facing + AttackData 만으로 만들어진다.
//    때리는 쪽이 누구인지 이 함수는 모른다 — 그래서 양쪽에서 쓸 수 있다.
//
//    ★ facing 이 -1 이면 outer < inner 가 되어 사각형이 뒤집힌다.
//      AABB 는 left <= right 를 전제하고 Intersects 가 그 전제에 의존하므로,
//      정규화하지 않으면 왼쪽을 볼 때 공격이 절대 맞지 않는다.
//      한 곳에 모아 두면 이 함정을 두 번 밟지 않는다.
// ----------------------------------------------------------------------------
inline AABB MakeAttackBox(float x, float y, int facing, const AttackData& a)
{
    const float inner = x     + facing * a.reach;
    const float outer = inner + facing * a.width;

    const float left  = (inner < outer) ? inner : outer;
    const float right = (inner < outer) ? outer : inner;

    const float centerY = y - a.heightFromFoot;
    return { left, centerY - a.height * 0.5f, right, centerY + a.height * 0.5f };
}
