// ============================================================================
//  DarkBubble  -  main.cpp
//
//  1단계: Win32 창 띄우기
//    여기까지는 DirectX 가 등장하지 않는다. windows.h 만으로 창이 뜬다.
//    InitD3D() 는 아직 속이 비어 있다.  <-- 2단계에서 직접 채운다.
//
//  ※ 이 파일은 UTF-8 (BOM 있음) 으로 저장되어 있다.
//    BOM 이 없으면 일본어 로캘 VS 가 CP932(Shift-JIS)로 오해해서 주석이 깨진다.
// ============================================================================

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// ---- D3D11 핵심 객체 (2단계부터 사용) ----
ComPtr<ID3D11Device>           g_device;      // 공장   : 리소스를 만든다
ComPtr<ID3D11DeviceContext>    g_context;     // 리모컨 : 그리기 명령을 쏜다
ComPtr<IDXGISwapChain>         g_swapChain;   // 화면   : Present 로 내보낸다
ComPtr<ID3D11RenderTargetView> g_rtv;         // 그림 대상 지정

// ---- 클라이언트 영역(실제로 그림이 그려지는 부분)의 크기 ----
constexpr int kClientWidth  = 1280;
constexpr int kClientHeight = 720;

// 창 클래스 이름. 프로그램 안에서 유일하기만 하면 아무 문자열이나 상관없다.
constexpr const wchar_t* kWindowClass = L"DarkBubbleWindowClass";

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool InitD3D(HWND hwnd, int width, int height);


// ============================================================================
//  WndProc
//    Windows 가 "이 창에 이런 일이 생겼다"고 알려주러 호출하는 콜백.
//    내가 부르는 함수가 아니다. Windows 가 부른다. 그래서 CALLBACK 이 붙는다.
// ============================================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        // 창이 파괴되었다. 메시지 루프에 "이제 끝내라"는 신호(WM_QUIT)를 넣는다.
        // 이 줄이 없으면 창은 사라져도 프로세스가 살아남는다.
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        // 개발 중엔 ESC 로 닫히면 편하다.
        if (wParam == VK_ESCAPE)
            DestroyWindow(hwnd);   // 이어서 WM_DESTROY 가 날아온다
        return 0;
    }

    // 내가 처리하지 않은 메시지는 전부 Windows 의 기본 동작에 맡긴다.
    // 이 줄이 빠지면 창이 움직이지도, 닫히지도 않는다.
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


// ============================================================================
//  CreateGameWindow
//    창을 하나 만들어 HWND(창 번호표)를 돌려준다. 실패하면 nullptr.
//    static = 이 파일 안에서만 보이는 함수 (내부 링키지)
// ============================================================================
static HWND CreateGameWindow(HINSTANCE hInstance, int nCmdShow)
{
    // ---- (1) 창의 "설계도"를 등록한다 ----
    //      {} 로 초기화하면 나머지 필드는 전부 0 / nullptr 이 된다.
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;          // 크기가 바뀌면 다시 그림
    wc.lpfnWndProc   = WndProc;                          // 위에서 만든 콜백을 연결
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);  // 없으면 커서가 이상해진다
    wc.lpszClassName = kWindowClass;

    if (RegisterClassExW(&wc) == 0)
        return nullptr;

    // ---- (2) 창 전체 크기를 계산한다 ----
    //      CreateWindowEx 에 주는 크기는 "테두리 + 타이틀바까지 포함한" 크기다.
    //      클라이언트 영역을 정확히 1280x720 으로 맞추려면 보정이 필요하다.
    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc = { 0, 0, kClientWidth, kClientHeight };
    AdjustWindowRect(&rc, style, FALSE);   // rc 가 "필요한 창 전체 크기"로 바뀐다

    // ---- (3) 실제로 창을 만든다. 인자는 전부 12 개 ----
    HWND hwnd = CreateWindowExW(
        0,                      //  1. 확장 스타일 (없음)
        kWindowClass,           //  2. (1) 에서 등록한 설계도 이름
        L"DarkBubble",          //  3. 타이틀바 문자열
        style,                  //  4. 창 스타일
        CW_USEDEFAULT,          //  5. x 위치는 Windows 에게 맡긴다
        CW_USEDEFAULT,          //  6. y 위치
        rc.right - rc.left,     //  7. (2) 에서 보정한 폭
        rc.bottom - rc.top,     //  8. (2) 에서 보정한 높이
        nullptr,                //  9. 부모 창 (없음)
        nullptr,                // 10. 메뉴 (없음)
        hInstance,              // 11. 모듈 핸들
        nullptr);               // 12. WndProc 에 넘길 추가 데이터 (지금은 없음)

    if (hwnd == nullptr)
        return nullptr;

    ShowWindow(hwnd, nCmdShow);
    return hwnd;
}


// ============================================================================
//  InitD3D        <--- ★ 2단계에서 여기를 직접 채운다 ★
// ============================================================================
bool InitD3D(HWND hwnd, int width, int height)
{
    // TODO(2단계)
    //   1. DXGI_SWAP_CHAIN_DESC 를 채운다
    //   2. D3D11CreateDeviceAndSwapChain(...) 으로 device / context / swapChain 생성
    //      Flags 에는 _DEBUG 일 때만 D3D11_CREATE_DEVICE_DEBUG 를 넣는다
    //
    // TODO(3단계)
    //   3. g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)) 로 백버퍼를 꺼낸다
    //   4. g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &g_rtv)
    //   5. g_context->OMSetRenderTargets(1, g_rtv.GetAddressOf(), nullptr)
    //   6. D3D11_VIEWPORT 를 채우고 g_context->RSSetViewports(1, &vp)
    //      -> 6번을 빠뜨리면 아무것도 안 그려진다. 에러도 안 난다.

    (void)hwnd; (void)width; (void)height;   // 아직 안 쓰므로 경고만 막아둔다
    return true;
}


// ============================================================================
//  Render         <--- ★ 4단계에서 여기를 직접 채운다 ★
// ============================================================================
static void Render()
{
    // TODO(4단계)
    //   const float clearColor[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
    //   g_context->ClearRenderTargetView(g_rtv.Get(), clearColor);
    //   g_swapChain->Present(1, 0);        // 1 = VSync ON
}


// ============================================================================
//  wWinMain
//    진입점. Unicode 프로젝트라 main 도 WinMain 도 아닌 wWinMain 이다.
// ============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    HWND hwnd = CreateGameWindow(hInstance, nCmdShow);
    if (hwnd == nullptr)
    {
        MessageBoxW(nullptr, L"창 생성에 실패했습니다.", L"DarkBubble", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!InitD3D(hwnd, kClientWidth, kClientHeight))
    {
        MessageBoxW(nullptr, L"D3D11 초기화에 실패했습니다.", L"DarkBubble", MB_OK | MB_ICONERROR);
        return 1;
    }

    // ---- 메시지 루프 ----
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        // PeekMessage 는 메시지가 없으면 곧바로 FALSE 를 돌려주고 지나간다(논블로킹).
        // PM_REMOVE 는 "꺼낸 메시지는 큐에서 지운다"는 뜻.
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);   // 키 입력을 문자 메시지로 변환
            DispatchMessageW(&msg);   // 여기서 WndProc 이 호출된다
        }
        else
        {
            // 처리할 메시지가 없는 시간 = 게임을 돌릴 시간
            Render();
        }
    }

    return static_cast<int>(msg.wParam);
}
