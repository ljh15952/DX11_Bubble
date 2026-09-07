// ============================================================================
//  DarkBubble  -  main.cpp
//    엔트리포인트만 담당한다. 게임의 내용은 Core/Game.cpp 에 있다.
//
//  ※ 이 프로젝트의 소스는 UTF-8 (BOM 있음) 으로 저장한다.
//    BOM 이 없으면 일본어 로캘 VS 가 CP932(Shift-JIS)로 오해해서 주석이 깨진다.
//    컴파일러 쪽은 프로젝트 옵션의 /utf-8 이 담당한다.
// ============================================================================

#include <windows.h>
#include <objbase.h>   // CoInitializeEx — WIC(이미지 로딩)이 COM 을 요구한다
#include <cstdio>      // freopen_s
#include <iostream>

#include "Core/Game.h"


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
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        MessageBoxW(nullptr, L"COM 초기화에 실패했습니다.", L"DarkBubble", MB_OK | MB_ICONERROR);
        return 1;
    }

    int exitCode = 1;
    {
        // Game 을 블록 안에 두어, CoUninitialize 보다 먼저 소멸하도록 보장한다.
        // COM 을 끈 뒤에 COM 객체를 해제하면 안 되기 때문이다.
        Game game;
        if (game.Initialize(hInstance, nCmdShow))
            exitCode = game.Run();
        game.Shutdown();
    }

    CoUninitialize();
    return exitCode;
}
