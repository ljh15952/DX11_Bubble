// ============================================================================
//  Window.h
//    Win32 창 하나를 소유한다.
//
//    WndProc 은 C 함수 포인터라서 멤버 함수를 그대로 넣을 수 없다.
//    (멤버 함수는 숨겨진 this 인자가 있어서 시그니처가 다르다)
//    그래서 static 함수로 일단 받은 뒤, 창에 붙여 둔 this 포인터를 꺼내
//    진짜 멤버 함수로 넘긴다. Win32 + C++ 의 표준 관용구다.
// ============================================================================
#pragma once

#include <windows.h>

class Window
{
public:
    bool Create(HINSTANCE hInstance, int nCmdShow,
                int clientWidth, int clientHeight, const wchar_t* title);

    // 쌓여 있는 메시지를 전부 처리한다.
    // WM_QUIT 를 받으면 false 를 돌려준다 = "루프를 끝내라".
    bool PumpMessages();

    void Close();

    HWND Handle()       const { return m_hwnd; }
    int  ClientWidth()  const { return m_clientWidth; }
    int  ClientHeight() const { return m_clientHeight; }
    int  ExitCode()     const { return m_exitCode; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd         = nullptr;
    int  m_clientWidth  = 0;
    int  m_clientHeight = 0;
    int  m_exitCode     = 0;
};
