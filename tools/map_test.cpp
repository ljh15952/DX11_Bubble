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

static void One(const wchar_t* path, const char* name, size_t portals)
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
}

int main()
{
    One(L"assets/data/maps/field.json", "field", 1);
    One(L"assets/data/maps/cave.json",  "cave",  1);

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
