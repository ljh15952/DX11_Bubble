// ============================================================================
//  Json.h
//    최소 JSON 파서. 밸런스 숫자를 파일로 빼기 위한 것이다.
//
//  ---- ★ 왜 직접 짜는가 ----
//    vcpkg 가 없어 의존성 하나 추가하는 것이 번거롭고, 우리가 쓰는 JSON 은
//    **숫자 · 문자열 · 불리언 · 배열 · 객체**뿐이다. 그 정도면 이 파일로 끝난다.
//    「외부 엔진을 쓰지 않는다」는 원칙의 연장이기도 하다.
//
//  ---- ★★ 없는 키를 물어도 죽지 않는다 ----
//    `v["startup"].Int(8)` 처럼 **기본값을 읽는 자리에 같이 적는다.**
//
//    예외를 던지지도 않고, 「키가 있는지」를 먼저 묻게 하지도 않는다.
//    그러면 읽는 코드가 조건문으로 뒤덮이고 결국 **기본값이 두 곳**(C++ 과
//    JSON)에 흩어진다 — 이 프로젝트가 계속 밟아 온 함정과 같은 모양이다.
//
//    없는 키는 **Null 값 하나**를 돌려주고, Null 은 무엇을 물어도 기본값을
//    돌려준다. 그래서 JSON 이 비어 있어도, 키가 몇 개뿐이어도 동작한다.
//
//  ---- 이 파서가 **안 하는** 것 ----
//    유니코드 이스케이프(\uXXXX), 주석, 후행 쉼표, 지수 표기의 엄밀한 검증.
//    필요해지면 그때 넣는다 — 지금 넣으면 쓰지도 않을 코드를 디버깅하게 된다.
// ============================================================================
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class JsonValue
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type Kind()    const { return m_type; }
    bool IsNull()  const { return m_type == Type::Null; }
    bool IsArray() const { return m_type == Type::Array; }

    // ---- 읽기 : 기본값을 **읽는 자리에** 적는다 ----
    double Num(double def = 0.0) const
    {
        return (m_type == Type::Number) ? m_num : def;
    }
    float Flt(float def = 0.0f) const
    {
        return (m_type == Type::Number) ? static_cast<float>(m_num) : def;
    }
    int Int(int def = 0) const
    {
        return (m_type == Type::Number) ? static_cast<int>(m_num) : def;
    }
    bool Bool(bool def = false) const
    {
        return (m_type == Type::Bool) ? m_bool : def;
    }
    std::string Str(std::string_view def = {}) const
    {
        return (m_type == Type::String) ? m_str : std::string(def);
    }

    // ---- 찾기 : 없으면 **Null** 이 돌아온다. 검사할 필요가 없다 ----
    const JsonValue& operator[](std::string_view key) const;
    const JsonValue& operator[](size_t index) const;

    size_t Size() const;   // 배열·객체의 원소 수. 그 밖에는 0

    // 객체를 순서대로 훑는다. 이름이 미리 정해지지 않은 목록에 쓴다.
    const std::vector<std::pair<std::string, JsonValue>>& Members() const { return m_obj; }

private:
    friend class JsonParser;

    Type        m_type = Type::Null;
    bool        m_bool = false;
    double      m_num  = 0.0;
    std::string m_str;

    // ★ 객체를 map 이 아니라 **벡터**로 둔다. 키가 많아야 스물이라
    //   선형 탐색이 더 빠르고, 무엇보다 **파일에 적힌 순서가 보존된다** —
    //   나중에 다시 써서 내보낼 때 순서가 뒤바뀌지 않는다.
    std::vector<JsonValue>                         m_arr;
    std::vector<std::pair<std::string, JsonValue>> m_obj;
};


namespace Json
{
    // 실패하면 nullopt. error 에는 **줄·칸 번호가 붙은** 메시지가 들어간다 —
    // 핫 리로드 중에 오타를 고쳐야 하므로 「어디가 틀렸는지」가 전부다.
    std::optional<JsonValue> Parse(std::string_view text, std::string* error);

    // ★ UTF-8 BOM 을 건너뛴다. 이 프로젝트의 도구가 전부 BOM 을 붙여 쓰므로,
    //   안 건너뛰면 **자기가 만든 파일을 자기가 못 읽는다.**
    std::optional<JsonValue> ParseFile(const wchar_t* path, std::string* error);
}
