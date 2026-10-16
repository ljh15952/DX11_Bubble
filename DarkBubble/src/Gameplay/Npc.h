// ============================================================================
//  Npc.h
//    NPC 카탈로그 + npcs.json 읽기 + 「지금 무슨 말을 하는가」 (10-b).
//
//  ---- ★ 파일 셋이 나눠 맡는다 ----
//      맵            **누가 어디에** 서 있는가   { "npc": "pilgrim", "box": [...] }
//      npcs.json     **언제 무슨 말을** 하는가   (조건 · 세우는 플래그 · 생김새)
//      dialogue.json **글**만                    (말하는 이 · 줄)
//
//    ★ 적이 enemies.json 의 종류를 **이름으로** 가리키는 것과 같은 모양이다.
//    ★ 글과 조건을 한 파일에 두면, 나중에 언어 표를 갈아 끼울 때 **조건까지**
//      복사해야 하고 언어마다 조건이 어긋난다. 조건은 언어가 없다.
//    ※ 10-a 에서는 맵이 대사 ID 와 생김새(row)를 들고 있었다. 대화가 여럿이
//      되자 맵에 적기에는 **이야기**가 됐다 — 맵은 배치만 한다.
//
//  ---- 이 파일은 Log 를 안 쓴다 (tools/npc_test.cpp 가 Json.cpp 만 붙인다) ----
// ============================================================================
#pragma once

#include "Gameplay/Flags.h"

#include <map>
#include <string>
#include <vector>

// 대화 하나를 고르는 조건과, 끝까지 들었을 때 일어나는 일.
struct TalkOption
{
    std::vector<std::string> ifAll;    // ★ **모두** 있어야 한다 (JSON: "if")
    std::vector<std::string> ifNone;   // ★ **하나도** 없어야 한다 (JSON: "ifNot")
    std::string              say;      // dialogue.json 의 키
    std::vector<std::string> sets;     // 끝까지 들으면 세운다

    bool Unconditional() const { return ifAll.empty() && ifNone.empty(); }
};

struct NpcType
{
    int row = 0;   // npc.png 의 몇 번째 줄(생김새). ★ 사람의 성질이라 맵이 아니라 여기 있다

    // ★ **위에서부터** 보고 조건이 맞는 첫 번째를 고른다. 순서가 곧 우선순위다 —
    //   특별한 것을 위에, 조건 없는 기본 대화를 **맨 아래**에.
    std::vector<TalkOption> talk;
};

using NpcCatalog = std::map<std::string, NpcType>;

namespace NpcIO
{
    // ★ 통째로 바꾼다. 실패하면 out 을 안 건드린다(옛 값 유지 — 다른 로더들과 같다).
    //   대화가 하나도 없는 NPC 는 건너뛴다(말을 걸어도 할 말이 없다).
    bool LoadInto(const wchar_t* path, NpcCatalog& out, std::string* error);
}

// 조건이 맞는 첫 대화. 하나도 없으면 nullptr.
//   ★ 기본 대화(조건 없음)를 맨 아래에 두면 nullptr 은 안 나온다 — 테스트가 본다.
const TalkOption* PickTalk(const NpcType& npc, const FlagSet& flags);
