#include "Gameplay/Dialogue.h"

#include "Core/Json.h"


bool DialogueIO::LoadInto(const wchar_t* path, DialogueCatalog& out, std::string* error)
{
    std::string err;
    const auto root = Json::ParseFile(path, &err);
    if (!root)
    {
        if (error) *error = err;
        return false;
    }

    // ★ 통째로 만들어 두고 마지막에 바꾼다 — 도중에 실패하면 반만 바뀐
    //   대사표가 남는다. 맵(MapIO)과 같은 순서다.
    DialogueCatalog fresh;
    for (const auto& [id, d] : (*root)["dialogues"].Members())
    {
        Dialogue dialogue;
        dialogue.speaker = d["speaker"].Str("?");

        const JsonValue& lines = d["lines"];
        for (size_t i = 0; i < lines.Size(); ++i)
            dialogue.lines.push_back(lines[i].Str());

        if (dialogue.lines.empty())
            continue;   // 눌러도 아무것도 안 나오는 대화는 없는 편이 낫다
        fresh[id] = std::move(dialogue);
    }

    if (fresh.empty())
    {
        if (error) *error = "dialogues 가 비어 있다";
        return false;
    }

    out = std::move(fresh);
    return true;
}


std::vector<WrappedRow> WrapText(std::string_view text, size_t columns)
{
    std::vector<WrappedRow> rows;
    if (columns == 0)
        return rows;

    size_t pos = 0;
    while (pos < text.size())
    {
        // 줄 머리의 공백은 버린다 — 접힌 자리의 공백이 다음 줄 첫 칸을 먹지 않게.
        while (pos < text.size() && text[pos] == ' ')
            ++pos;
        if (pos >= text.size())
            break;

        // 남은 것이 한 줄에 다 들어간다
        if (text.size() - pos <= columns)
        {
            rows.push_back({ pos, text.size() - pos });
            break;
        }

        // ★ 한 줄 폭 **바로 뒤** 글자까지 본다. 그 자리가 공백이면 꽉 찬 줄이
        //   단어 경계에서 정확히 끝난 것이다.
        const size_t cut = text.rfind(' ', pos + columns);
        if (cut == std::string_view::npos || cut <= pos)
        {
            // 한 단어가 한 줄보다 길다 — 어쩔 수 없이 자른다
            rows.push_back({ pos, columns });
            pos += columns;
        }
        else
        {
            rows.push_back({ pos, cut - pos });
            pos = cut + 1;
        }
    }
    return rows;
}


const std::string& DialogueRunner::Text() const
{
    static const std::string kNone;
    return Finished() ? kNone : m_dialogue.lines[m_line];
}


void DialogueRunner::Tick()
{
    if (Finished() || LineDone())
        return;

    if (++m_ticks >= kTicksPerChar)
    {
        m_ticks = 0;
        ++m_shown;
    }
}


void DialogueRunner::Press()
{
    if (Finished())
        return;

    // 아직 다 안 나왔다 → 그 줄을 끝까지 보여 준다(넘기지 않는다)
    if (!LineDone())
    {
        m_shown = Text().size();
        return;
    }

    // 다 나왔다 → 다음 줄. 마지막 줄이었으면 Finished 가 된다
    ++m_line;
    m_shown = 0;
    m_ticks = 0;
}
