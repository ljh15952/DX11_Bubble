#include "Core/Window.h"
#include "Input/Input.h"

namespace
{
    // 창 클래스 이름. 프로그램 안에서 유일하기만 하면 아무 문자열이나 상관없다.
    constexpr const wchar_t* kWindowClass = L"DarkBubbleWindowClass";
}


// ----------------------------------------------------------------------------
//  StaticWndProc
//    Windows 가 실제로 호출하는 콜백. 여기서 인스턴스를 찾아 넘긴다.
// ----------------------------------------------------------------------------
LRESULT CALLBACK Window::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 창이 만들어지는 첫 순간에 오는 메시지.
    // CreateWindowExW 의 12번째 인자로 넘긴 this 포인터가 여기 실려 온다.
    if (msg == WM_NCCREATE)
    {
        auto* cs   = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<Window*>(cs->lpCreateParams);

        // 창 자체에 this 포인터를 붙여 둔다. 이후 모든 메시지에서 꺼내 쓸 수 있다.
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }

    if (auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
        return self->HandleMessage(msg, wParam, lParam);

    // WM_NCCREATE 보다 먼저 오는 메시지(WM_GETMINMAXINFO 등)는 아직 this 가 없다.
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


// ----------------------------------------------------------------------------
//  HandleMessage
//    여기서부터는 평범한 멤버 함수. this 를 쓸 수 있다.
// ----------------------------------------------------------------------------
LRESULT Window::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        // 창이 파괴되었다. 메시지 루프에 "이제 끝내라"는 신호(WM_QUIT)를 넣는다.
        // 이 줄이 없으면 창은 사라져도 프로세스가 살아남는다.
        PostQuitMessage(0);
        return 0;

    // ---- 입력 시스템에 메시지를 배달한다 ----
    //      DirectXTK Keyboard 는 스스로 메시지를 받을 수 없다.
    //      WndProc 은 우리 것이므로 우리가 넘겨줘야 한다.
    case WM_ACTIVATEAPP:
        // 창이 포커스를 잃거나 얻을 때. 이걸 넘기지 않으면
        // 키를 누른 채 Alt+Tab 했다 돌아왔을 때 그 키가 눌린 채로 남는다.
        Input::ProcessMessage(msg, wParam, lParam);
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        Input::ProcessMessage(msg, wParam, lParam);
        break;   // return 0 이 아니라 break — Alt+F4 등을 Windows 가 처리하도록
    }

    // 내가 처리하지 않은 메시지는 전부 Windows 의 기본 동작에 맡긴다.
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}


// ----------------------------------------------------------------------------
//  Create
// ----------------------------------------------------------------------------
bool Window::Create(HINSTANCE hInstance, int nCmdShow,
                    int clientWidth, int clientHeight, const wchar_t* title)
{
    m_clientWidth  = clientWidth;
    m_clientHeight = clientHeight;

    // ---- (1) 창의 "설계도"를 등록한다 ----
    //      {} 로 초기화하면 나머지 필드는 전부 0 / nullptr 이 된다.
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;          // 크기가 바뀌면 다시 그림
    wc.lpfnWndProc   = StaticWndProc;                    // static 함수만 넣을 수 있다
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);  // 없으면 커서가 이상해진다
    wc.lpszClassName = kWindowClass;

    if (RegisterClassExW(&wc) == 0)
        return false;

    // ---- (2) 창 전체 크기를 계산한다 ----
    //      CreateWindowEx 에 주는 크기는 "테두리 + 타이틀바까지 포함한" 크기다.
    //      클라이언트 영역을 정확히 맞추려면 보정이 필요하다.
    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRect(&rc, style, FALSE);

    // ---- (3) 실제로 창을 만든다 ----
    //      마지막 인자로 this 를 넘긴다. 이것이 WM_NCCREATE 로 배달된다.
    HWND hwnd = CreateWindowExW(
        0,                      //  1. 확장 스타일 (없음)
        kWindowClass,           //  2. (1) 에서 등록한 설계도 이름
        title,                  //  3. 타이틀바 문자열
        style,                  //  4. 창 스타일
        CW_USEDEFAULT,          //  5. x 위치는 Windows 에게 맡긴다
        CW_USEDEFAULT,          //  6. y 위치
        rc.right - rc.left,     //  7. (2) 에서 보정한 폭
        rc.bottom - rc.top,     //  8. (2) 에서 보정한 높이
        nullptr,                //  9. 부모 창 (없음)
        nullptr,                // 10. 메뉴 (없음)
        hInstance,              // 11. 모듈 핸들
        this);                  // 12. ★ WM_NCCREATE 로 전달될 추가 데이터

    if (hwnd == nullptr)
        return false;

    ShowWindow(hwnd, nCmdShow);
    return true;
}


// ----------------------------------------------------------------------------
//  PumpMessages
//    쌓인 메시지를 전부 비운다. false 를 돌려주면 게임 루프를 끝내야 한다.
// ----------------------------------------------------------------------------
bool Window::PumpMessages()
{
    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        // WM_QUIT 은 창에 배달되는 메시지가 아니라 "루프를 끝내라"는 신호다.
        // Dispatch 하지 않고 여기서 바로 처리한다.
        if (msg.message == WM_QUIT)
        {
            m_exitCode = static_cast<int>(msg.wParam);
            return false;
        }

        TranslateMessage(&msg);   // 키 입력을 문자 메시지로 변환
        DispatchMessageW(&msg);   // 여기서 StaticWndProc 이 호출된다
    }
    return true;
}


void Window::Close()
{
    if (m_hwnd)
        DestroyWindow(m_hwnd);   // 이어서 WM_DESTROY 가 날아온다
}
