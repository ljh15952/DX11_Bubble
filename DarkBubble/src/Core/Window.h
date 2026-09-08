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

    // 창 크기가 바뀌었으면 true 를 돌려주고 플래그를 지운다.
    //
    // WndProc 안에서 바로 D3D 를 손대지 않고 이렇게 미루는 이유:
    //   ① WndProc 은 Windows 가 재귀적으로 부를 수 있어서, 그 안에서 스왑체인을
    //      재생성하면 재진입 문제가 생길 수 있다.
    //   ② 창 테두리를 드래그하는 동안 WM_SIZE 가 픽셀마다 쏟아진다.
    //      프레임당 한 번만 소비하면 여러 개가 자동으로 하나로 합쳐진다.
    bool ConsumeResize(int& outWidth, int& outHeight);

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
    bool m_resized      = false;
};
