#include "Core/Json.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    // 없는 키를 물었을 때 돌려줄 값. **하나만** 만들어 두고 참조로 돌려준다.
    const JsonValue& NullValue()
    {
        static const JsonValue kNull;
        return kNull;
    }
}


const JsonValue& JsonValue::operator[](std::string_view key) const
{
    if (m_type == Type::Object)
    {
        for (const auto& kv : m_obj)
            if (kv.first == key)
                return kv.second;
    }
    return NullValue();
}


const JsonValue& JsonValue::operator[](size_t index) const
{
    if (m_type == Type::Array && index < m_arr.size())
        return m_arr[index];
    return NullValue();
}


size_t JsonValue::Size() const
{
    if (m_type == Type::Array)  return m_arr.size();
    if (m_type == Type::Object) return m_obj.size();
    return 0;
}


// ============================================================================
//  JsonParser — 재귀 하강
//
//    ★ 문자를 하나씩 보며 「지금 무엇이 시작되는가」만 판단한다.
//      JSON 은 **첫 글자로 종류가 정해지므로**(`{` `[` `"` 숫자 `t` `f` `n`)
//      되돌아갈 일이 없다. 그래서 파서가 이렇게 짧다.
// ============================================================================
class JsonParser
{
public:
    JsonParser(std::string_view text, std::string* error)
        : m_text(text), m_error(error) {}

    bool ParseRoot(JsonValue& out)
    {
        Skip();
        if (!ParseValue(out)) return false;
        Skip();
        if (m_pos != m_text.size())
            return Fail("끝난 뒤에 글자가 더 있다");
        return true;
    }

private:
    std::string_view m_text;
    std::string*     m_error;
    size_t           m_pos = 0;

    // ---- 실패는 **어디서** 났는지가 전부다 ----
    bool Fail(const char* what)
    {
        if (!m_error) return false;

        // ★ 줄·칸은 **실패했을 때만** 센다. 파싱 도중에 계속 세면
        //   성공하는 경우까지 비용을 내게 된다.
        int line = 1, col = 1;
        for (size_t i = 0; i < m_pos && i < m_text.size(); ++i)
        {
            if (m_text[i] == 0x0A) { ++line; col = 1; }
            else                   { ++col; }
        }

        char buf[256];
        std::snprintf(buf, sizeof(buf), "%d행 %d칸: %s", line, col, what);
        *m_error = buf;
        return false;
    }

    bool Eof()  const { return m_pos >= m_text.size(); }
    char Peek() const { return Eof() ? 0 : m_text[m_pos]; }

    void Skip()
    {
        while (!Eof())
        {
            const char c = m_text[m_pos];
            if (c == 0x20 || c == 0x09 || c == 0x0D || c == 0x0A) ++m_pos;
            else break;
        }
    }

    bool Literal(std::string_view word)
    {
        if (m_text.compare(m_pos, word.size(), word) != 0) return false;
        m_pos += word.size();
        return true;
    }

    bool ParseValue(JsonValue& out)
    {
        if (Eof()) return Fail("값이 있어야 하는데 파일이 끝났다");

        switch (Peek())
        {
        case 0x7B: return ParseObject(out);          // {
        case 0x5B: return ParseArray(out);           // [
        case 0x22:                                   // "
            out.m_type = JsonValue::Type::String;
            return ParseString(out.m_str);
        case 0x74:                                   // t
            if (!Literal("true")) return Fail("true 인 줄 알았다");
            out.m_type = JsonValue::Type::Bool; out.m_bool = true;  return true;
        case 0x66:                                   // f
            if (!Literal("false")) return Fail("false 인 줄 알았다");
            out.m_type = JsonValue::Type::Bool; out.m_bool = false; return true;
        case 0x6E:                                   // n
            if (!Literal("null")) return Fail("null 인 줄 알았다");
            out.m_type = JsonValue::Type::Null; return true;
        default:   return ParseNumber(out);
        }
    }

    bool ParseString(std::string& out)
    {
        ++m_pos;   // 여는 따옴표
        out.clear();

        while (true)
        {
            if (Eof()) return Fail("문자열이 안 닫혔다");

            const char c = m_text[m_pos++];
            if (c == 0x22) return true;              // "
            if (c != 0x5C) { out.push_back(c); continue; }   // 역슬래시가 아니면 그대로

            if (Eof()) return Fail("역슬래시 뒤가 비었다");
            const char e = m_text[m_pos++];
            switch (e)
            {
            case 0x22: out.push_back(0x22); break;   // \"
            case 0x5C: out.push_back(0x5C); break;   // backslash
            case 0x2F: out.push_back(0x2F); break;   // /
            case 0x6E: out.push_back(0x0A); break;   // n
            case 0x74: out.push_back(0x09); break;   // t
            case 0x72: out.push_back(0x0D); break;   // r
            // ※ \uXXXX 는 안 받는다. 게임 내 글자가 ASCII 전용이라 쓸 일이 없다.
            default:   return Fail("모르는 이스케이프");
            }
        }
    }

    bool ParseNumber(JsonValue& out)
    {
        const size_t start = m_pos;
        if (Peek() == 0x2D || Peek() == 0x2B) ++m_pos;   // - +

        bool any = false;
        while (!Eof())
        {
            const char c = m_text[m_pos];
            const bool digit = (c >= 0x30 && c <= 0x39);
            if (digit || c == 0x2E || c == 0x65 || c == 0x45 || c == 0x2D || c == 0x2B)
            {
                ++m_pos; any = true;
            }
            else break;
        }
        if (!any) return Fail("숫자인 줄 알았다");

        // ★ strtod 에 넘기려고 임시 문자열을 만든다. string_view 는 널 종단이
        //   보장되지 않으므로 그대로 넘기면 **뒤쪽 글자까지 읽는다.**
        const std::string text(m_text.substr(start, m_pos - start));
        out.m_type = JsonValue::Type::Number;
        out.m_num  = std::strtod(text.c_str(), nullptr);
        return true;
    }

    bool ParseArray(JsonValue& out)
    {
        ++m_pos;   // [
        out.m_type = JsonValue::Type::Array;

        Skip();
        if (Peek() == 0x5D) { ++m_pos; return true; }    // ]

        while (true)
        {
            JsonValue v;
            Skip();
            if (!ParseValue(v)) return false;
            out.m_arr.push_back(std::move(v));

            Skip();
            if (Peek() == 0x2C) { ++m_pos; continue; }   // ,
            if (Peek() == 0x5D) { ++m_pos; return true; }
            return Fail("쉼표나 닫는 대괄호가 있어야 한다");
        }
    }

    bool ParseObject(JsonValue& out)
    {
        ++m_pos;   // {
        out.m_type = JsonValue::Type::Object;

        Skip();
        if (Peek() == 0x7D) { ++m_pos; return true; }    // }

        while (true)
        {
            Skip();
            if (Peek() != 0x22) return Fail("키는 따옴표로 시작해야 한다");

            std::string key;
            if (!ParseString(key)) return false;

            Skip();
            if (Peek() != 0x3A) return Fail("콜론이 있어야 한다");
            ++m_pos;

            JsonValue v;
            Skip();
            if (!ParseValue(v)) return false;
            out.m_obj.emplace_back(std::move(key), std::move(v));

            Skip();
            if (Peek() == 0x2C) { ++m_pos; continue; }   // ,
            if (Peek() == 0x7D) { ++m_pos; return true; }
            return Fail("쉼표나 닫는 중괄호가 있어야 한다");
        }
    }
};


std::optional<JsonValue> Json::Parse(std::string_view text, std::string* error)
{
    JsonValue  root;
    JsonParser parser(text, error);
    if (!parser.ParseRoot(root))
        return std::nullopt;
    return root;
}


std::optional<JsonValue> Json::ParseFile(const wchar_t* path, std::string* error)
{
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path, L"rb") != 0 || !fp)
    {
        if (error) *error = "파일을 열 수 없다";
        return std::nullopt;
    }

    std::string text;
    char        buf[4096];
    size_t      n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0)
        text.append(buf, n);
    std::fclose(fp);

    // ★ UTF-8 BOM 을 건너뛴다.
    //   이 프로젝트의 도구가 전부 BOM 을 붙이므로(일본어 로캘 VS 대책),
    //   안 건너뛰면 **자기가 만든 파일을 자기가 못 읽는다.**
    std::string_view view(text);
    if (view.size() >= 3 && static_cast<unsigned char>(view[0]) == 0xEF
                         && static_cast<unsigned char>(view[1]) == 0xBB
                         && static_cast<unsigned char>(view[2]) == 0xBF)
    {
        view.remove_prefix(3);
    }

    return Parse(view, error);
}
