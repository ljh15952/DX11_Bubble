// ============================================================================
//  enemies_test.cpp — enemies.json 이 C++ 파서로 읽히는지 확인.
//  돌리는 법은 tools/json_test.cpp 첫머리와 같다(파일 이름만 바꾼다).
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

int main()
{
    std::string err;
    auto root = Json::ParseFile(L"assets/data/enemies.json", &err);
    Check(root.has_value(), "enemies.json 을 읽었다");
    if (!root) { std::printf("      오류: %s\n", err.c_str()); return 1; }

    const JsonValue& g = (*root)["enemies"]["grunt"];
    Check(!g.IsNull(),                                   "grunt 항목이 있다");
    Check(g["poise"].Int(-1) == 15,                      "poise == 15");
    Check(g["parts"]["maxHp"]["torso"].Int(-1) == 100,   "3단 중첩: torso == 100");
    Check(g["parts"]["maxHp"]["lArm"].Int(-1) == 0,      "lArm == 0 (팔이 없다)");
    Check(g["parts"]["proneWhenLegsBroken"].Bool(false), "proneWhenLegsBroken == true");
    Check(g["swing"]["heightFromFoot"].Flt(0.0f) == 32.5f, "소수 32.5");
    Check(g["swing"]["clip"]["loop"].Bool(true) == false, "clip.loop == false");
    Check(g["bite"]["posture"].Str() == "prone",         "bite.posture == prone");
    Check(g["swing"]["posture"].Str() == "stand",        "swing.posture == stand");
    Check(g["sight"]["halfAngleDeg"].Int(-1) == 55,      "시야 55도");
    Check(g["hurt"]["lift"].Flt(0.0f) == 1.6f,           "hurt.lift == 1.6");
    Check(g["move"]["crawl"].Flt(0.0f) == 0.3f,          "crawl == 0.3");

    // 없는 것을 물어도 안전한가
    Check(g["없는키"]["더없음"].Int(9) == 9,             "없는 것의 자식도 기본값");
    Check((*root)["enemies"].Size() == 1,                "적 종류 1개");

    std::printf("\n%s (%d)\n", g_fail ? "== 실패 있음 ==" : "== 전부 통과 ==", g_fail);
    return g_fail;
}
