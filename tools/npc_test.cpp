// ============================================================================
//  npc_test.cpp — NPC 카탈로그(npcs.json)와 대화 고르기의 확인 (10-b).
//
//  돌리는 법은 tools/json_test.cpp 첫머리와 같다. 컴파일 줄만 다르다:
//    cl /nologo /std:c++20 /EHsc /W4 /utf-8 /I DarkBubble\src tools\npc_test.cpp DarkBubble\src\Gameplay\Npc.cpp DarkBubble\src\Core\Json.cpp /Fe:npc_test.exe
//
//  ★ 조건 있는 대화는 **그 조건을 만들어 봐야** 보인다 — 횃불 대사를 보려면
//    순례자를 만나고, 상자를 열고, 다시 말을 걸어야 한다. 파일만 보고 알 수
//    있는 것은 여기서 잡는다:
//    ① 고르기(PickTalk): if 는 모두 · ifNot 은 하나도 · 위에 있는 것이 먼저
//    ② ★ 맨 아래가 기본 대화인가 — 아니면 조건이 안 맞을 때 「...」만 나온다.
//       기본 대화가 **중간에** 있으면 그 아래는 영영 안 나온다
//    ③ ★★ 조건에 쓰인 플래그를 **누군가 세우는가** — 상자 플래그는 그 상자가
//       맵에 있는가, 나머지는 어느 대화의 sets 에 있는가. 아무도 안 세우는 플래그를
//       조건에 쓰면 그 대화는 **도달할 수 없다**(handoff §8 「도달할 수 없는 기능」)
//    ④ 고른 대사가 dialogue.json 에 있는가 (두 파일의 약속)
// ============================================================================
#include "Gameplay/Npc.h"
#include "Core/Json.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

static int g_fail = 0;
static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "  ok" : "FAIL", what);
    if (!ok) ++g_fail;
}

static std::string Say(const NpcType& npc, const FlagSet& flags)
{
    const TalkOption* t = PickTalk(npc, flags);
    return t ? t->say : std::string("(none)");
}

// 「종류:이름」 모양인가 — 콜론이 있고, 양쪽이 비지 않았다.
static bool Shaped(const std::string& f)
{
    const size_t c = f.find(':');
    return c != std::string::npos && c > 0 && c + 1 < f.size();
}

int main()
{
    // ========================================================================
    //  ① 고르기
    // ========================================================================
    std::printf("-- PickTalk --\n");
    NpcType t;
    t.talk = {
        TalkOption{ { "a", "b" }, {},      "both",    {} },
        TalkOption{ { "a" },      { "x" }, "a_not_x", {} },
        TalkOption{ {},           {},      "default", { "a" } },
    };
    Check(Say(t, {})           == "default", "아무것도 없으면 맨 아래 기본 대화");
    Check(Say(t, { "a" })      == "a_not_x", "if 가 맞으면 그 대화");
    Check(Say(t, { "b" })      == "default", "if 는 **모두** 있어야 한다");
    Check(Say(t, { "a", "x" }) == "default", "ifNot 이 하나라도 있으면 건너뛴다");
    Check(Say(t, { "a", "b" }) == "both",    "둘 다 맞으면 **위에 있는 것**이 먼저");
    Check(PickTalk(NpcType{}, {}) == nullptr, "대화가 없으면 nullptr");

    // ========================================================================
    //  ② ③ ④ 실제 파일
    // ========================================================================
    std::printf("-- assets/data/npcs.json --\n");
    NpcCatalog cat;
    std::string err;
    Check(NpcIO::LoadInto(L"assets/data/npcs.json", cat, &err), "읽었다");
    if (!err.empty()) std::printf("      오류: %s\n", err.c_str());

    auto dlg = Json::ParseFile(L"assets/data/dialogue.json", &err);
    Check(dlg.has_value(), "dialogue.json 을 읽었다 (대사를 맞춰 보려고)");
    if (!dlg) return 1;
    const JsonValue& lines = (*dlg)["dialogues"];

    // 맵에 실제로 있는 상자 → 세워질 수 있는 상자 플래그
    std::set<std::string> chestFlags;
    for (const char* map : { "field", "cave" })
    {
        const std::wstring path = L"assets/data/maps/" + std::wstring(map, map + std::char_traits<char>::length(map)) + L".json";
        auto m = Json::ParseFile(path.c_str(), &err);
        Check(m.has_value(), "맵을 읽었다 (상자 플래그를 맞춰 보려고)");
        if (!m) continue;
        const JsonValue& ch = (*m)["chests"];
        for (size_t i = 0; i < ch.Size(); ++i)
            chestFlags.insert(ChestFlag(map, ch[i]["id"].Str()));
    }

    // 대화가 세우는 플래그 전부
    std::set<std::string> setFlags;
    for (const auto& [id, npc] : cat)
        for (const TalkOption& o : npc.talk)
            setFlags.insert(o.sets.begin(), o.sets.end());

    bool lastDefault = true, noDead = true, says = true, reach = true, shaped = true;
    for (const auto& [id, npc] : cat)
    {
        if (npc.talk.empty() || !npc.talk.back().Unconditional())
        {
            lastDefault = false;
            std::printf("      맨 아래가 기본 대화가 아니다: %s\n", id.c_str());
        }
        for (size_t i = 0; i + 1 < npc.talk.size(); ++i)
        {
            if (npc.talk[i].Unconditional())
            {
                noDead = false;
                std::printf("      조건 없는 대화가 중간에 있다 — 그 아래는 안 나온다: %s / %s\n",
                            id.c_str(), npc.talk[i].say.c_str());
            }
        }

        for (const TalkOption& o : npc.talk)
        {
            if (lines[o.say].IsNull())
            {
                says = false;
                std::printf("      dialogue.json 에 없는 대사: %s / %s\n", id.c_str(), o.say.c_str());
            }

            for (const std::string& f : o.sets)
                if (!Shaped(f)) shaped = false;

            std::vector<std::string> used = o.ifAll;
            used.insert(used.end(), o.ifNone.begin(), o.ifNone.end());
            for (const std::string& f : used)
            {
                if (!Shaped(f)) shaped = false;

                const bool chest = f.rfind("chest:", 0) == 0;
                if ((chest && !chestFlags.count(f)) || (!chest && !setFlags.count(f)))
                {
                    reach = false;
                    std::printf("      아무도 안 세우는 플래그: %s / %s\n", id.c_str(), f.c_str());
                }
            }
        }
    }
    Check(lastDefault, "★ 모든 NPC 의 맨 아래가 조건 없는 기본 대화다");
    Check(noDead,      "조건 없는 대화가 중간에 없다 (아래 대화가 막히지 않는다)");
    Check(says,        "고를 수 있는 대사가 전부 dialogue.json 에 있다");
    Check(reach,       "★★ 조건에 쓰인 플래그를 누군가 세운다 (상자는 맵에 · 나머지는 sets 에)");
    Check(shaped,      "플래그가 전부 「종류:이름」 모양이다");

    // ========================================================================
    //  순례자의 세 갈래
    // ========================================================================
    std::printf("-- 순례자 --\n");
    auto it = cat.find("pilgrim");
    Check(it != cat.end(), "pilgrim 이 있다");
    if (it != cat.end())
    {
        const NpcType& p = it->second;
        const std::string torch = ChestFlag("field", "torch");
        Check(Say(p, {})                       == "pilgrim_hello", "처음 → hello");
        Check(Say(p, { "met:pilgrim" })        == "pilgrim_again", "만난 적 있음 → again");
        Check(Say(p, { "met:pilgrim", torch }) == "pilgrim_torch", "만났고 횃불 상자를 열었다 → torch");
        Check(Say(p, { torch })                == "pilgrim_hello", "상자만 열고 아직 안 만났다 → hello (처음 인사가 먼저)");

        const TalkOption* hello = PickTalk(p, {});
        Check(hello && hello->sets == std::vector<std::string>{ "met:pilgrim" },
              "hello 를 끝까지 들으면 met:pilgrim");
    }

    std::printf("\n%s (%d)\n", g_fail ? "== 실패 있음 ==" : "== 전부 통과 ==", g_fail);
    return g_fail;
}
