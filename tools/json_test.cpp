// ============================================================================
//  json_test.cpp — Core/Json 의 확인용. 게임과 따로 돈다.
//
//  ★ 이 프로젝트에서 **눈으로 못 보는 첫 코드**가 파서다. 그림도 움직임도
//    없으니 「돌려 보고 이상하면 안다」가 통하지 않는다. 그래서 검사를 남긴다.
//
//  돌리는 법 (PowerShell):
//    $vc = "C:\Program Files\Microsoft Visual Studio8\Professional\VC\Auxiliary\Buildcvars64.bat"
//    cmd /c "`"$vc`" >nul 2>&1 && set" | ForEach-Object {
//      if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($matches[1])" $matches[2] -ErrorAction SilentlyContinue } }
//    cd "D:\イジュンハ.DX\DX11_Bubble"
//    cl /nologo /std:c++20 /EHsc /W4 /utf-8 /I DarkBubble\src tools\json_test.cpp DarkBubble\src\Core\Json.cpp /Fe:json_test.exe
//    .\json_test.exe
//
//  ※ 작업 디렉터리가 리포지토리 루트여야 한다 — assets/data/weapons.json 을 읽는다.
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
    // ---- ① 실제 파일 ----
    std::string err;
    auto root = Json::ParseFile(L"assets/data/weapons.json", &err);
    Check(root.has_value(), "weapons.json 을 읽었다");
    if (!root)
    {
        std::printf("      오류: %s\n", err.c_str());
        return 1;
    }

    const JsonValue& light = (*root)["dagger"]["light"];
    Check(light["startup"].Int(-1) == 8,          "light.startup == 8");
    Check(light["reach"].Flt(-1.0f) == 12.0f,     "light.reach == 12");
    Check(light["clip"]["row"].Int(-1) == 2,      "light.clip.row == 2");
    Check(light["clip"]["loop"].Bool(true) == false, "light.clip.loop == false");

    // ---- ② 없는 키는 기본값 ----
    Check(light["nope"].Int(77) == 77,            "없는 키 -> 기본값");
    Check((*root)["없는항목"]["더없음"].Int(5) == 5, "없는 것의 자식도 안전하다");
    Check((*root)["dagger"].Size() == 9,          "dagger 항목이 9개");

    // ---- ③ 음수 · 소수 · 중첩 ----
    auto v = Json::Parse("{\"a\":[-1.5, 2e2, true, null, \"x\\ny\"]}", &err);
    Check(v.has_value(), "작은 JSON 을 읽었다");
    if (v)
    {
        const JsonValue& a = (*v)["a"];
        Check(a.Size() == 5,                      "배열 길이 5");
        Check(a[0].Flt(0.0f) == -1.5f,            "음수 소수");
        Check(a[1].Int(0) == 200,                 "지수 표기 2e2 == 200");
        Check(a[2].Bool(false) == true,           "true");
        Check(a[3].IsNull(),                      "null");
        Check(a[4].Str() == "x\ny",               "이스케이프 \\n");
    }

    // ---- ④ 오류는 어디서 났는지 말해야 한다 ----
    err.clear();
    auto bad = Json::Parse("{\n  \"a\": 1,\n  \"b\" 2\n}", &err);
    Check(!bad.has_value(), "잘못된 JSON 을 거부했다");
    Check(err.find("3행") != std::string::npos, "오류에 줄 번호가 있다");
    std::printf("      메시지: %s\n", err.c_str());

    std::printf("\n%s (%d)\n", g_fail ? "== 실패 있음 ==" : "== 전부 통과 ==", g_fail);
    return g_fail;
}
