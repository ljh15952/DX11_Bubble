// ============================================================================
//  Dialogue.h
//    대사 한 묶음 + dialogue.json 읽기 + 진행(DialogueRunner) (10-a).
//
//  ---- ★ 진행과 그리기를 나눈다 (design.md §6.5) ----
//    「대사를 순서대로 넘긴다 · 한 글자씩 드러낸다」는 **진행**이고,
//    상자에 그리는 것은 **그리기**다. 지금은 화면 위 상자(DialogueScene)
//    하나뿐이지만, 말풍선이 오면 그리는 쪽만 하나 더 생긴다 — 진행은 두 벌이
//    되지 않는다.
//    ★ 그리고 진행이 그림과 떨어져 있으니 **게임 없이 시험할 수 있다**
//      (tools/dialogue_test.cpp). 타자기 효과는 눈으로 확인하기 어렵다.
//
//  ---- ★ 대사는 ID 로 가리킨다 ----
//    맵의 NPC 는 대사 **ID** 만 들고 있다(포탈이 입구 **이름**을 가리키는 것과
//    같다). 글은 dialogue.json 한 곳에 있으므로, 나중에 한국어·일본어가 오면
//    **표 하나를 갈아 끼우면** 된다.
//
//  ---- 이 파일은 Log 를 안 쓴다 ----
//    실패는 `error` 로 돌려준다(SaveData 와 같다). 그래서 테스트가
//    Json.cpp 하나만 더 붙여 컴파일된다.
// ============================================================================
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

struct Dialogue
{
    std::string              speaker;   // 상자 위에 적는 이름. ★ 글과 같이 번역되므로 여기 있다
    std::vector<std::string> lines;     // 한 번 누를 때마다 한 줄
};

using DialogueCatalog = std::map<std::string, Dialogue>;

namespace DialogueIO
{
    // ★ 성공하면 out 을 **통째로** 바꾼다. 대사는 부분 갱신이 뜻이 없다 —
    //   맵과 같은 약속이다. 실패하면 out 을 안 건드린다(옛 대사 유지).
    //   줄이 하나도 없는 대사는 건너뛴다(눌러도 아무것도 안 나오는 대화가 된다).
    bool LoadInto(const wchar_t* path, DialogueCatalog& out, std::string* error);
}


// ============================================================================
//  상자의 크기 — **글자 수**로 정한다
//
//    ★ 그리는 쪽(DialogueScene)은 이 숫자에서 상자 폭을 **계산한다.**
//      반대로 하면(상자 폭을 픽셀로 정하고 글자 수를 따로 적으면) 둘 중
//      하나만 고쳤을 때 글이 상자를 뚫고 나간다 — 「같은 값이 두 곳에」다.
//    ★ 테스트도 이 숫자로 「모든 대사가 상자에 들어가는가」를 본다.
// ============================================================================
namespace DialogueBox
{
    constexpr size_t kColumns = 66;   // 한 줄에 들어가는 글자 수
    constexpr size_t kRows    = 3;    // 한 번에 보이는 줄 수
}


// ============================================================================
//  WrapText — 한 줄의 대사를 상자 폭에 맞춰 접는다
//
//    ★ 접은 결과를 **문자열이 아니라 위치**(원래 글의 몇 번째 글자부터 몇 개)
//      로 돌려준다. 타자기 효과는 원래 글의 「몇 글자까지 보였는가」 하나로
//      세는데, 접힌 줄마다 그 수를 나눠 가지려면 원래 글의 위치가 필요하다.
//    ★★ **다 드러난 뒤의 모양으로 미리 접는다.** 드러나는 만큼만 그때그때
//      접으면, 단어를 치는 도중에는 윗줄에 있다가 다 치는 순간 아랫줄로
//      **튀어 내려간다.**
//
//    규칙: 공백에서 접는다. 한 단어가 한 줄보다 길면 그 단어를 자른다.
//          줄 머리의 공백은 버린다(접힌 자리의 공백이 다음 줄 첫 칸을 먹지 않게).
//    ※ ASCII 전용이다. 한국어·일본어는 단어 경계가 공백이 아니라서 규칙이
//      다르다(§6.5 「알려진 장애물」) — 글리프 아틀라스와 같이 온다.
// ============================================================================
struct WrappedRow
{
    size_t start  = 0;   // 원래 글에서의 시작 위치
    size_t length = 0;
};

std::vector<WrappedRow> WrapText(std::string_view text, size_t columns);


// ============================================================================
//  DialogueRunner — 대화 하나를 진행한다 (그리지 않는다)
//
//    · Tick  : kTicksPerChar 틱마다 한 글자씩 드러낸다(타자기)
//    · Press : 아직 다 안 나왔으면 **그 줄을 끝까지**, 다 나왔으면 **다음 줄**
//
//    ★ 「다 보여 주기」와 「넘기기」를 한 버튼에 둔다. 버튼이 둘이면 읽는
//      속도가 빠른 사람은 늘 두 번 눌러야 하고, 느린 사람은 실수로 넘긴다.
//      한 번 누르면 **지금 줄을 다 보고**, 한 번 더 누르면 **넘어간다.**
//
//    ★ 대사를 **복사해서** 들고 있다. 대화 도중에 F6 으로 dialogue.json 을
//      다시 읽으면 카탈로그의 줄 수가 바뀔 수 있다 — 가리키고 있으면 읽던
//      줄 번호가 범위를 넘는다.
// ============================================================================
class DialogueRunner
{
public:
    // 2틱 = 초당 30글자. 66글자 한 줄이 약 2초에 다 나온다.
    static constexpr int kTicksPerChar = 2;

    explicit DialogueRunner(Dialogue d) : m_dialogue(std::move(d)) {}

    void Tick();
    void Press();

    bool Finished() const { return m_line >= m_dialogue.lines.size(); }
    bool LineDone() const { return m_shown >= Text().size(); }
    bool LastLine() const { return m_line + 1 >= m_dialogue.lines.size(); }

    const std::string& Speaker() const { return m_dialogue.speaker; }
    const std::string& Text()    const;   // 지금 줄 전체. 끝났으면 빈 글
    size_t             Shown()   const { return m_shown; }   // 지금 줄에서 드러난 글자 수

private:
    Dialogue m_dialogue;
    size_t   m_line  = 0;
    size_t   m_shown = 0;
    int      m_ticks = 0;
};
