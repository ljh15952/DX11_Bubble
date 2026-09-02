// ============================================================================
//  DarkBubble  -  main.cpp
//
//  1~4단계 완료:
//    1. Win32 창 생성 + 메시지 루프
//    2. D3D11 디바이스 / 컨텍스트 / 스왑체인 생성
//    3. 렌더 타겟 뷰(RTV) + 뷰포트
//    4. 매 프레임 화면 클리어 + Present
//
//  ※ 이 파일은 UTF-8 (BOM 있음) 으로 저장되어 있다.
//    BOM 이 없으면 일본어 로캘 VS 가 CP932(Shift-JIS)로 오해해서 주석이 깨진다.
//    컴파일러 쪽은 프로젝트 옵션의 /utf-8 이 담당한다.
// ============================================================================

#include <windows.h>
#include <objbase.h>   // CoInitializeEx — WIC(이미지 로딩)이 COM 을 요구한다
#include <d3d11.h>
#include <DirectXColors.h>   // DirectX::Colors::White 등 미리 정의된 색
#include <wrl/client.h>
#include <format>      // std::format (C++20). 로그 문자열을 만드는 데 쓴다.
#include <iostream>    // std::cout — 디버그 콘솔용
#include <cstdio>      // freopen_s
#include <memory>      // std::unique_ptr

// ---- DirectXTK (NuGet: directxtk_desktop_win10) ----
#include <SpriteBatch.h>          // 2D 스프라이트 일괄 그리기
#include <CommonStates.h>         // 자주 쓰는 블렌드/샘플러 상태 모음
#include <WICTextureLoader.h>     // PNG/JPG 등을 텍스처로 읽어온다

using Microsoft::WRL::ComPtr;

// ---- D3D11 핵심 객체 (2단계부터 사용) ----
ComPtr<ID3D11Device>           g_device;      // 공장   : 리소스를 만든다
ComPtr<ID3D11DeviceContext>    g_context;     // 리모컨 : 그리기 명령을 쏜다
ComPtr<IDXGISwapChain>         g_swapChain;   // 화면   : Present 로 내보낸다
ComPtr<ID3D11RenderTargetView> g_rtv;         // 그림 대상 지정

// ---- 그리기 리소스 (5단계) ----
//      SpriteBatch / CommonStates 는 DirectXTK 의 평범한 C++ 클래스다.
//      COM 이 아니므로 ComPtr 이 아니라 unique_ptr 로 관리한다.
std::unique_ptr<DirectX::SpriteBatch>  g_spriteBatch;
std::unique_ptr<DirectX::CommonStates> g_states;

// 텍스처는 COM 객체이므로 ComPtr.
// SRV(ShaderResourceView) = "이 텍스처를 셰이더 입력으로 취급하라"는 뷰.
// 3단계의 RTV 와 같은 구조다. 같은 텍스처라도 용도별로 뷰가 다르다.
ComPtr<ID3D11ShaderResourceView> g_testTexture;

// ---- 클라이언트 영역(실제로 그림이 그려지는 부분)의 크기 ----
constexpr int kClientWidth  = 1280;
constexpr int kClientHeight = 720;

// 창 클래스 이름. 프로그램 안에서 유일하기만 하면 아무 문자열이나 상관없다.
constexpr const wchar_t* kWindowClass = L"DarkBubbleWindowClass";

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool InitD3D(HWND hwnd, int width, int height);
bool InitResources();


// ============================================================================
//  AttachDebugConsole
//    SubSystem=Windows 라 이 프로그램에는 콘솔이 없다.
//    디버그 빌드에서만 콘솔을 하나 만들어 붙여서 std::cout 을 쓸 수 있게 한다.
//    Release 빌드에는 아예 컴파일되지 않으므로 비용이 0 이다.
// ============================================================================
#ifdef _DEBUG
static void AttachDebugConsole()
{
    if (!AllocConsole())
        return;   // 이미 콘솔이 있으면 실패한다. 그냥 넘어간다.

    // AllocConsole 이 만든 콘솔로 표준 입출력을 다시 연결한다.
    // 이 과정이 없으면 콘솔 창은 뜨지만 printf / cout 은 여전히 허공으로 간다.
    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    freopen_s(&dummy, "CONIN$",  "r", stdin);

    // C++ 스트림(cout)과 C 스트림(stdout)의 버퍼를 동기화한다.
    std::ios::sync_with_stdio(true);

    // 콘솔이 출력 바이트를 UTF-8 로 해석하게 한다.
    // 이게 없으면 CP932 로 읽어서 한국어가 깨진다.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"DarkBubble - Debug Console");

    std::cout << "[console] 디버그 콘솔 연결됨\n";
}
#endif


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
    // ---- (1) 스왑체인 서술자 ----
    //      {} 초기화 덕분에 안 쓰는 필드(RefreshRate / ScanlineOrdering / Scaling / Flags)는
    //      전부 0 = "알아서 해라" 가 된다. 창 모드에선 어차피 무시되는 값들이다.
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferDesc.Width  = static_cast<UINT>(width);
    scd.BufferDesc.Height = static_cast<UINT>(height);
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // RGBA 각 8비트. 셰이더에선 0.0~1.0
    scd.SampleDesc.Count   = 1;                          // MSAA 끔 (도트가 뭉개지므로)
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;  // 이 버퍼의 용도 = 그림 대상
    scd.BufferCount  = 2;                                // 플립 모델은 최소 2 장
    scd.OutputWindow = hwnd;
    scd.Windowed     = TRUE;                             // 개발 중엔 반드시 창 모드
    scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;    // 복사 없이 버퍼 포인터를 맞바꾼다

    // ---- (2) 원하는 기능 레벨 ----
    //      배열 위에서부터 시도해서 첫 번째로 성공한 것을 쓴다.
    const D3D_FEATURE_LEVEL wanted[] = { D3D_FEATURE_LEVEL_11_0 };

    // ---- (3) 생성 플래그 ----
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;   // 잘못된 호출을 출력 창에 문장으로 알려준다
#endif

    D3D_FEATURE_LEVEL obtained = {};

    // ---- (4) 디바이스 + 컨텍스트 + 스왑체인을 한 번에 만든다 ----
    //      디버그 레이어가 없을 때 재시도해야 하므로 람다로 묶어둔다.
    auto createDevice = [&](UINT createFlags) -> HRESULT
    {
        return D3D11CreateDeviceAndSwapChain(
            nullptr,                    //  1. 어댑터: nullptr = 기본 GPU
            D3D_DRIVER_TYPE_HARDWARE,   //  2. 실제 GPU 사용 (1번이 nullptr 일 때만 HARDWARE 가능)
            nullptr,                    //  3. 소프트웨어 래스터라이저 (안 씀)
            createFlags,                //  4. (3) 의 플래그
            wanted,                     //  5. 원하는 기능 레벨 배열
            ARRAYSIZE(wanted),          //  6. 배열 길이
            D3D11_SDK_VERSION,          //  7. 항상 이 매크로
            &scd,                       //  8. (1) 의 서술자
            &g_swapChain,               //  9. [출력] 스왑체인
            &g_device,                  // 10. [출력] 디바이스
            &obtained,                  // 11. [출력] 실제로 얻은 기능 레벨
            &g_context);                // 12. [출력] 컨텍스트
    };

    HRESULT hr = createDevice(flags);

    // 「グラフィックス ツール」(선택적 기능) 이 안 깔려 있으면 디버그 레이어 생성이 실패한다.
    // 개발 편의를 위해, 그 경우엔 디버그 플래그를 빼고 한 번 더 시도한다.
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING && (flags & D3D11_CREATE_DEVICE_DEBUG))
    {
        OutputDebugStringW(L"[D3D] 디버그 레이어 없음 -> 플래그를 빼고 재시도\n");
        hr = createDevice(flags & ~D3D11_CREATE_DEVICE_DEBUG);
    }

    if (FAILED(hr))
    {
        OutputDebugStringW(
            std::format(L"[D3D] 생성 실패 hr=0x{:08X}\n", static_cast<unsigned>(hr)).c_str());
        return false;
    }

    OutputDebugStringW(
        std::format(L"[D3D] 초기화 성공 FeatureLevel=0x{:04X}\n",
                    static_cast<unsigned>(obtained)).c_str());

    // ---- (5) 스왑체인에서 백버퍼(텍스처)를 꺼낸다 ----
    //      인덱스 0 만 접근할 수 있다. FLIP 모델에서 버퍼 로테이션은 DXGI 가 알아서 한다.
    //      backBuffer 는 지역 변수다. 텍스처의 소유자는 스왑체인이고,
    //      우리는 RTV 를 만들기 위해 잠깐 참조할 뿐이다. 함수를 나가면 자동 Release.
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
    {
        OutputDebugStringW(
            std::format(L"[D3D] GetBuffer 실패 hr=0x{:08X}\n", static_cast<unsigned>(hr)).c_str());
        return false;
    }

    // ---- (6) 그 텍스처를 "그림 대상"으로 해석하는 뷰를 만든다 ----
    //      2번째 인자 nullptr = "텍스처의 포맷을 그대로 써라".
    //      포맷을 다르게 해석하거나 밉/배열 슬라이스를 고를 때만 서술자를 넘긴다.
    hr = g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &g_rtv);
    if (FAILED(hr))
    {
        OutputDebugStringW(
            std::format(L"[D3D] RTV 생성 실패 hr=0x{:08X}\n", static_cast<unsigned>(hr)).c_str());
        return false;
    }

    // ---- (7) 뷰포트: 셰이더의 -1~+1 좌표를 실제 픽셀로 바꾸는 변환 규칙 ----
    //      MinDepth/MaxDepth 를 0/1 로 넣지 않으면 깊이 범위가 0 이 되어
    //      아무것도 그려지지 않는다. 에러도 안 나므로 찾기가 매우 어렵다.
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width    = static_cast<float>(width);
    vp.Height   = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &vp);

    // ※ OMSetRenderTargets 는 여기가 아니라 Render() 안에 있다.
    //   FLIP 모델은 Present() 때마다 렌더 타겟 바인딩이 풀리기 때문.

    OutputDebugStringW(L"[D3D] RTV / 뷰포트 준비 완료\n");
    return true;
}


// ============================================================================
//  InitResources
//    그리기에 쓸 리소스를 준비한다. InitD3D(장치 준비) 와는 역할이 다르므로 분리한다.
// ============================================================================
bool InitResources()
{
    // SpriteBatch 는 그리기 명령을 모았다가 한 번에 GPU 로 보낸다.
    // 스프라이트를 1000 장 그려도 드로우 콜은 몇 번으로 줄어든다.
    g_spriteBatch = std::make_unique<DirectX::SpriteBatch>(g_context.Get());

    // CommonStates 는 자주 쓰는 블렌드/샘플러/래스터라이저 상태를 미리 만들어 둔 것.
    // 직접 만들면 서술자를 몇십 줄 채워야 한다.
    g_states = std::make_unique<DirectX::CommonStates>(g_device.Get());

    // PNG 를 읽어 텍스처 + SRV 로 만든다.
    // 3번째 인자(ID3D11Resource**) 는 텍스처 원본. 크기를 재거나 할 때만 필요하므로 nullptr.
    // 경로는 작업 디렉터리 기준. 프로젝트 설정에서 $(SolutionDir) 로 잡아뒀다.
    HRESULT hr = DirectX::CreateWICTextureFromFile(
        g_device.Get(),
        L"assets/textures/test.png",
        nullptr,
        g_testTexture.GetAddressOf());

    if (FAILED(hr))
    {
        OutputDebugStringW(
            std::format(L"[RES] 텍스처 로드 실패 hr=0x{:08X}\n", static_cast<unsigned>(hr)).c_str());
        std::cout << "[RES] assets/textures/test.png 를 못 찾았습니다.\n"
                     "      디버깅 작업 디렉터리가 $(SolutionDir) 인지 확인하세요.\n";
        return false;
    }

    std::cout << "[RES] 리소스 준비 완료\n";
    return true;
}


// ============================================================================
//  Render
//    매 프레임 호출된다.
// ============================================================================
static void Render()
{
    // ---- (1) 렌더 타겟을 파이프라인에 묶는다 ----
    //      FLIP 모델은 Present() 때마다 이 바인딩이 풀린다. 그래서 매 프레임 다시 묶는다.
    //      OM = Output Merger, 파이프라인의 마지막 단계.
    //      1     = 렌더 타겟 개수 (여러 장에 동시에 그리는 MRT 도 가능)
    //      3번째 = 깊이/스텐실 뷰. 2D 라서 아직 필요 없다.
    //
    //      ※ &g_rtv 가 아니라 g_rtv.GetAddressOf() 인 것에 주의.
    //        &ComPtr 은 ReleaseAndGetAddressOf() 라서 먼저 Release 해버린다.
    g_context->OMSetRenderTargets(1, g_rtv.GetAddressOf(), nullptr);

    // ---- (2) 화면을 지운다 ----
    //      RGBA 각 0.0~1.0. 어두운 청회색.
    const float clearColor[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
    g_context->ClearRenderTargetView(g_rtv.Get(), clearColor);

    // ---- (3) 스프라이트를 그린다 ----
    //      Begin() ~ End() 사이에 Draw 를 모아두면, End() 에서 한꺼번에 GPU 로 보낸다.
    //
    //      2번째 인자 NonPremultiplied():
    //        WIC 로 읽은 PNG 는 알파가 "곱해지지 않은(straight)" 상태다.
    //        SpriteBatch 의 기본값은 곱해진(premultiplied) 알파용이라,
    //        그대로 두면 반투명 가장자리에 검은 테두리가 생긴다.
    //
    //      3번째 인자 PointClamp():
    //        점 샘플링(가장 가까운 픽셀). 기본값인 선형 보간을 쓰면 도트가 흐려진다.
    //        픽셀아트 게임에서는 사실상 필수.
    g_spriteBatch->Begin(
        DirectX::SpriteSortMode_Deferred,
        g_states->NonPremultiplied(),
        g_states->PointClamp());

    // 원본 크기 그대로 (100, 100) 위치에
    g_spriteBatch->Draw(g_testTexture.Get(), DirectX::XMFLOAT2(100.0f, 100.0f));

    // 4 배 확대해서 (400, 100) 위치에 — 점 샘플링이 먹었는지 확인용
    g_spriteBatch->Draw(g_testTexture.Get(), DirectX::XMFLOAT2(400.0f, 100.0f),
                        nullptr, DirectX::Colors::White, 0.0f,
                        DirectX::XMFLOAT2(0.0f, 0.0f), 4.0f);

    g_spriteBatch->End();

    // ---- (4) 백버퍼를 화면으로 내보낸다 ----
    //      1번째 인자 SyncInterval: 0 = 즉시(테어링 발생, fps 무제한)
    //                               1 = 다음 수직 동기까지 대기 = 60fps 고정
    //                               2 = 두 번째 동기까지 = 30fps
    //      VSync 를 켜면 루프가 저절로 60fps 로 묶여서 CPU 100% 문제도 사라진다.
    g_swapChain->Present(1, 0);
}


// ============================================================================
//  wWinMain
//    진입점. Unicode 프로젝트라 main 도 WinMain 도 아닌 wWinMain 이다.
// ============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
#ifdef _DEBUG
    AttachDebugConsole();
#endif

    // WIC(이미지 로딩)는 COM 위에서 동작한다. 텍스처를 읽기 전에 반드시 초기화해야 한다.
    // 빠뜨리면 CreateWICTextureFromFile 이 CO_E_NOTINITIALIZED 로 실패한다.
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hrCom))
    {
        MessageBoxW(nullptr, L"COM 초기화에 실패했습니다.", L"DarkBubble", MB_OK | MB_ICONERROR);
        return 1;
    }

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

    if (!InitResources())
    {
        MessageBoxW(nullptr, L"리소스 로드에 실패했습니다.\n출력 창을 확인하세요.",
                    L"DarkBubble", MB_OK | MB_ICONERROR);
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

    // ---- 정리 ----
    //   전역 변수의 소멸자는 wWinMain 이 끝난 "뒤에" 불린다.
    //   그때는 이미 CoUninitialize 가 지나간 뒤라 순서가 어긋난다.
    //   그래서 여기서 명시적으로 놓아준다.
    g_spriteBatch.reset();
    g_states.reset();
    g_testTexture.Reset();
    g_rtv.Reset();
    g_swapChain.Reset();
    g_context.Reset();
    g_device.Reset();

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
