#include "Core/Log.h"

#include <windows.h>
#include <iostream>

namespace
{
    unsigned long long g_tick = 0;

    const char* LevelTag(Log::Level level)
    {
        switch (level)
        {
        case Log::Level::Warn:  return "[warn ] ";
        case Log::Level::Error: return "[ERROR] ";
        default:                return "[info ] ";
        }
    }

    // ------------------------------------------------------------------------
    //  UTF-8 (narrow) -> UTF-16 (wide)
    //
    //  OutputDebugString 에는 A 판(narrow)과 W 판(wide) 이 있다.
    //  A 판을 쓰면 안 되는 이유:
    //    A 판은 받은 바이트를 "시스템 ANSI 코드페이지" 로 해석한다.
    //    이 PC 는 CP932(Shift-JIS) 라서 한국어가 존재하지 않고, 출력 창에서 깨진다.
    //  그래서 UTF-8 -> UTF-16 으로 변환한 뒤 W 판에 넘긴다.
    // ------------------------------------------------------------------------
    std::wstring Utf8ToWide(std::string_view utf8)
    {
        if (utf8.empty())
            return {};

        // 1) 필요한 길이를 먼저 물어본다 (출력 버퍼를 nullptr, 0 으로 주면 길이만 돌려준다)
        const int needed = MultiByteToWideChar(
            CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (needed <= 0)
            return {};

        // 2) 그 길이만큼 확보하고 실제로 변환한다
        std::wstring wide(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                            wide.data(), needed);
        return wide;
    }
}


void Log::SetTick(unsigned long long tick)
{
    g_tick = tick;
}


std::string Log::ToUtf8(std::wstring_view wide)
{
    if (wide.empty())
        return {};

    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};

    std::string utf8(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        utf8.data(), needed, nullptr, nullptr);
    return utf8;
}


void Log::Write(Level level, std::string_view message)
{
    // 한 줄을 먼저 완성한다.
    // 조각조각 << 로 흘리면 다른 스레드의 로그와 섞여 줄이 엉킨다.
    std::string line = std::format("[t{:6}]{}", g_tick, LevelTag(level));
    line += message;
    line += '\n';

    // ① 디버그 콘솔.
    //    Release 빌드에는 콘솔이 없어서 그냥 버려진다. 에러는 아니다.
    std::cout << line;
    if (level == Level::Error)
        std::cout << std::flush;   // 크래시 직전이라도 남게 한다

    // ② VS 출력 창. D3D11 디버그 레이어의 경고와 같은 창에 시간순으로 섞인다.
    OutputDebugStringW(Utf8ToWide(line).c_str());
}
