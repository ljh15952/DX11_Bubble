// ============================================================================
//  MapData.h
//    맵 한 장의 내용 전부 + map.json 읽기.
//
//  ---- ★ 새로 만든 것이 거의 없다 ----
//    지형은 이미 「사각형 목록」이었고(6-d), 적 배치도 이미 「좌표 목록」이었다
//    (7-a). 둘 다 **데이터 모양으로 만들어 둔 덕에** 파일로 옮기는 것이
//    형태를 안 바꾼다 — 코드에 적혀 있던 배열이 파일에서 온 배열이 될 뿐이다.
//
//    그때 「이 목록이 곧 맵 파일이 된다」고 적어 둔 것이 그대로 됐다.
//
//  ---- ★★ 포탈은 **좌표가 아니라 「입구 이름」**을 가리킨다 ----
//    `{ "to": "cave", "entry": "west" }`
//
//    목적지를 좌표로 적으면 저쪽 맵을 고칠 때마다 이쪽도 같이 고쳐야 한다 —
//    **같은 값이 두 곳에** 있는 것이고, 이 프로젝트가 계속 밟아 온 함정이다.
//    이름으로 가리키면 저쪽이 입구를 옮겨도 이쪽은 그대로다.
// ============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

#include "Core/AABB.h"

// ============================================================================
//  MapInteract — **E 를 누르면 무언가 일어나는 것**
//
//    ★ 포탈과 세이브 포인트를 **한 목록**에 둔다. 「발밑에 무엇이 있나」를
//      찾는 코드가 하나면 되고, 나중에 **상자·사람**이 종류 하나씩 늘어난다.
//      목록을 종류마다 나누면 찾는 코드도 종류마다 늘어난다.
//
//    ★★ 안 쓰는 칸이 남는 것은 값이 싸다 — 세이브 포인트는 to/entry 를
//      안 쓴다. AttackData 가 플레이어와 적의 칸을 같이 들고 있는 것과 같다.
//
//    ★ 그리고 이 구조가 §3.2.1.1 과 같다: **버튼은 하나이고, 무엇을 하는지는
//      그 자리에 무엇이 있는지가 정한다.**
// ============================================================================
enum class InteractKind
{
    Portal,      // 다른 맵으로
    SavePoint,   // 부활 지점을 여기로. 쉬면 회복하고 적이 되살아난다

    // ★ 8-h. 7-d 에서 「나중에 상자·사람이 오면 **종류만** 늘어난다」고 적어 둔
    //   그대로다 — 찾는 코드도, 버튼도, 그리는 자리도 안 늘었다.
    Chest,       // 한 번 열면 끝. 안에 든 것이 발밑에 떨어진다
};

struct MapInteract
{
    InteractKind kind = InteractKind::Portal;
    AABB         box{};

    std::string  to;      // 포탈 : 어느 맵으로
    std::string  entry;   // 포탈 : 그 맵의 어느 입구로
    std::string  name;    // 표시용

    // ---- id — 상자 (8-h) · 화톳불 (9) ----
    //   ★ **이 맵 안에서** 유일하면 된다. 「열렸는가」「어디서 쉬었는가」는
    //     「맵 이름 + id」로 기억한다 — 두 맵에 같은 id 가 있어도 안 섞인다.
    //   ★★ 화톳불을 **좌표가 아니라 id 로** 기억하는 이유: 저장 파일에 좌표를
    //     적어 두면 맵을 고칠 때 낡는다(포탈이 입구 **이름**을 가리키는 것과 같다).
    //     `name` 을 열쇠로 쓰지 않는 이유: 화면용 글자라 고치면 열쇠도 바뀐다.
    std::string  id;
    std::string  item;    // 상자 : 안에 든 것 (items.json 의 키)

    // ★ `? :` 가 아니라 switch 다. 종류가 둘일 때는 `? :` 로 충분했는데
    //   셋이 되는 순간 하나가 조용히 빠진다(handoff §9.1 의 자매 규칙).
    const char* Prompt() const
    {
        switch (kind)
        {
        case InteractKind::Portal:    return "E : ENTER";
        case InteractKind::SavePoint: return "E : REST";
        case InteractKind::Chest:     return "E : OPEN";
        }
        return "E";
    }
};

struct MapEnemySpawn
{
    float       x      = 0.0f;
    int         facing = -1;   // ★ 배치만으로 §3.9 B 의 시야가 전술이 된다
    std::string type   = "grunt";
};

struct MapData
{
    // 월드 크기. ★ 맵마다 다르다 — 카메라가 여기에 맞춰 멈춘다.
    float worldWidth  = 1920.0f;
    float worldHeight =  760.0f;

    // 부활·스폰이 놓이는 높이. ★ 맵마다 다르다.
    float groundY     =  680.0f;

    // ★★ 어두운 곳인가 (8-g). 전에는 F5 로 전부 끄고 켰다.
    //   **맵이 정해야** 한다 — 들판까지 어두우면 빛(蛍 · 횃불)이 **선택이 아니라
    //   필수**가 되어, 지문 한 칸이 영원히 묶인다. 동굴만 어두우면
    //   「동굴에 들어가기 전에 무엇을 바꿀까」가 생긴다(§1.2 의 예 그대로).
    bool  dark        = false;

    std::vector<AABB>          solids;      // 고체 — 바닥 · 벽 · 천장
    std::vector<AABB>          platforms;   // ★ 발판 — 위에서만 받친다(아래·옆은 통과)
    std::vector<MapEnemySpawn> enemies;
    std::vector<MapInteract>   interacts;   // 포탈 · 세이브 포인트 · 상자

    // 입구 이름 -> x. ★ 포탈이 **이 이름**을 가리킨다.
    //   "start" 는 특별하다 — 부활 지점이자 맵을 직접 열었을 때의 기본 입구다.
    std::map<std::string, float> entries;

    // 입구를 찾는다. 없으면 "start", 그것도 없으면 맵 가운데.
    //   ★ 「없으면 죽는다」로 두면 맵 하나 고치다 오타 났을 때 게임이 멈춘다.
    float EntryX(const std::string& name) const;
};


namespace MapIO
{
    // 성공하면 out 을 **통째로 바꾼다**(맵은 부분 갱신이 의미 없다).
    // ★ 실패하면 out 을 건드리지 않는다 — 다른 로더들과 같은 약속이다.
    bool Load(const wchar_t* path, MapData& out, std::string* error);
}
