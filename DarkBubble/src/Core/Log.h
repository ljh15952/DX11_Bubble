// ============================================================================
//  Log.h
//    한 번 호출하면 두 곳에 동시에 쓴다.
//
//      ① 디버그 콘솔 (std::cout)
//      ② VS 출력 창  (OutputDebugStringW)
//
//    ②가 중요하다. D3D11 디버그 레이어의 경고가 출력 창으로 나오기 때문에,
//    내 로그도 거기로 보내면 「내가 X 를 호출한 직후 D3D 가 경고했다」는
//    시간 순서를 한 화면에서 볼 수 있다.
//    로그를 콘솔에만 찍으면 두 창을 번갈아 보며 순서를 추측해야 한다.
// ============================================================================
#pragma once

#include <format>
#include <string>
#include <string_view>

namespace Log
{
    enum class Level { Info, Warn, Error };

    // 실제 출력. 보통은 아래 Info / Warn / Error 를 쓴다.
    void Write(Level level, std::string_view message);

    // 로그 앞에 붙는 틱 번호. Game 이 매 틱 갱신한다.
    // 「공격 발생 8틱」같은 프레임 데이터를 조정할 때
    // 무슨 일이 몇 틱째에 일어났는지 보려면 이게 필요하다.
    void SetTick(unsigned long long tick);

    // Win32 API 는 경로를 와이드 문자열로 다루지만 로그는 UTF-8 이므로 변환이 필요하다.
    std::string ToUtf8(std::wstring_view wide);


    // ------------------------------------------------------------------------
    //  편의 래퍼
    //
    //  가변 템플릿(class... Args) + std::format_string 조합이다.
    //  인자 개수와 타입이 서식 문자열과 맞지 않으면 컴파일 에러가 난다.
    //  (printf 계열은 런타임에 조용히 망가진다)
    //
    //  Args&&... + std::forward 는 "받은 그대로 넘긴다"는 뜻이다.
    //  복사가 일어나지 않게 하려는 관용구로, 완벽한 전달(perfect forwarding)이라 부른다.
    // ------------------------------------------------------------------------
    template <class... Args>
    void Info(std::format_string<Args...> fmt, Args&&... args)
    {
        Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    template <class... Args>
    void Warn(std::format_string<Args...> fmt, Args&&... args)
    {
        Write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
    }

    template <class... Args>
    void Error(std::format_string<Args...> fmt, Args&&... args)
    {
        Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }
}
