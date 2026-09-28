// ============================================================================
//  map_test.cpp — maps/*.json 이 C++ 파서로 읽히는지 확인.
//  돌리는 법은 tools/json_test.cpp 첫머리와 같다(파일 이름만 바꾼다).
//
//  ★ 여기서 처음으로 **배열 안의 배열**(solids)을 읽는다. 지금까지의 파일은
//    전부 객체였으므로, 이 모양은 여기서 처음 시험된다.
// ============================================================================
#include "Core/Json.h"

#include <cstdio>
#include <string>

static int g_fail = 0;
static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "  ok" : "FAIL", what);
    if (!ok) ++g_fail;
}

// ★ 상자 하나가 **어떤 지형의 윗면에** 놓였는가.
//   아랫변 == 그 지형의 윗면 이고, 가운데 x 가 그 지형의 좌우 안에 있어야 한다.
//   「바닥(groundY)보다 위」만 보면 **허공에 뜬 상자**를 못 잡는다 — 발판 위에
//   놓으려다 좌표를 잘못 적으면 공중에 뜨는데, 그건 groundY 보다 위라 통과한다.
static bool SitsOnSolid(const JsonValue& box, const JsonValue& solids)
{
    if (box.Size() != 4) return false;
    const float cx     = (box[size_t(0)].Flt() + box[size_t(2)].Flt()) * 0.5f;
    const float bottom = box[size_t(3)].Flt();

    for (size_t i = 0; i < solids.Size(); ++i)
    {
        const JsonValue& s = solids[i];
        const float l = s[size_t(0)].Flt(), top = s[size_t(1)].Flt(), r = s[size_t(2)].Flt();
        if (bottom == top && cx > l && cx < r)
            return true;
    }
    return false;
}

static void One(const wchar_t* path, const char* name, size_t portals, size_t saves,
                size_t chests, const JsonValue& items)
{
    std::string err;
    auto m = Json::ParseFile(path, &err);
    std::printf("-- %s --\n", name);
    Check(m.has_value(), "읽었다");
    if (!m) { std::printf("      오류: %s\n", err.c_str()); return; }

    Check((*m)["world"]["w"].Flt(0.0f) > 0.0f,      "world.w 가 있다");
    Check((*m)["solids"].Size() > 0,                "solids 가 비어 있지 않다");
    Check((*m)["solids"][size_t(0)].Size() == 4,    "solid 하나가 값 4개 (배열 안의 배열)");
    Check((*m)["entries"]["start"].Flt(-1.0f) >= 0.0f, "start 입구가 있다");
    Check((*m)["portals"].Size() == portals,        "포탈 수");
    Check(!(*m)["portals"][size_t(0)]["to"].Str().empty(), "포탈에 목적지가 있다");
    Check((*m)["enemies"][size_t(0)]["facing"].Int(0) != 0, "적에 facing 이 있다");

    // ---- 세이브 포인트 ----
    //   ★ 여기서 **부활한다.** 상자를 잘못 놓으면 「죽으면 바닥에 박혀
    //     일어나는」 맵이 되고, 그건 게임을 돌려야만 보인다.
    //     파일만 보고 알 수 있는 것은 파일만 보고 잡는다.
    const JsonValue& sv = (*m)["savePoints"];
    Check(sv.Size() == saves, "세이브 포인트 수");

    const float groundY = (*m)["world"]["groundY"].Flt(0.0f);
    const float worldW  = (*m)["world"]["w"]      .Flt(0.0f);
    bool placed = true;
    for (size_t i = 0; i < sv.Size(); ++i)
    {
        const JsonValue& b = sv[i]["box"];
        if (b.Size() != 4) { placed = false; break; }

        const float left   = b[size_t(0)].Flt();
        const float right  = b[size_t(2)].Flt();
        const float bottom = b[size_t(3)].Flt();

        // 바닥보다 아래 = 지형 속. 맵 밖 = 카메라가 못 따라간다.
        if (bottom > groundY || left < 0.0f || right > worldW) placed = false;
    }
    Check(placed, "세이브 포인트가 맵 안, 지면 위에 있다");

    // ★ 부활할 자리이므로 **실제로 딛고 설 곳**이어야 한다.
    const JsonValue& solids = (*m)["solids"];
    bool savesSit = true;
    for (size_t i = 0; i < sv.Size(); ++i)
        if (!SitsOnSolid(sv[i]["box"], solids)) savesSit = false;
    Check(savesSit, "세이브 포인트가 지형 윗면에 놓였다");

    // ---- 상자 (8-h) ----
    const JsonValue& ch = (*m)["chests"];
    Check(ch.Size() == chests, "상자 수");

    bool sits = true, known = true, unique = true;
    for (size_t i = 0; i < ch.Size(); ++i)
    {
        if (!SitsOnSolid(ch[i]["box"], solids)) sits = false;

        // ★★ **두 파일을 엇갈려 본다.** 맵의 상자가 items.json 에 없는 물건을
        //   가리키면, 열었는데 아무것도 안 나오거나 이름 없는 물건이 떨어진다.
        //   맵 파일만 보고도, 물건 파일만 보고도 **안 보이는** 실수다.
        if (items[ch[i]["item"].Str()].IsNull()) known = false;

        // ★ id 가 겹치면 하나를 열 때 **둘 다 열린 것**이 된다(열림은 id 로 기억한다).
        for (size_t j = i + 1; j < ch.Size(); ++j)
            if (ch[i]["id"].Str() == ch[j]["id"].Str()) unique = false;
    }
    Check(sits,   "상자가 지형 윗면에 놓였다 (허공에 뜨지 않았다)");
    Check(known,  "상자 안의 물건이 items.json 에 있다");
    Check(unique, "상자 id 가 맵 안에서 겹치지 않는다");
}

int main()
{
    std::string ierr;
    auto items = Json::ParseFile(L"assets/data/items.json", &ierr);
    Check(items.has_value(), "items.json 을 읽었다 (상자 내용물을 맞춰 보려고)");
    if (!items) return 1;
    const JsonValue& catalog = (*items)["items"];

    One(L"assets/data/maps/field.json", "field", 1, 2, 7, catalog);
    One(L"assets/data/maps/cave.json",  "cave",  1, 1, 3, catalog);

    // 서로를 가리키는지 — 이름이 안 맞으면 못 돌아온다
    std::string e;
    auto f = Json::ParseFile(L"assets/data/maps/field.json", &e);
    auto c = Json::ParseFile(L"assets/data/maps/cave.json",  &e);
    if (f && c)
    {
        const std::string toCave  = (*f)["portals"][size_t(0)]["entry"].Str();
        const std::string toField = (*c)["portals"][size_t(0)]["entry"].Str();
        Check(!(*c)["entries"][toCave].IsNull(),  "field 의 포탈이 cave 의 실제 입구를 가리킨다");
        Check(!(*f)["entries"][toField].IsNull(), "cave 의 포탈이 field 의 실제 입구를 가리킨다");
    }

    std::printf("\n%s (%d)\n", g_fail ? "== 실패 있음 ==" : "== 전부 통과 ==", g_fail);
    return g_fail;
}
