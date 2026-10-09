// ============================================================================
//  dialogue_test.cpp — 대화(Dialogue)의 확인. 게임과 따로 돈다 (10-a).
//
//  돌리는 법은 tools/json_test.cpp 첫머리와 같다. 컴파일 줄만 다르다:
//    cl /nologo /std:c++20 /EHsc /W4 /utf-8 /I DarkBubble\src tools\dialogue_test.cpp DarkBubble\src\Gameplay\Dialogue.cpp DarkBubble\src\Core\Json.cpp /Fe:dialogue_test.exe
//
//  ★ 타자기 효과와 줄 접기는 **눈으로 확인하기 어렵다** — 한 글자가 2틱(33ms)에
//    나오고, 단어가 줄 끝에서 튀는 것은 한 번 지나가면 끝이다. 그래서 여기서 센다:
//    ① 진행(DialogueRunner): 몇 틱에 몇 글자 · 누르면 끝까지 · 다시 누르면 다음 줄
//    ② 접기(WrapText): 공백에서 접는다 · 긴 단어는 자른다 · 글자를 잃지 않는다
//    ③ ★ 실제 대사표: 전부 ASCII 인가 · 전부 **상자(66 x 3)에 들어가는가**
// ============================================================================
#include "Gameplay/Dialogue.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "  ok" : "FAIL", what);
    if (!ok) ++g_fail;
}

static std::vector<std::string> Rows(std::string_view text, size_t columns)
{
    std::vector<std::string> out;
    for (const WrappedRow& r : WrapText(text, columns))
        out.emplace_back(text.substr(r.start, r.length));
    return out;
}

static size_t NonSpace(std::string_view s)
{
    size_t n = 0;
    for (const char c : s) if (c != ' ') ++n;
    return n;
}

int main()
{
    // ========================================================================
    //  ① 진행
    // ========================================================================
    std::printf("-- DialogueRunner --\n");
    {
        Dialogue d{ "X", { "abc", "de" } };
        DialogueRunner r(d);
        d.lines.clear();   // ★ 원본을 지워도 진행은 그대로여야 한다(복사해서 든다 — F6 대비)

        Check(!r.Finished() && r.Text() == "abc" && r.Shown() == 0, "처음: 첫 줄, 아무것도 안 보인다");

        for (int i = 0; i < DialogueRunner::kTicksPerChar; ++i) r.Tick();
        Check(r.Shown() == 1, "2틱에 한 글자");

        for (int i = 0; i < 100; ++i) r.Tick();
        Check(r.Shown() == 3 && r.LineDone(), "줄 끝에서 멈춘다 (글자 수를 넘지 않는다)");

        r.Press();
        Check(r.Text() == "de" && r.Shown() == 0 && r.LastLine(), "다 나온 뒤 누르면 다음 줄");

        r.Tick();
        r.Press();
        Check(r.Text() == "de" && r.LineDone(), "★ 나오는 중에 누르면 **그 줄을 끝까지** (넘기지 않는다)");

        r.Press();
        Check(r.Finished() && r.Text().empty(), "마지막 줄에서 누르면 끝");

        r.Press();
        r.Tick();
        Check(r.Finished(), "끝난 뒤에 눌러도 아무 일 없다");
    }
    {
        DialogueRunner r(Dialogue{ "X", { "" } });
        Check(r.LineDone(), "빈 줄은 처음부터 다 나온 것이다");
        r.Press();
        Check(r.Finished(), "빈 줄도 한 번 누르면 넘어간다");
    }

    // ========================================================================
    //  ② 접기
    // ========================================================================
    std::printf("-- WrapText --\n");
    Check(Rows("hello world", 5) == std::vector<std::string>{ "hello", "world" }, "공백에서 접는다");
    Check(Rows("abcd efgh", 4)   == std::vector<std::string>{ "abcd", "efgh" },  "폭이 꽉 찬 자리의 공백에서 접는다");
    Check(Rows("abcdefghij", 4)  == std::vector<std::string>{ "abcd", "efgh", "ij" }, "한 줄보다 긴 단어는 자른다");
    Check(Rows("a  b", 1)        == std::vector<std::string>{ "a", "b" },        "줄 머리의 공백은 버린다");
    Check(Rows("hi", 10)         == std::vector<std::string>{ "hi" },            "짧으면 한 줄");
    Check(Rows("", 10).empty() && Rows("   ", 10).empty(),                        "빈 글 · 공백뿐인 글은 줄이 없다");

    // ★ 위치가 원래 글을 가리키는가 — 타자기가 이 위치로 「몇 글자까지 보였나」를 나눈다
    {
        const std::string t = "one two three four";
        const auto rows = WrapText(t, 8);   // "one two" / "three" / "four"
        Check(rows.size() == 3 && rows[1].start == 8 && rows[1].length == 5 && rows[2].start == 14,
              "줄의 위치가 원래 글의 위치다");
    }

    // ========================================================================
    //  ③ 실제 대사표
    // ========================================================================
    std::printf("-- assets/data/dialogue.json --\n");
    DialogueCatalog cat;
    std::string err;
    Check(DialogueIO::LoadInto(L"assets/data/dialogue.json", cat, &err), "읽었다");
    if (!err.empty()) std::printf("      오류: %s\n", err.c_str());
    Check(!cat.empty(), "대사가 하나 이상 있다");

    bool ascii = true, fits = true, kept = true, named = true;
    for (const auto& [id, d] : cat)
    {
        if (d.speaker.empty() || d.speaker.size() > DialogueBox::kColumns) named = false;

        std::vector<std::string> texts = d.lines;
        texts.push_back(d.speaker);
        for (const std::string& line : texts)
        {
            // ★ 글꼴이 ASCII(32~126) 전용이다. 그 밖의 바이트는 빈칸이나 엉뚱한 글자가 된다.
            for (const char c : line)
            {
                const unsigned char u = static_cast<unsigned char>(c);
                if (u < 32 || u > 126)
                {
                    ascii = false;
                    std::printf("      ASCII 가 아니다: %s / \"%s\"\n", id.c_str(), line.c_str());
                    break;
                }
            }
        }

        for (const std::string& line : d.lines)
        {
            const auto rows = WrapText(line, DialogueBox::kColumns);

            // ★ 상자에 다 들어가는가 — 넘치면 아랫줄이 **안 그려진다**(잘린 대사)
            if (rows.size() > DialogueBox::kRows)
            {
                fits = false;
                std::printf("      상자를 넘는다(%zu줄): %s / \"%s\"\n", rows.size(), id.c_str(), line.c_str());
            }

            // ★ 접으면서 글자를 잃지 않았는가 (공백 말고 전부 어느 줄엔가 있다)
            size_t inRows = 0;
            for (const WrappedRow& r : rows)
            {
                if (r.length > DialogueBox::kColumns) kept = false;
                inRows += NonSpace(std::string_view(line).substr(r.start, r.length));
            }
            if (inRows != NonSpace(line)) kept = false;
        }
    }
    Check(named, "말하는 이의 이름이 있고 한 줄에 들어간다");
    Check(ascii, "★ 글이 전부 ASCII 다 (글꼴이 ASCII 전용)");
    Check(fits,  "★ 모든 대사가 상자(66 x 3)에 들어간다");
    Check(kept,  "접어도 글자를 잃지 않는다 · 한 줄이 폭을 넘지 않는다");

    std::printf("\n%s (%d)\n", g_fail ? "== 실패 있음 ==" : "== 전부 통과 ==", g_fail);
    return g_fail;
}
